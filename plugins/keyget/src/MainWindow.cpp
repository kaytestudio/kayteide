#include "MainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QCloseEvent>
#include <QMessageBox>
#include <QDateTime>
#include <QSettings>
#include <QCoreApplication>
#include <QFileInfo>
#include <QApplication>
#include <QDir>
#include <QIcon>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("KayteIDEKeyGet Auto-Installer");
    setMinimumSize(820, 620);
    resize(960, 700);

    m_checker = new UpdateChecker(this);
    m_builder = new BuildManager(this);
    m_installer = new AppInstaller(this);

    setupUi();
    setupTray();
    applyStyleSheet();

    // Restore settings. Each setter fires onSettingsChanged(), which would save
    // the half-filled form over the stored values - so hold off until done.
    m_loadingSettings = true;
    QSettings s("ringsce", "KayteIDEUpdater");
    m_sourceDirEdit->setText(s.value("sourceDir",
        QDir::homePath() + "/kayteide-src").toString());
    m_buildDirEdit->setText(s.value("buildDir",
        QDir::homePath() + "/kayteide-build").toString());
    m_installDirEdit->setText(s.value("installDir",
        QDir::homePath() + "/.local").toString());
    m_jobsSpin->setValue(s.value("jobs", 4).toInt());
    m_intervalSpin->setValue(s.value("interval", 30).toInt());
    m_autoCheckBox->setChecked(s.value("autoCheck", true).toBool());
    m_installChk->setChecked(s.value("runInstall", false).toBool());
    m_appEdit->setText(s.value("installedApp", defaultInstalledApp()).toString());
    m_autoUpdateChk->setChecked(s.value("autoUpdate", true).toBool());
    m_loadingSettings = false;

    onSettingsChanged(); // push settings into objects

    // Checker signals
    connect(m_checker, &UpdateChecker::updateAvailable, this, &MainWindow::onUpdateAvailable);
    connect(m_checker, &UpdateChecker::alreadyUpToDate, this, &MainWindow::onAlreadyUpToDate);
    connect(m_checker, &UpdateChecker::checkFailed,     this, &MainWindow::onCheckFailed);
    connect(m_checker, &UpdateChecker::checkStarted,    this, &MainWindow::onCheckStarted);

    // Builder signals
    connect(m_builder, &BuildManager::stepChanged,    this, &MainWindow::onBuildStepChanged);
    connect(m_builder, &BuildManager::buildSucceeded, this, &MainWindow::onBuildSucceeded);
    connect(m_builder, &BuildManager::buildFailed,    this, &MainWindow::onBuildFailed);
    connect(m_builder, &BuildManager::logLine,        m_log, &LogWidget::appendLog);
    connect(m_builder, &BuildManager::progressChanged,m_progressBar, &QProgressBar::setValue);

    // Installer signals: close KayteIDE, swap in the new build, reopen it
    connect(m_installer, &AppInstaller::logLine, m_log, &LogWidget::appendLog);
    connect(m_installer, &AppInstaller::postponed, this, [this](const QString &msg) {
        setStatus("⏸ " + msg, "#d29922");
        m_log->appendLog(msg, true);
        m_installBtn->setEnabled(true);
    });
    connect(m_installer, &AppInstaller::finished, this, [this](bool ok, const QString &msg) {
        setStatus((ok ? "✔ " : "✘ ") + msg, ok ? "#3fb950" : "#f85149");
        m_log->appendLog(msg, !ok);
        m_installBtn->setEnabled(!ok);
        if (ok) {
            m_checker->setCurrentSha(AppInstaller::installedCommit(installedApp()));
            m_tray->showMessage("KayteIDE Updated", msg, QSystemTrayIcon::Information, 5000);
        }
    });

    if (m_autoCheckBox->isChecked())
        m_checker->startAutoCheck();

    setStatus("Ready - idle.", "#8b949e");
}

// ─────────────────────────────────────────────────────────────────────────────
// UI setup
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    // ── Header ───────────────────────────────────────────────────────────────
    auto *headerWidget = new QWidget;
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0,0,0,0);

    auto *titleLabel = new QLabel("KayteIDEKeyGet Auto-Installer");
    titleLabel->setObjectName("titleLabel");

    auto *repoLabel = new QLabel(
        "<a href='https://github.com/ringsce/keyget' "
        "style='color:#58a6ff;'>github.com/ringsce/keyget</a>");
    repoLabel->setOpenExternalLinks(true);
    repoLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(repoLabel);
    rootLayout->addWidget(headerWidget);

    // ── Status card ──────────────────────────────────────────────────────────
    auto *statusGroup = new QGroupBox("Status");
    auto *statusGrid  = new QGridLayout(statusGroup);

    m_statusLabel    = new QLabel("Idle");
    m_commitLabel    = new QLabel("-");
    m_lastCheckLabel = new QLabel("-");
    m_progressBar    = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setObjectName("progressBar");

    statusGrid->addWidget(new QLabel("Status:"),      0, 0);
    statusGrid->addWidget(m_statusLabel,               0, 1);
    statusGrid->addWidget(new QLabel("Latest commit:"),1, 0);
    statusGrid->addWidget(m_commitLabel,               1, 1);
    statusGrid->addWidget(new QLabel("Last check:"),   2, 0);
    statusGrid->addWidget(m_lastCheckLabel,            2, 1);
    statusGrid->addWidget(m_progressBar,               3, 0, 1, 2);
    statusGrid->setColumnStretch(1, 1);

    rootLayout->addWidget(statusGroup);

    // ── Buttons ──────────────────────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;
    m_checkBtn = new QPushButton("🔍  Check Now");
    m_buildBtn = new QPushButton("🔨  Pull & Build");
    m_abortBtn = new QPushButton("⛔  Abort");
    m_installBtn = new QPushButton("⬇  Install Now");
    m_installBtn->setToolTip("Close KayteIDE, install the last build (keeping its toolchain) and reopen it");
    m_installBtn->setEnabled(false);
    m_buildBtn->setObjectName("buildBtn");
    m_abortBtn->setObjectName("abortBtn");
    m_abortBtn->setEnabled(false);

    btnRow->addWidget(m_checkBtn);
    btnRow->addWidget(m_buildBtn);
    btnRow->addWidget(m_installBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_abortBtn);
    rootLayout->addLayout(btnRow);

    connect(m_checkBtn, &QPushButton::clicked, this, &MainWindow::onCheckNowClicked);
    connect(m_buildBtn, &QPushButton::clicked, this, &MainWindow::onBuildClicked);
    connect(m_abortBtn, &QPushButton::clicked, this, &MainWindow::onAbortClicked);
    connect(m_installBtn, &QPushButton::clicked, this, &MainWindow::onInstallClicked);

    // ── Splitter: settings | log ──────────────────────────────────────────────
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(6);

    // Settings panel
    auto *settingsGroup = new QGroupBox("Build Settings");
    settingsGroup->setMinimumWidth(260);
    settingsGroup->setMaximumWidth(320);
    auto *form = new QFormLayout(settingsGroup);

    m_sourceDirEdit  = new QLineEdit;
    m_buildDirEdit   = new QLineEdit;
    m_installDirEdit = new QLineEdit;
    m_jobsSpin       = new QSpinBox;
    m_intervalSpin   = new QSpinBox;
    m_autoCheckBox   = new QCheckBox("Enable auto-check");
    m_installChk     = new QCheckBox("Run cmake --install");
    m_appEdit        = new QLineEdit;
    m_appEdit->setToolTip("The KayteIDE.app to update (closed, replaced and reopened)");
    m_autoUpdateChk  = new QCheckBox("Build && install new commits automatically");
    m_autoUpdateChk->setToolTip("When a new commit lands on main: build it, close KayteIDE, "
                                "install the build and reopen KayteIDE");

    m_jobsSpin->setRange(1, 32);
    m_intervalSpin->setRange(1, 1440);
    m_intervalSpin->setSuffix(" min");

    auto makeBrowseRow = [&](QLineEdit *edit) {
        auto *w = new QWidget;
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0,0,0,0);
        auto *btn = new QPushButton("...");
        btn->setFixedWidth(28);
        h->addWidget(edit);
        h->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [edit, this](){
            QString dir = QFileDialog::getExistingDirectory(this, "Select directory",
                edit->text().isEmpty() ? QDir::homePath() : edit->text());
            if (!dir.isEmpty()) { edit->setText(dir); onSettingsChanged(); }
        });
        return w;
    };

    form->addRow("Source dir:",  makeBrowseRow(m_sourceDirEdit));
    form->addRow("Build dir:",   makeBrowseRow(m_buildDirEdit));
    form->addRow("Install dir:", makeBrowseRow(m_installDirEdit));
    form->addRow("Parallel jobs:", m_jobsSpin);
    form->addRow("Check interval:", m_intervalSpin);
    form->addRow(m_autoCheckBox);
    form->addRow(m_installChk);
    {
        auto *w = new QWidget;
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0,0,0,0);
        auto *btn = new QPushButton("...");
        btn->setFixedWidth(28);
        h->addWidget(m_appEdit);
        h->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this] {
            const QString app = QFileDialog::getOpenFileName(this, "Select KayteIDE.app",
                QFileInfo(installedApp()).absolutePath(), "Applications (*.app)");
            if (!app.isEmpty()) { m_appEdit->setText(app); onSettingsChanged(); }
        });
        form->addRow("KayteIDE app:", w);
    }
    form->addRow(m_autoUpdateChk);
    connect(m_appEdit, &QLineEdit::editingFinished, this, &MainWindow::onSettingsChanged);
    connect(m_autoUpdateChk, &QCheckBox::toggled, this, &MainWindow::onSettingsChanged);

    connect(m_sourceDirEdit,  &QLineEdit::editingFinished, this, &MainWindow::onSettingsChanged);
    connect(m_buildDirEdit,   &QLineEdit::editingFinished, this, &MainWindow::onSettingsChanged);
    connect(m_installDirEdit, &QLineEdit::editingFinished, this, &MainWindow::onSettingsChanged);
    connect(m_jobsSpin,    QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onSettingsChanged);
    connect(m_intervalSpin,QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onSettingsChanged);
    connect(m_autoCheckBox, &QCheckBox::toggled, this, &MainWindow::onSettingsChanged);
    connect(m_installChk,   &QCheckBox::toggled, this, &MainWindow::onSettingsChanged);

    // Log panel
    auto *logGroup = new QGroupBox("Build Log");
    auto *logLayout = new QVBoxLayout(logGroup);
    m_log = new LogWidget;
    auto *clearLogBtn = new QPushButton("Clear log");
    clearLogBtn->setFixedHeight(24);
    connect(clearLogBtn, &QPushButton::clicked, m_log, &LogWidget::clear);
    logLayout->addWidget(m_log);
    logLayout->addWidget(clearLogBtn);

    splitter->addWidget(settingsGroup);
    splitter->addWidget(logGroup);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    rootLayout->addWidget(splitter, 1);
}

void MainWindow::setupTray()
{
    m_tray = new QSystemTrayIcon(QIcon::fromTheme("system-software-update"), this);
    m_trayMenu = new QMenu(this);

    auto *showAct  = m_trayMenu->addAction("Show");
    auto *checkAct = m_trayMenu->addAction("Check for updates");
    m_trayMenu->addSeparator();
    auto *quitAct  = m_trayMenu->addAction("Quit");

    connect(showAct,  &QAction::triggered, this, &QWidget::show);
    connect(checkAct, &QAction::triggered, this, &MainWindow::onCheckNowClicked);
    connect(quitAct,  &QAction::triggered, qApp, &QApplication::quit);
    connect(m_tray,   &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);

    m_tray->setContextMenu(m_trayMenu);
    m_tray->setToolTip("KayteIDE Auto-Updater");
    m_tray->show();
}

void MainWindow::applyStyleSheet()
{
    qApp->setStyleSheet(R"(
        QMainWindow, QWidget {
            background-color: #0d1117;
            color: #c9d1d9;
            font-family: 'Inter', 'Segoe UI', 'SF Pro Text', sans-serif;
            font-size: 13px;
        }
        QGroupBox {
            border: 1px solid #30363d;
            border-radius: 8px;
            margin-top: 10px;
            padding-top: 8px;
            color: #8b949e;
            font-weight: 600;
            font-size: 11px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
        }
        QLabel#titleLabel {
            font-size: 18px;
            font-weight: 700;
            color: #f0f6fc;
        }
        QPushButton {
            background-color: #21262d;
            color: #c9d1d9;
            border: 1px solid #30363d;
            border-radius: 6px;
            padding: 6px 16px;
            font-weight: 500;
        }
        QPushButton:hover  { background-color: #30363d; border-color: #8b949e; }
        QPushButton:pressed{ background-color: #161b22; }
        QPushButton#buildBtn {
            background-color: #238636;
            color: #ffffff;
            border-color: #2ea043;
            font-weight: 600;
        }
        QPushButton#buildBtn:hover  { background-color: #2ea043; }
        QPushButton#buildBtn:disabled { background-color: #21262d; color: #484f58; }
        QPushButton#abortBtn {
            background-color: #6e2c2c;
            color: #ffa198;
            border-color: #f85149;
        }
        QPushButton#abortBtn:hover  { background-color: #8e3c3c; }
        QPushButton#abortBtn:disabled { background-color: #21262d; color: #484f58; border-color: #30363d; }
        QProgressBar#progressBar {
            background-color: #161b22;
            border: 1px solid #30363d;
            border-radius: 4px;
            height: 16px;
            text-align: center;
            color: #c9d1d9;
            font-size: 11px;
        }
        QProgressBar#progressBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #1f6feb, stop:1 #388bfd);
            border-radius: 3px;
        }
        QLineEdit, QSpinBox {
            background-color: #161b22;
            color: #c9d1d9;
            border: 1px solid #30363d;
            border-radius: 5px;
            padding: 4px 8px;
        }
        QLineEdit:focus, QSpinBox:focus { border-color: #58a6ff; }
        QCheckBox { color: #c9d1d9; }
        QCheckBox::indicator {
            width: 14px;
            height: 14px;
            border: 1px solid #30363d;
            border-radius: 3px;
            background: #161b22;
        }
        QCheckBox::indicator:checked { background: #1f6feb; border-color: #388bfd; }
        QSplitter::handle { background: #21262d; }
        QScrollBar:vertical {
            background: #161b22;
            width: 8px;
        }
        QScrollBar::handle:vertical {
            background: #30363d;
            border-radius: 4px;
            min-height: 20px;
        }
        QFormLayout QLabel { color: #8b949e; }
    )");
}

// ─────────────────────────────────────────────────────────────────────────────
// Checker slots
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onCheckStarted()
{
    // Compare GitHub's latest commit with the one the installed app was built from.
    m_checker->setCurrentSha(AppInstaller::installedCommit(installedApp()));
    setStatus("Checking for updates...", "#79c0ff");
    m_lastCheckLabel->setText(QDateTime::currentDateTime().toString("hh:mm:ss dd/MM/yyyy"));
}

void MainWindow::onUpdateAvailable(const CommitInfo &commit)
{
    m_updatePending = true;
    const QString msg = QString("%1 - %2 (%3)")
        .arg(commit.shortSha, commit.message, commit.author);
    m_commitLabel->setText(msg);
    setStatus("⬆ Update available!", "#3fb950");

    m_tray->showMessage("KayteIDE Update Available",
        QString("New commit: %1\n%2").arg(commit.shortSha, commit.message),
        QSystemTrayIcon::Information, 5000);

    m_log->appendSection(QString("Update detected: %1").arg(commit.shortSha));
    m_log->appendLog(QString("Commit: %1").arg(commit.message));
    m_log->appendLog(QString("Author: %1  (%2)").arg(commit.author,
        commit.timestamp.toString(Qt::ISODate)));
    m_log->appendLog(QString("URL: %1").arg(commit.url));

    // Build it (once per commit); onBuildSucceeded installs it.
    if (m_autoUpdateChk->isChecked() && !m_builder->isRunning() && !m_installer->isBusy()
        && commit.sha != m_buildingSha) {
        m_buildingSha = commit.sha;
        m_log->appendLog("Building the new commit automatically...");
        onBuildClicked();
    }
}

void MainWindow::onAlreadyUpToDate()
{
    m_updatePending = false;
    setStatus("✔ Already up to date.", "#3fb950");
    m_log->appendLog("✔ Repository is up to date.");
}

void MainWindow::onCheckFailed(const QString &error)
{
    setStatus("✘ Check failed: " + error, "#f85149");
    m_log->appendLog("✘ Check error: " + error, true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Builder slots
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onBuildStepChanged(BuildStep step, const QString &desc)
{
    setStatus(desc, "#79c0ff");
    m_log->appendSection(desc);
    Q_UNUSED(step)
}

void MainWindow::onBuildSucceeded()
{
    setStatus("✔ Build succeeded!", "#3fb950");
    m_log->appendLog("✔ KayteIDE built successfully.");
    setControlsEnabled(true);
    m_abortBtn->setEnabled(false);
    m_progressBar->setValue(100);
    m_updatePending = false;

#ifdef Q_OS_MACOS
    m_installBtn->setEnabled(true);
    if (m_autoUpdateChk->isChecked()) {
        m_log->appendSection("Installing the new build...");
        onInstallClicked();
        return;
    }
#endif
    m_tray->showMessage("KayteIDE Built",
        "Build succeeded! Use Install Now to replace KayteIDE with it.",
        QSystemTrayIcon::Information, 5000);
}

void MainWindow::onInstallClicked()
{
#ifdef Q_OS_MACOS
    m_installBtn->setEnabled(false);
    setStatus("Installing - closing KayteIDE...", "#79c0ff");
    m_installer->install(m_builder->builtApp(), installedApp());
#else
    m_log->appendLog("Installing the app bundle is only supported on macOS; "
                     "use \"Run cmake --install\" instead.", true);
#endif
}

// The KayteIDE.app this updater lives in (Contents/MacOS/KayteIDEUpdater),
// else /Applications/KayteIDE.app.
QString MainWindow::defaultInstalledApp()
{
    QDir dir(QCoreApplication::applicationDirPath());          // .../Contents/MacOS
    if (dir.cdUp() && dir.cdUp() && dir.dirName().endsWith(".app")
        && QFileInfo::exists(dir.filePath("Contents/MacOS/KayteIDE")))
        return dir.absolutePath();
    return "/Applications/KayteIDE.app";
}

QString MainWindow::installedApp() const
{
    const QString app = m_appEdit->text().trimmed();
    return app.isEmpty() ? defaultInstalledApp() : QDir::cleanPath(app);
}

void MainWindow::onBuildFailed(const QString &reason)
{
    m_buildingSha.clear();   // let the next check try this commit again
    setStatus("✘ Build failed: " + reason, "#f85149");
    m_log->appendLog("✘ Build failed: " + reason, true);
    setControlsEnabled(true);
    m_abortBtn->setEnabled(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Button slots
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onCheckNowClicked()
{
    m_checker->checkNow();
}

void MainWindow::onBuildClicked()
{
    m_log->appendSection("Starting full build pipeline...");
    setControlsEnabled(false);
    m_abortBtn->setEnabled(true);
    m_progressBar->setValue(0);
    m_builder->startBuild();
}

void MainWindow::onAbortClicked()
{
    m_builder->abort();
    setControlsEnabled(true);
    m_abortBtn->setEnabled(false);
    setStatus("Build aborted.", "#d29922");
}

void MainWindow::onSettingsChanged()
{
    if (m_loadingSettings) return;
    m_builder->setSourceDir(m_sourceDirEdit->text());
    m_builder->setBuildDir(m_buildDirEdit->text());
    m_builder->setInstallDir(m_installDirEdit->text());
    m_builder->setMakeJobs(m_jobsSpin->value());
    m_builder->setRunInstall(m_installChk->isChecked());
    m_checker->setCheckInterval(m_intervalSpin->value());
    m_checker->setCurrentSha(AppInstaller::installedCommit(installedApp()));

    if (m_autoCheckBox->isChecked())
        m_checker->startAutoCheck();
    else
        m_checker->stopAutoCheck();

    // Persist
    QSettings s("ringsce", "KayteIDEUpdater");
    s.setValue("sourceDir",  m_sourceDirEdit->text());
    s.setValue("buildDir",   m_buildDirEdit->text());
    s.setValue("installDir", m_installDirEdit->text());
    s.setValue("jobs",       m_jobsSpin->value());
    s.setValue("interval",   m_intervalSpin->value());
    s.setValue("autoCheck",  m_autoCheckBox->isChecked());
    s.setValue("runInstall", m_installChk->isChecked());
    s.setValue("installedApp", m_appEdit->text());
    s.setValue("autoUpdate", m_autoUpdateChk->isChecked());
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick)
        show();
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setStatus(const QString &text, const QString &color)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(QString("color: %1; font-weight: 600;").arg(color));
}

void MainWindow::setControlsEnabled(bool enabled)
{
    m_checkBtn->setEnabled(enabled);
    m_buildBtn->setEnabled(enabled);
    m_sourceDirEdit->setEnabled(enabled);
    m_buildDirEdit->setEnabled(enabled);
    m_installDirEdit->setEnabled(enabled);
    m_jobsSpin->setEnabled(enabled);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_builder->isRunning()) {
        int r = QMessageBox::question(this, "Build in progress",
            "A build is running. Quit anyway?",
            QMessageBox::Yes | QMessageBox::No);
        if (r == QMessageBox::No) {
            event->ignore();
            return;
        }
        m_builder->abort();
    }
    // Minimize to tray instead of closing
    hide();
    event->ignore();
}
