#include "FileEdits.hpp"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>

#include <algorithm>

namespace Kayte::Llm {

// ─── Parsing ─────────────────────────────────────────────────────────────────

namespace {

bool isMarker(const QString &line, const char *marker)
{
    const QString t = line.trimmed();
    return t.startsWith(QLatin1String(marker));
}

// The file name a block belongs to: the nearest preceding line that looks like
// a path (bare, `quoted`, **bold**, "File: ...", or a ```lang path fence).
QString pathFromLine(QString line)
{
    line = line.trimmed();
    static const QRegularExpression fence(QStringLiteral(R"(^```\w*\s+(\S+)$)"));
    if (const auto m = fence.match(line); m.hasMatch()) line = m.captured(1);
    if (line.startsWith(QLatin1String("```"))) return {};
    line.remove(QRegularExpression(QStringLiteral(R"(^(?:#+\s*|[-*]\s+|(?:File|Path|Filename)\s*:\s*))"),
                                   QRegularExpression::CaseInsensitiveOption));
    for (const QString &wrap : { QStringLiteral("**"), QStringLiteral("`"), QStringLiteral("\""), QStringLiteral("'") })
        if (line.startsWith(wrap) && line.endsWith(wrap) && line.size() > 2 * wrap.size())
            line = line.mid(wrap.size(), line.size() - 2 * wrap.size());
    line = line.trimmed();
    if (line.endsWith(QLatin1Char(':'))) line.chop(1);
    // A path: no spaces, and either has a folder ("src/x", ".git/HEAD") or a
    // name with an extension ("main.cpp") - not a word ending a sentence.
    static const QRegularExpression chars(QStringLiteral(R"(^[\w.\-/\\+@]+$)"));
    static const QRegularExpression withExt(QStringLiteral(R"(^[\w\-+@]+(?:\.[\w\-+@]+)*\.[A-Za-z0-9]{1,10}$)"));
    static const QRegularExpression known(QStringLiteral(R"(^(?:CMakeLists\.txt|Makefile|Dockerfile|LICENSE|README)$)"));
    if (line.isEmpty() || line.size() > 200 || !chars.match(line).hasMatch()) return {};
    if (!line.contains(QLatin1Char('/')) && !withExt.match(line).hasMatch() && !known.match(line).hasMatch()) return {};
    return line;
}

} // namespace

QList<FileEdit> parseEdits(const QString &answer, const QString &openFile)
{
    QList<FileEdit> out;
    QList<FileEdit> loose;                 // pathless ``` blocks, for the open-file fallback
    const QStringList lines = answer.split(QLatin1Char('\n'));
    QString lastPath;
    int lastPathLine = -100;               // where lastPath was seen
    int markerFenceEnd = -1;               // closing ``` of a fence around SEARCH blocks
    for (int i = 0; i < lines.size(); ++i) {
        const QString &l = lines[i];
        const QString t = l.trimmed();
        if (i == markerFenceEnd) continue;

        if (t.startsWith(QLatin1String("```"))) {
            // A fence: either it wraps search/replace blocks (scan inside as
            // usual) or it is a whole file.
            int close = i + 1;
            bool hasMarkers = false;
            while (close < lines.size() && !lines[close].trimmed().startsWith(QLatin1String("```"))) {
                hasMarkers = hasMarkers || isMarker(lines[close], "<<<<<<<");
                ++close;
            }
            const QString fencePath = pathFromLine(t);   // ```cpp src/main.cpp
            if (hasMarkers || close >= lines.size()) {
                if (!fencePath.isEmpty()) { lastPath = fencePath; lastPathLine = i; }
                markerFenceEnd = close;
                continue;                                // markers are handled line by line
            }
            FileEdit e;
            e.wholeFile = true;
            e.replace = lines.mid(i + 1, close - i - 1).join(QLatin1Char('\n'));
            // The path on the fence, or on one of the two lines just before it.
            e.path = !fencePath.isEmpty() ? fencePath : (i - lastPathLine <= 2 ? lastPath : QString());
            if (!e.replace.trimmed().isEmpty()) (e.path.isEmpty() ? loose : out) << e;
            i = close;
            continue;
        }

        if (!isMarker(l, "<<<<<<<")) {
            const QString p = pathFromLine(l);
            if (!p.isEmpty()) { lastPath = p; lastPathLine = i; }
            continue;
        }
        FileEdit e;
        e.path = lastPath;
        QStringList search, replace;
        int j = i + 1;
        bool separated = false;
        while (j < lines.size() && !isMarker(lines[j], ">>>>>>>")) {
            if (!separated && isMarker(lines[j], "=======")) { separated = true; ++j; continue; }
            (separated ? replace : search) << lines[j++];
        }
        if (j >= lines.size()) break;              // unterminated block - ignore
        if (separated) {
            e.search = search.join(QLatin1Char('\n'));
            e.replace = replace.join(QLatin1Char('\n'));
        } else {                                   // no =======: the model sent the whole file
            e.wholeFile = true;
            e.replace = search.join(QLatin1Char('\n'));
        }
        if (!e.path.isEmpty()) out << e;
        i = j;
    }

    // Nothing recognisable, one code block, a file open in the editor: it is
    // the new version of that file (planEdits checks it really is all of it).
    const bool anyBlocks = std::any_of(out.cbegin(), out.cend(), [](const FileEdit &e) { return !e.wholeFile; });
    if (out.isEmpty() && !anyBlocks && loose.size() == 1 && !openFile.isEmpty()) {
        FileEdit e = loose.first();
        e.path = openFile;
        out << e;
    }
    return out;
}

// ─── Planning ────────────────────────────────────────────────────────────────

namespace {

QString normalizePath(const QString &root, QString p, QString *error)
{
    p = p.trimmed();
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (p.startsWith(QLatin1String("./"))) p = p.mid(2);
    const QString abs = QDir::cleanPath(QFileInfo(p).isAbsolute() ? p : QDir(root).filePath(p));
    const QString r = QDir::cleanPath(QDir(root).absolutePath());
    if (!abs.startsWith(r + QLatin1Char('/'))) {
        *error = QStringLiteral("%1 is outside the project - not changed").arg(p);
        return {};
    }
    if (abs.contains(QLatin1String("/.git/")) || abs.endsWith(QLatin1String("/.git"))) {
        *error = QStringLiteral("%1 is inside .git - not changed").arg(p);
        return {};
    }
    return abs;
}

// Index of `needle` in `hay`, exactly once; else -1 (sets *count).
int uniqueIndexOf(const QString &hay, const QString &needle, int *count)
{
    *count = 0;
    int first = -1;
    for (int at = hay.indexOf(needle); at >= 0; at = hay.indexOf(needle, at + 1)) {
        if (++*count == 1) first = at;
        if (*count > 1) return -1;
    }
    return first;
}

// Models often get indentation or trailing spaces slightly wrong: match the
// SEARCH lines against the file's lines ignoring that, and return the range
// [start, end) of characters it covers.
bool fuzzyRange(const QString &text, const QString &search, int *start, int *end)
{
    const QStringList hay = text.split(QLatin1Char('\n'));
    QStringList needle = search.split(QLatin1Char('\n'));
    while (!needle.isEmpty() && needle.first().trimmed().isEmpty()) needle.removeFirst();
    while (!needle.isEmpty() && needle.last().trimmed().isEmpty()) needle.removeLast();
    if (needle.isEmpty()) return false;
    int found = -1;
    for (int i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (int k = 0; k < needle.size() && ok; ++k)
            ok = hay[i + k].trimmed() == needle[k].trimmed();
        if (!ok) continue;
        if (found >= 0) return false;   // ambiguous
        found = i;
    }
    if (found < 0) return false;
    int pos = 0;
    for (int i = 0; i < found; ++i) pos += int(hay[i].size()) + 1;
    int len = 0;
    for (int k = 0; k < needle.size(); ++k) len += int(hay[found + k].size()) + (k + 1 < needle.size() ? 1 : 0);
    *start = pos;
    *end = pos + len;
    return true;
}

// The replacement re-indented by how much the file's first matched line is
// indented relative to the SEARCH's first line.
QString reindent(const QString &replace, const QString &search, const QString &matchedFirstLine)
{
    auto indentOf = [](const QString &s) { int n = 0; while (n < s.size() && (s[n] == QLatin1Char(' ') || s[n] == QLatin1Char('\t'))) ++n; return s.left(n); };
    QString firstSearch;
    for (const QString &l : search.split(QLatin1Char('\n'))) if (!l.trimmed().isEmpty()) { firstSearch = l; break; }
    const QString have = indentOf(matchedFirstLine), had = indentOf(firstSearch);
    if (have == had) return replace;
    QStringList out;
    for (const QString &l : replace.split(QLatin1Char('\n'))) {
        if (l.trimmed().isEmpty()) out << l;
        else if (l.startsWith(had)) out << have + l.mid(had.size());
        else out << l;
    }
    return out.join(QLatin1Char('\n'));
}

// Whether `next` is a complete new version of `current` rather than an
// excerpt: it keeps most of the file's lines and is not much shorter.
bool looksLikeWholeFile(const QString &current, const QString &next)
{
    QStringList cur;
    for (const QString &l : current.split(QLatin1Char('\n'))) if (!l.trimmed().isEmpty()) cur << l.trimmed();
    if (cur.size() <= 3) return true;
    QHash<QString, int> have;
    for (const QString &l : next.split(QLatin1Char('\n'))) if (!l.trimmed().isEmpty()) ++have[l.trimmed()];
    int kept = 0, nextLines = 0;
    for (int n : std::as_const(have)) nextLines += n;
    for (const QString &l : std::as_const(cur)) {
        auto it = have.find(l);
        if (it != have.end() && *it > 0) { --*it; ++kept; }
    }
    return kept * 10 >= cur.size() * 6 && nextLines * 10 >= cur.size() * 7;   // >=60 % kept, >=70 % as long
}

} // namespace

EditPlan planEdits(const QList<FileEdit> &edits, const QString &root, const TextProvider &current)
{
    EditPlan plan;
    QHash<QString, QString> text;          // working copy per file
    QHash<QString, bool> existed;
    QHash<QString, QString> original;
    QStringList order;

    for (const FileEdit &e : edits) {
        QString err;
        const QString abs = normalizePath(root, e.path, &err);
        if (abs.isEmpty()) { plan.errors << err; continue; }
        if (!text.contains(abs)) {
            const std::optional<QString> cur = current ? current(abs) : std::nullopt;
            existed[abs] = cur.has_value();
            original[abs] = cur.value_or(QString());
            text[abs] = cur.value_or(QString());
            order << abs;
        }
        QString &t = text[abs];
        const QString rel = QDir(root).relativeFilePath(abs);

        if (e.wholeFile) {
            QString next = e.replace;
            if (!next.endsWith(QLatin1Char('\n'))) next += QLatin1Char('\n');
            if (!t.isEmpty() && !looksLikeWholeFile(t, next)) {
                plan.errors << QStringLiteral("%1: the code shown is only part of the file - it was not "
                                              "written over the file (ask for the change as SEARCH/REPLACE "
                                              "blocks, or for the complete file)").arg(rel);
                continue;
            }
            t = next;
            continue;
        }

        if (e.search.trimmed().isEmpty()) {          // new file (or text for an empty one)
            if (!t.isEmpty()) {
                plan.errors << QStringLiteral("%1 already has content - an empty SEARCH only creates new files").arg(rel);
                continue;
            }
            t = e.replace.endsWith(QLatin1Char('\n')) ? e.replace : e.replace + QLatin1Char('\n');
            continue;
        }
        if (!existed.value(abs) && t.isEmpty()) {
            plan.errors << QStringLiteral("%1 does not exist - nothing to search in").arg(rel);
            continue;
        }
        int count = 0;
        const int at = uniqueIndexOf(t, e.search, &count);
        if (at >= 0) {
            t.replace(at, e.search.size(), e.replace);
            continue;
        }
        int s = 0, end = 0;
        if (count == 0 && fuzzyRange(t, e.search, &s, &end)) {
            const int lineStart = t.lastIndexOf(QLatin1Char('\n'), s - 1) + 1;
            const QString firstLine = t.mid(lineStart, t.indexOf(QLatin1Char('\n'), s) - lineStart);
            QString rep = reindent(e.replace, e.search, firstLine);
            while (rep.startsWith(QLatin1Char('\n'))) rep.remove(0, 1);
            while (rep.endsWith(QLatin1Char('\n'))) rep.chop(1);
            t.replace(s, end - s, rep);
            continue;
        }
        plan.errors << (count > 1
            ? QStringLiteral("%1: the text to replace appears %2 times - the block was not applied").arg(rel).arg(count)
            : QStringLiteral("%1: the text to replace was not found - the block was not applied").arg(rel));
    }

    for (const QString &abs : order) {
        if (existed.value(abs) && text[abs] == original[abs]) continue;
        if (!existed.value(abs) && text[abs].isEmpty()) continue;
        FileChange c;
        c.path = abs;
        if (existed.value(abs)) c.before = original[abs].toUtf8();
        c.after = text[abs].toUtf8();
        countLines(c);
        plan.changes << c;
    }
    return plan;
}

bool writeFileDirect(const QString &abs, const std::optional<QString> &text, QString *error)
{
    if (!text) {
        if (!QFile::exists(abs) || QFile::remove(abs)) return true;
        if (error) *error = QStringLiteral("cannot delete %1").arg(abs);
        return false;
    }
    QDir().mkpath(QFileInfo(abs).absolutePath());
    QSaveFile f(abs);
    if (!f.open(QIODevice::WriteOnly) || f.write(text->toUtf8()) < 0 || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

void countLines(FileChange &c)
{
    // Lines only in one side (a multiset difference - enough for a summary).
    QHash<QString, int> before;
    if (c.before) for (const QString &l : QString::fromUtf8(*c.before).split(QLatin1Char('\n'))) ++before[l];
    c.added = c.removed = 0;
    if (c.after) {
        for (const QString &l : QString::fromUtf8(*c.after).split(QLatin1Char('\n'))) {
            auto it = before.find(l);
            if (it != before.end() && *it > 0) --*it;
            else ++c.added;
        }
    }
    for (int n : std::as_const(before)) c.removed += n;
}

// ─── Snapshot ────────────────────────────────────────────────────────────────

namespace {

bool skipDir(const QString &dir)
{
    const QString name = QFileInfo(dir).fileName();
    static const QSet<QString> skip { QStringLiteral(".git"), QStringLiteral(".svn"), QStringLiteral(".hg"),
                                      QStringLiteral("node_modules"), QStringLiteral("_deps"), QStringLiteral("CMakeFiles"),
                                      QStringLiteral(".kayteide"), QStringLiteral("__pycache__") };
    return skip.contains(name) || name.endsWith(QLatin1String("_autogen")) ||
           QFileInfo::exists(QDir(dir).filePath(QStringLiteral("CMakeCache.txt")));
}

bool looksText(const QByteArray &head)
{
    return !head.contains('\0');
}

} // namespace

void ProjectSnapshot::take(const QString &root)
{
    m_root = QDir(root).absolutePath();
    m_files.clear();
    qint64 kept = 0;
    QStringList dirs { m_root };
    int files = 0;
    while (!dirs.isEmpty() && files < 50000) {
        const QString d = dirs.takeFirst();
        const QFileInfoList entries = QDir(d).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks);
        for (const QFileInfo &fi : entries) {
            if (fi.isDir()) { if (!skipDir(fi.absoluteFilePath())) dirs << fi.absoluteFilePath(); continue; }
            Entry e;
            e.size = fi.size();
            e.mtime = fi.lastModified().toMSecsSinceEpoch();
            if (e.size <= 2 * 1024 * 1024 && kept + e.size <= 64LL * 1024 * 1024) {
                QFile f(fi.absoluteFilePath());
                if (f.open(QIODevice::ReadOnly)) {
                    const QByteArray data = f.readAll();
                    if (looksText(data.left(8000))) { e.content = data; kept += data.size(); }
                }
            }
            m_files.insert(fi.absoluteFilePath(), e);
            ++files;
        }
    }
}

QList<FileChange> ProjectSnapshot::changesSince() const
{
    QList<FileChange> out;
    if (m_root.isEmpty()) return out;
    QSet<QString> seen;
    QStringList dirs { m_root };
    while (!dirs.isEmpty()) {
        const QString d = dirs.takeFirst();
        for (const QFileInfo &fi : QDir(d).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks)) {
            if (fi.isDir()) { if (!skipDir(fi.absoluteFilePath())) dirs << fi.absoluteFilePath(); continue; }
            const QString p = fi.absoluteFilePath();
            seen.insert(p);
            const auto it = m_files.constFind(p);
            if (it != m_files.cend() && it->size == fi.size() && it->mtime == fi.lastModified().toMSecsSinceEpoch())
                continue;
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly)) continue;
            FileChange c;
            c.path = p;
            c.after = f.readAll();
            if (it != m_files.cend()) {
                if (!it->content) {           // too large / binary to have been kept
                    c.undoable = false;
                    out << c;
                    continue;
                }
                if (*it->content == *c.after) continue;   // touched, same content
                c.before = *it->content;
            }
            countLines(c);
            out << c;
        }
    }
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it) {
        if (seen.contains(it.key())) continue;
        FileChange c;
        c.path = it.key();
        c.before = it->content;
        c.after = std::nullopt;
        c.undoable = it->content.has_value();
        countLines(c);
        out << c;
    }
    std::sort(out.begin(), out.end(), [](const FileChange &a, const FileChange &b) { return a.path < b.path; });
    return out;
}

// ─── Summary ─────────────────────────────────────────────────────────────────

QString changeSummary(const QList<FileChange> &changes, const QString &root, const QString &undoLink)
{
    if (changes.isEmpty()) return {};
    QStringList lines;
    for (const FileChange &c : changes) {
        const QString rel = QDir(root).relativeFilePath(c.path);
        QUrl url;
        url.setScheme(QStringLiteral("kayte-src"));
        url.setPath(QLatin1Char('/') + rel);
        url.setQuery(QStringLiteral("line=1"));
        const QString link = c.after ? QStringLiteral("[%1](%2)").arg(rel, url.toString(QUrl::FullyEncoded))
                                     : QStringLiteral("`%1`").arg(rel);
        QString what;
        if (!c.before && c.after && c.undoable) what = QStringLiteral("new, %1 lines").arg(c.added);
        else if (!c.after) what = QStringLiteral("deleted");
        else if (!c.undoable) what = QStringLiteral("changed (binary or large - cannot be undone)");
        else what = QStringLiteral("+%1 −%2").arg(c.added).arg(c.removed);
        lines << QStringLiteral("- %1 - %2").arg(link, what);
    }
    QString md = QStringLiteral("**Changed %1 file%2:**\n\n%3")
                     .arg(changes.size()).arg(changes.size() == 1 ? "" : "s").arg(lines.join(QLatin1Char('\n')));
    if (!undoLink.isEmpty()) md += QStringLiteral("\n\n[↶ Undo these changes](%1)").arg(undoLink);
    return md;
}

} // namespace Kayte::Llm
