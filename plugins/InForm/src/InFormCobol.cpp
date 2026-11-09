// InFormCobol.cpp - KayteIDE InForm plugin
#include "InFormCobol.hpp"

#include <QHash>
#include <QRegularExpression>
#include <QTextStream>

namespace InForm {

namespace {

// Pixels per character cell, and where the form starts on the screen
// (line 1 holds the form's caption).
constexpr int kCellWidth  = 8;
constexpr int kCellHeight = 20;
constexpr int kFirstLine  = 3;
constexpr int kColumns    = 80;
constexpr int kQuitKey    = 1010;   // F10
constexpr int kEscKey     = 2005;   // only with COB_SCREEN_ESC=Y

// A COBOL alphanumeric literal; quotes are escaped by doubling them.
QString literal(QString s)
{
    s.remove(QLatin1Char('\r')).replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + s + QLatin1Char('"');
}

QString handlerName(const Control &c) { return cobolName(c.name) + QStringLiteral("-CLICK"); }

QString handlerStub(const Control &c)
{
    return QStringLiteral("%1.\n    MOVE %2 TO WS-MESSAGE.\n")
        .arg(handlerName(c), literal(c.name + QStringLiteral(" clicked")));
}

bool hasHandler(const QString &source, const Control &c)
{
    const QRegularExpression re(
        QStringLiteral(R"(^\s*%1\s*\.)").arg(QRegularExpression::escape(handlerName(c))),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    return re.match(source).hasMatch();
}

QList<const Control *> buttons(const Document &doc)
{
    QList<const Control *> out;
    for (const Control &c : doc.controls())
        if (c.type == ControlType::Button) out << &c;
    return out;
}

// Function key of each button (0 = Enter): the default button gets Enter,
// the others F1-F9 in form order. Buttons left over get none.
QHash<QString, int> buttonKeys(const Document &doc)
{
    QHash<QString, int> keys;
    int next = 1;
    for (const Control *c : buttons(doc)) {
        if (c->isDefaultButton && !keys.values().contains(0)) keys.insert(c->name, 0);
        else if (next <= 9) keys.insert(c->name, 1000 + next++);
    }
    return keys;
}

QString keyLabel(int key)
{
    return key == 0 ? QStringLiteral("Enter") : QStringLiteral("F%1").arg(key - 1000);
}

// "PIC 9(3)"-style digits needed for a value up to `max`.
int digitsFor(double max) { return int(QString::number(qMax(1, int(max))).size()); }

} // namespace

QString cobolName(const QString &name)
{
    QString out;
    for (const QChar ch : name) {
        if (ch.isLetterOrNumber() && ch.unicode() < 128) out += ch.toUpper();
        else if (!out.endsWith(QLatin1Char('-'))) out += QLatin1Char('-');
    }
    while (out.startsWith(QLatin1Char('-'))) out.remove(0, 1);
    if (out.isEmpty()) out = QStringLiteral("CONTROL");
    if (out.at(0).isDigit()) out.prepend(QStringLiteral("C-"));
    // Room for the WS- prefix and -SCREEN / -CLICK suffixes within 31 chars.
    out.truncate(23);
    while (out.endsWith(QLatin1Char('-'))) out.chop(1);
    return out;
}

QString cobolProgram(const Document &doc, QStringList *warnings)
{
    const Control *form = doc.form();
    const QString formName = form ? form->name : QStringLiteral("Form1");
    const QString title = form && !form->caption.isEmpty() ? form->caption : formName;
    const QString program = cobolName(formName);
    const QString screen = program + QStringLiteral("-SCREEN");
    const QHash<QString, int> keys = buttonKeys(doc);

    QStringList data;     // WORKING-STORAGE entries for the controls
    QStringList fields;   // SCREEN SECTION entries
    int lastLine = kFirstLine;

    const auto warn = [warnings](const QString &w) { if (warnings) *warnings << w; };
    const auto at = [&](int line, int col, const QString &clause) {
        fields << QStringLiteral("   05 LINE %1 COL %2 %3.").arg(line).arg(col).arg(clause);
        lastLine = qMax(lastLine, line);
    };

    for (const Control &c : doc.controls()) {
        if (c.type == ControlType::Form) continue;
        const QPoint pos = doc.absolutePos(c.name);
        const int col = 1 + qMax(0, pos.x()) / kCellWidth;
        const int top = kFirstLine + qMax(0, pos.y()) / kCellHeight;
        const int rows = qMax(1, (c.geometry.height() + kCellHeight / 2) / kCellHeight);
        const int line = top + (rows - 1) / 2;   // one-line controls sit mid-height
        const int room = kColumns - col + 1;     // cells left on the screen line
        const int width = qMin(qMax(1, c.geometry.width() / kCellWidth), room);
        const QString field = QStringLiteral("WS-") + cobolName(c.name);
        const QString caption = c.caption.simplified();

        if (room < 1) {
            warn(QObject::tr("%1 is past column %2 of the screen").arg(c.name).arg(kColumns));
            continue;
        }

        switch (c.type) {
        case ControlType::Label:
            if (!caption.isEmpty()) at(line, col, QStringLiteral("VALUE ") + literal(caption.left(room)));
            break;

        case ControlType::TextBox: {
            data << QStringLiteral("01 %1 PIC X(%2) VALUE %3.").arg(field).arg(width)
                        .arg(c.text.isEmpty() ? QStringLiteral("SPACES") : literal(c.text.left(width)));
            QString clause = QStringLiteral("PIC X(%1) USING %2 UNDERLINE").arg(width).arg(field);
            if (c.flag(QStringLiteral("PasswordField"))) clause += QStringLiteral(" SECURE");
            at(line, col, clause);
            break;
        }

        case ControlType::CheckBox:
        case ControlType::RadioButton:
        case ControlType::ToggleSwitch: {
            const bool radio = c.type == ControlType::RadioButton;
            data << QStringLiteral("01 %1 PIC X VALUE %2.").arg(field,
                        c.number(QStringLiteral("Value")) != 0 ? QStringLiteral("\"X\"") : QStringLiteral("SPACE"));
            at(line, col, QStringLiteral("VALUE ") + literal(radio ? QStringLiteral("(") : QStringLiteral("[")));
            if (room > 1) at(line, col + 1, QStringLiteral("PIC X USING ") + field);
            if (room > 2) {
                QString after = (radio ? QStringLiteral(") ") : QStringLiteral("] ")) + caption;
                at(line, col + 2, QStringLiteral("VALUE ") + literal(after.left(room - 2).trimmed()));
            }
            break;
        }

        case ControlType::ListBox:
        case ControlType::DropdownList: {
            const int selected = int(c.number(QStringLiteral("Value")));   // 1-based, 0 = none
            const QString value = selected > 0 && selected <= c.items.size() ? c.items.at(selected - 1)
                                : c.items.isEmpty() ? QString() : c.items.first();
            if (!c.items.isEmpty())
                data << QStringLiteral("*> %1 items: %2").arg(c.name, c.items.join(QStringLiteral(", ")));
            data << QStringLiteral("01 %1 PIC X(%2) VALUE %3.").arg(field).arg(width)
                        .arg(value.isEmpty() ? QStringLiteral("SPACES") : literal(value.left(width)));
            at(line, col, QStringLiteral("PIC X(%1) USING %2 UNDERLINE").arg(width).arg(field));
            break;
        }

        case ControlType::TrackBar:
        case ControlType::ProgressBar: {
            const double max = c.number(QStringLiteral("Max"), c.type == ControlType::TrackBar ? 10 : 100);
            const int digits = digitsFor(max);
            data << QStringLiteral("01 %1 PIC 9(%2) VALUE %3.").arg(field).arg(digits)
                        .arg(qBound(0, int(c.number(QStringLiteral("Value"))), int(max)));
            const QString pic = digits > 1 ? QStringLiteral("Z(%1)9").arg(digits - 1) : QStringLiteral("9");
            if (c.type == ControlType::TrackBar)
                at(line, col, QStringLiteral("PIC 9(%1) USING %2 UNDERLINE").arg(digits).arg(field));
            else
                at(line, col, QStringLiteral("PIC %1 FROM %2").arg(pic, field));
            const bool percent = c.type == ControlType::ProgressBar && int(max) == 100;
            if (room > digits)
                at(line, col + digits, QStringLiteral("VALUE ")
                       + literal(percent ? QStringLiteral("%") : QStringLiteral("/%1").arg(int(max))));
            break;
        }

        case ControlType::Frame: {
            const int frameRows = qMax(2, c.geometry.height() / kCellHeight);
            const bool closed = c.geometry.width() / kCellWidth <= room;
            QString edge = QStringLiteral("+") + QString(qMax(0, width - 2), QLatin1Char('-'))
                           + (closed && width > 1 ? QStringLiteral("+") : QString());
            QString head = edge;
            if (!caption.isEmpty() && width > 4) {
                const QString t = (QLatin1Char(' ') + caption + QLatin1Char(' ')).left(width - 3);
                head.replace(2, t.size(), t);
            }
            at(top, col, QStringLiteral("VALUE ") + literal(head));
            for (int r = 1; r < frameRows - 1; ++r) {
                at(top + r, col, QStringLiteral("VALUE \"|\""));
                if (closed && width > 1) at(top + r, col + width - 1, QStringLiteral("VALUE \"|\""));
            }
            at(top + frameRows - 1, col, QStringLiteral("VALUE ") + literal(edge));
            break;
        }

        case ControlType::Button:
            if (!keys.contains(c.name)) {
                warn(QObject::tr("%1: no function key left (F1-F9 are taken)").arg(c.name));
                break;
            }
            at(line, col, QStringLiteral("VALUE ")
                   + literal(QStringLiteral("[%1] %2").arg(keyLabel(keys[c.name]), caption).left(room))
                   + QStringLiteral(" HIGHLIGHT"));
            break;

        default:
            warn(QObject::tr("%1 (%2) has no terminal equivalent")
                     .arg(c.name, QString::fromLatin1(typeInfo(c.type).name)));
            break;
        }
    }

    const int statusLine = lastLine + 2;

    QString out;
    QTextStream s(&out);
    // In column 8 the directive is valid fixed format too, so cobc switches
    // without its "free format detected" note.
    s << "       >>SOURCE FORMAT FREE\n"
      << "*> " << formName << " - GnuCOBOL program for the form designed in KayteIDE.\n"
      << "*> Build and run it in a terminal:\n"
      << "*>   cobc -x " << formName << ".cob && ./" << formName << '\n'
      << "*> Buttons are pressed with the key shown on them; F10 quits (Esc too,\n"
      << "*> with COB_SCREEN_EXCEPTIONS=Y COB_SCREEN_ESC=Y in the environment).\n\n"
      << "IDENTIFICATION DIVISION.\n"
      << "PROGRAM-ID. " << program << ".\n\n"
      << "ENVIRONMENT DIVISION.\n"
      << "CONFIGURATION SECTION.\n"
      << "SPECIAL-NAMES.\n"
      << "    CRT STATUS IS WS-KEY.\n\n"
      << "DATA DIVISION.\n"
      << "WORKING-STORAGE SECTION.\n"
      << "01 WS-KEY         PIC 9(4).\n"
      << "01 WS-DONE        PIC X VALUE \"N\".\n"
      << "   88 FORM-DONE   VALUE \"Y\".\n"
      << "01 WS-MESSAGE     PIC X(" << kColumns - 2 << ") VALUE SPACES.\n";
    if (!data.isEmpty()) {
        s << "\n*> Controls\n";
        for (const QString &d : data) s << d << '\n';
    }

    s << "\nSCREEN SECTION.\n"
      << "01 " << screen << " BLANK SCREEN.\n"
      << "   05 LINE 1 COL 1 VALUE " << literal(title.left(kColumns)) << " HIGHLIGHT.\n";
    for (const QString &f : fields) s << f << '\n';
    s << "   05 LINE " << statusLine << " COL 1 PIC X(" << kColumns - 2 << ") FROM WS-MESSAGE.\n"
      << "   05 LINE " << statusLine + 1 << " COL 1 VALUE \"F10 Quit\".\n";

    s << "\nPROCEDURE DIVISION.\n"
      << "MAIN-PROCEDURE.\n"
      << "    PERFORM UNTIL FORM-DONE\n"
      << "        ACCEPT " << screen << '\n'
      << "        EVALUATE WS-KEY\n";
    for (const Control *c : buttons(doc)) {
        if (!keys.contains(c->name)) continue;
        s << QStringLiteral("            WHEN %1 PERFORM %2")
                 .arg(keys[c->name], 4, 10, QLatin1Char('0')).arg(handlerName(*c), -24)
          << "*> " << keyLabel(keys[c->name]) << '\n';
    }
    s << "            WHEN " << kQuitKey << " SET FORM-DONE TO TRUE\n"
      << "            WHEN " << kEscKey << " SET FORM-DONE TO TRUE\n"
      << "        END-EVALUATE\n"
      << "    END-PERFORM\n"
      << "    STOP RUN.\n";

    QString handlers;
    for (const Control *c : buttons(doc))
        if (keys.contains(c->name)) handlers += QLatin1Char('\n') + handlerStub(*c);
    if (!handlers.isEmpty())
        s << "\n*> Event handlers - one paragraph per button, run by its key\n" << handlers;
    return out;
}

QString missingCobolHandlers(const Document &doc, const QString &existingSource)
{
    QString out;
    for (const Control *c : buttons(doc))
        if (!hasHandler(existingSource, *c)) out += QLatin1Char('\n') + handlerStub(*c);
    if (out.isEmpty()) return {};
    return QStringLiteral("\n*> Handlers for buttons added in the form designer\n") + out;
}

} // namespace InForm
