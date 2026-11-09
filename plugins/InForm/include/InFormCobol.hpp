#pragma once
// InFormCobol.hpp - KayteIDE InForm plugin
// RAD for COBOL: turns an InForm form into a GnuCOBOL program whose
// SCREEN SECTION draws the form in a terminal. Build and run it with
//
//     cobc -x Form1.cob && ./Form1
//
// The form is laid out on a character grid (one cell per 8x20 pixels):
//   Label        -> literal              TextBox      -> PIC X(n) USING field
//   CheckBox     -> [X] field + caption  RadioButton  -> (X) field + caption
//   ListBox /    -> PIC X(n) field,      TrackBar     -> numeric PIC 9 field
//   DropdownList   holding the item     ProgressBar  -> numeric PIC Z9 display
//   Frame        -> ASCII box            Button       -> [F2] caption
// Buttons are pressed with a function key (F1-F9, in form order; the default
// button with Enter) and run a <NAME>-CLICK paragraph. F10 quits.
// PictureBoxes, menus and fonts have no terminal equivalent and are left out.

#include "InFormDocument.hpp"

namespace InForm {

// A COBOL user-defined word for an InForm name ("txt_Name1" -> "TXT-NAME1").
QString cobolName(const QString &name);

// The GnuCOBOL program (free source format) for the form. Controls it
// cannot draw are left out and described in `warnings`.
QString cobolProgram(const Document &doc, QStringList *warnings = nullptr);

// The <NAME>-CLICK paragraphs `doc` needs that `existingSource` lacks, ready
// to append - so regenerating never overwrites code already written.
QString missingCobolHandlers(const Document &doc, const QString &existingSource);

} // namespace InForm
