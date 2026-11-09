// cmakeautoadd.cpp - KayteIDE
#include "cmakeautoadd.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace CMakeAuto {

// ─── Parsing ─────────────────────────────────────────────────────────────────

namespace {

bool isIdentChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

// Length of "[==[" at `i` (0 if not a bracket opener); `eq` gets the '=' count.
int bracketOpen(const QString &t, int i, int *eq)
{
    if (i >= t.size() || t[i] != QLatin1Char('[')) return 0;
    int j = i + 1, n = 0;
    while (j < t.size() && t[j] == QLatin1Char('=')) { ++j; ++n; }
    if (j < t.size() && t[j] == QLatin1Char('[')) { *eq = n; return j - i + 1; }
    return 0;
}

int bracketEnd(const QString &t, int from, int eq)
{
    const QString close = QLatin1Char(']') + QString(eq, QLatin1Char('=')) + QLatin1Char(']');
    const int e = t.indexOf(close, from);
    return e < 0 ? int(t.size()) : e + int(close.size());
}

int skipComment(const QString &t, int i)   // t[i] == '#'
{
    int eq = 0;
    if (const int open = bracketOpen(t, i + 1, &eq)) return bracketEnd(t, i + 1 + open, eq);
    const int nl = t.indexOf(QLatin1Char('\n'), i);
    return nl < 0 ? int(t.size()) : nl;
}

} // namespace

QList<Command> parse(const QString &t)
{
    QList<Command> out;
    const int n = int(t.size());
    int i = 0;
    while (i < n) {
        const QChar c = t[i];
        if (c == QLatin1Char('#')) { i = skipComment(t, i); continue; }
        if (!(c.isLetter() || c == QLatin1Char('_')) || (i > 0 && isIdentChar(t[i - 1]))) { ++i; continue; }
        int j = i;
        while (j < n && isIdentChar(t[j])) ++j;
        int k = j;
        while (k < n && (t[k] == QLatin1Char(' ') || t[k] == QLatin1Char('\t'))) ++k;
        if (k >= n || t[k] != QLatin1Char('(')) { i = j; continue; }

        Command cmd;
        cmd.name = t.mid(i, j - i).toLower();
        cmd.start = i;
        int p = k + 1, depth = 1;
        while (p < n && depth > 0) {
            const QChar ch = t[p];
            if (ch.isSpace()) { ++p; continue; }
            if (ch == QLatin1Char('#')) { p = skipComment(t, p); continue; }
            if (ch == QLatin1Char('(')) { ++depth; ++p; continue; }
            if (ch == QLatin1Char(')')) { if (--depth == 0) break; ++p; continue; }
            Token tok;
            tok.start = p;
            int eq = 0;
            if (ch == QLatin1Char('"')) {
                int q = p + 1;
                while (q < n && t[q] != QLatin1Char('"')) { if (t[q] == QLatin1Char('\\')) ++q; ++q; }
                tok.end = qMin(n, q + 1);
                tok.quoted = true;
                tok.value = t.mid(p + 1, q - p - 1);
            } else if (const int open = bracketOpen(t, p, &eq)) {
                tok.end = bracketEnd(t, p + open, eq);
                tok.quoted = true;
                tok.value = t.mid(p + open, tok.end - p - open - eq - 2);
            } else {
                int q = p;
                while (q < n && !t[q].isSpace() && t[q] != QLatin1Char('(') && t[q] != QLatin1Char(')') &&
                       t[q] != QLatin1Char('#')) {
                    if (t[q] == QLatin1Char('"')) {             // unquoted arg with a quoted part
                        ++q;
                        while (q < n && t[q] != QLatin1Char('"')) ++q;
                    }
                    ++q;
                }
                tok.end = q;
                tok.value = t.mid(p, q - p);
            }
            tok.raw = t.mid(tok.start, tok.end - tok.start);
            cmd.args << tok;
            p = tok.end;
        }
        cmd.closeParen = qMin(p, n);
        out << cmd;
        i = qMin(p + 1, n);
    }
    return out;
}

// ─── Files ───────────────────────────────────────────────────────────────────

namespace {

enum class Kind { None, Source, Header, Form, Resource };

Kind kindOf(const QString &file)
{
    const QString s = QFileInfo(file).suffix().toLower();
    static const QSet<QString> src { QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"),
                                     QStringLiteral("cxx"), QStringLiteral("c++"), QStringLiteral("m"),
                                     QStringLiteral("mm"), QStringLiteral("cu") };
    static const QSet<QString> hdr { QStringLiteral("h"), QStringLiteral("hh"), QStringLiteral("hpp"),
                                     QStringLiteral("hxx"), QStringLiteral("h++"), QStringLiteral("inl"),
                                     QStringLiteral("ipp"), QStringLiteral("tpp") };
    if (src.contains(s)) return Kind::Source;
    if (hdr.contains(s)) return Kind::Header;
    if (s == QLatin1String("ui")) return Kind::Form;
    if (s == QLatin1String("qrc")) return Kind::Resource;
    return Kind::None;
}

// Commands whose arguments are not source lists.
bool ignoredCommand(const QString &name)
{
    static const QSet<QString> skip {
        QStringLiteral("include_directories"), QStringLiteral("target_include_directories"),
        QStringLiteral("link_directories"), QStringLiteral("target_link_libraries"), QStringLiteral("message"),
        QStringLiteral("install"), QStringLiteral("file"), QStringLiteral("configure_file"),
        QStringLiteral("find_package"), QStringLiteral("include"), QStringLiteral("add_subdirectory"),
        QStringLiteral("set_source_files_properties"), QStringLiteral("set_target_properties"),
        QStringLiteral("set_property"), QStringLiteral("add_custom_command"), QStringLiteral("add_custom_target"),
        QStringLiteral("execute_process"), QStringLiteral("option"), QStringLiteral("if"), QStringLiteral("elseif"),
        QStringLiteral("list"), QStringLiteral("string"), QStringLiteral("get_filename_component"),
        QStringLiteral("target_compile_definitions"), QStringLiteral("target_compile_options"),
        QStringLiteral("source_group"), QStringLiteral("export"), QStringLiteral("cmake_path"),
    };
    return skip.contains(name);
}

// ${VAR} expansion with the variables a file defines through set(VAR value).
class Vars
{
public:
    Vars(const QString &cmakeDir, const QString &topDir)
    {
        m[QStringLiteral("CMAKE_CURRENT_SOURCE_DIR")] = cmakeDir;
        m[QStringLiteral("CMAKE_CURRENT_LIST_DIR")] = cmakeDir;
        m[QStringLiteral("CMAKE_SOURCE_DIR")] = topDir;
        m[QStringLiteral("PROJECT_SOURCE_DIR")] = topDir;
    }
    void learn(const QList<Command> &cmds)
    {
        for (const Command &c : cmds) {
            if (c.name != QLatin1String("set") || c.args.size() < 2) continue;
            int values = 0;   // set(VAR one-value [CACHE ...|PARENT_SCOPE])
            for (int i = 1; i < c.args.size(); ++i) {
                const QString v = c.args[i].value;
                if (v == QLatin1String("CACHE") || v == QLatin1String("PARENT_SCOPE")) break;
                ++values;
            }
            if (values == 1 && !m.contains(c.args[0].value)) m[c.args[0].value] = c.args[1].value;
        }
    }
    // Expanded text, or empty when a variable is unknown.
    QString expand(QString s, int depth = 0) const
    {
        static const QRegularExpression var(QStringLiteral(R"(\$\{([A-Za-z0-9_]+)\})"));
        for (auto it = var.globalMatch(s); it.hasNext();) {
            const auto mt = it.next();
            if (!m.contains(mt.captured(1))) return {};
        }
        for (auto it = m.cbegin(); it != m.cend(); ++it)
            s.replace(QStringLiteral("${%1}").arg(it.key()), it.value());
        if (s.contains(QLatin1String("${")) && depth < 5) return expand(s, depth + 1);
        return s.contains(QLatin1String("${")) ? QString() : s;
    }
private:
    QHash<QString, QString> m;
};

QString resolveArg(const Vars &vars, const QString &cmakeDir, const QString &value)
{
    const QString v = vars.expand(value);
    if (v.isEmpty() || v.contains(QLatin1Char(';')) || v.contains(QLatin1String("$<"))) return {};
    return QDir::cleanPath(QFileInfo(v).isAbsolute() ? v : QDir(cmakeDir).filePath(v));
}

} // namespace

bool isListable(const QString &file)
{
    if (kindOf(file) == Kind::None) return false;
    const QString name = QFileInfo(file).fileName();
    static const QRegularExpression generated(QStringLiteral(R"(^(moc_|ui_|qrc_|\.#|#)|~$|\.(swp|tmp)$)"));
    if (generated.match(name).hasMatch()) return false;
    static const QRegularExpression genDir(QStringLiteral(R"((/_deps/|_autogen/|/CMakeFiles/|/\.git/))"));
    return !genDir.match(file).hasMatch();
}

Edit planAdd(const QString &cmakeFile, const QString &text, const QString &topDir, const QString &fileIn)
{
    Edit e;
    e.cmakeFile = cmakeFile;
    const QString file = QDir::cleanPath(QFileInfo(fileIn).absoluteFilePath());
    e.file = file;
    const QString cmakeDir = QFileInfo(cmakeFile).absolutePath();
    const QString dir = QFileInfo(file).absolutePath();
    const Kind kind = kindOf(file);
    const QString base = QFileInfo(file).fileName();

    const QList<Command> cmds = parse(text);
    Vars vars(cmakeDir, topDir);
    vars.learn(cmds);

    struct Cand { int cmd; int score; QList<int> sameKind; QList<int> sameDir; };
    QList<Cand> cands;
    for (int ci = 0; ci < cmds.size(); ++ci) {
        const Command &c = cmds[ci];
        if (c.name == QLatin1String("file") && !c.args.isEmpty() &&
            (c.args[0].value == QLatin1String("GLOB") || c.args[0].value == QLatin1String("GLOB_RECURSE"))) {
            for (int a = 2; a < c.args.size(); ++a) {
                const QString pat = resolveArg(vars, cmakeDir, c.args[a].value);
                if (pat.isEmpty()) continue;
                const bool recurse = c.args[0].value == QLatin1String("GLOB_RECURSE");
                const QString patDir = QFileInfo(pat).absolutePath();
                if ((recurse ? file.startsWith(patDir + QLatin1Char('/')) : dir == patDir) &&
                    QDir::match(QFileInfo(pat).fileName(), base)) {
                    e.reason = QStringLiteral("picked up by file(%1) in %2 - re-run CMake to include it")
                                   .arg(c.args[0].value, QFileInfo(cmakeFile).fileName());
                    return e;
                }
            }
            continue;
        }
        if (ignoredCommand(c.name)) continue;
        Cand cand { ci, 0, {}, {} };
        for (int a = 0; a < c.args.size(); ++a) {
            const QString path = resolveArg(vars, cmakeDir, c.args[a].value);
            if (path.isEmpty() || kindOf(path) == Kind::None) continue;
            if (path == file) {
                e.reason = QStringLiteral("already listed in %1").arg(QFileInfo(cmakeFile).fileName());
                return e;
            }
            if (QFileInfo(path).absolutePath() != dir) continue;
            cand.sameDir << a;
            if (kindOf(path) == kind) cand.sameKind << a;
        }
        cand.score = int(cand.sameKind.size()) * 10 + int(cand.sameDir.size());
        if (cand.score > 0) cands << cand;
    }
    if (cands.isEmpty()) {
        e.reason = QStringLiteral("no list in %1 holds files from %2")
                       .arg(QFileInfo(cmakeFile).fileName(), QDir(topDir).relativeFilePath(dir));
        return e;
    }
    // The list holding most of its neighbours (same kind first); on a tie the
    // earlier one (a set(SOURCES ...) before the add_executable using it).
    const Cand *best = &cands.first();
    for (const Cand &c : cands) if (c.score > best->score) best = &c;
    const Command &cmd = cmds[best->cmd];
    const QList<int> &siblings = best->sameKind.isEmpty() ? best->sameDir : best->sameKind;

    // Sorted neighbours -> keep the order; otherwise append after the last one.
    bool sorted = true;
    for (int s = 1; s < siblings.size(); ++s)
        if (QFileInfo(cmd.args[siblings[s - 1]].value).fileName().compare(
                QFileInfo(cmd.args[siblings[s]].value).fileName(), Qt::CaseInsensitive) > 0)
            sorted = false;
    int before = -1;
    if (sorted && siblings.size() > 1)
        for (int a : siblings)
            if (QFileInfo(cmd.args[a].value).fileName().compare(base, Qt::CaseInsensitive) > 0) { before = a; break; }
    const int anchor = before >= 0 ? before : siblings.last();
    const Token &tok = cmd.args[anchor];

    // The new entry, written like its neighbour: same prefix / ${VAR}, quotes.
    const int slash = tok.raw.lastIndexOf(QLatin1Char('/'));
    QString entry;
    if (slash >= 0) entry = tok.raw.left(slash + 1) + base + (tok.quoted ? tok.raw.right(1) : QString());
    else entry = tok.quoted ? QStringLiteral("\"%1\"").arg(base) : base;
    if (!tok.quoted && base.contains(QLatin1Char(' '))) entry = QLatin1Char('"') + entry + QLatin1Char('"');

    // One entry per line when the neighbour has its own line; otherwise inline.
    const int lineStart = text.lastIndexOf(QLatin1Char('\n'), tok.start - 1) + 1;
    const QString lead = text.mid(lineStart, tok.start - lineStart);
    const bool ownLine = lead.trimmed().isEmpty();
    if (before >= 0) {
        e.offset = tok.start;
        e.insert = ownLine ? entry + QLatin1Char('\n') + lead : entry + QLatin1Char(' ');
    } else {
        e.offset = tok.end;
        e.insert = ownLine ? QLatin1Char('\n') + lead + entry : QLatin1Char(' ') + entry;
    }
    e.entry = entry;
    e.listName = cmd.name + QLatin1Char('(') + (cmd.args.isEmpty() ? QString() : cmd.args.first().value) + QLatin1Char(')');
    return e;
}

QStringList sourceDirs(const QString &cmakeFile, const QString &text, const QString &topDir)
{
    const QString cmakeDir = QFileInfo(cmakeFile).absolutePath();
    const QList<Command> cmds = parse(text);
    Vars vars(cmakeDir, topDir);
    vars.learn(cmds);
    QSet<QString> dirs;
    for (const Command &c : cmds) {
        if (ignoredCommand(c.name)) continue;
        for (const Token &a : c.args) {
            const QString path = resolveArg(vars, cmakeDir, a.value);
            if (!path.isEmpty() && isListable(path)) dirs.insert(QFileInfo(path).absolutePath());
        }
    }
    return QStringList(dirs.cbegin(), dirs.cend());
}

QStringList subdirectories(const QString &cmakeFile, const QString &text, const QString &topDir)
{
    const QString cmakeDir = QFileInfo(cmakeFile).absolutePath();
    const QList<Command> cmds = parse(text);
    Vars vars(cmakeDir, topDir);
    vars.learn(cmds);
    QStringList out;
    for (const Command &c : cmds) {
        if (c.name != QLatin1String("add_subdirectory") || c.args.isEmpty()) continue;
        const QString dir = resolveArg(vars, cmakeDir, c.args.first().value);
        const QString f = QDir(dir).filePath(QStringLiteral("CMakeLists.txt"));
        if (!dir.isEmpty() && QFileInfo::exists(f)) out << QDir::cleanPath(f);
    }
    return out;
}

} // namespace CMakeAuto

// ═════════════════════════════════════════════════════════════════════════════
//  CMakeAutoAdder
// ═════════════════════════════════════════════════════════════════════════════

namespace {

bool isBuildDir(const QString &path) { return QFileInfo::exists(QDir(path).filePath(QStringLiteral("CMakeCache.txt"))); }

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

CMakeAutoAdder::CMakeAutoAdder(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(400);   // let editors finish writing (and git finish checking out)
    connect(&m_debounce, &QTimer::timeout, this, &CMakeAutoAdder::processPending);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &CMakeAutoAdder::onDirectoryChanged);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { rescan(); });   // a CMakeLists.txt changed
}

void CMakeAutoAdder::setProjectRoot(const QString &dir)
{
    const QString root = dir.isEmpty() ? QString() : QDir(dir).absolutePath();
    if (root == m_root) return;
    m_root = root;
    m_snapshot.clear();
    m_pendingDirs.clear();
    rescan();
}

void CMakeAutoAdder::setEnabled(bool on)
{
    if (on == m_enabled) return;
    m_enabled = on;
    m_snapshot.clear();     // turning it back on: only files created from now on
    m_pendingDirs.clear();
    rescan();
}

void CMakeAutoAdder::rescan()
{
    if (!m_watcher->directories().isEmpty()) m_watcher->removePaths(m_watcher->directories());
    if (!m_watcher->files().isEmpty()) m_watcher->removePaths(m_watcher->files());
    // Folders seen before keep their snapshot and are re-checked below: a
    // file created while CMakeLists.txt was being rewritten must still count
    // as new.
    const QHash<QString, QSet<QString>> previous = std::exchange(m_snapshot, {});
    m_cmakeFiles.clear();
    if (!m_enabled || m_root.isEmpty() || !QFileInfo::exists(QDir(m_root).filePath(QStringLiteral("CMakeLists.txt"))))
        return;

    // The CMakeLists.txt files the build uses: the top one and whatever its
    // add_subdirectory() calls reach (a stray CMakeLists.txt in a folder the
    // build never enters must not receive files).
    QStringList queue { QDir(m_root).filePath(QStringLiteral("CMakeLists.txt")) };
    QSet<QString> dirs;
    while (!queue.isEmpty() && m_cmakeFiles.size() < 500) {
        const QString f = QDir::cleanPath(queue.takeFirst());
        if (m_cmakeFiles.contains(f) || !f.startsWith(m_root)) continue;
        m_cmakeFiles << f;
        m_watcher->addPath(f);
        const QString text = readAll(f);
        queue << CMakeAuto::subdirectories(f, text, m_root);
        for (const QString &d : CMakeAuto::sourceDirs(f, text, m_root))
            if (d.startsWith(m_root) && QDir(d).exists()) dirs.insert(d);
    }

    // Watch every folder those files list sources from.
    bool recheck = false;
    for (const QString &d : std::as_const(dirs)) {
        if (previous.contains(d)) {
            m_snapshot.insert(d, previous.value(d));
            m_pendingDirs.insert(d);
            recheck = true;
        } else {
            QSet<QString> files;
            for (const QString &name : QDir(d).entryList(QDir::Files))
                if (CMakeAuto::isListable(QDir(d).filePath(name))) files.insert(name);
            m_snapshot.insert(d, files);
        }
        m_watcher->addPath(d);
    }
    if (recheck) m_debounce.start();
}

void CMakeAutoAdder::onDirectoryChanged(const QString &dir)
{
    m_pendingDirs.insert(dir);
    m_debounce.start();
}

void CMakeAutoAdder::processPending()
{
    const QSet<QString> dirs = std::exchange(m_pendingDirs, {});
    for (const QString &d : dirs) {
        QSet<QString> now;
        for (const QString &name : QDir(d).entryList(QDir::Files))
            if (CMakeAuto::isListable(QDir(d).filePath(name))) now.insert(name);
        const QSet<QString> before = m_snapshot.value(d);
        m_snapshot.insert(d, now);
        QStringList added(now.cbegin(), now.cend());
        added.erase(std::remove_if(added.begin(), added.end(), [&](const QString &n) { return before.contains(n); }),
                    added.end());
        added.sort(Qt::CaseInsensitive);
        if (added.size() > 20) {   // a checkout or an unpacked archive, not a new file
            emit message(tr("%n new files in %1 - not added to CMakeLists.txt automatically", nullptr, int(added.size()))
                             .arg(QDir(m_root).relativeFilePath(d)));
            continue;
        }
        for (const QString &name : added) addFile(QDir(d).filePath(name));
    }
}

QStringList CMakeAutoAdder::cmakeFilesFor(const QString &file) const
{
    // The build's CMake files: those in folders enclosing the file (nearest
    // first), then the others (a top-level file may list sources anywhere).
    QStringList enclosing, others;
    const QString dir = QFileInfo(file).absolutePath();
    for (const QString &f : m_cmakeFiles) {
        const QString d = QFileInfo(f).absolutePath();
        (dir == d || dir.startsWith(d + QLatin1Char('/')) ? enclosing : others) << f;
    }
    std::sort(enclosing.begin(), enclosing.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    return enclosing + others;
}

bool CMakeAutoAdder::addFile(const QString &fileIn)
{
    if (!m_enabled || m_root.isEmpty()) return false;
    const QString file = QDir::cleanPath(QFileInfo(fileIn).absoluteFilePath());
    if (!file.startsWith(m_root + QLatin1Char('/')) || !CMakeAuto::isListable(file) || !QFileInfo::exists(file)) return false;
    for (QString d = QFileInfo(file).absolutePath(); d.startsWith(m_root) && d != m_root; d = QFileInfo(d).absolutePath())
        if (isBuildDir(d)) return false;

    QString lastReason;
    for (const QString &cmake : cmakeFilesFor(file)) {
        const CMakeAuto::Edit e = CMakeAuto::planAdd(cmake, readAll(cmake), m_root, file);
        if (e.offset < 0) {
            if (e.reason.startsWith(QLatin1String("already")) || e.reason.startsWith(QLatin1String("picked"))) {
                if (e.reason.startsWith(QLatin1String("picked")))
                    emit message(QStringLiteral("%1: %2").arg(QFileInfo(file).fileName(), e.reason));
                return false;
            }
            if (lastReason.isEmpty()) lastReason = e.reason;   // the nearest file's reason is the useful one
            continue;   // try the next CMakeLists.txt
        }
        bool ok = false;
        if (applyEdit) {
            ok = applyEdit(e);
        } else {
            QString text = readAll(cmake);
            text.insert(e.offset, e.insert);
            QSaveFile out(cmake);
            ok = out.open(QIODevice::WriteOnly) && out.write(text.toUtf8()) >= 0 && out.commit();
        }
        if (ok) emit fileAdded(file, cmake, e.listName);
        else emit message(tr("Could not add %1 to %2").arg(QFileInfo(file).fileName(), cmake));
        return ok;
    }
    if (!lastReason.isEmpty())
        emit message(tr("%1 was not added to CMakeLists.txt: %2").arg(QFileInfo(file).fileName(), lastReason));
    return false;
}
