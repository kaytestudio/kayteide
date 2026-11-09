// componentseditordock.cpp - KayteIDE
#include "componentseditordock.h"
#include "componenticons.h"
#include "uicanvaswidget.h"
#include "widgetpalettedock.h"

#include "InFormKayte.hpp"
#include "InFormTypes.hpp"

#include <QLabel>
#include <QListWidgetItem>
#include <QPalette>
#include <QSplitter>
#include <QVBoxLayout>

ComponentsEditorDock::ComponentsEditorDock(UiCanvasWidget *canvas, const QFont &faFont, QWidget *parent)
    : QDockWidget(tr("Components"), parent)
    , m_faFont(faFont)
    , m_canvas(canvas)
{
    setObjectName(QStringLiteral("ComponentsEditorDock"));
    setAllowedAreas(Qt::AllDockWidgetAreas);

    auto *splitter = new QSplitter(Qt::Vertical, this);

    // ── Toolbox: InForm controls, draggable onto the canvas ───────────────────
    auto *toolPane = new QWidget(splitter);
    auto *toolLay  = new QVBoxLayout(toolPane);
    toolLay->setContentsMargins(4, 4, 4, 0);
    auto *toolTitle = new QLabel(tr("<b>InForm Toolbox</b>"), toolPane);
    toolTitle->setToolTip(tr("Drag a control onto the UI Designer, or double-click to add it"));
    toolLay->addWidget(toolTitle);

    m_toolbox = new PaletteListWidget(toolPane);
    for (InForm::ControlType t : InForm::designableTypes()) {
        auto *it = new QListWidgetItem(QString::fromLatin1(InForm::typeInfo(t).name), m_toolbox);
        it->setData(Qt::UserRole, InForm::paletteKey(t));
        it->setIcon(iconFor(InForm::paletteKey(t)));
        const QString kfm = InForm::kfmControlClass(t);
        it->setToolTip(tr("InForm %1 - drag onto the form or double-click\nKayte (.kfm): %2")
                           .arg(QString::fromLatin1(InForm::typeInfo(t).name),
                                kfm.isEmpty() ? tr("not supported - left out of Kayte exports")
                                              : kfm.mid(3)));
    }
    toolLay->addWidget(m_toolbox);
    connect(m_toolbox, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        m_canvas->addWidget(it->data(Qt::UserRole).toString());
    });

    // ── Components placed on the current form ────────────────────────────────
    auto *formPane = new QWidget(splitter);
    auto *formLay  = new QVBoxLayout(formPane);
    formLay->setContentsMargins(4, 4, 4, 4);
    formLay->addWidget(new QLabel(tr("<b>On this form</b>"), formPane));

    m_listHint = new QLabel(tr("No components yet - drag one from the toolbox above "
                               "or the Widget Palette, or use UI Designer > InForm > Import."),
                            formPane);
    m_listHint->setWordWrap(true);
    m_listHint->setForegroundRole(QPalette::PlaceholderText); // readable on light and dark themes
    formLay->addWidget(m_listHint);

    m_list = new QListWidget(formPane);
    m_list->setAlternatingRowColors(true);
    m_list->setIconSize(QSize(16, 16));
    formLay->addWidget(m_list, 1);

    splitter->addWidget(toolPane);
    splitter->addWidget(formPane);
    splitter->setStretchFactor(0, 3); // the toolbox lists 15 controls
    splitter->setStretchFactor(1, 2);
    setWidget(splitter);

    connect(m_list, &QListWidget::currentRowChanged,
            this, &ComponentsEditorDock::onRowActivated);

    connect(m_canvas, &UiCanvasWidget::canvasModified,
            this, &ComponentsEditorDock::refreshList);
    connect(m_canvas, &UiCanvasWidget::itemSelectionChanged,
            this, &ComponentsEditorDock::onCanvasSelectionChanged);

    refreshList();
}

void ComponentsEditorDock::refreshList()
{
    m_syncing = true;
    m_list->clear();
    for (const CanvasItem &ci : m_canvas->items()) {
        // InForm controls inside a Frame / menu are indented under their parent.
        QString label = QStringLiteral("%1  [%2]").arg(ci.objectName, ci.widgetType);
        if (!ci.parentName.isEmpty())
            label = QStringLiteral("    ↳ ") + label + QStringLiteral("  in ") + ci.parentName;
        auto *it = new QListWidgetItem(iconFor(ci.widgetType), label);
        if (InForm::isPaletteKey(ci.widgetType) &&
            InForm::kfmControlClass(InForm::typeFromPaletteKey(ci.widgetType)).isEmpty())
            it->setToolTip(tr("Kayte .kfm forms have no %1 - it is left out of Kayte exports")
                               .arg(ci.widgetType.mid(8)));
        m_list->addItem(it);
    }
    m_list->setCurrentRow(m_canvas->selectedIndex());
    m_listHint->setVisible(m_list->count() == 0);
    m_syncing = false;
}

QIcon ComponentsEditorDock::iconFor(const QString &widgetType)
{
    return componentIcon(widgetType, m_faFont);
}

void ComponentsEditorDock::onCanvasSelectionChanged(const CanvasItem *)
{
    if (m_syncing) return;
    m_syncing = true;
    m_list->setCurrentRow(m_canvas->selectedIndex());
    m_syncing = false;
}

void ComponentsEditorDock::onRowActivated(int row)
{
    if (m_syncing) return;
    m_canvas->selectItemExternally(row);
}
