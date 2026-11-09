#include "newprojectdialog.h"
#include "ui_newprojectdialog.h" // This is a generated header from the .ui file.

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {
const char *kLocationKey = "newProject/location";
}

NewProjectDialog::NewProjectDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::NewProjectDialog)
{
    ui->setupUi(this);

    // Start where the last project was created (default: ~/Documents).
    const QString last = QSettings().value(QLatin1String(kLocationKey)).toString();
    ui->locationLineEdit->setText(
        QFileInfo(last).isDir() ? last
                                : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));

    connect(ui->browseButton, &QPushButton::clicked, this, &NewProjectDialog::browse);
    connect(ui->projectNameLineEdit, &QLineEdit::textChanged, this, &NewProjectDialog::validate);
    connect(ui->locationLineEdit, &QLineEdit::textChanged, this, &NewProjectDialog::validate);
    connect(this, &QDialog::accepted, this, [this] {
        QSettings().setValue(QLatin1String(kLocationKey), location());
    });

    ui->projectNameLineEdit->setFocus();
    validate();
}

NewProjectDialog::~NewProjectDialog()
{
    delete ui;
}

QString NewProjectDialog::projectName() const
{
    return ui->projectNameLineEdit->text().trimmed();
}

NewProjectDialog::ProjectType NewProjectDialog::projectType() const
{
    return ProjectType(ui->projectTypeComboBox->currentIndex());
}

QString NewProjectDialog::location() const
{
    QString dir = ui->locationLineEdit->text().trimmed();
    if (dir.startsWith(QLatin1String("~/")) || dir == QLatin1String("~"))
        dir = QDir::homePath() + dir.mid(1);
    return QDir::cleanPath(dir);
}

QString NewProjectDialog::projectDir() const
{
    return QDir(location()).absoluteFilePath(projectName());
}

void NewProjectDialog::browse()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Project Location"), location());
    if (!dir.isEmpty())
        ui->locationLineEdit->setText(dir);
}

// OK only for a usable name and a location where the project folder can be
// created; the label says what's wrong, or where the project will go.
void NewProjectDialog::validate()
{
    static const QRegularExpression badChars(QStringLiteral(R"([/\\:*?"<>|])"));
    const QString name = projectName();
    QString problem;
    if (name.isEmpty())
        problem = tr("Enter a project name.");
    else if (name.contains(badChars) || name.startsWith(QLatin1Char('.')))
        problem = tr("The name can't contain / \\ : * ? \" < > | or start with a dot.");
    else if (location().isEmpty() || !QFileInfo(location()).isDir())
        problem = tr("Choose an existing folder for the location.");
    else if (!QFileInfo(location()).isWritable())
        problem = tr("The location isn't writable.");
    else if (QFileInfo::exists(projectDir()))
        problem = tr("%1 already exists - choose another name or location.").arg(projectDir());

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
    ui->summaryLabel->setText(problem.isEmpty() ? tr("Creates %1").arg(QDir::toNativeSeparators(projectDir()))
                                                : problem);
    ui->summaryLabel->setEnabled(problem.isEmpty());
}
