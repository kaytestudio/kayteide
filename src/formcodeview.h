#pragma once
// formcodeview.h - KayteIDE
// The "Code" tab under the UI Designer: shows, live, the code for the widgets
// on the canvas - a Kayte program / form, a GnuCOBOL program, an InForm form,
// Qt C++ or .ui XML.
// Editing a form format (.kfm, .frm, .ui, Qt C++) syncs back to the canvas;
// an edited Kayte or COBOL program is kept as written and only gains new
// handlers.

#include <QHash>
#include <QSet>
#include <QWidget>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSyntaxHighlighter;
class QTimer;
class UiCanvasWidget;
class InFormIntegration;

class FormCodeView : public QWidget
{
    Q_OBJECT
public:
    FormCodeView(UiCanvasWidget *canvas, InFormIntegration *inform, QWidget *parent = nullptr);

    // Regenerates the code now (it also refreshes itself while shown).
    void refresh();

signals:
    // "Save As..." wrote the code to this file.
    void fileSaved(const QString &path);

protected:
    void showEvent(QShowEvent *event) override;

private:
    // Values are saved in the settings: add new languages at the end.
    enum Language { KayteProgram, KjsProgram, BasicProgram, KayteForm, InFormForm, QtCpp, QtUi,
                    CobolProgram };
    static bool isProgram(Language lang) { return lang <= BasicProgram || lang == CobolProgram; }

    Language currentLanguage() const;
    QString generate(Language lang, QString *note);
    QString qtCpp() const;
    QString fileSuffix(Language lang) const;
    void    setHighlighter(Language lang);
    void    setNote(const QString &text, bool error = false);
    void    highlightSelected();
    void    onSaveAs();
    void    flushPendingSync();
    void    syncToCanvas(Language lang);

    UiCanvasWidget     *m_canvas;
    InFormIntegration  *m_inform;
    QComboBox          *m_language;
    QLabel             *m_note;
    QPushButton        *m_reset;
    QPlainTextEdit     *m_editor;
    QSyntaxHighlighter *m_highlighter { nullptr };
    QTimer             *m_refreshTimer;
    QTimer             *m_syncTimer;

    bool m_settingText { false };   // our own setPlainText, not the user typing
    bool m_applying    { false };   // pushing a code edit to the canvas
    bool m_unsynced    { false };   // typed since the last sync
    Language m_shownLanguage { KayteProgram };   // what the editor text is written in
    // Widgets the current form text describes; canvas widgets outside it
    // (no equivalent in that format) are left alone when the text is edited.
    QSet<QString> m_baseNames;
    // Programs the user has edited, by Language - kept as written.
    QHash<int, QString> m_programEdits;
};
