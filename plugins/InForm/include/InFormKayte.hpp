#pragma once
// InFormKayte.hpp - KayteIDE InForm plugin
// Bridges InForm forms and Kayte lang (github.com/ringsce/kayte-lang).
// Kayte programs load windows from .kfm form files with QT "loadform"; the
// pixel-positioned INI flavour of .kfm read by libkayte_qt6 is:
//
//     [FORM:Form1]
//     [CONTROL:Form1:vctForm]
//       Caption="My Form"
//       Width=400
//       Height=300
//     [CONTROL:Button1:vctButton]
//       Caption="OK"
//       Left=20
//       ...
//
// It knows Button, Label, TextBox, CheckBox, ComboBox and ListBox, and binds
// a SUB named <Control>_Click to a control's click by VB convention.

#include "InFormDocument.hpp"

namespace InForm {

enum class KayteLanguage {
    Kayte,   // .kayte - Kayte BASIC front end
    Kjs,     // .kjs   - JavaScript-like front end
    Basic,   // .bas   - Kayte BASIC (compile without --qbs: QuickBASIC mode has no QT)
};

// .kayte / .kjs / .bas -> language (Kayte for anything else).
KayteLanguage kayteLanguageForFile(const QString &filePath);

// The .kfm control class for an InForm control ("vctButton"), or an empty
// string when Kayte forms have no equivalent (Frame, PictureBox, menus, ...).
QString kfmControlClass(ControlType type);

// Writes the form as an INI .kfm. Controls with no Kayte equivalent are left
// out and described in `warnings`; controls inside frames keep their place
// on the form (a .kfm has no containers).
QString writeKfm(const Document &doc, QStringList *warnings = nullptr);
bool    saveKfm(const Document &doc, const QString &filePath,
                QStringList *warnings = nullptr, QString *error = nullptr);

// Reads an INI .kfm back into an InForm document. The declarative flavour
// (form X { layout: VBox { ... } }) is laid out by Qt at run time and has no
// positions, so it is refused with an explanatory error.
bool readKfm(const QString &source, Document *out, QString *error = nullptr);
bool loadKfm(const QString &filePath, Document *out, QString *error = nullptr);

// A Kayte program that opens the form: QT "loadform", a variable per control
// (QT "find"), list items / check states the .kfm cannot hold, and a
// <Control>_Click handler for every clickable control.
QString kayteProgram(const Document &doc, const QString &kfmFileName, KayteLanguage lang);

// The <Control>_Click handlers `doc` needs that `existingSource` lacks, ready
// to append - so re-exporting a form never overwrites code already written.
QString missingKayteHandlers(const Document &doc, const QString &existingSource, KayteLanguage lang);

} // namespace InForm
