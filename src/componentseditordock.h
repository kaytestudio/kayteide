#pragma once
// componentseditordock.h - KayteIDE
// Left-side dock with two parts:
//   - Toolbox - the InForm (QB64) controls; drag one onto the UiCanvasWidget
//     or double-click it to add it to the form.
//   - On this form - the components currently placed on the canvas, kept in
//     sync with canvas selection/edits.

#include <QDockWidget>
#include <QFont>
#include <QIcon>
#include <QListWidget>

class QLabel;
class PaletteListWidget;
class UiCanvasWidget;
struct CanvasItem;

class ComponentsEditorDock : public QDockWidget
{
    Q_OBJECT
public:
    // `faFont` is the IDE's Font Awesome font, used to draw the control icons.
    ComponentsEditorDock(UiCanvasWidget *canvas, const QFont &faFont, QWidget *parent = nullptr);

private slots:
    void refreshList();
    void onCanvasSelectionChanged(const CanvasItem *item);
    void onRowActivated(int row);

private:
    QIcon iconFor(const QString &widgetType); // see componenticons.h

    QFont                 m_faFont;
    UiCanvasWidget    *m_canvas   { nullptr };
    PaletteListWidget *m_toolbox  { nullptr };
    QListWidget       *m_list     { nullptr };
    QLabel            *m_listHint { nullptr };
    bool               m_syncing  { false }; // guards against selection feedback loops
};
