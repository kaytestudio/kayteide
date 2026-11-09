// InFormConvert.cpp - KayteIDE InForm plugin
#include "InFormConvert.hpp"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <optional>

namespace InForm {

namespace {

// QB64's default 16-colour palette, which QBColor(n) indexes.
const QColor kQbColors[16] = {
    {  0,   0,   0}, {  0,   0, 170}, {  0, 170,   0}, {  0, 170, 170},
    {170,   0,   0}, {170,   0, 170}, {170,  85,   0}, {170, 170, 170},
    { 85,  85,  85}, { 85,  85, 255}, { 85, 255,  85}, { 85, 255, 255},
    {255,  85,  85}, {255,  85, 255}, {255, 255,  85}, {255, 255, 255},
};

// One VB-DOS text cell. vbdos2inform used InForm's 8x16 font cell and added
// window margins (+5 / +15) to every size and position, which makes one-row
// controls 31px tall on a 16px grid, so neighbouring rows overlap. Rows here
// are 24px - room for InForm's 23px default control height - and only the
// form itself gets the margins.
constexpr int kCharWidth  = 8;
constexpr int kCharHeight = 24;

std::optional<int> charUnits(const QString &value, int unit, int margin)
{
    static const QRegularExpression re(QStringLiteral(R"(^Char\s*\(\s*([-+0-9.]+))"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(value);
    if (!m.hasMatch()) return std::nullopt;
    return int(m.captured(1).toDouble() * unit) + margin;
}

std::optional<QColor> qbColor(const QString &value)
{
    static const QRegularExpression re(QStringLiteral(R"(^QBColor\s*\(\s*(\d+))"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(value);
    if (!m.hasMatch()) return std::nullopt;
    return kQbColors[m.captured(1).toInt() & 15];
}

// VB string literal ("a ""b""") -> text; anything else is taken verbatim.
QString vbString(const QString &value)
{
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        return value.mid(1, value.size() - 2).replace(QStringLiteral("\"\""), QStringLiteral("\""));
    return value;
}

ControlType vbType(const QString &vb)
{
    static const QHash<QString, ControlType> map = {
        { QStringLiteral("label"),         ControlType::Label        },
        { QStringLiteral("combobox"),      ControlType::DropdownList },
        { QStringLiteral("drivelistbox"),  ControlType::DropdownList },
        { QStringLiteral("commandbutton"), ControlType::Button       },
        { QStringLiteral("listbox"),       ControlType::ListBox      },
        { QStringLiteral("dirlistbox"),    ControlType::ListBox      },
        { QStringLiteral("filelistbox"),   ControlType::ListBox      },
        { QStringLiteral("frame"),         ControlType::Frame        },
        { QStringLiteral("checkbox"),      ControlType::CheckBox     },
        { QStringLiteral("optionbutton"),  ControlType::RadioButton  },
        { QStringLiteral("picturebox"),    ControlType::PictureBox   },
        { QStringLiteral("textbox"),       ControlType::TextBox      },
    };
    return map.value(vb.toLower(), ControlType::PictureBox); // as vbdos2inform does
}

bool canHaveFocus(ControlType t)
{
    switch (t) {
    case ControlType::Button: case ControlType::CheckBox: case ControlType::RadioButton:
    case ControlType::TextBox: case ControlType::ListBox: case ControlType::DropdownList:
    case ControlType::TrackBar: case ControlType::ToggleSwitch:
        return true;
    default:
        return false;
    }
}

} // namespace

bool isVbDosForm(const QString &source)
{
    QTextStream s(const_cast<QString *>(&source), QIODevice::ReadOnly);
    const QString first  = s.readLine().trimmed();
    const QString second = s.readLine().trimmed();
    return first == QLatin1String("Version 1.00") &&
           second.startsWith(QLatin1String("BEGIN Form "), Qt::CaseInsensitive);
}

bool convertVbDosFile(const QString &filePath, Document *out, QString *error)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    return convertVbDosForm(QString::fromLatin1(f.readAll()), out, error);
}

bool convertVbDosForm(const QString &source, Document *out, QString *error)
{
    if (!isVbDosForm(source)) {
        if (error) *error = QStringLiteral("not a VB-DOS text form (expected \"Version 1.00\" and \"BEGIN Form\")");
        return false;
    }
    out->clear();

    QStringList lines = source.split(QLatin1Char('\n'));
    for (QString &l : lines) l = l.trimmed();

    QSet<QString> usedNames;
    // Indices into out->controls() of the BEGIN ... END blocks currently open.
    QList<int> stack;
    QColor formFore, formBack;

    for (int ln = 1; ln < lines.size(); ++ln) {
        const QString &a = lines[ln];
        if (a.isEmpty()) continue;

        if (a.startsWith(QLatin1String("BEGIN "), Qt::CaseInsensitive)) {
            const QString rest = a.mid(6).trimmed();
            const int sp = rest.indexOf(QLatin1Char(' '));
            const QString vbClass = sp < 0 ? rest : rest.left(sp);
            QString name = sp < 0 ? vbClass : rest.mid(sp + 1).trimmed();

            Control c;
            if (stack.isEmpty() && vbClass.compare(QLatin1String("Form"), Qt::CaseInsensitive) == 0) {
                c.type = ControlType::Form;
                c.fontSize = 16; // vbdos2inform: SetFont("", 16) - InForm's built-in font
            } else {
                c.type = vbType(vbClass);
                // Control arrays share a name in VB-DOS; InForm needs unique ones.
                const QString base = name;
                for (int i = 2; usedNames.contains(name.toLower()); ++i)
                    name = base + QLatin1Char('_') + QString::number(i); // Option1, Option1_2, ...
                // Children of a Frame belong to the innermost open one.
                for (int i = int(stack.size()) - 1; i >= 0; --i)
                    if (out->controls()[stack[i]].type == ControlType::Frame) {
                        c.parentName = out->controls()[stack[i]].name;
                        break;
                    }
                if (canHaveFocus(c.type)) c.properties.insert(QStringLiteral("CanHaveFocus"), true);
            }
            c.name = name;
            usedNames.insert(name.toLower());
            out->controls() << c;
            stack << int(out->controls().size()) - 1;
            continue;
        }

        if (a.compare(QLatin1String("END"), Qt::CaseInsensitive) == 0) {
            if (!stack.isEmpty()) stack.removeLast();
            if (stack.isEmpty()) break; // end of the form
            continue;
        }

        const int eq = a.indexOf(QLatin1Char('='));
        if (eq < 0 || stack.isEmpty()) continue;
        Control &c = out->controls()[stack.last()];
        const QString prop  = a.left(eq).trimmed();
        const QString value = a.mid(eq + 1).trimmed();
        const bool isForm = c.type == ControlType::Form;

        if (prop.compare(QLatin1String("Width"), Qt::CaseInsensitive) == 0) {
            if (auto v = charUnits(value, kCharWidth, isForm ? 5 : 0)) c.geometry.setWidth(*v);
        } else if (prop.compare(QLatin1String("Height"), Qt::CaseInsensitive) == 0) {
            if (auto v = charUnits(value, kCharHeight, isForm ? 15 : 0)) c.geometry.setHeight(*v);
        } else if (prop.compare(QLatin1String("Left"), Qt::CaseInsensitive) == 0) {
            if (auto v = charUnits(value, kCharWidth, 0); v && !isForm) c.geometry.moveLeft(*v);
        } else if (prop.compare(QLatin1String("Top"), Qt::CaseInsensitive) == 0) {
            if (auto v = charUnits(value, kCharHeight, 0); v && !isForm) c.geometry.moveTop(*v);
        } else if (prop.compare(QLatin1String("BackColor"), Qt::CaseInsensitive) == 0) {
            if (auto col = qbColor(value)) {
                if (isForm) formBack = *col;
                if (isForm || *col != formBack) c.properties.insert(QStringLiteral("BackColor"), *col);
            }
        } else if (prop.compare(QLatin1String("ForeColor"), Qt::CaseInsensitive) == 0) {
            if (auto col = qbColor(value)) {
                if (isForm) formFore = *col;
                if (isForm || *col != formFore) c.properties.insert(QStringLiteral("ForeColor"), *col);
            }
        } else if (prop.compare(QLatin1String("Caption"), Qt::CaseInsensitive) == 0) {
            c.caption = vbString(value);
        } else if (prop.compare(QLatin1String("Text"), Qt::CaseInsensitive) == 0) {
            c.text = vbString(value);
        } else if (prop.compare(QLatin1String("Enabled"), Qt::CaseInsensitive) == 0) {
            if (value == QLatin1String("0")) c.properties.insert(QStringLiteral("Disabled"), true);
        } else if (prop.compare(QLatin1String("Visible"), Qt::CaseInsensitive) == 0) {
            if (value == QLatin1String("0")) c.properties.insert(QStringLiteral("Hidden"), true);
        }
    }

    if (!out->form()) {
        if (error) *error = QStringLiteral("no BEGIN Form block found");
        return false;
    }
    return true;
}

QString eventSkeleton(const Document &doc, const QString &frmFileName)
{
    // Same layout as InForm's SaveForm (UiEditor.bas) writes for a new form.
    QStringList all, focus, text, value, gifs;
    QString defaultButton;
    bool gifExtension = false;
    for (const Control &c : doc.controls()) {
        if (c.type == ControlType::MenuPanel || c.type == ControlType::Font) continue;
        if (c.type != ControlType::ContextMenu) all << c.name;
        if (c.flag(QStringLiteral("CanHaveFocus"))) focus << c.name;
        if (c.type == ControlType::TextBox) text << c.name;
        switch (c.type) {
        case ControlType::ListBox: case ControlType::DropdownList: case ControlType::TrackBar:
        case ControlType::ToggleSwitch: case ControlType::CheckBox: case ControlType::RadioButton:
            value << c.name;
            break;
        default:
            break;
        }
        if (c.isDefaultButton) defaultButton = c.name;
        if (c.animatedGif) { gifExtension = true; gifs << c.name; }
    }

    QString out;
    QTextStream s(&out);
    s << "': This program uses\n"
      << "': InForm - GUI library for QB64 (KayteIDE Qt6 port)\n"
      << "': Original InForm by Fellippe Heitor - https://github.com/FellippeHeitor/InForm\n"
      << "'-----------------------------------------------------------\n\n"
      << "': Controls' IDs: ------------------------------------------------------------------\n";
    for (const Control &c : doc.controls())
        if (c.type != ControlType::MenuPanel && c.type != ControlType::Font)
            s << "DIM SHARED " << c.name << " AS LONG\n";
    s << "\n': External modules: ---------------------------------------------------------------\n";
    if (gifExtension) s << "'$INCLUDE:'InForm\\extensions\\gifplay.bi'\n";
    s << "'$INCLUDE:'InForm\\InForm.bi'\n"
      << "'$INCLUDE:'InForm\\xp.uitheme'\n"
      << "'$INCLUDE:'" << frmFileName << "'\n";
    if (gifExtension) s << "'$INCLUDE:'InForm\\extensions\\gifplay.bm'\n";
    s << "\n': Event procedures: ---------------------------------------------------------------\n";

    const auto selectCase = [&s](const QStringList &ids) {
        s << "    SELECT CASE id\n";
        for (const QString &id : ids) s << "        CASE " << id << "\n\n";
        s << "    END SELECT\n";
    };

    s << "SUB __UI_BeforeInit\n\nEND SUB\n\n";

    s << "SUB __UI_OnLoad\n";
    if (!defaultButton.isEmpty()) s << "    __UI_DefaultButtonID = " << defaultButton << '\n';
    else s << '\n';
    s << "END SUB\n\n";

    s << "SUB __UI_BeforeUpdateDisplay\n"
      << "    'This event occurs at approximately 60 frames per second.\n"
      << "    'You can change the update frequency by calling SetFrameRate DesiredRate%\n";
    if (!gifs.isEmpty()) {
        s << "\n    'The lines below ensure your GIFs will display properly;\n"
          << "    'Please refer to the documentation in 'InForm/extensions/README - gifplay.txt'\n";
        for (const QString &g : gifs) s << "    UpdateGif " << g << '\n';
    } else {
        s << '\n';
    }
    s << "END SUB\n\n";

    s << "SUB __UI_BeforeUnload\n"
      << "    'If you set __UI_UnloadSignal = False here you can\n"
      << "    'cancel the user's request to close.\n\nEND SUB\n\n";

    const auto handler = [&](const char *sub, const QStringList &ids, const char *comment = nullptr) {
        s << "SUB " << sub << '\n';
        if (comment) s << comment;
        selectCase(ids);
        s << "END SUB\n\n";
    };
    handler("__UI_Click (id AS LONG)", all);
    handler("__UI_MouseEnter (id AS LONG)", all);
    handler("__UI_MouseLeave (id AS LONG)", all);
    handler("__UI_FocusIn (id AS LONG)", focus);
    handler("__UI_FocusOut (id AS LONG)", focus,
            "    'This event occurs right before a control loses focus.\n"
            "    'To prevent a control from losing focus, set __UI_KeepFocus = True below.\n");
    handler("__UI_MouseDown (id AS LONG)", all);
    handler("__UI_MouseUp (id AS LONG)", all);
    handler("__UI_KeyPress (id AS LONG)", focus,
            "    'When this event is fired, __UI_KeyHit will contain the code of the key hit.\n"
            "    'You can change it and even cancel it by making it = 0\n");
    handler("__UI_TextChanged (id AS LONG)", text);
    handler("__UI_ValueChanged (id AS LONG)", value);
    s << "SUB __UI_FormResized\n\nEND SUB\n\n";

    s << "'$INCLUDE:'InForm\\InForm.ui'\n";
    return out;
}

} // namespace InForm
