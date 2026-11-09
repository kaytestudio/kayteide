// InFormDocument.cpp - KayteIDE InForm plugin
// .frm reader/writer. The format is the QB64 code InForm's SaveForm emits
// (see InForm/UiEditor.bas): one __UI_NewControl call per control followed by
// property assignments, all inside SUB __UI_LoadForm.
#include "InFormDocument.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <optional>

namespace InForm {

// ─────────────────────────────────────────────────────────────────────────────
//  QB64 lexical helpers
// ─────────────────────────────────────────────────────────────────────────────

QString encodeBasicString(const QString &text)
{
    // Same rule as UiEditor's SpecialCharsToEscapeCode$: control chars, '"'
    // and '\' become \<code>; so the result is always a valid QB64 literal.
    QString out = QStringLiteral("\"");
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u < 32 || u == '"' || u == '\\')
            out += QLatin1Char('\\') + QString::number(u) + QLatin1Char(';');
        else
            out += c;
    }
    return out + QLatin1Char('"');
}

QString decodeBasicString(const QString &text)
{
    static const QRegularExpression esc(QStringLiteral(R"(\\(\d{1,3});)"));
    QString out;
    int last = 0;
    auto it = esc.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        out += QChar(m.captured(1).toUShort());
        last = m.capturedEnd();
    }
    return out + text.mid(last);
}

namespace {

// Splits "a, f(b, c), \"d,e\"" at top-level commas.
QStringList splitArgs(const QString &s)
{
    QStringList out;
    QString cur;
    int depth = 0;
    bool inStr = false;
    for (const QChar c : s) {
        if (c == QLatin1Char('"')) inStr = !inStr;
        if (!inStr) {
            if (c == QLatin1Char('(')) ++depth;
            else if (c == QLatin1Char(')')) --depth;
            else if (c == QLatin1Char(',') && depth == 0) {
                out << cur.trimmed();
                cur.clear();
                continue;
            }
        }
        cur += c;
    }
    if (!cur.trimmed().isEmpty() || !out.isEmpty()) out << cur.trimmed();
    return out;
}

// Strips a trailing ' comment that is not inside a string literal.
QString stripComment(const QString &line)
{
    bool inStr = false;
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == QLatin1Char('"')) inStr = !inStr;
        else if (line[i] == QLatin1Char('\'') && !inStr) return line.left(i);
    }
    return line;
}

// Evaluates a string expression: "lit" [+ CHR$(n) | + "lit"]...
std::optional<QString> evalString(const QString &expr)
{
    static const QRegularExpression chr(QStringLiteral(R"(^CHR\$\s*\(\s*(\d+)\s*\)$)"),
                                        QRegularExpression::CaseInsensitiveOption);
    QString result;
    QString term;
    bool inStr = false;
    auto flush = [&]() -> bool {
        const QString t = term.trimmed();
        term.clear();
        if (t.size() >= 2 && t.startsWith(QLatin1Char('"')) && t.endsWith(QLatin1Char('"'))) {
            result += decodeBasicString(t.mid(1, t.size() - 2));
            return true;
        }
        const auto m = chr.match(t);
        if (m.hasMatch()) { result += QChar(m.captured(1).toUShort()); return true; }
        return false;
    };
    for (const QChar c : expr) {
        if (c == QLatin1Char('"')) inStr = !inStr;
        if (c == QLatin1Char('+') && !inStr) {
            if (!flush()) return std::nullopt;
            continue;
        }
        term += c;
    }
    if (!flush()) return std::nullopt;
    return result;
}

std::optional<double> evalNumber(const QString &expr)
{
    QString t = expr.trimmed();
    if (t.compare(QLatin1String("True"),  Qt::CaseInsensitive) == 0) return -1.0;
    if (t.compare(QLatin1String("False"), Qt::CaseInsensitive) == 0) return 0.0;
    // QB64 type suffixes: ! # % & ~ ##
    while (!t.isEmpty() && QStringLiteral("!#%&~").contains(t.back())) t.chop(1);
    bool ok = false;
    const double v = t.toDouble(&ok);
    if (ok) return v;
    return std::nullopt;
}

// Parses a property right-hand side into the QVariant kinds InForm::Control uses.
std::optional<QVariant> evalProperty(const QString &expr)
{
    const QString t = expr.trimmed();
    if (t.compare(QLatin1String("True"),  Qt::CaseInsensitive) == 0) return QVariant(true);
    if (t.compare(QLatin1String("False"), Qt::CaseInsensitive) == 0) return QVariant(false);

    static const QRegularExpression rgb(
        QStringLiteral(R"(^_RGBA?(?:32)?\s*\((.*)\)$)"), QRegularExpression::CaseInsensitiveOption);
    if (const auto m = rgb.match(t); m.hasMatch()) {
        const QStringList a = splitArgs(m.captured(1));
        QList<int> v;
        for (const QString &s : a) {
            const auto n = evalNumber(s);
            if (!n) return std::nullopt;
            v << int(*n);
        }
        if (v.size() == 3) return QVariant(QColor(v[0], v[1], v[2]));
        if (v.size() == 4) return QVariant(QColor(v[0], v[1], v[2], v[3]));
        return std::nullopt;
    }

    static const QRegularExpression symbol(QStringLiteral(R"(^__UI_[A-Za-z0-9_]+$)"));
    if (symbol.match(t).hasMatch()) return QVariant(t);

    if (const auto n = evalNumber(t)) return QVariant(*n);
    return std::nullopt;
}

QString formatNumber(double v)
{
    return QString::number(v, 'g', 15);
}

QString formatProperty(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Bool:
        return v.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    case QMetaType::QColor: {
        const QColor c = v.value<QColor>();
        if (c.alpha() == 255)
            return QStringLiteral("_RGB32(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue());
        return QStringLiteral("_RGBA32(%1, %2, %3, %4)")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    }
    case QMetaType::QString:
        return v.toString();
    default:
        return formatNumber(v.toDouble());
    }
}

// "__UI_GetID(\"Name\")" -> Name   (empty when the expression is something else)
QString getIdName(const QString &expr)
{
    static const QRegularExpression re(QStringLiteral(R"re(^__UI_GetID\s*\(\s*"([^"]*)"\s*\)$)re"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(expr.trimmed());
    return m.hasMatch() ? m.captured(1) : QString();
}

bool isContainerPass(ControlType t)
{
    return t == ControlType::Form || t == ControlType::Frame ||
           t == ControlType::MenuBar || t == ControlType::ContextMenu;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Document - queries
// ─────────────────────────────────────────────────────────────────────────────

Control *Document::find(const QString &name)
{
    for (Control &c : m_controls)
        if (c.name.compare(name, Qt::CaseInsensitive) == 0) return &c;
    return nullptr;
}

const Control *Document::find(const QString &name) const
{
    return const_cast<Document *>(this)->find(name);
}

Control *Document::form()
{
    for (Control &c : m_controls)
        if (c.type == ControlType::Form) return &c;
    return nullptr;
}

const Control *Document::form() const
{
    return const_cast<Document *>(this)->form();
}

QStringList Document::childrenOf(const QString &name) const
{
    QStringList out;
    for (const Control &c : m_controls)
        if (c.parentName.compare(name, Qt::CaseInsensitive) == 0) out << c.name;
    return out;
}

QPoint Document::absolutePos(const QString &name) const
{
    QPoint p;
    QSet<QString> seen; // guards against parent cycles in hand-edited files
    for (const Control *c = find(name); c && !seen.contains(c->name); ) {
        seen.insert(c->name);
        p += c->geometry.topLeft();
        c = c->parentName.isEmpty() ? nullptr : find(c->parentName);
    }
    return p;
}

void Document::clear()
{
    m_controls.clear();
    m_bindings.clear();
    m_extraLines.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Document - .frm reader
// ─────────────────────────────────────────────────────────────────────────────

bool Document::loadFrm(const QString &filePath, QString *error)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    // QB64 sources are code page 437/1252; Latin-1 keeps every byte intact.
    return parseFrm(QString::fromLatin1(f.readAll()), error);
}

bool Document::parseFrm(const QString &source, QString *error)
{
    clear();

    // Join QB64 line continuations (" _" at end of line).
    QStringList lines;
    {
        QString pending;
        for (QString raw : source.split(QLatin1Char('\n'))) {
            raw.remove(QLatin1Char('\r'));
            if (raw.endsWith(QLatin1String(" _"))) { pending += raw.chopped(1); continue; }
            lines << pending + raw;
            pending.clear();
        }
        if (!pending.isEmpty()) lines << pending;
    }

    static const QRegularExpression reNewControl(
        QStringLiteral(R"(^__UI_NewID\s*=\s*__UI_NewControl\s*\((.*)\)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSetCaption(
        QStringLiteral(R"(^SetCaption\s+__UI_NewID\s*,\s*(.*)$)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reAddItem(
        QStringLiteral(R"(^AddItem\s+__UI_NewID\s*,\s*(.*)$)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reArray(
        QStringLiteral(R"(^(ToolTip|Text|Mask)\s*\(\s*__UI_NewID\s*\)\s*=\s*(.*)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reLoadImage(
        QStringLiteral(R"(^LoadImage\s+Control\s*\(\s*__UI_NewID\s*\)\s*,\s*(.*)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reOpenGif(
        QStringLiteral(R"(^__UI_RegisterResult\s*=\s*OpenGif\s*\(\s*__UI_NewID\s*,\s*(.*)\)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rePlayGif(
        QStringLiteral(R"(^IF\s+__UI_RegisterResult\s+THEN\s+PlayGif\s+__UI_NewID$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reProperty(
        QStringLiteral(R"(^Control\s*\(\s*(__UI_NewID|__UI_FormID)\s*\)\s*\.\s*([A-Za-z_]\w*)\s*=\s*(.*)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSetFont(
        QStringLiteral(R"(^SetFont\s*\((.*)\)$)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reDefault(
        QStringLiteral(R"(^__UI_DefaultButtonID\s*=\s*(.*)$)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reBind(
        QStringLiteral(R"(^__UI_Bind\s+(.*)$)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reExeIcon(
        QStringLiteral(R"(^\$EXEICON\s*:\s*'([^']*)')"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reResize(
        QStringLiteral(R"(^\$RESIZE\s*:\s*ON)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSub(
        QStringLiteral(R"(^SUB\s+([A-Za-z_]\w*))"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reEndSub(
        QStringLiteral(R"(^END\s+SUB\b)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reMetaIf(
        QStringLiteral(R"(^\$(IF|ELSEIF|ELSE|END\s*IF)\b)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reIgnore(
        QStringLiteral(R"(^(DIM\s|__UI_RegisterResult\s*=\s*0$))"), QRegularExpression::CaseInsensitiveOption);

    int  conditionalDepth = 0; // inside $IF ... $END IF
    bool inLoadForm = false;
    bool sawLoadForm = false;
    QString exeIcon;
    bool resizable = false;
    Control *cur = nullptr;
    QString formContextMenu;

    auto keepExtra = [&](const QString &stmt) {
        if (cur) cur->extraLines << stmt;
        else     m_extraLines << stmt;
    };

    for (int ln = 0; ln < lines.size(); ++ln) {
        const QString trimmed = lines[ln].trimmed();
        if (trimmed.isEmpty()) continue;

        // Metacommands live inside a comment-like syntax - handle them first.
        if (const auto m = reExeIcon.match(trimmed); m.hasMatch()) { exeIcon = m.captured(1); continue; }
        if (reResize.match(trimmed).hasMatch()) { resizable = true; continue; }

        const QString stmt = stripComment(trimmed).trimmed();
        if (stmt.isEmpty()) continue;

        if (const auto m = reSub.match(stmt); m.hasMatch()) {
            inLoadForm = m.captured(1).compare(QLatin1String("__UI_LoadForm"), Qt::CaseInsensitive) == 0;
            sawLoadForm = sawLoadForm || inLoadForm;
            continue;
        }
        if (reEndSub.match(stmt).hasMatch()) { inLoadForm = false; cur = nullptr; continue; }
        if (!inLoadForm) continue; // __UI_AssignIDs is regenerated on save
        if (reIgnore.match(stmt).hasMatch()) continue;

        // Platform conditionals are kept verbatim, and so is whatever they
        // guard - except new controls, which still have to exist in the model.
        if (const auto m = reMetaIf.match(stmt); m.hasMatch()) {
            const QString kw = m.captured(1).toUpper();
            if (kw == QLatin1String("IF")) ++conditionalDepth;
            else if (kw.startsWith(QLatin1String("END"))) conditionalDepth = qMax(0, conditionalDepth - 1);
            keepExtra(stmt);
            continue;
        }
        if (conditionalDepth > 0 && !reNewControl.match(stmt).hasMatch()) { keepExtra(stmt); continue; }

        if (const auto m = reNewControl.match(stmt); m.hasMatch()) {
            const QStringList a = splitArgs(m.captured(1));
            if (a.size() < 7) {
                if (error) *error = QStringLiteral("line %1: malformed __UI_NewControl").arg(ln + 1);
                return false;
            }
            Control c;
            c.type = typeFromName(a[0]);
            const auto name = evalString(a[1]);
            if (c.type == ControlType::None || !name) {
                if (error) *error = QStringLiteral("line %1: unknown control type or name").arg(ln + 1);
                return false;
            }
            c.name = *name;
            c.geometry = QRect(int(evalNumber(a[4]).value_or(0)), int(evalNumber(a[5]).value_or(0)),
                               int(evalNumber(a[2]).value_or(0)), int(evalNumber(a[3]).value_or(0)));
            c.parentName = getIdName(a[6]);
            m_controls << c;
            cur = &m_controls.last();
            continue;
        }

        if (const auto m = reBind.match(stmt); m.hasMatch()) {
            const QStringList a = splitArgs(m.captured(1));
            if (a.size() == 4) {
                Binding b { getIdName(a[0]), getIdName(a[1]),
                            evalString(a[2]).value_or(QString()), evalString(a[3]).value_or(QString()) };
                if (!b.source.isEmpty() && !b.target.isEmpty()) { m_bindings << b; continue; }
            }
            m_extraLines << stmt;
            continue;
        }

        if (const auto m = reDefault.match(stmt); m.hasMatch()) {
            const QString rhs = m.captured(1).trimmed();
            if (rhs.compare(QLatin1String("__UI_NewID"), Qt::CaseInsensitive) == 0 && cur)
                cur->isDefaultButton = true;
            else if (Control *c = find(rhs))
                c->isDefaultButton = true;
            else
                keepExtra(stmt);
            continue;
        }

        if (const auto m = reProperty.match(stmt); m.hasMatch()) {
            const bool onForm = m.captured(1).compare(QLatin1String("__UI_FormID"), Qt::CaseInsensitive) == 0;
            const QString prop = m.captured(2);
            const QString rhs  = m.captured(3).trimmed();

            if (prop.compare(QLatin1String("ContextMenuID"), Qt::CaseInsensitive) == 0) {
                const QString menu = getIdName(rhs);
                if (!menu.isEmpty()) {
                    if (onForm)   formContextMenu = menu;
                    else if (cur) cur->contextMenu = menu;
                    continue;
                }
            }
            if (!onForm && cur && prop.compare(QLatin1String("Font"), Qt::CaseInsensitive) == 0) {
                if (const auto f = reSetFont.match(rhs); f.hasMatch()) {
                    const QStringList a = splitArgs(f.captured(1));
                    if (a.size() == 2) {
                        const auto files = evalString(a[0]);
                        const auto size  = evalNumber(a[1]);
                        if (files && size) { cur->fontFiles = *files; cur->fontSize = int(*size); continue; }
                    }
                }
            }
            if (!onForm && cur) {
                if (const auto v = evalProperty(rhs)) { cur->properties.insert(prop, *v); continue; }
            }
            keepExtra(stmt);
            continue;
        }

        if (!cur) { m_extraLines << stmt; continue; }

        if (const auto m = reSetCaption.match(stmt); m.hasMatch()) {
            if (const auto s = evalString(m.captured(1))) { cur->caption = *s; continue; }
        } else if (const auto m = reAddItem.match(stmt); m.hasMatch()) {
            if (const auto s = evalString(m.captured(1))) { cur->items << *s; continue; }
        } else if (const auto m = reArray.match(stmt); m.hasMatch()) {
            if (const auto s = evalString(m.captured(2))) {
                const QString which = m.captured(1).toLower();
                if (which == QLatin1String("tooltip")) cur->toolTip = *s;
                else if (which == QLatin1String("text")) cur->text = *s;
                else cur->mask = *s;
                continue;
            }
        } else if (const auto m = reLoadImage.match(stmt); m.hasMatch()) {
            if (const auto s = evalString(m.captured(1))) { cur->text = *s; continue; }
        } else if (const auto m = reOpenGif.match(stmt); m.hasMatch()) {
            if (const auto s = evalString(m.captured(1))) { cur->text = *s; cur->animatedGif = true; continue; }
        } else if (rePlayGif.match(stmt).hasMatch()) {
            cur->autoPlayGif = true;
            continue;
        }
        keepExtra(stmt);
    }

    if (!sawLoadForm) {
        if (error) *error = QStringLiteral("no SUB __UI_LoadForm found - not an InForm .frm file");
        return false;
    }

    if (Control *f = form()) {
        if (!exeIcon.isEmpty() && f->text.isEmpty()) f->text = exeIcon;
        if (resizable) f->properties.insert(QStringLiteral("CanResize"), true);
        if (!formContextMenu.isEmpty()) f->contextMenu = formContextMenu;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Document - .frm writer
// ─────────────────────────────────────────────────────────────────────────────

bool Document::saveFrm(const QString &filePath, QString *error) const
{
    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(toFrm().toLatin1());
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

QString Document::toFrm() const
{
    QString out;
    QTextStream s(&out);
    const QString in = QStringLiteral("    ");

    s << "': This form was generated by\n"
      << "': InForm - GUI library for QB64 (KayteIDE Qt6 port)\n"
      << "': Original InForm by Fellippe Heitor - https://github.com/FellippeHeitor/InForm\n"
      << "'-----------------------------------------------------------\n"
      << "SUB __UI_LoadForm\n\n";

    const Control *frm = form();
    if (frm && !frm->text.isEmpty())
        s << in << "$EXEICON:'" << frm->text << "'\n";
    if (frm && frm->flag(QStringLiteral("CanResize")))
        s << in << "$RESIZE:ON\n";
    s << in << "DIM __UI_NewID AS LONG, __UI_RegisterResult AS LONG\n\n";

    for (const QString &l : m_extraLines) s << in << l << '\n';
    if (!m_extraLines.isEmpty()) s << '\n';

    // InForm writes the form and containers first, then everything else. Within
    // the second pass a control is only emitted once its parent exists, so
    // nested menu items always follow the item they hang off.
    QList<const Control *> order;
    QSet<QString> emitted;
    for (const Control &c : m_controls)
        if (isContainerPass(c.type)) { order << &c; emitted.insert(c.name.toLower()); }
    QList<const Control *> pending;
    for (const Control &c : m_controls)
        if (!isContainerPass(c.type) && c.type != ControlType::MenuPanel && c.type != ControlType::Font)
            pending << &c;
    while (!pending.isEmpty()) {
        bool progressed = false;
        for (int i = 0; i < pending.size(); ) {
            const Control *c = pending[i];
            if (c->parentName.isEmpty() || emitted.contains(c->parentName.toLower()) || !find(c->parentName)) {
                order << c;
                emitted.insert(c->name.toLower());
                pending.removeAt(i);
                progressed = true;
            } else {
                ++i;
            }
        }
        if (!progressed) { order << pending; break; } // parent cycle - emit as-is
    }

    QString formContextMenu = frm ? frm->contextMenu : QString();

    for (const Control *c : order) {
        const QRect &g = c->geometry;
        s << in << "__UI_NewID = __UI_NewControl(__UI_Type_" << typeInfo(c->type).name << ", "
          << encodeBasicString(c->name) << ", "
          << g.width() << ", " << g.height() << ", " << g.x() << ", " << g.y() << ", ";
        if (c->parentName.isEmpty()) s << "0)\n";
        else s << "__UI_GetID(" << encodeBasicString(c->parentName) << "))\n";
        s << in << "__UI_RegisterResult = 0\n";

        if (c->type == ControlType::ContextMenu) {
            s << '\n';
            if (!formContextMenu.isEmpty() && c->name.compare(formContextMenu, Qt::CaseInsensitive) == 0) {
                s << in << "Control(__UI_FormID).ContextMenuID = __UI_GetID("
                  << encodeBasicString(formContextMenu) << ")\n\n";
                formContextMenu.clear();
            }
            continue;
        }

        if (c->isDefaultButton) s << in << "__UI_DefaultButtonID = __UI_NewID\n";
        if (!c->caption.isEmpty()) s << in << "SetCaption __UI_NewID, " << encodeBasicString(c->caption) << '\n';
        if (!c->toolTip.isEmpty()) s << in << "ToolTip(__UI_NewID) = " << encodeBasicString(c->toolTip) << '\n';

        for (const QString &item : c->items)
            s << in << "AddItem __UI_NewID, " << encodeBasicString(item) << '\n';

        if (!c->text.isEmpty()) {
            switch (c->type) {
            case ControlType::Form:
                break; // written as $EXEICON above
            case ControlType::PictureBox:
            case ControlType::Button:
            case ControlType::MenuItem:
                if (c->animatedGif) {
                    s << in << "__UI_RegisterResult = OpenGif(__UI_NewID, " << encodeBasicString(c->text) << ")\n";
                    if (c->autoPlayGif) s << in << "IF __UI_RegisterResult THEN PlayGif __UI_NewID\n";
                } else {
                    s << in << "LoadImage Control(__UI_NewID), " << encodeBasicString(c->text) << '\n';
                }
                break;
            default:
                s << in << "Text(__UI_NewID) = " << encodeBasicString(c->text) << '\n';
                break;
            }
        }
        if (!c->mask.isEmpty()) s << in << "Mask(__UI_NewID) = " << encodeBasicString(c->mask) << '\n';

        for (auto it = c->properties.cbegin(); it != c->properties.cend(); ++it)
            s << in << "Control(__UI_NewID)." << it.key() << " = " << formatProperty(it.value()) << '\n';
        if (!c->contextMenu.isEmpty() && c->type != ControlType::Form)
            s << in << "Control(__UI_NewID).ContextMenuID = __UI_GetID(" << encodeBasicString(c->contextMenu) << ")\n";
        if (!c->fontFiles.isEmpty() || c->fontSize > 0)
            s << in << "Control(__UI_NewID).Font = SetFont(" << encodeBasicString(c->fontFiles) << ", "
              << c->fontSize << ")\n";

        for (const QString &l : c->extraLines) s << in << l << '\n';
        s << '\n';
    }

    // A form context menu whose ContextMenu control is missing still round-trips.
    if (!formContextMenu.isEmpty())
        s << in << "Control(__UI_FormID).ContextMenuID = __UI_GetID(" << encodeBasicString(formContextMenu) << ")\n\n";

    if (!m_bindings.isEmpty()) {
        s << in << "'Control bindings:\n";
        for (const Binding &b : m_bindings)
            s << in << "__UI_Bind __UI_GetID(" << encodeBasicString(b.source) << "), __UI_GetID("
              << encodeBasicString(b.target) << "), " << encodeBasicString(b.sourceProperty) << ", "
              << encodeBasicString(b.targetProperty) << '\n';
    }

    s << "END SUB\n\nSUB __UI_AssignIDs\n";
    for (const Control *c : order)
        s << in << c->name << " = __UI_GetID(" << encodeBasicString(c->name) << ")\n";
    s << "END SUB\n";
    return out;
}

} // namespace InForm
