#pragma once
// projectimportsdialog.h - KayteIDE
// Project > Project Imports...: which VS Code / Qt Creator / clang-tidy
// settings the .xprj reads, and what each of them contributes.

#include "projectimport.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

class ProjectImportsDialog : public QDialog
{
    Q_OBJECT
public:
    ProjectImportsDialog(const QString &projectDir, const QList<XprjImport> &imports, QWidget *parent = nullptr);

    QList<XprjImport> imports() const;

private:
    struct Row {
        XprjImport::Kind kind;
        QCheckBox *use  { nullptr };
        QLineEdit *path { nullptr };
    };
    void refreshSummary();
    QString absolute(const QString &path) const;

    QString         m_dir;
    QList<Row>      m_rows;
    QPlainTextEdit *m_summary { nullptr };
};
