// projectimportsdialog.cpp - KayteIDE
#include "projectimportsdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

ProjectImportsDialog::ProjectImportsDialog(const QString &projectDir, const QList<XprjImport> &imports, QWidget *parent)
    : QDialog(parent)
    , m_dir(projectDir)
{
    setWindowTitle(tr("Project Imports"));
    resize(720, 520);
    auto *lay = new QVBoxLayout(this);

    auto *intro = new QLabel(tr("Settings from other tools that this project (.xprj) reads every time it opens. "
                                "Build configurations they define appear in Project > Build Configuration "
                                "(named 'VS Code: ...' / 'Qt Creator: ...'); edit them in the original files."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    const QList<XprjImport> detected = XprjProject::detectImports(projectDir);
    auto *grid = new QGridLayout;
    int row = 0;
    for (XprjImport::Kind kind : { XprjImport::VSCode, XprjImport::QtCreator, XprjImport::ClangTidy }) {
        XprjImport imp { kind, QString(), false };
        bool found = false;
        for (const XprjImport &i : imports) if (i.kind == kind) { imp = i; found = true; }
        if (!found)                                   // offer what the folder has, unticked
            for (const XprjImport &i : detected) if (i.kind == kind) { imp = i; imp.enabled = false; }

        Row r { kind };
        r.use = new QCheckBox(XprjImport::label(kind), this);
        r.use->setChecked(imp.enabled && !imp.path.isEmpty());
        r.path = new QLineEdit(imp.path, this);
        r.path->setPlaceholderText(kind == XprjImport::VSCode    ? tr(".vscode folder")
                                 : kind == XprjImport::QtCreator ? tr(".qtcreator folder or CMakeLists.txt.user / *.pro.user")
                                                                 : tr(".clang-tidy file"));
        auto *browse = new QPushButton(tr("Browse..."), this);
        connect(browse, &QPushButton::clicked, this, [this, r, kind] {
            const QString start = absolute(r.path->text()).isEmpty() ? m_dir : absolute(r.path->text());
            QString chosen;
            if (kind == XprjImport::VSCode)
                chosen = QFileDialog::getExistingDirectory(this, tr("VS Code Settings Folder"), start);
            else
                chosen = QFileDialog::getOpenFileName(this, kind == XprjImport::QtCreator ? tr("Qt Creator Settings")
                                                                                          : tr("clang-tidy Configuration"),
                                                      start, kind == XprjImport::QtCreator ? tr("Qt Creator settings (*.user)")
                                                                                          : tr("clang-tidy (.clang-tidy *.yaml *.yml);;All Files (*)"),
                                                      nullptr, QFileDialog::DontUseNativeDialog); // shows dot-files
            if (chosen.isEmpty()) return;
            const QString rel = QDir(m_dir).relativeFilePath(chosen);
            r.path->setText(rel.startsWith(QLatin1String("..")) ? chosen : rel);
            r.use->setChecked(true);
        });
        connect(r.use, &QCheckBox::toggled, this, &ProjectImportsDialog::refreshSummary);
        connect(r.path, &QLineEdit::editingFinished, this, &ProjectImportsDialog::refreshSummary);
        grid->addWidget(r.use, row, 0);
        grid->addWidget(r.path, row, 1);
        grid->addWidget(browse, row, 2);
        m_rows << r;
        ++row;
    }
    grid->setColumnStretch(1, 1);
    lay->addLayout(grid);

    lay->addWidget(new QLabel(tr("What they provide:"), this));
    m_summary = new QPlainTextEdit(this);
    m_summary->setReadOnly(true);
    m_summary->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    lay->addWidget(m_summary, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto *reread = buttons->addButton(tr("Re-read"), QDialogButtonBox::ResetRole);
    connect(reread, &QPushButton::clicked, this, &ProjectImportsDialog::refreshSummary);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);

    refreshSummary();
}

QString ProjectImportsDialog::absolute(const QString &path) const
{
    if (path.trimmed().isEmpty()) return {};
    return QFileInfo(path).isAbsolute() ? path : QDir(m_dir).absoluteFilePath(path.trimmed());
}

QList<XprjImport> ProjectImportsDialog::imports() const
{
    QList<XprjImport> out;
    for (const Row &r : m_rows)
        if (!r.path->text().trimmed().isEmpty())
            out << XprjImport { r.kind, r.path->text().trimmed(), r.use->isChecked() };
    return out;
}

void ProjectImportsDialog::refreshSummary()
{
    QStringList lines;
    for (const Row &r : m_rows) {
        const QString label = XprjImport::label(r.kind);
        const QString path = absolute(r.path->text());
        if (!r.use->isChecked() || path.isEmpty()) {
            lines << QStringLiteral("%1 - not used").arg(label) << QString();
            continue;
        }
        lines << QStringLiteral("%1 - %2").arg(label, QDir::toNativeSeparators(path));
        if (r.kind == XprjImport::ClangTidy) {
            const ClangTidyConfig c = ProjectImporters::readClangTidy(path);
            if (!c.ok) { lines << QStringLiteral("  ✗ ") + c.error << QString(); continue; }
            for (const QString &s : c.summary) lines << QStringLiteral("  ") + s;
            const QString exe = ProjectImporters::findClangTidy();
            lines << (exe.isEmpty() ? tr("  ✗ clang-tidy is not installed (brew install llvm)")
                                    : tr("  clang-tidy: %1").arg(exe));
        } else {
            const ProjectImport p = r.kind == XprjImport::VSCode ? ProjectImporters::readVSCode(m_dir, path)
                                                                 : ProjectImporters::readQtCreator(m_dir, path);
            if (!p.ok) { lines << QStringLiteral("  ✗ ") + p.error << QString(); continue; }
            for (const QString &s : p.summary) lines << QStringLiteral("  ") + s;
            for (const BuildConfiguration &c : p.configs)
                lines << QStringLiteral("    - %1%2").arg(c.name, c.name == p.preferredActive ? tr("  (default)") : QString());
            for (const QString &n : p.notes) lines << QStringLiteral("  ! ") + n;
        }
        lines << QString();
    }
    m_summary->setPlainText(lines.join(QLatin1Char('\n')));
}
