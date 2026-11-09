#pragma once
// InFormTypes.hpp - KayteIDE InForm plugin
// Qt6 C++ port of the control model from InForm (Fellippe Heitor's GUI engine
// for QB64, see InForm/InForm.bi). A control here is a plain value: the
// designer, the .frm reader/writer and the Qt runtime all share it.

#include <QColor>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace InForm {

// Numbering matches InForm's __UI_Type_* constants so .frmbin ids stay valid.
enum class ControlType : int {
    None         = 0,
    Form         = 1,
    Frame        = 2,
    Button       = 3,
    Label        = 4,
    CheckBox     = 5,
    RadioButton  = 6,
    TextBox      = 7,
    ProgressBar  = 8,
    ListBox      = 9,
    DropdownList = 10,
    MenuBar      = 11,
    MenuItem     = 12,
    MenuPanel    = 13,
    PictureBox   = 14,
    TrackBar     = 15,
    ContextMenu  = 16,
    Font         = 17,
    ToggleSwitch = 18,
};

// Static facts about a control type (InForm's __UI_Types table).
struct ControlTypeInfo
{
    ControlType type;
    const char *name;        // "Button" - InForm name, without the __UI_Type_ prefix
    QSize       defaultSize; // size a freshly inserted control gets
    ControlType turnsInto;   // Edit > Convert type target (None = cannot convert)
    const char *qtClass;     // closest Qt widget class ("" = not a widget, e.g. MenuItem)
};

const ControlTypeInfo &typeInfo(ControlType type);

// "Button" or "__UI_Type_Button" -> ControlType::Button (None when unknown).
ControlType typeFromName(const QString &name);

// Types a user can place from the IDE's widget palette, in palette order.
QList<ControlType> designableTypes();

// The IDE's palette / canvas identify InForm controls as "InForm::<Name>".
QString     paletteKey(ControlType type);
ControlType typeFromPaletteKey(const QString &key);   // None when not an InForm key
bool        isPaletteKey(const QString &key);

// Best InForm equivalent for a Qt widget class ("QPushButton" -> Button).
ControlType typeFromQtClass(const QString &qtClass);

// Properties InForm's editor assigns to a newly inserted control.
QVariantMap defaultProperties(ControlType type);

// InForm text alignment / style symbols, kept verbatim in property values.
inline constexpr const char *kLeft        = "__UI_Left";
inline constexpr const char *kCenter      = "__UI_Center";
inline constexpr const char *kRight       = "__UI_Right";
inline constexpr const char *kTop         = "__UI_Top";
inline constexpr const char *kMiddle      = "__UI_Middle";
inline constexpr const char *kBottom      = "__UI_Bottom";
inline constexpr const char *kTransparent = "__UI_Transparent";
inline constexpr const char *kBullet      = "__UI_Bullet";
inline constexpr const char *kNumericWithBounds = "__UI_NumericWithBounds";

// ─── One control of a form ───────────────────────────────────────────────────
struct Control
{
    ControlType type { ControlType::None };
    QString     name;
    QString     parentName;   // Frame / MenuBar / MenuItem / ContextMenu it belongs to
    QRect       geometry;     // relative to the parent, as in __UI_NewControl

    QString     caption;      // SetCaption
    QString     text;         // Text()  (Form/Button/PictureBox/MenuItem: image path)
    QString     toolTip;      // ToolTip()
    QString     mask;         // Mask()  (TextBox input mask)
    QStringList items;        // AddItem (ListBox / DropdownList)
    QString     fontFiles;    // SetFont("a.ttf?b.ttf", size) - '?'-separated fallbacks
    int         fontSize { 0 };
    QString     contextMenu;  // ContextMenuID = __UI_GetID("...")
    bool        isDefaultButton { false };
    bool        animatedGif  { false };  // image loaded through the gifplay extension
    bool        autoPlayGif  { false };

    // Control(__UI_NewID).<Property> = value. Values are bool (True/False),
    // double, QColor (_RGB32) or QString (an InForm symbol such as "__UI_Middle").
    QVariantMap properties;

    // Statements this port does not model, written back to the .frm unchanged.
    QStringList extraLines;
    // Kayte .kfm "Key=Value" lines not modelled (OnChange=...), kept for .kfm round-trips.
    QStringList kfmExtra;

    bool    flag (const QString &prop) const { return properties.value(prop).toBool(); }
    double  number(const QString &prop, double def = 0) const
    { return properties.contains(prop) ? properties.value(prop).toDouble() : def; }
    QString symbol(const QString &prop) const { return properties.value(prop).toString(); }
    QColor  color(const QString &prop) const  { return properties.value(prop).value<QColor>(); }
};

// __UI_Bind: keeps a property of two controls in sync, both directions.
struct Binding
{
    QString source;
    QString target;
    QString sourceProperty;
    QString targetProperty;
};

} // namespace InForm
