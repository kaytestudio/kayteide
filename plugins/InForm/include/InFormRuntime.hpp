#pragma once
// InFormRuntime.hpp - KayteIDE InForm plugin
// Qt6 replacement for InForm's QB64 GUI engine (InForm/InForm.ui). It turns an
// InForm::Document into live Qt widgets and reports InForm's events
// (__UI_Click, __UI_ValueChanged, ...) as Qt signals. The API keeps InForm's
// vocabulary - Caption, Text, Value, AddItem - addressed by control name.

#include "InFormDocument.hpp"

#include <QFont>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariant>

class QAction;
class QMenu;
class QWidget;

namespace InForm {

// InForm's event SUBs, one per value.
enum class Event {
    Click, MouseEnter, MouseLeave, FocusIn, FocusOut,
    MouseDown, MouseUp, KeyPress, TextChanged, ValueChanged,
};
QString eventName(Event e); // "Click", "ValueChanged", ...

class Runtime : public QObject
{
    Q_OBJECT
public:
    explicit Runtime(QObject *parent = nullptr);
    ~Runtime() override;

    // Builds the form's widget tree. The returned top-level widget belongs to
    // the caller (nullptr when the document has no Form control). Building
    // again discards the widgets of the previous build.
    QWidget *build(const Document &doc, QWidget *parent = nullptr);

    // Directory that relative image / font paths in the form resolve against
    // (normally the folder holding the .frm). Defaults to the working directory.
    void setBaseDirectory(const QString &dir) { m_baseDir = dir; }

    QWidget *formWidget() const { return m_form; }
    QWidget *widget(const QString &name) const;
    QAction *action(const QString &name) const;   // MenuItem controls
    const Document &document() const { return m_doc; }

    // ── InForm-style accessors ────────────────────────────────────────────────
    QString caption(const QString &name) const;
    void    setCaption(const QString &name, const QString &caption);
    QString text(const QString &name) const;
    void    setText(const QString &name, const QString &text);
    double  value(const QString &name) const;     // CheckBox: -1/0, ListBox: 1-based index
    void    setValue(const QString &name, double value);
    void    addItem(const QString &name, const QString &item);
    void    resetList(const QString &name);
    void    setDisabled(const QString &name, bool disabled);
    void    setHidden(const QString &name, bool hidden);

    // Generic get/set by InForm property name (used by __UI_Bind bindings).
    QVariant property(const QString &name, const QString &prop) const;
    void     setProperty(const QString &name, const QString &prop, const QVariant &value);

signals:
    void event(InForm::Event e, const QString &controlName);
    void clicked(const QString &controlName);
    void valueChanged(const QString &controlName);
    void textChanged(const QString &controlName);

protected:
    bool eventFilter(QObject *watched, QEvent *ev) override;

private:
    struct Entry {
        Control           control;
        QPointer<QWidget> widget;
        QPointer<QAction> action;
        QPointer<QMenu>   menu;       // MenuBar / ContextMenu / MenuItem with children
        double            scale { 1 }; // TrackBar: slider units per InForm unit
    };

    QWidget *createWidget(int index, QWidget *parent);
    void     buildMenus(QWidget *form);
    void     applyCommon(int index);
    void     wireSignals(int index);
    void     track(QObject *obj, int index);
    void     emitEvent(Event ev, const QString &name);
    void     propagateBindings(const QString &name);
    void     applyPlatformConditionals();
    QString  resolvePath(const QString &path) const;
    QFont    resolveFont(const Control &c, const QFont &base) const;

    Entry       *entry(const QString &name);
    const Entry *entry(const QString &name) const;

    Document               m_doc;
    QList<Entry>           m_entries;
    QHash<QString, int>    m_index;      // lower-case name -> m_entries index
    QHash<QObject *, int>  m_objectIndex;
    QPointer<QWidget>      m_form;
    QPointer<QWidget>      m_defaultButton;
    QString                m_baseDir;
    bool                   m_inBinding { false };
};

} // namespace InForm
