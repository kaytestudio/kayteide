#pragma once
// uicanvaswidget.h - KayteIDE
// Drop-target canvas: drag widgets from the palette, position them,
// then export to a standard Qt .ui XML file.

#include <QWidget>
#include <QList>
#include <QPoint>
#include <QSize>
#include <QRect>
#include <QString>
#include <QMenu>
#include <QUndoStack>
#include <QUndoCommand>
#include <QVariantMap>

// ─── One placed widget record ─────────────────────────────────────────────────
struct CanvasItem
{
    QString widgetType;   // "QPushButton", "QLabel", ...
    QString objectName;   // "pushButton_1", "label_3", ...
    QString labelText;    // displayed text / placeholder
    QRect   geometry;     // position + size on canvas
    bool    selected { false };

    // InForm controls ("InForm::Button", ...) only: the Frame / menu the control
    // belongs to, and the InForm properties carried through .frm round-trips.
    QString     parentName;
    QVariantMap properties;
};

// ─── UiCanvasWidget ───────────────────────────────────────────────────────────
class UiCanvasWidget : public QWidget
{
    Q_OBJECT
public:
    explicit UiCanvasWidget(QWidget *parent = nullptr);

    // ── Mutation API (used by undo commands) ──────────────────────────────────
    void       insertItem(const CanvasItem &item);
    void       removeItem(int index);
    CanvasItem getItem   (int index) const;
    void       moveItem  (int index, QPoint topLeft);
    int        itemCount () const { return m_items.size(); }

    // ── User-facing ───────────────────────────────────────────────────────────
    bool       exportUiFile(const QString &filePath) const;
    QString    toUiXml() const;                          // the .ui XML exportUiFile writes
    // The Qt class a canvas item stands for (InForm controls map to their
    // closest Qt widget; empty when there is none, e.g. menu entries).
    static QString qtClassOf(const CanvasItem &item);
    void       clearCanvas();
    void       setItems(const QList<CanvasItem> &items); // replace everything (e.g. .frm import)
    // Replace everything but keep the undo history and the selection (by
    // object name) - used by ReplaceItemsCmd when the Code tab is edited.
    void       replaceItems(const QList<CanvasItem> &items);

    // Adds a widget of `type` (a palette key) at `pos`, or at the next free
    // spot when `pos` is null - used by double-clicks in the side panels.
    void       addWidget(const QString &type, QPoint pos = QPoint(-1, -1));
    QUndoStack *undoStack() { return m_undoStack; }

    // ── Read access for side panels (Components / Property editors) ───────────
    const QList<CanvasItem> &items() const { return m_items; }
    int  selectedIndex() const { return m_selectedIndex; }

    // Select from outside (e.g. a click in the Components editor list)
    void selectItemExternally(int index) { selectItem(index); update(); }

    // Apply edits made in the Property editor to the selected item
    void applyItemEdits(int index, const QString &objectName,
                         const QString &labelText, const QRect &geometry);

signals:
    void itemSelectionChanged(const CanvasItem *item); // nullptr = nothing selected
    void canvasModified();

protected:
    void paintEvent        (QPaintEvent        *e) override;
    void dragEnterEvent    (QDragEnterEvent    *e) override;
    void dragMoveEvent     (QDragMoveEvent     *e) override;
    void dropEvent         (QDropEvent         *e) override;
    void mousePressEvent   (QMouseEvent        *e) override;
    void mouseMoveEvent    (QMouseEvent        *e) override;
    void mouseReleaseEvent (QMouseEvent        *e) override;
    void keyPressEvent     (QKeyEvent          *e) override;
    void contextMenuEvent  (QContextMenuEvent  *e) override;
    QSize sizeHint() const override { return { 640, 480 }; }

private:
    void  drawGrid            (QPainter &p) const;
    void  drawItem            (QPainter &p, const CanvasItem &item) const;
    void  drawSelectionHandles(QPainter &p, const CanvasItem &item) const;

    int   itemAt    (const QPoint &pos) const;
    // Frames / group boxes carry what sits inside them when moved.
    bool       isContainer       (int index) const;
    QList<int> containedItems    (int index) const;
    void       moveWithContents  (int index, QPoint newTopLeft);
    void  selectItem(int index);
    void  deleteSelected();
    void  snapToGrid(QPoint &pt) const;

    QString uniqueName  (const QString &type) const;
    QString defaultLabel(const QString &type) const;

    QList<CanvasItem> m_items;
    int     m_selectedIndex  { -1 };
    QPoint  m_dragOffset;
    QPoint  m_moveStartPos;   // canvas-local top-left when a drag began (for undo)
    bool    m_draggingItem   { false };
    QList<int>    m_dragChildren;     // items moving along with a dragged container
    QList<QPoint> m_dragChildStart;   // their top-left when the drag began

    QPoint  m_dropPreviewPt;
    bool    m_showDropPreview{ false };
    QString m_dropPreviewType;

    static constexpr int GRID = 8;
    QUndoStack *m_undoStack  { nullptr };
};

// ─── Undo commands (defined inline - they're tiny) ────────────────────────────

class AddItemCmd : public QUndoCommand {
public:
    AddItemCmd(UiCanvasWidget *c, CanvasItem item)
        : QUndoCommand(QLatin1String("Add ") + item.widgetType)
        , m_c(c), m_item(std::move(item)) {}
    void redo() override { m_c->insertItem(m_item); }
    void undo() override { m_c->removeItem(m_c->itemCount() - 1); }
private:
    UiCanvasWidget *m_c;
    CanvasItem      m_item;
};

class DeleteItemCmd : public QUndoCommand {
public:
    DeleteItemCmd(UiCanvasWidget *c, int idx)
        : QUndoCommand(QLatin1String("Delete widget"))
        , m_c(c), m_idx(idx), m_item(c->getItem(idx)) {}
    void redo() override { m_c->removeItem(m_idx); }
    void undo() override { m_c->insertItem(m_item); }
private:
    UiCanvasWidget *m_c;
    int             m_idx;
    CanvasItem      m_item;
};

class MoveItemCmd : public QUndoCommand {
public:
    MoveItemCmd(UiCanvasWidget *c, int idx, QPoint from, QPoint to)
        : QUndoCommand(QLatin1String("Move widget"))
        , m_c(c), m_idx(idx), m_from(from), m_to(to) {}
    void redo() override { m_c->moveItem(m_idx, m_to); }
    void undo() override { m_c->moveItem(m_idx, m_from); }
private:
    UiCanvasWidget *m_c;
    int    m_idx;
    QPoint m_from, m_to;
};

class ReplaceItemsCmd : public QUndoCommand {
public:
    ReplaceItemsCmd(UiCanvasWidget *c, QList<CanvasItem> before, QList<CanvasItem> after)
        : QUndoCommand(QLatin1String("Edit code"))
        , m_c(c), m_before(std::move(before)), m_after(std::move(after)) {}
    void redo() override { m_c->replaceItems(m_after); }
    void undo() override { m_c->replaceItems(m_before); }
private:
    UiCanvasWidget   *m_c;
    QList<CanvasItem> m_before, m_after;
};
