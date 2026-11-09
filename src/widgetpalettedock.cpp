// widgetpalettedock.cpp - KayteIDE
#include "widgetpalettedock.h"

#if __has_include(<IconsFontAwesome6.h>)
#  include <IconsFontAwesome6.h>
#else
// Minimal FA6 fallback glyphs (UTF-8 encoded)
#  define ICON_FA_SQUARE_CHECK    "\xef\x85\x8c"
#  define ICON_FA_KEYBOARD        "\xef\x84\x9c"
#  define ICON_FA_IMAGE           "\xef\x80\xbe"
#  define ICON_FA_TABLE_CELLS     "\xef\x80\x8a"
#  define ICON_FA_OBJECT_GROUP    "\xef\x89\x87"
#  define ICON_FA_BARS            "\xef\x83\x89"
#  define ICON_FA_MAGNIFYING_GLASS "\xef\x80\x82"
#  define ICON_FA_SLIDERS         "\xef\x87\x9e"
#endif

#include "componenticons.h"
#include "InFormTypes.hpp"

#include <QApplication>
#include <QPixmap>
#include <QPainter>

// ─────────────────────────────────────────────────────────────────────────────
// PaletteListWidget - drag-enabled
// ─────────────────────────────────────────────────────────────────────────────

PaletteListWidget::PaletteListWidget(QWidget *parent)
    : QListWidget(parent)
{
    setDragEnabled(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setIconSize(QSize(20, 20));
    setSpacing(2);
    setStyleSheet(QStringLiteral(
        "QListWidget { border: none; background: transparent; }"
        "QListWidget::item { padding: 4px 6px; border-radius: 4px; }"
        "QListWidget::item:hover { background: rgba(100,140,255,0.15); }"
        "QListWidget::item:selected { background: rgba(100,140,255,0.35); }"
    ));
}

void PaletteListWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragStartPos = event->pos();
    QListWidget::mousePressEvent(event);
}

void PaletteListWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - m_dragStartPos).manhattanLength()
            < QApplication::startDragDistance()) return;

    QListWidgetItem *item = currentItem();
    if (!item) return;

    const QString widgetType = item->data(Qt::UserRole).toString();
    if (widgetType.isEmpty()) return;

    auto *drag     = new QDrag(this);
    auto *mimeData = new QMimeData;
    mimeData->setText(widgetType); // canvas reads this
    mimeData->setData(QStringLiteral("application/x-kayteide-widget"), widgetType.toUtf8());

    // Thumbnail pixmap
    QPixmap pix(80, 28);
    pix.fill(QColor(80, 120, 220, 200));
    QPainter pm(&pix);
    pm.setPen(Qt::white);
    pm.setFont(QFont(QStringLiteral("sans-serif"), 9));
    pm.drawText(pix.rect(), Qt::AlignCenter, item->text());
    pm.end();

    drag->setMimeData(mimeData);
    drag->setPixmap(pix);
    drag->setHotSpot({ pix.width() / 2, pix.height() / 2 });
    drag->exec(Qt::CopyAction);
}

// ─────────────────────────────────────────────────────────────────────────────
// WidgetPaletteDock
// ─────────────────────────────────────────────────────────────────────────────

WidgetPaletteDock::WidgetPaletteDock(const QFont &faFont, QWidget *parent)
    : QDockWidget(tr("Widget Palette"), parent)
    , m_faFont(faFont)
{
    setObjectName(QStringLiteral("WidgetPaletteDock"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable   |
                QDockWidget::DockWidgetFloatable  |
                QDockWidget::DockWidgetClosable);
    setMinimumWidth(180);
    setMaximumWidth(300);

    // ── Custom title bar ──────────────────────────────────────────────────────
    auto *titleBar = new QWidget(this);
    auto *titleLay = new QHBoxLayout(titleBar);
    titleLay->setContentsMargins(6, 2, 6, 2);
    auto *iconLbl = new QLabel(QString::fromUtf8(ICON_FA_OBJECT_GROUP), titleBar);
    iconLbl->setFont(m_faFont);
    auto *txtLbl  = new QLabel(tr("  Widget Palette"), titleBar);
    QFont bold = txtLbl->font(); bold.setBold(true); txtLbl->setFont(bold);
    titleLay->addWidget(iconLbl);
    titleLay->addWidget(txtLbl);
    titleLay->addStretch();
    setTitleBarWidget(titleBar);

    // ── Body ──────────────────────────────────────────────────────────────────
    auto *body = new QWidget(this);
    auto *lay  = new QVBoxLayout(body);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    // Search bar
    m_searchEdit = new QLineEdit(body);
    m_searchEdit->setPlaceholderText(tr("Filter widgets..."));
    if (const QIcon search = glyphIcon(ICON_FA_MAGNIFYING_GLASS,
                                       palette().color(QPalette::PlaceholderText), m_faFont);
        !search.isNull())
        m_searchEdit->addAction(search, QLineEdit::LeadingPosition);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { border-radius: 4px; padding: 4px 8px; }"
    ));
    connect(m_searchEdit, &QLineEdit::textChanged, this, &WidgetPaletteDock::onSearchChanged);
    lay->addWidget(m_searchEdit);

    // Toolbox
    m_toolBox = new QToolBox(body);
    m_toolBox->setStyleSheet(QStringLiteral(
        "QToolBox::tab { background: palette(button); border-radius: 4px; "
        "                padding: 4px; font-weight: bold; }"
        "QToolBox::tab:selected { background: palette(highlight); color: palette(highlighted-text); }"
    ));
    lay->addWidget(m_toolBox, 1);

    setWidget(body);
    buildPalette();
}

PaletteListWidget *WidgetPaletteDock::addCategory(
        const QString &title,
        const char    *categoryKey,
        const QList<PaletteItem> &items)
{
    auto *list = new PaletteListWidget(m_toolBox);

    for (const PaletteItem &pi : items) {
        auto *item = new QListWidgetItem(componentIcon(pi.type, m_faFont), pi.label, list);
        item->setData(Qt::UserRole, pi.type);
        item->setToolTip(pi.type);
    }

    m_toolBox->addItem(list, componentCategoryIcon(QLatin1String(categoryKey), m_faFont), title);
    return list;
}

void WidgetPaletteDock::buildPalette()
{
    // ── Buttons ───────────────────────────────────────────────────────────────
    addCategory(tr("Buttons"), "Buttons", {
        { tr("Push Button"),    QStringLiteral("QPushButton") },
        { tr("Tool Button"),    QStringLiteral("QToolButton") },
        { tr("Radio Button"),   QStringLiteral("QRadioButton") },
        { tr("Check Box"),      QStringLiteral("QCheckBox") },
        { tr("Command Link"),   QStringLiteral("QCommandLinkButton") },
        { tr("Dialog Buttons"), QStringLiteral("QDialogButtonBox") },
    });

    // ── Input ────────────────────────────────────────────────────────────────
    addCategory(tr("Input"), "Input", {
        { tr("Line Edit"),      QStringLiteral("QLineEdit") },
        { tr("Text Edit"),      QStringLiteral("QTextEdit") },
        { tr("Plain Text Edit"),QStringLiteral("QPlainTextEdit") },
        { tr("Spin Box"),       QStringLiteral("QSpinBox") },
        { tr("Double Spin Box"),QStringLiteral("QDoubleSpinBox") },
        { tr("Combo Box"),      QStringLiteral("QComboBox") },
        { tr("Date Edit"),      QStringLiteral("QDateEdit") },
        { tr("Time Edit"),      QStringLiteral("QTimeEdit") },
        { tr("Date/Time Edit"), QStringLiteral("QDateTimeEdit") },
        { tr("Slider"),         QStringLiteral("QSlider") },
        { tr("Dial"),           QStringLiteral("QDial") },
        { tr("Key Sequence Edit"),QStringLiteral("QKeySequenceEdit") },
    });

    // ── Display ──────────────────────────────────────────────────────────────
    addCategory(tr("Display"), "Display", {
        { tr("Label"),          QStringLiteral("QLabel") },
        { tr("Pixmap"),         QStringLiteral("QLabel") },
        { tr("LCD Number"),     QStringLiteral("QLCDNumber") },
        { tr("Progress Bar"),   QStringLiteral("QProgressBar") },
        { tr("Calendar"),       QStringLiteral("QCalendarWidget") },
        { tr("Graphics View"),  QStringLiteral("QGraphicsView") },
        { tr("OpenGL Widget"),  QStringLiteral("QOpenGLWidget") },
        { tr("Web Engine View"),QStringLiteral("QWebEngineView") },
    });

    // ── Item Views ───────────────────────────────────────────────────────────
    addCategory(tr("Item Views"), "Item Views", {
        { tr("List View"),      QStringLiteral("QListView") },
        { tr("List Widget"),    QStringLiteral("QListWidget") },
        { tr("Tree View"),      QStringLiteral("QTreeView") },
        { tr("Tree Widget"),    QStringLiteral("QTreeWidget") },
        { tr("Table View"),     QStringLiteral("QTableView") },
        { tr("Table Widget"),   QStringLiteral("QTableWidget") },
        { tr("Column View"),    QStringLiteral("QColumnView") },
        { tr("Undo View"),      QStringLiteral("QUndoView") },
    });

    // ── Containers ───────────────────────────────────────────────────────────
    addCategory(tr("Containers"), "Containers", {
        { tr("Group Box"),      QStringLiteral("QGroupBox") },
        { tr("Scroll Area"),    QStringLiteral("QScrollArea") },
        { tr("Tool Box"),       QStringLiteral("QToolBox") },
        { tr("Tab Widget"),     QStringLiteral("QTabWidget") },
        { tr("Stacked Widget"), QStringLiteral("QStackedWidget") },
        { tr("Frame"),          QStringLiteral("QFrame") },
        { tr("Widget"),         QStringLiteral("QWidget") },
        { tr("Dock Widget"),    QStringLiteral("QDockWidget") },
        { tr("Splitter"),       QStringLiteral("QSplitter") },
    });

    // ── Layouts ──────────────────────────────────────────────────────────────
    addCategory(tr("Layouts"), "Layouts", {
        { tr("Vertical Layout"),    QStringLiteral("QVBoxLayout") },
        { tr("Horizontal Layout"),  QStringLiteral("QHBoxLayout") },
        { tr("Grid Layout"),        QStringLiteral("QGridLayout") },
        { tr("Form Layout"),        QStringLiteral("QFormLayout") },
        { tr("Spacer - H"),         QStringLiteral("QSpacerItem_H") },
        { tr("Spacer - V"),         QStringLiteral("QSpacerItem_V") },
    });

    // ── Menu / Toolbar ───────────────────────────────────────────────────────
    addCategory(tr("Menus && Bars"), "Menus & Bars", { // && - QToolBox treats & as a mnemonic
        { tr("Menu Bar"),       QStringLiteral("QMenuBar") },
        { tr("Tool Bar"),       QStringLiteral("QToolBar") },
        { tr("Status Bar"),     QStringLiteral("QStatusBar") },
    });

    // ── InForm (QB64) controls - see plugins/InForm ──────────────────────────
    QList<PaletteItem> inform;
    for (InForm::ControlType t : InForm::designableTypes())
        inform.append({ QString::fromLatin1(InForm::typeInfo(t).name), InForm::paletteKey(t) });
    addCategory(tr("InForm (QB64)"), "InForm", inform);
}

void WidgetPaletteDock::onSearchChanged(const QString &text)
{
    const QString lower = text.toLower().trimmed();

    for (int i = 0; i < m_toolBox->count(); ++i) {
        auto *list = qobject_cast<PaletteListWidget *>(m_toolBox->widget(i));
        if (!list) continue;

        int visible = 0;
        for (int j = 0; j < list->count(); ++j) {
            auto *item  = list->item(j);
            const bool match = lower.isEmpty() ||
                               item->text().toLower().contains(lower) ||
                               item->data(Qt::UserRole).toString().toLower().contains(lower);
            item->setHidden(!match);
            if (match) ++visible;
        }
        // Hide the whole tab when nothing matches
        m_toolBox->setItemEnabled(i, visible > 0);
    }
}
