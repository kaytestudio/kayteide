#pragma once
// informintegration.h - KayteIDE
// Embeds the InForm plugin (plugins/InForm) in the UI designer: imports InForm
// .frm forms onto the UiCanvasWidget, exports the canvas back to .frm, and
// previews it live through InForm::Runtime.

#include <QObject>
#include <QStringList>

#include "InFormDocument.hpp"
#include "uicanvaswidget.h"

class QMainWindow;
class UiCanvasWidget;

class InFormIntegration : public QObject
{
    Q_OBJECT
public:
    InFormIntegration(UiCanvasWidget *canvas, QMainWindow *window);

    // Canvas ⇄ InForm document. `skipped` counts canvas widgets with no
    // InForm equivalent (e.g. a QCalendarWidget), which are left out.
    InForm::Document documentFromCanvas(int *skipped = nullptr) const;
    void             loadDocument(const InForm::Document &doc);

    // The canvas items for `doc` without touching the canvas (used when the
    // Code tab is edited), and the size the canvas needs to hold them.
    QList<CanvasItem> itemsFromDocument(const InForm::Document &doc, QSize *canvasSize = nullptr) const;
    // Takes the form's name, caption and size from `doc`, keeping the rest.
    void             updateFormFromDocument(const InForm::Document &doc);

public slots:
    void onImportFrm();
    void onConvertVbDos();      // vbdos2inform: VB-DOS .frm -> InForm .frm + .bas
    void onImportKfm();         // Kayte .kfm form -> canvas
    void onExportKayte();       // canvas -> Kayte .kfm + .kayte / .kjs / .bas program
    void onExportWeb();         // canvas + .kjs program -> web app (HTML + kayte-web.js)
    void onExportFrm();
    void onPreview();

private:
    // Loads a converted VB-DOS form and offers to save <name>_InForm.frm/.bas.
    void finishVbDosConversion(const QString &sourcePath, const InForm::Document &doc);

    UiCanvasWidget  *m_canvas { nullptr };
    QMainWindow     *m_window { nullptr };

    // Form-level state from the last import that the canvas has no place for.
    InForm::Control         m_formMeta;
    QList<InForm::Binding>  m_bindings;
    QStringList             m_docExtra;
    QString                 m_lastPath;       // last InForm .frm
    QString                 m_lastKaytePath;  // last Kayte .kfm / program
};
