// componenticons.cpp - KayteIDE
#include "componenticons.h"

#include "InFormTypes.hpp"

#include <QColor>
#include <QFontDatabase>
#include <QHash>
#include <QPainter>
#include <QPixmap>

#if __has_include(<IconsFontAwesome6.h>)
#  include <IconsFontAwesome6.h>
#  define KAYTE_COMPONENT_ICONS 1
#else
#  define KAYTE_COMPONENT_ICONS 0
#endif

namespace {

struct Glyph { const char *glyph; QColor color; };

#if KAYTE_COMPONENT_ICONS
const QColor kButton   (100, 149, 237);
const QColor kInput    ( 72, 199, 142);
const QColor kDisplay  (255, 200,  80);
const QColor kList     (232, 120, 120);
const QColor kContainer(170, 120, 230);
const QColor kLayout   (150, 160, 190);
const QColor kMenu     (255, 150,  70);
const QColor kInForm   (  0, 170, 150);

Glyph glyphFor(const QString &type)
{
    if (InForm::isPaletteKey(type)) {
        switch (InForm::typeFromPaletteKey(type)) {
        case InForm::ControlType::Button:       return { ICON_FA_HAND_POINTER,      kButton    };
        case InForm::ControlType::Label:        return { ICON_FA_FONT,              kDisplay   };
        case InForm::ControlType::TextBox:      return { ICON_FA_I_CURSOR,          kInput     };
        case InForm::ControlType::CheckBox:     return { ICON_FA_SQUARE_CHECK,      kButton    };
        case InForm::ControlType::RadioButton:  return { ICON_FA_CIRCLE_DOT,        kButton    };
        case InForm::ControlType::ToggleSwitch: return { ICON_FA_TOGGLE_ON,         kButton    };
        case InForm::ControlType::ListBox:      return { ICON_FA_LIST,              kList      };
        case InForm::ControlType::DropdownList: return { ICON_FA_SQUARE_CARET_DOWN, kList      };
        case InForm::ControlType::TrackBar:     return { ICON_FA_SLIDERS,           kInput     };
        case InForm::ControlType::ProgressBar:  return { ICON_FA_BARS_PROGRESS,     kDisplay   };
        case InForm::ControlType::PictureBox:   return { ICON_FA_IMAGE,             kDisplay   };
        case InForm::ControlType::Frame:        return { ICON_FA_OBJECT_GROUP,      kContainer };
        case InForm::ControlType::MenuBar:      return { ICON_FA_BARS,              kMenu      };
        case InForm::ControlType::MenuItem:     return { ICON_FA_LIST_UL,           kMenu      };
        case InForm::ControlType::ContextMenu:  return { ICON_FA_RECTANGLE_LIST,    kMenu      };
        default:                                return { ICON_FA_SQUARE,            kInForm    };
        }
    }

    static const QHash<QString, Glyph> qt = {
        // Buttons
        { QStringLiteral("QPushButton"),        { ICON_FA_HAND_POINTER,       kButton    } },
        { QStringLiteral("QToolButton"),        { ICON_FA_WRENCH,             kButton    } },
        { QStringLiteral("QRadioButton"),       { ICON_FA_CIRCLE_DOT,         kButton    } },
        { QStringLiteral("QCheckBox"),          { ICON_FA_SQUARE_CHECK,       kButton    } },
        { QStringLiteral("QCommandLinkButton"), { ICON_FA_ARROW_RIGHT,        kButton    } },
        { QStringLiteral("QDialogButtonBox"),   { ICON_FA_CIRCLE_CHECK,       kButton    } },
        // Input
        { QStringLiteral("QLineEdit"),          { ICON_FA_I_CURSOR,           kInput     } },
        { QStringLiteral("QTextEdit"),          { ICON_FA_ALIGN_LEFT,         kInput     } },
        { QStringLiteral("QPlainTextEdit"),     { ICON_FA_ALIGN_LEFT,         kInput     } },
        { QStringLiteral("QSpinBox"),           { ICON_FA_HASHTAG,            kInput     } },
        { QStringLiteral("QDoubleSpinBox"),     { ICON_FA_HASHTAG,            kInput     } },
        { QStringLiteral("QComboBox"),          { ICON_FA_SQUARE_CARET_DOWN,  kInput     } },
        { QStringLiteral("QDateEdit"),          { ICON_FA_CALENDAR_DAY,       kInput     } },
        { QStringLiteral("QTimeEdit"),          { ICON_FA_CLOCK,              kInput     } },
        { QStringLiteral("QDateTimeEdit"),      { ICON_FA_CALENDAR,           kInput     } },
        { QStringLiteral("QSlider"),            { ICON_FA_SLIDERS,            kInput     } },
        { QStringLiteral("QDial"),              { ICON_FA_GAUGE,              kInput     } },
        { QStringLiteral("QKeySequenceEdit"),   { ICON_FA_KEYBOARD,           kInput     } },
        // Display
        { QStringLiteral("QLabel"),             { ICON_FA_FONT,               kDisplay   } },
        { QStringLiteral("QLCDNumber"),         { ICON_FA_CALCULATOR,         kDisplay   } },
        { QStringLiteral("QProgressBar"),       { ICON_FA_BARS_PROGRESS,      kDisplay   } },
        { QStringLiteral("QCalendarWidget"),    { ICON_FA_CALENDAR_DAYS,      kDisplay   } },
        { QStringLiteral("QGraphicsView"),      { ICON_FA_SHAPES,             kDisplay   } },
        { QStringLiteral("QOpenGLWidget"),      { ICON_FA_CUBE,               kDisplay   } },
        { QStringLiteral("QWebEngineView"),     { ICON_FA_GLOBE,              kDisplay   } },
        // Item views
        { QStringLiteral("QListView"),          { ICON_FA_LIST,               kList      } },
        { QStringLiteral("QListWidget"),        { ICON_FA_LIST,               kList      } },
        { QStringLiteral("QTreeView"),          { ICON_FA_SITEMAP,            kList      } },
        { QStringLiteral("QTreeWidget"),        { ICON_FA_SITEMAP,            kList      } },
        { QStringLiteral("QTableView"),         { ICON_FA_TABLE,              kList      } },
        { QStringLiteral("QTableWidget"),       { ICON_FA_TABLE,              kList      } },
        { QStringLiteral("QColumnView"),        { ICON_FA_TABLE_COLUMNS,      kList      } },
        { QStringLiteral("QUndoView"),          { ICON_FA_ROTATE_LEFT,        kList      } },
        // Containers
        { QStringLiteral("QGroupBox"),          { ICON_FA_OBJECT_GROUP,       kContainer } },
        { QStringLiteral("QScrollArea"),        { ICON_FA_UP_DOWN,            kContainer } },
        { QStringLiteral("QToolBox"),           { ICON_FA_LAYER_GROUP,        kContainer } },
        { QStringLiteral("QTabWidget"),         { ICON_FA_FOLDER,             kContainer } },
        { QStringLiteral("QStackedWidget"),     { ICON_FA_CLONE,              kContainer } },
        { QStringLiteral("QFrame"),             { ICON_FA_SQUARE,             kContainer } },
        { QStringLiteral("QWidget"),            { ICON_FA_WINDOW_MAXIMIZE,    kContainer } },
        { QStringLiteral("QDockWidget"),        { ICON_FA_WINDOW_RESTORE,     kContainer } },
        { QStringLiteral("QSplitter"),          { ICON_FA_GRIP_LINES_VERTICAL, kContainer } },
        // Layouts
        { QStringLiteral("QVBoxLayout"),        { ICON_FA_GRIP_LINES,         kLayout    } },
        { QStringLiteral("QHBoxLayout"),        { ICON_FA_GRIP_LINES_VERTICAL, kLayout   } },
        { QStringLiteral("QGridLayout"),        { ICON_FA_TABLE_CELLS,        kLayout    } },
        { QStringLiteral("QFormLayout"),        { ICON_FA_TABLE_LIST,         kLayout    } },
        { QStringLiteral("QSpacerItem_H"),      { ICON_FA_ARROWS_LEFT_RIGHT,  kLayout    } },
        { QStringLiteral("QSpacerItem_V"),      { ICON_FA_ARROWS_UP_DOWN,     kLayout    } },
        // Menus & bars
        { QStringLiteral("QMenuBar"),           { ICON_FA_BARS,               kMenu      } },
        { QStringLiteral("QToolBar"),           { ICON_FA_SCREWDRIVER_WRENCH, kMenu      } },
        { QStringLiteral("QStatusBar"),         { ICON_FA_CIRCLE_INFO,        kMenu      } },
    };
    return qt.value(type, { ICON_FA_SQUARE, kContainer });
}

Glyph categoryGlyph(const QString &category)
{
    static const QHash<QString, Glyph> map = {
        { QStringLiteral("Buttons"),      { ICON_FA_HAND_POINTER,    kButton    } },
        { QStringLiteral("Input"),        { ICON_FA_KEYBOARD,        kInput     } },
        { QStringLiteral("Display"),      { ICON_FA_IMAGE,           kDisplay   } },
        { QStringLiteral("Item Views"),   { ICON_FA_LIST,            kList      } },
        { QStringLiteral("Containers"),   { ICON_FA_OBJECT_GROUP,    kContainer } },
        { QStringLiteral("Layouts"),      { ICON_FA_TABLE_CELLS,     kLayout    } },
        { QStringLiteral("Menus & Bars"), { ICON_FA_BARS,            kMenu      } },
        { QStringLiteral("InForm"),       { ICON_FA_WINDOW_MAXIMIZE, kInForm    } },
    };
    return map.value(category, { ICON_FA_SQUARE, kLayout });
}
#endif

QIcon render(const Glyph &g, const QFont &faFont)
{
    if (!g.glyph || !QFontDatabase::families().contains(faFont.family())) return {};
    QIcon icon;
    for (int px : { 16, 20, 24, 32, 40, 48 }) {
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::TextAntialiasing);
        QFont f = faFont;
        f.setPixelSize(qRound(px * 0.8));
        p.setFont(f);
        p.setPen(g.color);
        p.drawText(pm.rect(), Qt::AlignCenter, QString::fromUtf8(g.glyph));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace

QIcon componentIcon(const QString &widgetType, const QFont &faFont)
{
#if KAYTE_COMPONENT_ICONS
    static QHash<QString, QIcon> cache;
    const auto it = cache.constFind(widgetType);
    if (it != cache.cend()) return *it;
    const QIcon icon = render(glyphFor(widgetType), faFont);
    if (!icon.isNull()) cache.insert(widgetType, icon);
    return icon;
#else
    Q_UNUSED(widgetType); Q_UNUSED(faFont);
    return {};
#endif
}

QIcon componentCategoryIcon(const QString &category, const QFont &faFont)
{
#if KAYTE_COMPONENT_ICONS
    return render(categoryGlyph(category), faFont);
#else
    Q_UNUSED(category); Q_UNUSED(faFont);
    return {};
#endif
}

QIcon glyphIcon(const char *glyph, const QColor &color, const QFont &faFont)
{
    return render({ glyph, color }, faFont);
}
