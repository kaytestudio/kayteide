#include "AppInstaller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace {
constexpr int kQuitTimeoutSeconds = 120;   // time to answer a save prompt

QString controlSocketName()
{
    return QStringLiteral("KayteIDE-control-") + QString::fromLocal8Bit(qgetenv("USER"));
}

QString ideExecutable(const QString &app)
{
    return QDir::cleanPath(app) + QStringLiteral("/Contents/MacOS/KayteIDE");
}

// Moves everything in `from` that `to` lacks (recursing into folders both
// have), so freshly built files in `to` win and nothing is copied.
bool moveMissing(const QString &from, const QString &to, QStringList *moved)
{
    QDir src(from);
    for (const QFileInfo &fi : src.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        const QString dest = to + QLatin1Char('/') + fi.fileName();
        const QFileInfo destInfo(dest);
        if (!destInfo.exists() && !destInfo.isSymLink()) {
            if (!QFile::rename(fi.absoluteFilePath(), dest)) return false;
            moved->append(fi.fileName());
        } else if (fi.isDir() && !fi.isSymLink() && destInfo.isDir() && !destInfo.isSymLink()) {
            if (!moveMissing(fi.absoluteFilePath(), dest, moved)) return false;
        }
    }
    return true;
}
} // namespace

AppInstaller::AppInstaller(QObject *parent)
    : QObject(parent)
    , m_poll(new QTimer(this))
{
    m_poll->setInterval(1000);
    connect(m_poll, &QTimer::timeout, this, &AppInstaller::waitForExit);
}

QString AppInstaller::installedCommit(const QString &app)
{
    QFile f(QDir::cleanPath(app) + QStringLiteral("/Contents/Resources/kayteide-commit.txt"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QString sha = QString::fromLatin1(f.readAll()).trimmed();
    return sha == QLatin1String("unknown") ? QString() : sha;
}

bool AppInstaller::isRunning(const QString &app)
{
    QProcess pgrep;
    pgrep.start(QStringLiteral("/usr/bin/pgrep"),
                {QStringLiteral("-f"), QStringLiteral("^") + QRegularExpression::escape(ideExecutable(app))
                                           + QStringLiteral("( |$)")});
    pgrep.waitForFinished(3000);
    return pgrep.exitCode() == 0;
}

void AppInstaller::install(const QString &newApp, const QString &installedApp)
{
    if (m_busy) return;
    m_newApp = QDir::cleanPath(newApp);
    m_installedApp = QDir::cleanPath(installedApp);

    if (!QFileInfo::exists(ideExecutable(m_newApp))) {
        emit finished(false, tr("No built KayteIDE.app at %1").arg(m_newApp));
        return;
    }
    if (!QFileInfo(m_installedApp).absoluteDir().exists()) {
        emit finished(false, tr("The install location %1 doesn't exist.")
                                 .arg(QFileInfo(m_installedApp).absolutePath()));
        return;
    }
    m_busy = true;

    if (!isRunning(m_installedApp)) {
        emit logLine(tr("KayteIDE isn't running."));
        swapAndRelaunch();
        return;
    }
    requestQuit();
}

void AppInstaller::requestQuit()
{
    emit logLine(tr("Asking KayteIDE to close (it may ask to save open files)..."));
    QLocalSocket socket;
    socket.connectToServer(controlSocketName());
    bool asked = false;
    if (socket.waitForConnected(2000)) {
        socket.write("quit\n");
        socket.flush();
        asked = socket.waitForReadyRead(3000) && socket.readLine().trimmed() == "ok";
        socket.disconnectFromServer();
    }
    if (!asked) {
        // An IDE without the control channel (older build): the regular
        // macOS quit request, which also goes through its close handling.
        emit logLine(tr("No control channel; sending a quit request instead."));
        QProcess::execute(QStringLiteral("/usr/bin/osascript"),
                          {QStringLiteral("-e"),
                           QStringLiteral("tell application \"%1\" to quit").arg(m_installedApp)});
    }
    m_waited = 0;
    m_poll->start();
}

void AppInstaller::waitForExit()
{
    if (!isRunning(m_installedApp)) {
        m_poll->stop();
        emit logLine(tr("KayteIDE closed."));
        swapAndRelaunch();
        return;
    }
    if (++m_waited >= kQuitTimeoutSeconds) {
        m_poll->stop();
        m_busy = false;
        emit postponed(tr("KayteIDE is still open (a save prompt may have been cancelled). "
                          "Install postponed - use Install Now when you're ready."));
    }
}

void AppInstaller::swapAndRelaunch()
{
    const QString staging = m_installedApp + QStringLiteral(".new");
    const QString backup  = m_installedApp + QStringLiteral(".old");
    QDir(staging).removeRecursively();
    QDir(backup).removeRecursively();

    // 1. The new app next to the installed one (same volume, so the swap is
    //    two renames).
    emit logLine(tr("Copying the new build to %1").arg(staging));
    QProcess ditto;
    ditto.start(QStringLiteral("/usr/bin/ditto"), {m_newApp, staging});
    if (!ditto.waitForFinished(300000) || ditto.exitCode() != 0) {
        QDir(staging).removeRecursively();
        return done(false, tr("Could not copy the new build: %1")
                               .arg(QString::fromLocal8Bit(ditto.readAllStandardError()).trimmed()));
    }

    // 2. Keep the installed toolchain (moved, not copied: it's ~2.6 GB).
    const QString oldTools = m_installedApp + QStringLiteral("/Contents/tools");
    if (QFileInfo(oldTools).isDir()) {
        QDir().mkpath(staging + QStringLiteral("/Contents/tools"));
        QStringList moved;
        if (!moveMissing(oldTools, staging + QStringLiteral("/Contents/tools"), &moved)) {
            // Put back what was already moved, then give up.
            QStringList back;
            moveMissing(staging + QStringLiteral("/Contents/tools"), oldTools, &back);
            QDir(staging).removeRecursively();
            return done(false, tr("Could not move the toolchain into the new app."));
        }
        if (!moved.isEmpty())
            emit logLine(tr("Kept the installed toolchain (%1).").arg(moved.join(QStringLiteral(", "))));
    }

    // 3. Swap.
    const bool hadOld = QFileInfo::exists(m_installedApp);
    if (hadOld && !QFile::rename(m_installedApp, backup)) {
        QStringList back;
        moveMissing(staging + QStringLiteral("/Contents/tools"), oldTools, &back);
        QDir(staging).removeRecursively();
        return done(false, tr("Could not move the installed app aside (is it in use?)."));
    }
    if (!QFile::rename(staging, m_installedApp)) {
        if (hadOld) QFile::rename(backup, m_installedApp);   // restore
        return done(false, tr("Could not put the new app in place."));
    }
    if (hadOld) QDir(backup).removeRecursively();

    // 4. Reopen.
    emit logLine(tr("Installed %1 - reopening KayteIDE.").arg(m_installedApp));
    relaunch(1);
}

// Right after the old IDE exits, Launch Services may still count it as
// running and just "activate" it, so nothing opens: wait a moment, force a
// new instance, and check that it really started (one retry).
void AppInstaller::relaunch(int attempt)
{
    QTimer::singleShot(1500, this, [this, attempt] {
        QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                {QStringLiteral("-n"), m_installedApp});
        QTimer::singleShot(8000, this, [this, attempt] {
            const QString commit = installedCommit(m_installedApp).left(7);
            const QString version = commit.isEmpty() ? tr("the new build") : commit;
            if (isRunning(m_installedApp))
                return done(true, tr("KayteIDE was updated to %1 and reopened.").arg(version));
            if (attempt < 2) {
                emit logLine(tr("KayteIDE didn't start yet; trying again."));
                return relaunch(attempt + 1);
            }
            done(true, tr("KayteIDE was updated to %1, but didn't reopen - start it yourself.")
                           .arg(version));
        });
    });
}

void AppInstaller::done(bool ok, const QString &message)
{
    m_busy = false;
    emit finished(ok, message);
}
