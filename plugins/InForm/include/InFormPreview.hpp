#pragma once
// InFormPreview.hpp - KayteIDE InForm plugin
// Runs an InForm form live (through InForm::Runtime) next to a log of the
// InForm events it raises - the Qt counterpart of InForm's UiEditorPreview.

#include "InFormRuntime.hpp"

#include <QDialog>

class QPlainTextEdit;

namespace InForm {

class PreviewDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreviewDialog(const Document &doc, const QString &baseDir = {},
                           QWidget *parent = nullptr);

    Runtime *runtime() const { return m_runtime; }

private:
    Runtime        *m_runtime { nullptr };
    QPlainTextEdit *m_log     { nullptr };
};

} // namespace InForm
