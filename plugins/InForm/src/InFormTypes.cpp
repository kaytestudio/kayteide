// InFormTypes.cpp - KayteIDE InForm plugin
#include "InFormTypes.hpp"

#include <QHash>

#include <iterator>

namespace InForm {

namespace {

// Mirrors the __UI_Type() table in InForm.bi (default sizes, TurnsInto).
const ControlTypeInfo kTypes[] = {
    { ControlType::None,         "",             {  0,   0}, ControlType::None,         ""                },
    { ControlType::Form,         "Form",         {640, 480}, ControlType::None,         "QWidget"         },
    { ControlType::Frame,        "Frame",        {230, 150}, ControlType::None,         "QGroupBox"       },
    { ControlType::Button,       "Button",       { 80,  23}, ControlType::None,         "QPushButton"     },
    { ControlType::Label,        "Label",        {150,  23}, ControlType::None,         "QLabel"          },
    { ControlType::CheckBox,     "CheckBox",     {150,  23}, ControlType::ToggleSwitch, "QCheckBox"       },
    { ControlType::RadioButton,  "RadioButton",  {150,  23}, ControlType::None,         "QRadioButton"    },
    { ControlType::TextBox,      "TextBox",      {120,  23}, ControlType::None,         "QLineEdit"       },
    { ControlType::ProgressBar,  "ProgressBar",  {300,  23}, ControlType::None,         "QProgressBar"    },
    { ControlType::ListBox,      "ListBox",      {200, 200}, ControlType::DropdownList, "QListWidget"     },
    { ControlType::DropdownList, "DropdownList", {200,  23}, ControlType::ListBox,      "QComboBox"       },
    { ControlType::MenuBar,      "MenuBar",      { 60,  22}, ControlType::ContextMenu,  ""                },
    { ControlType::MenuItem,     "MenuItem",     {120,  18}, ControlType::None,         ""                },
    { ControlType::MenuPanel,    "MenuPanel",    {  0,   0}, ControlType::None,         ""                },
    { ControlType::PictureBox,   "PictureBox",   {230, 150}, ControlType::None,         "QLabel"          },
    { ControlType::TrackBar,     "TrackBar",     {300,  40}, ControlType::None,         "QSlider"         },
    { ControlType::ContextMenu,  "ContextMenu",  { 22,  22}, ControlType::MenuBar,      ""                },
    { ControlType::Font,         "Font",         {  0,   0}, ControlType::None,         ""                },
    { ControlType::ToggleSwitch, "ToggleSwitch", { 40,  17}, ControlType::CheckBox,     "QCheckBox"       },
};

constexpr const char *kPalettePrefix = "InForm::";

} // namespace

const ControlTypeInfo &typeInfo(ControlType type)
{
    const int i = static_cast<int>(type);
    if (i < 0 || i >= int(std::size(kTypes))) return kTypes[0];
    return kTypes[i];
}

ControlType typeFromName(const QString &name)
{
    QString n = name.trimmed();
    if (n.startsWith(QLatin1String("__UI_Type_"), Qt::CaseInsensitive))
        n = n.mid(10);
    for (const ControlTypeInfo &ti : kTypes)
        if (*ti.name && n.compare(QLatin1String(ti.name), Qt::CaseInsensitive) == 0)
            return ti.type;
    return ControlType::None;
}

QList<ControlType> designableTypes()
{
    return {
        ControlType::Button,      ControlType::Label,        ControlType::TextBox,
        ControlType::CheckBox,    ControlType::RadioButton,  ControlType::ToggleSwitch,
        ControlType::ListBox,     ControlType::DropdownList, ControlType::TrackBar,
        ControlType::ProgressBar, ControlType::PictureBox,   ControlType::Frame,
        ControlType::MenuBar,     ControlType::MenuItem,     ControlType::ContextMenu,
    };
}

QString paletteKey(ControlType type)
{
    return QLatin1String(kPalettePrefix) + QLatin1String(typeInfo(type).name);
}

bool isPaletteKey(const QString &key)
{
    return key.startsWith(QLatin1String(kPalettePrefix));
}

ControlType typeFromPaletteKey(const QString &key)
{
    if (!isPaletteKey(key)) return ControlType::None;
    return typeFromName(key.mid(int(qstrlen(kPalettePrefix))));
}

ControlType typeFromQtClass(const QString &qtClass)
{
    static const QHash<QString, ControlType> map = {
        { QStringLiteral("QPushButton"),        ControlType::Button       },
        { QStringLiteral("QToolButton"),        ControlType::Button       },
        { QStringLiteral("QCommandLinkButton"), ControlType::Button       },
        { QStringLiteral("QLabel"),             ControlType::Label        },
        { QStringLiteral("QLCDNumber"),         ControlType::Label        },
        { QStringLiteral("QCheckBox"),          ControlType::CheckBox     },
        { QStringLiteral("QRadioButton"),       ControlType::RadioButton  },
        { QStringLiteral("QLineEdit"),          ControlType::TextBox      },
        { QStringLiteral("QTextEdit"),          ControlType::TextBox      },
        { QStringLiteral("QPlainTextEdit"),     ControlType::TextBox      },
        { QStringLiteral("QSpinBox"),           ControlType::TextBox      },
        { QStringLiteral("QDoubleSpinBox"),     ControlType::TextBox      },
        { QStringLiteral("QProgressBar"),       ControlType::ProgressBar  },
        { QStringLiteral("QListWidget"),        ControlType::ListBox      },
        { QStringLiteral("QListView"),          ControlType::ListBox      },
        { QStringLiteral("QComboBox"),          ControlType::DropdownList },
        { QStringLiteral("QSlider"),            ControlType::TrackBar     },
        { QStringLiteral("QDial"),              ControlType::TrackBar     },
        { QStringLiteral("QGroupBox"),          ControlType::Frame        },
        { QStringLiteral("QFrame"),             ControlType::Frame        },
        { QStringLiteral("QGraphicsView"),      ControlType::PictureBox   },
    };
    return map.value(qtClass, ControlType::None);
}

QVariantMap defaultProperties(ControlType type)
{
    QVariantMap p;
    switch (type) {
    case ControlType::Button:
    case ControlType::CheckBox:
    case ControlType::RadioButton:
    case ControlType::ToggleSwitch:
    case ControlType::DropdownList:
        p[QStringLiteral("CanHaveFocus")] = true;
        break;
    case ControlType::TextBox:
    case ControlType::ListBox:
        p[QStringLiteral("CanHaveFocus")] = true;
        p[QStringLiteral("HasBorder")]    = true;
        break;
    case ControlType::Label:
        p[QStringLiteral("VAlign")]    = QString::fromLatin1(kMiddle);
        p[QStringLiteral("BackStyle")] = QString::fromLatin1(kTransparent);
        break;
    case ControlType::Frame:
    case ControlType::PictureBox:
        p[QStringLiteral("HasBorder")] = true;
        break;
    case ControlType::TrackBar:
        p[QStringLiteral("CanHaveFocus")] = true;
        p[QStringLiteral("Max")]          = 10.0;
        p[QStringLiteral("Interval")]     = 1.0;
        break;
    case ControlType::ProgressBar:
        p[QStringLiteral("Max")]            = 100.0;
        p[QStringLiteral("ShowPercentage")] = true;
        break;
    default:
        break;
    }
    return p;
}

} // namespace InForm
