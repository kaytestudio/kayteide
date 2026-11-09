#pragma once

#include <QObject>
#include <QString>

class QTimer;

// Replaces an installed KayteIDE.app with a freshly built one (macOS):
//   1. asks the running IDE to quit (its normal close, so unsaved files are
//      offered for saving) and waits for it to exit;
//   2. copies the new app next to the installed one, moves the installed
//      app's toolchain (Contents/tools: FPC, Lazarus, QEMU, Kayte SDK) into
//      it, and swaps the two with renames - the old app stays untouched if
//      anything fails before the swap;
//   3. reopens KayteIDE.
class AppInstaller : public QObject
{
    Q_OBJECT

public:
    explicit AppInstaller(QObject *parent = nullptr);

    void install(const QString &newApp, const QString &installedApp);
    bool isBusy() const { return m_busy; }

    // The commit an installed app was built from (Contents/Resources/
    // kayteide-commit.txt), or "" when unknown.
    static QString installedCommit(const QString &app);
    // True while the KayteIDE inside `app` is running.
    static bool isRunning(const QString &app);

signals:
    void logLine(const QString &line, bool isError = false);
    void finished(bool ok, const QString &message);
    // The IDE didn't close (e.g. its save prompt was cancelled).
    void postponed(const QString &message);

private:
    void requestQuit();
    void waitForExit();
    void swapAndRelaunch();
    void relaunch(int attempt);
    void done(bool ok, const QString &message);

    QString m_newApp;
    QString m_installedApp;
    QTimer *m_poll  = nullptr;
    int     m_waited = 0;
    bool    m_busy  = false;
};
