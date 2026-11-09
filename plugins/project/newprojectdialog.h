#ifndef NEWPROJECTDIALOG_H
#define NEWPROJECTDIALOG_H

#include <QDialog>

// Forward declaration to avoid including the full Ui header in this file.
namespace Ui {
    class NewProjectDialog;
}

// Asks for a project's name, type and parent folder. OK is enabled only for
// a valid name whose folder doesn't exist yet; the caller creates the files.
class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    enum ProjectType { KayteApplication = 0, CppConsole, PascalConsole, QtWidgets };

    explicit NewProjectDialog(QWidget *parent = nullptr);
    ~NewProjectDialog();

    QString projectName() const;
    ProjectType projectType() const;
    QString location() const;      // parent folder
    QString projectDir() const;    // location / name

private:
    void browse();
    void validate();

    Ui::NewProjectDialog *ui;
};

#endif // NEWPROJECTDIALOG_H
