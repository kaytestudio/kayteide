#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QProcess>

class QJsonObject;

namespace Kayte::Llm {

// Runs Claude (Claude Code CLI) and Codex (OpenAI Codex CLI) as the assistant's
// cloud providers. Each request is a one-shot `claude -p` / `codex exec` run in
// the project folder - read-only, or able to edit files (never run commands)
// when the assistant's "Edit files" mode is on. Sign-in is done by the user with the CLIs
// themselves, so KayteIDE never handles credentials.
//
// Signals use the same (tag, text) shape as OllamaClient so the panel can treat
// every provider alike.
class CliAgentClient : public QObject
{
    Q_OBJECT
public:
    enum class Provider { Claude, Codex };
    Q_ENUM(Provider)

    explicit CliAgentClient(QObject *parent = nullptr);
    ~CliAgentClient() override;

    static QString name(Provider p);
    static QString executable(Provider p);   // empty when the CLI isn't installed

    void checkLogin(Provider p);

    // allowEdits: the agent may change files in workingDir (never run
    // commands); otherwise it only reads.
    void run(const QString &tag, Provider p, const QString &model,
             const QString &prompt, const QString &workingDir, bool allowEdits = false);
    void abort(const QString &tag);
    bool isBusy(const QString &tag) const { return m_jobs.contains(tag); }

signals:
    void loginStatus(Kayte::Llm::CliAgentClient::Provider provider, bool installed,
                     bool loggedIn, const QString &detail);
    void activity(const QString &tag, const QString &what);   // e.g. "Claude is reading files..."
    void token(const QString &tag, const QString &text);
    void finished(const QString &tag, const QString &fullText);
    void failed(const QString &tag, const QString &error);

private:
    struct Job {
        QProcess     *process { nullptr };
        Provider      provider { Provider::Claude };
        QString       model;
        QByteArray    buffer;        // partial stdout line
        QString       text;          // streamed answer so far
        QString       error;         // error reported in the event stream
        QString       lastMessageFile;
        QByteArray    stderrTail;
        double        costUsd { -1 };
        QString       itemId;        // Codex: message currently being streamed
        int           itemStart { 0 };
        QElapsedTimer timer;
    };

    void onReadyRead(const QString &tag);
    void onFinished(const QString &tag, int exitCode, QProcess::ExitStatus status);
    void handleClaudeEvent(const QString &tag, Job &job, const QJsonObject &event);
    void handleCodexEvent(const QString &tag, Job &job, const QJsonObject &event);

    QHash<QString, Job> m_jobs;
};

} // namespace Kayte::Llm
