#pragma once
// componenticons.h - KayteIDE
// Icons for UI-designer components (Qt widgets such as "QLineEdit" and InForm
// controls such as "InForm::Button"), drawn from the Font Awesome font in the
// colour of their kind - the same accents the UI Designer canvas uses. Shared
// by the Widget Palette and the Components dock so both show the same icons.

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

// Icon for a palette / canvas widget type. Empty when Font Awesome is not
// available. Results are cached per type.
QIcon componentIcon(const QString &widgetType, const QFont &faFont);

// Icon for a palette category ("Buttons", "Input", ... "InForm").
QIcon componentCategoryIcon(const QString &category, const QFont &faFont);

// Any Font Awesome glyph (UTF-8, e.g. ICON_FA_MAGNIFYING_GLASS) as an icon.
QIcon glyphIcon(const char *glyph, const QColor &color, const QFont &faFont);
