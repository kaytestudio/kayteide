#pragma once
// InFormConvert.hpp - KayteIDE InForm plugin
// Qt6 C++ port of InForm/vbdos2inform.bas: converts a Visual Basic for DOS
// text form (.frm starting with "Version 1.00") into an InForm form, and
// generates the matching QB64 event-procedure skeleton (.bas).

#include "InFormDocument.hpp"

namespace InForm {

// True when `source` looks like a VB-DOS text form ("Version 1.00" + BEGIN Form).
bool isVbDosForm(const QString &source);

// VB-DOS form text -> InForm document. Mirrors vbdos2inform, except that a
// Char(n) text cell becomes 8x24 px (vbdos2inform's 8x16 overlapped one-row
// controls). QBColor(n) becomes _RGB32,
// control arrays get numbered names (Option1, Option1_2, ...) and unknown
// control types become PictureBoxes.
bool convertVbDosForm(const QString &source, Document *out, QString *error = nullptr);
bool convertVbDosFile(const QString &filePath, Document *out, QString *error = nullptr);

// The QB64 main program InForm pairs with a .frm: control ID variables,
// $INCLUDEs of the InForm runtime and the form, and one SUB per InForm event
// with a SELECT CASE over the controls that can raise it.
QString eventSkeleton(const Document &doc, const QString &frmFileName);

} // namespace InForm
