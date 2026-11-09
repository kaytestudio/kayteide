// InFormWeb.cpp - KayteIDE InForm plugin
#include "InFormWeb.hpp"
#include "InFormKayte.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace InForm {

namespace {

QString readResource(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString cssColor(const QColor &c)
{
    return c.alpha() == 255 ? c.name(QColor::HexRgb)
                            : QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue())
                                  .arg(c.alphaF(), 0, 'f', 3);
}

bool isSymbol(const Control &c, const char *prop, const char *symbol)
{
    return c.symbol(QLatin1String(prop)).compare(QLatin1String(symbol), Qt::CaseInsensitive) == 0;
}

// OnClick=SubName / OnClick=SubName() from a .kfm control -> { "onclick": "SubName" }
QJsonObject handlersOf(const Control &c)
{
    QJsonObject h;
    for (const QString &line : c.kfmExtra) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0) continue;
        const QString key = line.left(eq).trimmed().toLower();
        if (!key.startsWith(QLatin1String("on")) || key.size() <= 2) continue;
        QString sub = line.mid(eq + 1).trimmed();
        if (sub.size() >= 2 && sub.startsWith(QLatin1Char('"')) && sub.endsWith(QLatin1Char('"')))
            sub = sub.mid(1, sub.size() - 2);
        if (sub.endsWith(QLatin1String("()"))) sub.chop(2);
        if (!sub.isEmpty()) h.insert(key, sub);
    }
    return h;
}

bool isIdentStart(QChar c) { return c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('$'); }
bool isIdentChar(QChar c)  { return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$'); }

} // namespace

// ─── Form -> JSON ─────────────────────────────────────────────────────────────

QString webFormJson(const Document &doc, const QString &kfmFileName)
{
    QJsonObject form;
    const Control *f = doc.form();
    form.insert(QStringLiteral("name"), f ? f->name : QStringLiteral("Form1"));
    form.insert(QStringLiteral("file"), kfmFileName);
    form.insert(QStringLiteral("caption"), f ? (f->caption.isEmpty() ? f->name : f->caption) : QStringLiteral("Form1"));
    form.insert(QStringLiteral("width"), f ? f->geometry.width() : 400);
    form.insert(QStringLiteral("height"), f ? f->geometry.height() : 300);
    if (f && !f->contextMenu.isEmpty()) form.insert(QStringLiteral("contextMenu"), f->contextMenu);

    QJsonArray controls;
    for (const Control &c : doc.controls()) {
        if (c.type == ControlType::Form || c.type == ControlType::MenuPanel || c.type == ControlType::Font)
            continue;
        QJsonObject o;
        o.insert(QStringLiteral("type"), QLatin1String(typeInfo(c.type).name));
        o.insert(QStringLiteral("name"), c.name);
        o.insert(QStringLiteral("parent"), c.parentName);
        o.insert(QStringLiteral("x"), c.geometry.x());
        o.insert(QStringLiteral("y"), c.geometry.y());
        o.insert(QStringLiteral("w"), c.geometry.width());
        o.insert(QStringLiteral("h"), c.geometry.height());
        o.insert(QStringLiteral("caption"), c.caption);
        o.insert(QStringLiteral("text"), c.text);
        if (!c.toolTip.isEmpty()) o.insert(QStringLiteral("tooltip"), c.toolTip);
        // For controls a .kfm holds, the .kjs program adds the list items and
        // initial values (kayteProgram writes those calls), exactly as on the
        // desktop - the page must not add them a second time.
        const bool programInitialises = !kfmControlClass(c.type).isEmpty();
        if (!c.items.isEmpty() && !programInitialises)
            o.insert(QStringLiteral("items"), QJsonArray::fromStringList(c.items));

        o.insert(QStringLiteral("value"), programInitialises ? 0.0 : c.number(QStringLiteral("Value")));
        o.insert(QStringLiteral("hasValue"), c.properties.contains(QStringLiteral("Value")));
        o.insert(QStringLiteral("min"), c.number(QStringLiteral("Min")));
        o.insert(QStringLiteral("max"), c.number(QStringLiteral("Max"), c.type == ControlType::TrackBar ? 10 : 100));
        o.insert(QStringLiteral("minInterval"), c.number(QStringLiteral("MinInterval")));
        o.insert(QStringLiteral("disabled"), c.flag(QStringLiteral("Disabled")));
        o.insert(QStringLiteral("hidden"), c.flag(QStringLiteral("Hidden")));
        if (c.properties.contains(QStringLiteral("HasBorder")))
            o.insert(QStringLiteral("hasBorder"), c.flag(QStringLiteral("HasBorder")));

        o.insert(QStringLiteral("align"), isSymbol(c, "Align", kCenter) ? QStringLiteral("center")
                                        : isSymbol(c, "Align", kRight) ? QStringLiteral("right") : QStringLiteral("left"));
        o.insert(QStringLiteral("valign"), isSymbol(c, "VAlign", kMiddle) ? QStringLiteral("middle")
                                         : isSymbol(c, "VAlign", kBottom) ? QStringLiteral("bottom") : QStringLiteral("top"));
        o.insert(QStringLiteral("wordWrap"), c.flag(QStringLiteral("WordWrap")));
        o.insert(QStringLiteral("password"), c.flag(QStringLiteral("PasswordField")));
        o.insert(QStringLiteral("multiline"), c.flag(QStringLiteral("Multiline")));
        o.insert(QStringLiteral("numeric"), isSymbol(c, "NumericOnly", kNumericWithBounds) ? QStringLiteral("bounds")
                                          : c.flag(QStringLiteral("NumericOnly")) ? QStringLiteral("any")
                                                                                  : QStringLiteral("none"));
        o.insert(QStringLiteral("transparent"), isSymbol(c, "BackStyle", kTransparent));
        o.insert(QStringLiteral("stretch"), c.flag(QStringLiteral("Stretch")));
        o.insert(QStringLiteral("showPercentage"), c.flag(QStringLiteral("ShowPercentage")));
        o.insert(QStringLiteral("bullet"), isSymbol(c, "BulletStyle", kBullet));
        o.insert(QStringLiteral("default"), c.isDefaultButton);
        if (c.properties.contains(QStringLiteral("ForeColor")))
            o.insert(QStringLiteral("foreColor"), cssColor(c.color(QStringLiteral("ForeColor"))));
        if (c.properties.contains(QStringLiteral("BackColor")))
            o.insert(QStringLiteral("backColor"), cssColor(c.color(QStringLiteral("BackColor"))));
        if (c.fontSize > 0) o.insert(QStringLiteral("fontSize"), c.fontSize);
        if (!c.contextMenu.isEmpty()) o.insert(QStringLiteral("contextMenu"), c.contextMenu);
        const QJsonObject handlers = handlersOf(c);
        if (!handlers.isEmpty()) o.insert(QStringLiteral("handlers"), handlers);
        controls.append(o);
    }
    form.insert(QStringLiteral("controls"), controls);
    return QString::fromUtf8(QJsonDocument(form).toJson(QJsonDocument::Compact));
}

// ─── .kjs -> browser JavaScript ───────────────────────────────────────────────

KjsTranslation translateKjsForBrowser(const QString &src)
{
    KjsTranslation out;
    QString &js = out.js;
    js.reserve(src.size() + 256);
    int loopId = 0;
    bool warnedDivision = false, warnedWait = false, warnedProcess = false;
    const int n = int(src.size());

    auto lineOf = [&src](int pos) { return int(src.left(pos).count(QLatin1Char('\n'))) + 1; };
    auto skipSpaces = [&](int p, bool newlines) {
        while (p < n && (src[p] == QLatin1Char(' ') || src[p] == QLatin1Char('\t') ||
                         (newlines && (src[p] == QLatin1Char('\n') || src[p] == QLatin1Char('\r')))))
            ++p;
        return p;
    };
    auto wordAt = [&](int p) {
        int e = p;
        while (e < n && isIdentChar(src[e])) ++e;
        return src.mid(p, e - p);
    };
    // Copies a string literal starting at p (on its quote); returns the index after it.
    auto stringEnd = [&](int p) {
        const QChar q = src[p];
        for (++p; p < n; ++p) {
            if (src[p] == QLatin1Char('\\')) { ++p; continue; }
            if (src[p] == q) return p + 1;
            if (src[p] == QLatin1Char('\n') && q != QLatin1Char('`')) return p; // unterminated - stop at line end
        }
        return n;
    };

    int i = 0;
    while (i < n) {
        const QChar c = src[i];

        // Comments and strings pass through untouched.
        if (c == QLatin1Char('/') && i + 1 < n && src[i + 1] == QLatin1Char('/')) {
            int e = src.indexOf(QLatin1Char('\n'), i);
            if (e < 0) e = n;
            js += src.mid(i, e - i);
            i = e;
            continue;
        }
        if (c == QLatin1Char('/') && i + 1 < n && src[i + 1] == QLatin1Char('*')) {
            int e = src.indexOf(QStringLiteral("*/"), i + 2);
            e = e < 0 ? n : e + 2;
            js += src.mid(i, e - i);
            i = e;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`')) {
            const int e = stringEnd(i);
            js += src.mid(i, e - i);
            i = e;
            continue;
        }
        if (c == QLatin1Char('/')) {
            if (!warnedDivision) {
                out.warnings << QStringLiteral("line %1: in .kjs, / divides whole numbers (7 / 2 is 3); in a "
                                               "browser it gives fractions (3.5) - use Math.trunc(a / b) where "
                                               "the program relies on whole numbers").arg(lineOf(i));
                warnedDivision = true;
            }
            js += c;
            ++i;
            continue;
        }

        if (!isIdentStart(c) || (i > 0 && (isIdentChar(src[i - 1]) || src[i - 1] == QLatin1Char('.')))) {
            js += c;
            ++i;
            continue;
        }

        const QString word = wordAt(i);
        const int after = i + int(word.size());

        // let / const: in .kjs a declaration is local to the whole function
        // (blocks have no scope of their own) - exactly what var does.
        if ((word == QLatin1String("let") || word == QLatin1String("const")) &&
            skipSpaces(after, true) < n && isIdentStart(src[skipSpaces(after, true)])) {
            js += QLatin1String("var");
            i = after;
            continue;
        }

        // call f()  ->  f()
        if (word == QLatin1String("call")) {
            const int p = skipSpaces(after, false);
            if (p < n && isIdentStart(src[p])) { i = p; continue; }
        }

        // for i from A to B [step S]  ->  a counting for loop, bounds evaluated once
        if (word == QLatin1String("for")) {
            int p = skipSpaces(after, false);
            const QString var = (p < n && isIdentStart(src[p])) ? wordAt(p) : QString();
            if (!var.isEmpty()) {
                p = skipSpaces(p + int(var.size()), false);
                if (wordAt(p) == QLatin1String("from")) {
                    p += 4;
                    // Split the rest of the header at the words "to" / "step" (depth 0,
                    // outside strings), ending at "{" or the end of the line.
                    QString parts[3];
                    int part = 0, depth = 0, q = p;
                    for (; q < n; ++q) {
                        const QChar ch = src[q];
                        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'') || ch == QLatin1Char('`')) {
                            const int e = stringEnd(q);
                            parts[part] += src.mid(q, e - q);
                            q = e - 1;
                            continue;
                        }
                        if (ch == QLatin1Char('(') || ch == QLatin1Char('[')) ++depth;
                        if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) --depth;
                        if (depth == 0 && (ch == QLatin1Char('{') || ch == QLatin1Char('\n'))) break;
                        if (depth == 0 && isIdentStart(ch) && !isIdentChar(src[q - 1])) {
                            const QString w = wordAt(q);
                            if ((w == QLatin1String("to") && part == 0) || (w == QLatin1String("step") && part == 1)) {
                                ++part;
                                q += int(w.size()) - 1;
                                continue;
                            }
                        }
                        parts[part] += ch;
                    }
                    if (part >= 1) {
                        ++loopId;
                        const QString to = QStringLiteral("__kayteTo%1").arg(loopId);
                        const QString st = QStringLiteral("__kayteStep%1").arg(loopId);
                        const QString step = part == 2 ? parts[2].trimmed() : QStringLiteral("1");
                        js += QStringLiteral("for (var %1 = (%2), %3 = (%4), %5 = (%6); %5 >= 0 ? %1 <= %3 : %1 >= %3; %1 += %5) ")
                                  .arg(var, parts[0].trimmed(), to, parts[1].trimmed(), st, step);
                        i = q; // continue at "{" (or the line end before a one-line body)
                        continue;
                    }
                }
            }
        }

        // Things a browser cannot do the desktop way.
        if (word == QLatin1String("qt")) {
            int p = skipSpaces(after, false);
            if (p < n && src[p] == QLatin1Char('(')) {
                p = skipSpaces(p + 1, false);
                if (p < n && (src[p] == QLatin1Char('"') || src[p] == QLatin1Char('\''))) {
                    const QString cmd = src.mid(p + 1, stringEnd(p) - p - 2).toLower();
                    if ((cmd == QLatin1String("wait") || cmd == QLatin1String("poll")) && !warnedWait) {
                        out.warnings << QStringLiteral("line %1: qt(\"%2\") blocks until an event - a browser can't; "
                                                       "use <Control>_Click functions or qt(\"on\", ...) instead")
                                            .arg(lineOf(i)).arg(cmd);
                        warnedWait = true;
                    }
                }
            }
        }
        if ((word == QLatin1String("process") || word == QLatin1String("qml")) && !warnedProcess) {
            const int p = skipSpaces(after, false);
            if (p < n && src[p] == QLatin1Char('(')) {
                out.warnings << QStringLiteral("line %1: %2() is not available in a browser").arg(lineOf(i)).arg(word);
                warnedProcess = true;
            }
        }

        js += word;
        i = after;
    }
    return out;
}

// ─── Runtime files and the page ──────────────────────────────────────────────

QString webRuntimeScript() { return readResource(QStringLiteral(":/kayte-web/kayte-web.js")); }
QString webRuntimeStyle()  { return readResource(QStringLiteral(":/kayte-web/kayte-web.css")); }

QString webPage(const Document &doc, const QString &kfmFileName,
                const QString &programScript, const QString &title)
{
    // JSON inside <script> must not contain "</" (it could close the element).
    QString json = webFormJson(doc, kfmFileName);
    json.replace(QLatin1String("</"), QLatin1String("<\\/")).replace(QLatin1String("<!--"), QLatin1String("<\\!--"));
    const QString t = title.toHtmlEscaped();
    return QStringLiteral(
               "<!doctype html>\n"
               "<html lang=\"en\">\n"
               "<head>\n"
               "<meta charset=\"utf-8\">\n"
               "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
               "<meta name=\"generator\" content=\"KayteIDE\">\n"
               "<title>%1</title>\n"
               "<link rel=\"stylesheet\" href=\"kayte-web.css\">\n"
               "</head>\n"
               "<body>\n"
               "<main id=\"kayte-app\" aria-label=\"%1\"></main>\n"
               "<pre id=\"kayte-console\" aria-live=\"polite\" hidden></pre>\n"
               "<!-- The form, as KayteIDE designed it; qt(\"loadform\", \"%2\") builds it. -->\n"
               "<script type=\"application/json\" class=\"kayte-form\" data-file=\"%2\">%3</script>\n"
               "<script src=\"kayte-web.js\"></script>\n"
               "<script src=\"%4\"></script>\n"
               "</body>\n"
               "</html>\n")
        .arg(t, kfmFileName.toHtmlEscaped(), json, programScript.toHtmlEscaped());
}

bool exportWebApp(const Document &docIn, const QString &kjsPath, const QString &kfmFileName,
                  const QString &outDir, const QString &assetDir, WebExport *result, QString *error)
{
    auto fail = [error](const QString &msg) { if (error) *error = msg; return false; };
    WebExport r;

    QFile kjs(kjsPath);
    if (!kjs.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("cannot read %1: %2").arg(kjsPath, kjs.errorString()));
    const KjsTranslation tr = translateKjsForBrowser(QString::fromUtf8(kjs.readAll()));
    r.warnings << tr.warnings;

    if (!QDir().mkpath(outDir)) return fail(QStringLiteral("cannot create %1").arg(outDir));
    const QDir out(outDir);

    // Images the form shows are copied next to the page; the page refers to them
    // by a relative path so the folder can be served from anywhere.
    Document doc = docIn;
    QSet<QString> copied;
    for (Control &c : doc.controls()) {
        if (c.text.isEmpty() || (c.type != ControlType::PictureBox && c.type != ControlType::Button &&
                                 c.type != ControlType::MenuItem))
            continue;
        const QFileInfo src(QFileInfo(c.text).isAbsolute() ? c.text : QDir(assetDir).filePath(c.text));
        if (!src.exists()) {
            r.warnings << QStringLiteral("%1: image %2 not found - left as is").arg(c.name, c.text);
            continue;
        }
        const QString rel = QStringLiteral("assets/") + src.fileName();
        if (!copied.contains(rel)) {
            out.mkpath(QStringLiteral("assets"));
            QFile::remove(out.filePath(rel));
            if (!QFile::copy(src.absoluteFilePath(), out.filePath(rel)))
                return fail(QStringLiteral("cannot copy %1").arg(src.absoluteFilePath()));
            copied.insert(rel);
            r.files << out.filePath(rel);
        }
        c.text = rel;
        if (c.type == ControlType::Button || c.type == ControlType::MenuItem)
            r.warnings << QStringLiteral("%1: button / menu icons are not shown on the web page").arg(c.name);
    }

    const QString programJs = QFileInfo(kjsPath).completeBaseName() + QStringLiteral(".js");
    const Control *form = doc.form();
    const QString title = form ? (form->caption.isEmpty() ? form->name : form->caption) : QStringLiteral("Kayte");

    const struct { QString name; QString content; } files[] = {
        { QStringLiteral("index.html"), webPage(doc, kfmFileName, programJs, title) },
        { QStringLiteral("kayte-web.js"), webRuntimeScript() },
        { QStringLiteral("kayte-web.css"), webRuntimeStyle() },
        { programJs, QStringLiteral("// Generated by KayteIDE from %1 for kayte-web.js - edit the .kjs, not this file.\n")
                         .arg(QFileInfo(kjsPath).fileName()) + tr.js },
    };
    for (const auto &f : files) {
        if (f.content.isEmpty()) return fail(QStringLiteral("%1 is missing from the InForm plugin's resources").arg(f.name));
        QSaveFile sf(out.filePath(f.name));
        if (!sf.open(QIODevice::WriteOnly | QIODevice::Text) || sf.write(f.content.toUtf8()) < 0 || !sf.commit())
            return fail(QStringLiteral("cannot write %1").arg(out.filePath(f.name)));
        r.files << out.filePath(f.name);
    }
    r.indexPath = out.filePath(QStringLiteral("index.html"));
    if (result) *result = r;
    return true;
}

} // namespace InForm
