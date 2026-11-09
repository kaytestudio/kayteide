#include "AssistantPanel.hpp"
#include "LlmLog.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QDateTime>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace Kayte::Llm {

namespace {

const char *kAnswerTag  = "answer";
const char *kSuggestTag = "suggest";

constexpr int kContextChunks   = 5;
constexpr int kHistoryMessages = 10;

// Budgets per request. The local model's memory grows with its context window,
// so it gets a lean prompt that fits Ollama's 4K default; Claude/Codex get more.
struct Budget { int contextChars, openFileChars, historyChars; };
constexpr Budget kLocalBudget { 6000,  3000,  1500 };
constexpr Budget kCloudBudget { 16000, 12000, 6000 };

// Rough token estimate (~3.5 chars per token for code/English) + answer room.
int contextWindowFor(qsizetype promptChars)
{
    const qsizetype tokens = promptChars * 10 / 35 + 1024;
    return tokens <= 4096 ? 4096 : 8192;
}

const char *kSourceScheme = "kayte-src";

QString fence(const QString &language)
{
    static const QHash<QString, QString> tags = {
        { "C", "c" }, { "C++", "cpp" }, { "Kayte", "pascal" },
        { "VB", "vbnet" }, { "BASIC", "basic" }, { "CMake", "cmake" },
    };
    return tags.value(language);
}

QString sourceLink(const QString &relPath, int line)
{
    QUrl u;
    u.setScheme(QString::fromLatin1(kSourceScheme));
    u.setPath(QLatin1Char('/') + relPath);
    u.setQuery(QStringLiteral("line=%1").arg(line));
    return QStringLiteral("[%1:%2](%3)")
        .arg(relPath).arg(line).arg(u.toString(QUrl::FullyEncoded));
}

// "1. How does X work?" / "- **Why** Y" -> plain questions.
QStringList parsePrompts(const QString &text, int max)
{
    static const QRegularExpression lead(QStringLiteral(R"(^\s*(?:[-*•]|\d+[.)])\s*)"));
    QStringList out;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line.remove(lead);
        line.remove(QStringLiteral("**"));
        line = line.trimmed();
        if (line.startsWith(QLatin1Char('"')) && line.endsWith(QLatin1Char('"')))
            line = line.mid(1, line.size() - 2).trimmed();
        if (line.size() < 10 || line.endsWith(QLatin1Char(':'))) continue;
        out << line;
        if (out.size() >= max) break;
    }
    return out;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

AssistantPanel::AssistantPanel(QWidget *parent)
    : QWidget(parent)
{
    m_session = AssistantSession::create(QString(), QString());
    buildUi();

    connect(&m_client, &OllamaClient::serverReady, this, &AssistantPanel::onServerReady);
    connect(&m_client, &OllamaClient::modelsListed, this, [this](const QStringList &models) {
        m_ollamaModels = models;
        rebuildModelList();
        if (models.isEmpty() && providerOf(currentKey()) == QLatin1String("ollama"))
            m_statusLbl->setText(tr("No local models installed - run `ollama pull llama3.2`, "
                                    "or pick Claude / Codex."));
    });
    connect(&m_client, &OllamaClient::token,    this, &AssistantPanel::onToken);
    connect(&m_client, &OllamaClient::finished, this, &AssistantPanel::onFinished);
    connect(&m_client, &OllamaClient::failed,   this, &AssistantPanel::onFailed);
    connect(&m_index,  &CodeIndex::ready,       this, &AssistantPanel::onIndexReady);

    // Claude / Codex (via their CLIs) feed the same slots as Ollama.
    connect(&m_cli, &CliAgentClient::token,       this, &AssistantPanel::onToken);
    connect(&m_cli, &CliAgentClient::finished,    this, &AssistantPanel::onFinished);
    connect(&m_cli, &CliAgentClient::failed,      this, &AssistantPanel::onFailed);
    connect(&m_cli, &CliAgentClient::loginStatus, this, &AssistantPanel::onLoginStatus);
    connect(&m_cli, &CliAgentClient::activity, this, [this](const QString &tag, const QString &what) {
        if (tag == QLatin1String(kAnswerTag)) m_statusLbl->setText(what);
    });

    rebuildModelList();
    m_statusLbl->setText(tr("Connecting to Ollama..."));
    m_client.ensureServer();
    updateSessionLabel();
    render();
}

AssistantPanel::~AssistantPanel()
{
    saveSession();
}

void AssistantPanel::buildUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    // ── Top bar: model | learn | new | history ──────────────────────────────
    auto *top = new QHBoxLayout;
    m_model = new QComboBox(this);
    m_model->setToolTip(tr("Model: local (Ollama), Claude or Codex"));
    m_model->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(m_model, &QComboBox::activated, this, [this] { onModelChanged(true); });

    m_learnBtn = new QToolButton(this);
    m_learnBtn->setText(tr("Learn"));
    m_learnBtn->setToolTip(tr("Read the project's C, C++, Kayte, VB, BASIC and CMake files"));
    connect(m_learnBtn, &QToolButton::clicked, this, &AssistantPanel::learnProject);

    auto *newBtn = new QToolButton(this);
    newBtn->setText(tr("New"));
    newBtn->setToolTip(tr("Start a new session (the current one is saved)"));
    connect(newBtn, &QToolButton::clicked, this, &AssistantPanel::newSession);

    m_historyBtn = new QToolButton(this);
    m_historyBtn->setText(tr("Resume"));
    m_historyBtn->setToolTip(tr("Resume a previous session"));
    m_historyBtn->setPopupMode(QToolButton::InstantPopup);
    m_historyBtn->setMenu(new QMenu(m_historyBtn));
    connect(m_historyBtn->menu(), &QMenu::aboutToShow, this, &AssistantPanel::populateHistoryMenu);

    m_editBtn = new QToolButton(this);
    m_editBtn->setText(tr("Edit files"));
    m_editBtn->setCheckable(true);
    m_editBtn->setToolTip(tr("On: the assistant makes the changes you ask for in the project's files "
                             "(each answer lists them, with Undo). Off: it only answers."));
    m_editMode = QSettings().value(QStringLiteral("assistant/editFiles"), true).toBool();
    m_editBtn->setChecked(m_editMode);
    connect(m_editBtn, &QToolButton::toggled, this, &AssistantPanel::setEditsFiles);

    top->addWidget(m_model, 1);
    top->addWidget(m_editBtn);
    top->addWidget(m_learnBtn);
    top->addWidget(newBtn);
    top->addWidget(m_historyBtn);
    lay->addLayout(top);

    // ── Session hash + status ───────────────────────────────────────────────
    m_sessionLbl = new QLabel(this);
    m_sessionLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_sessionLbl->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sessionLbl, &QLabel::customContextMenuRequested, this, [this](const QPoint &p) {
        QMenu menu;
        menu.addAction(tr("Copy session hash"), this, [this] {
            QApplication::clipboard()->setText(m_session.id);
        });
        menu.addAction(tr("Copy resume command"), this, [this] {
            QApplication::clipboard()->setText(QStringLiteral("KayteIDE --resume ") + m_session.id);
        });
        menu.exec(m_sessionLbl->mapToGlobal(p));
    });

    m_statusLbl = new QLabel(this);
    m_statusLbl->setWordWrap(true);
    QFont small = m_statusLbl->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    m_statusLbl->setFont(small);
    m_sessionLbl->setFont(small);

    lay->addWidget(m_sessionLbl);
    lay->addWidget(m_statusLbl);

    // ── Transcript ──────────────────────────────────────────────────────────
    m_transcript = new QTextBrowser(this);
    m_transcript->setOpenLinks(false);
    connect(m_transcript, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == QLatin1String("kayte-undo")) {
            undoChanges(url.path().toInt());
        } else if (url.scheme() == QLatin1String(kSourceScheme)) {
            const QString rel  = url.path().mid(1);
            const int     line = QUrlQuery(url).queryItemValue(QStringLiteral("line")).toInt();
            emit openFileRequested(QDir(m_index.root()).absoluteFilePath(rel), line);
        } else {
            QDesktopServices::openUrl(url);
        }
    });
    lay->addWidget(m_transcript, 1);

    // ── Suggested prompts ───────────────────────────────────────────────────
    m_suggestBox = new QWidget(this);
    m_suggestLay = new QVBoxLayout(m_suggestBox);
    m_suggestLay->setContentsMargins(0, 0, 0, 0);
    m_suggestLay->setSpacing(3);
    m_suggestBox->hide();
    lay->addWidget(m_suggestBox);

    // ── Input ───────────────────────────────────────────────────────────────
    m_input = new QPlainTextEdit(this);
    m_input->setPlaceholderText(tr("Ask about the code...  (Enter to send, Shift+Enter for a new line)"));
    m_input->setFixedHeight(72);
    m_input->installEventFilter(this);

    m_sendBtn = new QPushButton(tr("Send"), this);
    connect(m_sendBtn, &QPushButton::clicked, this, &AssistantPanel::sendOrStop);

    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input, 1);
    auto *attachBtn = new QPushButton(tr("Attach..."), this);
    attachBtn->setToolTip(tr("Attach text or source files to the next message"));
    connect(attachBtn, &QPushButton::clicked, this, &AssistantPanel::attachFiles);
    inputRow->addWidget(attachBtn, 0, Qt::AlignBottom);
    inputRow->addWidget(m_sendBtn, 0, Qt::AlignBottom);
    lay->addLayout(inputRow);

    m_attachmentLabel = new QLabel(this);
    m_attachmentLabel->setWordWrap(true);
    m_attachmentLabel->hide();
    lay->addWidget(m_attachmentLabel);

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(60);
    connect(m_renderTimer, &QTimer::timeout, this, &AssistantPanel::render);
}

bool AssistantPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            !(ke->modifiers() & Qt::ShiftModifier)) {
            if (!m_busy) sendOrStop();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ─────────────────────────────────────────────────────────────────────────────
// Server / model
// ─────────────────────────────────────────────────────────────────────────────

void AssistantPanel::onServerReady(bool ok, const QString &error)
{
    m_serverOk    = ok;
    m_ollamaError = error;
    LlmLog::write(QStringLiteral("Ollama"), ok ? QStringLiteral("server ready")
                                               : QStringLiteral("unavailable: ") + error);
    if (ok) m_client.listModels();
    if (providerOf(currentKey()) == QLatin1String("ollama"))
        showOllamaStatus();
}

void AssistantPanel::showOllamaStatus()
{
    if (!m_serverOk) {
        m_statusLbl->setText(QStringLiteral("<span style='color:#e06c75'>%1</span> "
                                            "<a href='retry'>%2</a>")
                                 .arg(m_ollamaError.isEmpty() ? tr("Ollama is not running.")
                                                              : m_ollamaError.toHtmlEscaped(),
                                      tr("Retry")));
        m_statusLbl->setTextFormat(Qt::RichText);
        disconnect(m_statusLbl, &QLabel::linkActivated, nullptr, nullptr);
        connect(m_statusLbl, &QLabel::linkActivated, this, [this] {
            m_statusLbl->setText(tr("Connecting to Ollama..."));
            m_client.ensureServer();
        });
        return;
    }
    m_statusLbl->setTextFormat(Qt::PlainText);
    m_statusLbl->setText(m_index.isReady()
        ? tr("Learned %1 files (%2)").arg(m_index.fileCount()).arg(m_index.summary())
        : tr("Connected. Open a project or press Learn to read its code."));
}

QString AssistantPanel::currentKey() const
{
    return m_model->currentData().toString();
}

QString AssistantPanel::currentLabel() const
{
    return m_model->currentText();
}

QString AssistantPanel::providerOf(const QString &key)
{
    const int slash = key.indexOf(QLatin1Char('/'));
    return slash < 0 ? QStringLiteral("ollama") : key.left(slash);   // old sessions: bare Ollama name
}

QString AssistantPanel::modelOf(const QString &key)
{
    const int slash = key.indexOf(QLatin1Char('/'));
    return slash < 0 ? key : key.mid(slash + 1);
}

CliAgentClient::Provider AssistantPanel::cliProvider(const QString &key)
{
    return providerOf(key) == QLatin1String("codex") ? CliAgentClient::Provider::Codex
                                                    : CliAgentClient::Provider::Claude;
}

bool AssistantPanel::providerReady() const
{
    const QString key = currentKey();
    if (key.isEmpty()) return false;
    if (providerOf(key) == QLatin1String("ollama"))
        return m_serverOk && !modelOf(key).isEmpty();
    return m_cliSignedIn.value(int(cliProvider(key)));
}

void AssistantPanel::rebuildModelList()
{
    // Keep the user's (or resumed session's) choice; otherwise prefer local
    // llama3.2 once Ollama has listed its models.
    QString wanted = m_modelChosen ? m_session.model : QString();
    if (!wanted.isEmpty() && !wanted.contains(QLatin1Char('/')))
        wanted.prepend(QStringLiteral("ollama/"));

    QSignalBlocker block(m_model);
    m_model->clear();
    for (const QString &m : std::as_const(m_ollamaModels))
        m_model->addItem(tr("%1  (local)").arg(m), QStringLiteral("ollama/") + m);
    if (!m_ollamaModels.isEmpty()) m_model->insertSeparator(m_model->count());
    m_model->addItem(tr("Claude - Opus 5.5"),   QStringLiteral("claude/claude-opus-5-5"));
    m_model->addItem(tr("Claude - Sonnet 5.5"), QStringLiteral("claude/claude-sonnet-5-5"));
    m_model->addItem(tr("Claude - Haiku 4.5"),  QStringLiteral("claude/claude-haiku-4-5"));
    m_model->addItem(tr("Codex - default model"), QStringLiteral("codex/"));

    int idx = m_model->findData(wanted);
    if (idx < 0) idx = m_model->findData(QStringLiteral("ollama/llama3.2:latest"));
    if (idx < 0 && !m_ollamaModels.isEmpty()) idx = 0;
    if (idx < 0) idx = m_model->findData(QStringLiteral("claude/claude-opus-5-5"));
    m_model->setCurrentIndex(idx);
    onModelChanged(false);
}

void AssistantPanel::onModelChanged(bool userInitiated)
{
    const QString key = currentKey();
    if (key.isEmpty()) return;
    // Switching away from a local model frees its RAM right away instead of
    // waiting for Ollama's keep-alive to expire.
    if (providerOf(m_activeKey) == QLatin1String("ollama") && m_activeKey != key)
        m_client.unload(modelOf(m_activeKey));
    m_activeKey = key;
    m_session.model = key;
    if (userInitiated) m_modelChosen = true;
    if (providerOf(key) == QLatin1String("ollama")) {
        showOllamaStatus();
        return;
    }
    const auto p = cliProvider(key);
    if (userInitiated) LlmLog::write(QStringLiteral("panel"), QStringLiteral("selected %1").arg(currentLabel()));
    m_statusLbl->setText(tr("Checking %1 sign-in...").arg(CliAgentClient::name(p)));
    m_cli.checkLogin(p);
}

void AssistantPanel::onLoginStatus(CliAgentClient::Provider p, bool installed,
                                   bool loggedIn, const QString &detail)
{
    const bool wasSignedIn = m_cliSignedIn.value(int(p));
    m_cliSignedIn[int(p)] = loggedIn;
    const bool current = providerOf(currentKey()) != QLatin1String("ollama") &&
                         cliProvider(currentKey()) == p;

    if (loggedIn) {
        if (current) {
            m_statusLbl->setTextFormat(Qt::PlainText);
            m_statusLbl->setText(tr("%1 signed in: %2").arg(CliAgentClient::name(p), detail));
            if (!wasSignedIn && m_index.isReady() && m_session.messages.isEmpty())
                requestSuggestions(false);
        }
        return;
    }

    if (!current) return;
    m_statusLbl->setTextFormat(Qt::PlainText);
    m_statusLbl->setText(notReadyText(p, installed));
}

QString AssistantPanel::notReadyText(CliAgentClient::Provider p, bool installed) const
{
    return installed
        ? tr("%1 is not signed in. Sign in with its CLI in a terminal, then pick it again.")
              .arg(CliAgentClient::name(p))
        : tr("%1 CLI is not installed.").arg(CliAgentClient::name(p));
}

void AssistantPanel::abortTag(const QString &tag)
{
    m_client.abort(tag);
    m_cli.abort(tag);
}

// ─────────────────────────────────────────────────────────────────────────────
// Learning the project
// ─────────────────────────────────────────────────────────────────────────────

void AssistantPanel::setProjectRoot(const QString &root)
{
    if (root.isEmpty()) return;
    const QString abs = QDir(root).absolutePath();
    // Never crawl the whole home directory or filesystem by accident.
    if (abs == QDir::homePath() || abs == QDir::rootPath()) return;
    if (abs == m_index.root() && m_index.isReady()) return;

    if (m_session.messages.isEmpty())
        m_session.projectRoot = abs;

    if (m_index.isBuilding()) { m_pendingRoot = abs; return; }
    m_learnBtn->setEnabled(false);
    m_statusLbl->setText(tr("Learning %1...").arg(QDir(abs).dirName()));
    m_index.build(abs);
}

void AssistantPanel::learnProject()
{
    QString root = m_session.projectRoot.isEmpty() ? m_index.root() : m_session.projectRoot;
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        root = QFileDialog::getExistingDirectory(this, tr("Choose a project to learn"));
        if (root.isEmpty()) return;
    }
    // Explicit Learn always re-reads (files may have changed).
    m_session.projectRoot = QDir(root).absolutePath();
    if (m_index.isBuilding()) return;
    m_learnBtn->setEnabled(false);
    m_statusLbl->setText(tr("Learning %1...").arg(QDir(root).dirName()));
    m_index.build(m_session.projectRoot);
}

void AssistantPanel::onIndexReady()
{
    m_learnBtn->setEnabled(true);
    if (!m_pendingRoot.isEmpty()) {
        const QString next = std::exchange(m_pendingRoot, QString());
        if (next != m_index.root()) { setProjectRoot(next); return; }
    }

    const QString msg = m_index.fileCount() == 0
        ? tr("No C, C++, Kayte, VB, BASIC or CMake files found in %1.").arg(m_index.root())
        : tr("Learned %1 files, %2 chunks (%3)")
              .arg(m_index.fileCount()).arg(m_index.chunkCount()).arg(m_index.summary());
    m_statusLbl->setText(msg);
    emit statusMessage(msg);

    // Fresh session: let the LLM propose prompts for learning this codebase.
    if (m_index.isReady() && m_session.messages.isEmpty() && providerReady())
        requestSuggestions(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Asking
// ─────────────────────────────────────────────────────────────────────────────

QString AssistantPanel::systemPrompt() const
{
    QString p = QStringLiteral(
        "You are Kayte Assistant, a local coding assistant embedded in KayteIDE. "
        "You help the developer understand and work on their project, which may use "
        "C, C++, Kayte (a Pascal-like language), Visual Basic, BASIC and CMake. "
        "Base answers on the project code you are given; when you refer to it, cite "
        "files as path:line. If the context does not contain the answer, say so "
        "instead of guessing. Be concise and use Markdown code blocks.");
    if (m_index.isReady())
        p += QStringLiteral("\n\nProject root: %1\nLanguages: %2")
                 .arg(m_index.root(), m_index.summary());
    if (!m_editMode) return p;

    // Earlier answers (edit mode off, or an older KayteIDE) may say the files
    // are read-only; the history is part of the prompt, so overrule them.
    p += QStringLiteral(
        "\n\nYou can now change the project's files. If earlier messages in this conversation "
        "say the workspace is read-only or that you cannot edit files, that is out of date: "
        "do not repeat it, make the requested changes.");

    if (providerOf(currentKey()) == QLatin1String("ollama")) {
        // Local models write the changes as search/replace blocks, which
        // KayteIDE applies (FileEdits.hpp).
        p += QStringLiteral(
            "\n\nWhen the user asks for a change to the code, make it: answer with a short "
            "explanation, then one block per change in exactly this format:\n\n"
            "path/to/file.ext\n"
            "<<<<<<< SEARCH\n"
            "the exact existing lines to change, copied from the file\n"
            "=======\n"
            "the new lines\n"
            ">>>>>>> REPLACE\n\n"
            "Rules: put the file path (relative to the project root) on the line before "
            "<<<<<<< SEARCH. SEARCH must match the file exactly and only once, so include a few "
            "unchanged lines around the change for context, and only use text you were shown. "
            "Keep each block small; use several blocks for several places. To create a new file, "
            "leave SEARCH empty and put the whole file in the replacement. For a short file you may "
            "instead give the complete new file: its path on one line, then the whole file in a "
            "``` code block. Do not use these blocks when the user only asks a question.");
    } else {
        p += QStringLiteral(
            "\n\nWhen the user asks for a change to the code, make it directly in the project's "
            "files with your edit tools. Keep changes minimal and "
            "focused, then briefly say what you changed. Do not edit files when the user only "
            "asks a question.");
    }
    return p;
}

void AssistantPanel::setEditsFiles(bool on)
{
    m_editMode = on;
    QSettings().setValue(QStringLiteral("assistant/editFiles"), on);
    if (m_editBtn && m_editBtn->isChecked() != on) m_editBtn->setChecked(on);
    emit statusMessage(on ? tr("Kayte Assistant will make the changes you ask for in your files.")
                          : tr("Kayte Assistant will only answer - it won't change files."));
}

QString AssistantPanel::projectRootForEdits() const
{
    return m_index.isReady() ? m_index.root() : m_session.projectRoot;
}

// Applies (Ollama) or collects (Claude / Codex) the changes behind an answer
// and returns the summary to show under it.
QString AssistantPanel::applyAnswerEdits(const QString &answer)
{
    const QString root = projectRootForEdits();
    if (root.isEmpty()) return {};
    QList<FileChange> changes;
    QStringList errors;
    if (providerOf(currentKey()) == QLatin1String("ollama")) {
        QString openRel;
        if (m_currentFile) {
            const QString open = m_currentFile().first;
            if (!open.isEmpty() && open.startsWith(root + QLatin1Char('/'))) openRel = QDir(root).relativeFilePath(open);
        }
        const QList<FileEdit> edits = parseEdits(answer, openRel);
        if (edits.isEmpty()) {
            // Code in the answer but no block KayteIDE could apply: say so,
            // rather than leaving the user to wonder why nothing was written.
            if (!answer.contains(QLatin1String("```"))) return {};
            LlmLog::write(QStringLiteral("panel"), QStringLiteral("edit mode: answer had code but no applicable edit blocks"));
            return tr("*No file was changed: the answer has no SEARCH/REPLACE block (or file path + whole file) "
                      "that could be applied. Ask again, e.g. \"apply this to <file>\", or use a larger model.*");
        }
        const EditPlan plan = planEdits(edits, root, m_textProvider ? m_textProvider : TextProvider(
            [](const QString &abs) -> std::optional<QString> {
                QFile f(abs);
                if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
                return QString::fromUtf8(f.readAll());
            }));
        errors = plan.errors;
        changes = applyChanges(plan.changes, &errors);   // the ones written, so they can be undone
    } else {
        if (m_snapshot.isEmpty()) return {};
        changes = m_snapshot.changesSince();
        m_snapshot = ProjectSnapshot {};
    }

    LlmLog::write(QStringLiteral("panel"), QStringLiteral("edit mode: %1 file(s) changed, %2 not applied")
                                               .arg(changes.size()).arg(errors.size()));
    QString md;
    if (!changes.isEmpty()) {
        m_changeSets << changes;
        md = changeSummary(changes, root, QStringLiteral("kayte-undo:%1").arg(m_changeSets.size() - 1));
        QStringList paths;
        for (const FileChange &c : std::as_const(changes)) paths << c.path;
        emit filesChanged(paths);
        emit statusMessage(tr("Kayte Assistant changed %n file(s).", nullptr, int(changes.size())));
    }
    if (!errors.isEmpty())
        md += (md.isEmpty() ? QString() : QStringLiteral("\n\n")) +
              tr("**Not applied:**\n\n- %1").arg(errors.join(QStringLiteral("\n- ")));
    return md;
}

QList<FileChange> AssistantPanel::applyChanges(const QList<FileChange> &changes, QStringList *errors)
{
    const FileWriter write = m_writer ? m_writer : FileWriter(&writeFileDirect);
    QList<FileChange> written;
    for (const FileChange &c : changes) {
        QString err;
        const std::optional<QString> text = c.after ? std::optional<QString>(QString::fromUtf8(*c.after)) : std::nullopt;
        if (write(c.path, text, &err)) written << c;
        else *errors << tr("%1: %2").arg(QDir(projectRootForEdits()).relativeFilePath(c.path), err);
    }
    return written;
}

// Puts files back as they were before an answer changed them. A file edited
// again since is left alone (and named), so later work is never lost.
void AssistantPanel::undoChanges(int set)
{
    if (set < 0 || set >= m_changeSets.size()) {
        emit statusMessage(tr("Those changes can no longer be undone here (the session was reopened)."));
        return;
    }
    if (m_undone.contains(set)) {
        emit statusMessage(tr("Those changes were already undone."));
        return;
    }
    const QString root = projectRootForEdits();
    const FileWriter write = m_writer ? m_writer : FileWriter(&writeFileDirect);
    QStringList restored, skipped, paths;
    for (const FileChange &c : std::as_const(m_changeSets[set])) {
        const QString rel = QDir(root).relativeFilePath(c.path);
        if (!c.undoable) { skipped << tr("%1 (not kept)").arg(rel); continue; }
        std::optional<QString> now;
        if (m_textProvider) now = m_textProvider(c.path);
        else if (QFile f(c.path); f.open(QIODevice::ReadOnly)) now = QString::fromUtf8(f.readAll());
        const std::optional<QString> after = c.after ? std::optional<QString>(QString::fromUtf8(*c.after)) : std::nullopt;
        if (now != after) { skipped << tr("%1 (edited since)").arg(rel); continue; }
        QString err;
        if (write(c.path, c.before ? std::optional<QString>(QString::fromUtf8(*c.before)) : std::nullopt, &err)) {
            restored << rel;
            paths << c.path;
        } else {
            skipped << tr("%1 (%2)").arg(rel, err);
        }
    }
    m_undone.insert(set);
    QString note = tr("*Undid the changes to %1.*").arg(restored.isEmpty() ? tr("no files") : restored.join(QStringLiteral(", ")));
    if (!skipped.isEmpty()) note += tr(" *Left as they are: %1.*").arg(skipped.join(QStringLiteral(", ")));
    m_session.messages.append({ QStringLiteral("assistant"), note, QDateTime::currentDateTime() });
    render();
    saveSession();
    if (!paths.isEmpty()) emit filesChanged(paths);
}

void AssistantPanel::sendOrStop()
{
    if (m_busy) {
        abortTag(QString::fromLatin1(kAnswerTag));
        finishAnswer(m_streaming, tr("*(stopped)*"));
        return;
    }
    const QString prompt = m_input->toPlainText().trimmed();
    if (prompt.isEmpty()) return;
    m_input->clear();
    ask(prompt);
}

void AssistantPanel::attachFiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Attach files to the next message"), QString(),
        tr("Text and source files (*);;All files (*)"));
    for (const QString &path : paths) {
        const QString abs = QFileInfo(path).absoluteFilePath();
        if (!m_attachments.contains(abs)) m_attachments << abs;
    }
    m_attachmentLabel->setText(m_attachments.isEmpty()
        ? QString()
        : tr("Attached for next message: %1").arg(m_attachments.join(QStringLiteral(", "))));
    m_attachmentLabel->setVisible(!m_attachments.isEmpty());
}

void AssistantPanel::ask(const QString &prompt)
{
    if (m_busy || prompt.trimmed().isEmpty()) return;
    if (!providerReady()) {
        if (providerOf(currentKey()) == QLatin1String("ollama")) {
            m_statusLbl->setText(tr("Ollama is not ready yet."));
        } else {
            const auto p = cliProvider(currentKey());
            m_input->setPlainText(prompt);   // keep the question for after sign-in
            m_statusLbl->setTextFormat(Qt::PlainText);
            m_statusLbl->setText(notReadyText(p, !CliAgentClient::executable(p).isEmpty()));
        }
        return;
    }

    // ── Context: open file + best-matching project chunks ───────────────────
    QString context;
    QStringList sources;
    QString openPath, openText;
    if (m_currentFile) std::tie(openPath, openText) = m_currentFile();

    QString openRel;
    if (!openPath.isEmpty() && m_index.isReady())
        openRel = QDir(m_index.root()).relativeFilePath(openPath);

    const Budget budget = providerOf(currentKey()) == QLatin1String("ollama") ? kLocalBudget
                                                                              : kCloudBudget;
    for (const QString &path : std::as_const(m_attachments)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            context += tr("Attached file %1 could not be read.\n\n").arg(path);
            continue;
        }
        const QByteArray bytes = file.read(budget.openFileChars + 1);
        const QString text = QString::fromUtf8(bytes);
        if (text.contains(QChar::ReplacementCharacter)) {
            context += tr("Attached file %1 is binary and was not included.\n\n").arg(path);
            continue;
        }
        const QString name = QFileInfo(path).fileName();
        context += QStringLiteral("Attached file: %1\n````\n%2%3\n````\n\n")
            .arg(name, text.left(budget.openFileChars),
                 bytes.size() > budget.openFileChars ? QStringLiteral("\n... (truncated)") : QString());
    }
    if (!openPath.isEmpty() && !openText.isEmpty()) {
        const QString lang = CodeIndex::languageFor(QFileInfo(openPath).fileName());
        context += QStringLiteral("Currently open file: %1\n```%2\n%3%4\n```\n\n")
            .arg(openRel.isEmpty() ? openPath : openRel, fence(lang),
                 openText.left(budget.openFileChars),
                 openText.size() > budget.openFileChars ? QStringLiteral("\n... (truncated)") : QString());
    }

    const auto chunks = m_index.search(prompt + QLatin1Char(' ') + QFileInfo(openPath).baseName(),
                                       kContextChunks + 2);
    int used = 0;
    for (const CodeChunk &c : chunks) {
        if (c.file == openRel) continue;   // already included in full
        if (used >= kContextChunks || context.size() + c.text.size() > budget.contextChars) break;
        context += QStringLiteral("%1 (lines %2-%3):\n```%4\n%5\n```\n\n")
            .arg(c.file).arg(c.startLine).arg(c.endLine).arg(fence(c.language), c.text);
        sources << sourceLink(c.file, c.startLine);
        ++used;
    }
    m_lastSources = sources.isEmpty()
        ? QString()
        : tr("\n\n<small>Context: %1</small>").arg(sources.join(QStringLiteral(" | ")));

    // ── Messages: system + recent history + this question with context ──────
    QJsonArray msgs;
    msgs.append(QJsonObject { { "role", "system" }, { "content", systemPrompt() } });
    const int from = qMax(0, int(m_session.messages.size()) - kHistoryMessages);
    auto historyText = [&](int i) {
        const QString &c = m_session.messages[i].content;
        return c.size() <= budget.historyChars ? c
                                               : c.left(budget.historyChars) + QStringLiteral(" ...");
    };
    for (int i = from; i < m_session.messages.size(); ++i)
        msgs.append(QJsonObject { { "role",    m_session.messages[i].role },
                                  { "content", historyText(i) } });
    msgs.append(QJsonObject {
        { "role", "user" },
        { "content", context.isEmpty()
              ? prompt
              : QStringLiteral("Project context:\n\n%1Question: %2").arg(context, prompt) },
    });

    // CLI providers take one prompt: fold system + history + question into it.
    QString flat = systemPrompt() + QStringLiteral("\n\n");
    for (int i = from; i < m_session.messages.size(); ++i)
        flat += (m_session.messages[i].role == QLatin1String("user") ? QStringLiteral("### User\n")
                                                                     : QStringLiteral("### Assistant\n"))
              + historyText(i) + QStringLiteral("\n\n");
    flat += QStringLiteral("### User\n") + msgs.last().toObject().value(QStringLiteral("content")).toString();

    const QString key = currentKey();
    m_session.messages.append({ QStringLiteral("user"), prompt, QDateTime::currentDateTime() });
    m_attachments.clear();
    m_attachmentLabel->clear();
    m_attachmentLabel->hide();
    m_session.model = key;
    m_streaming.clear();
    setSuggestions({});
    abortTag(QString::fromLatin1(kSuggestTag));
    setBusy(true);
    updateSessionLabel();
    render();

    m_askStarted = QDateTime::currentMSecsSinceEpoch();
    LlmLog::write(QStringLiteral("panel"), QStringLiteral("ask via %1, session %2, %3 chars, edit files %4")
                                               .arg(currentLabel(), m_session.id).arg(prompt.size())
                                               .arg(m_editMode ? QStringLiteral("on") : QStringLiteral("off")));
    if (providerOf(key) == QLatin1String("ollama")) {
        qsizetype chars = 0;
        for (const auto &m : std::as_const(msgs))
            chars += m.toObject().value(QStringLiteral("content")).toString().size();
        m_ollamaCtx = contextWindowFor(chars);
        m_client.chat(QString::fromLatin1(kAnswerTag), modelOf(key), msgs, -1, m_ollamaCtx);
    } else {
        const QString root = m_index.isReady() ? m_index.root() : m_session.projectRoot;
        if (m_editMode && !root.isEmpty()) m_snapshot.take(root);   // to report (and undo) its edits
        m_cli.run(QString::fromLatin1(kAnswerTag), cliProvider(key), modelOf(key), flat, root, m_editMode);
    }
}

void AssistantPanel::onToken(const QString &tag, const QString &text)
{
    if (tag != QLatin1String(kAnswerTag)) return;
    m_streaming += text;
    scheduleRender();
}

void AssistantPanel::onFinished(const QString &tag, const QString &text)
{
    if (tag == QLatin1String(kAnswerTag)) {
        if (m_busy) finishAnswer(text);
    } else if (tag == QLatin1String(kSuggestTag)) {
        m_session.suggestions = parsePrompts(text, 5);
        setSuggestions(m_session.suggestions);
        saveSession();
    }
}

void AssistantPanel::onFailed(const QString &tag, const QString &error)
{
    LlmLog::write(QStringLiteral("panel"), QStringLiteral("%1 failed via %2: %3")
                                               .arg(tag, currentLabel(), error));
    if (tag == QLatin1String(kAnswerTag) && m_busy) {
        finishAnswer(m_streaming, tr("*Error: %1*").arg(error));
    } else {
        m_statusLbl->setText(tr("Could not get suggestions: %1").arg(error));
    }
}

void AssistantPanel::finishAnswer(const QString &text, const QString &note)
{
    QString content = text.trimmed();
    if (!note.isEmpty()) content += QStringLiteral("\n\n") + note;
    // Edit mode: a finished answer's changes are applied (local models) or
    // collected (Claude / Codex already made them) - stopped / failed ones
    // still report what an agent changed so it can be undone.
    if (m_editMode && (note.isEmpty() || providerOf(currentKey()) != QLatin1String("ollama"))) {
        const QString edits = applyAnswerEdits(text);
        if (!edits.isEmpty()) content += QStringLiteral("\n\n") + edits;
    }
    content += m_lastSources;

    m_session.messages.append({ QStringLiteral("assistant"), content,
                                QDateTime::currentDateTime() });
    LlmLog::write(QStringLiteral("panel"), QStringLiteral("answer via %1: %2 chars in %3s%4")
        .arg(currentLabel()).arg(text.size())
        .arg((QDateTime::currentMSecsSinceEpoch() - m_askStarted) / 1000.0, 0, 'f', 1)
        .arg(note.isEmpty() ? QString() : QStringLiteral(" (") + note + QLatin1Char(')')));
    m_session.updated = QDateTime::currentDateTime();
    m_streaming.clear();
    setBusy(false);
    render();
    saveSession();

    if (note.isEmpty()) requestSuggestions(true);
}

void AssistantPanel::setBusy(bool busy)
{
    m_busy = busy;
    m_sendBtn->setText(busy ? tr("Stop") : tr("Send"));
    m_model->setEnabled(!busy);
    if (busy) {
        m_statusLbl->setTextFormat(Qt::PlainText);
        m_statusLbl->setText(tr("Thinking with %1...").arg(currentLabel()));
    }
    else if (m_index.isReady())
        m_statusLbl->setText(tr("Learned %1 files (%2)")
                                 .arg(m_index.fileCount()).arg(m_index.summary()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Prompts the LLM proposes for learning
// ─────────────────────────────────────────────────────────────────────────────

void AssistantPanel::requestSuggestions(bool followUp)
{
    if (!providerReady()) return;

    QString request;
    if (followUp && m_session.messages.size() >= 2) {
        const auto &q = m_session.messages.at(m_session.messages.size() - 2);
        const auto &a = m_session.messages.last();
        request = QStringLiteral(
            "The developer asked:\n%1\n\nThe answer was:\n%2\n\n"
            "Suggest 3 follow-up questions the developer could ask next to understand "
            "this codebase more deeply. One question per line, no numbering, "
            "at most 15 words each. Output only the questions.")
            .arg(q.content, a.content.left(3000));
    } else {
        if (!m_index.isReady()) return;
        QString excerpts;
        // Build files and entry points say the most about a project.
        for (const QString &f : m_index.files()) {
            const QString name = QFileInfo(f).fileName().toLower();
            if (name != QLatin1String("cmakelists.txt") && !name.startsWith(QLatin1String("main")))
                continue;
            const auto chunks = m_index.chunksForFile(f);
            if (!chunks.isEmpty())
                excerpts += QStringLiteral("%1:\n%2\n\n")
                    .arg(f, chunks.first().text.section(QLatin1Char('\n'), 0, 30));
            if (excerpts.size() > 6000) break;
        }
        request = QStringLiteral(
            "Project languages: %1\n\nFiles:\n%2\n\nExcerpts:\n%3"
            "Write 5 questions a developer new to this codebase should ask to learn how "
            "it works. Make them specific to these files. One question per line, no "
            "numbering, at most 15 words each. Output only the questions.")
            .arg(m_index.summary(), m_index.files().mid(0, 80).join(QLatin1Char('\n')),
                 excerpts);
    }

    const QString system = QStringLiteral("You write short, concrete study prompts about source code.");
    const QString key = currentKey();
    if (providerOf(key) == QLatin1String("ollama")) {
        QJsonArray msgs;
        msgs.append(QJsonObject { { "role", "system" }, { "content", system } });
        msgs.append(QJsonObject { { "role", "user" }, { "content", request } });
        // Same window as the last answer, so Ollama doesn't reload the model.
        m_client.chat(QString::fromLatin1(kSuggestTag), modelOf(key), msgs, 200, m_ollamaCtx);
    } else {
        // Prompt ideas don't need the big model: use Haiku for Claude.
        const auto p = cliProvider(key);
        m_cli.run(QString::fromLatin1(kSuggestTag), p,
                  p == CliAgentClient::Provider::Claude ? QStringLiteral("claude-haiku-4-5")
                                                        : modelOf(key),
                  system + QStringLiteral("\n\n") + request, m_index.root());
    }
}

void AssistantPanel::setSuggestions(const QStringList &prompts)
{
    while (QLayoutItem *item = m_suggestLay->takeAt(0)) {
        if (QWidget *w = item->widget()) w->deleteLater();   // may be the clicked button
        delete item;
    }
    if (prompts.isEmpty()) { m_suggestBox->hide(); return; }

    auto *title = new QLabel(tr("<b>Prompts to learn from:</b>"), m_suggestBox);
    m_suggestLay->addWidget(title);
    for (const QString &p : prompts) {
        auto *btn = new QPushButton(p, m_suggestBox);
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral("QPushButton { text-align: left; padding: 3px 6px; }"
                                          "QPushButton:hover { text-decoration: underline; }"));
        connect(btn, &QPushButton::clicked, this, [this, p] { ask(p); });
        m_suggestLay->addWidget(btn);
    }
    m_suggestBox->show();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sessions
// ─────────────────────────────────────────────────────────────────────────────

void AssistantPanel::saveSession()
{
    if (m_session.messages.isEmpty()) return;
    m_session.updated = QDateTime::currentDateTime();
    m_session.save();
}

void AssistantPanel::newSession()
{
    if (m_busy) sendOrStop();
    saveSession();
    m_session = AssistantSession::create(m_index.root(), currentKey());
    setSuggestions({});
    updateSessionLabel();
    render();
    if (m_index.isReady()) requestSuggestions(false);
}

bool AssistantPanel::resumeSession(const QString &idOrPrefix, QString *error)
{
    AssistantSession loaded;
    if (!AssistantSession::load(idOrPrefix, &loaded, error)) return false;

    if (m_busy) sendOrStop();
    saveSession();
    m_session = loaded;

    m_modelChosen = true;
    rebuildModelList();   // selects the session's model (and checks its sign-in)
    setSuggestions(m_session.suggestions);
    updateSessionLabel();
    render();

    if (!m_session.projectRoot.isEmpty() && QFileInfo(m_session.projectRoot).isDir() &&
        m_session.projectRoot != m_index.root()) {
        if (m_index.isBuilding()) m_pendingRoot = m_session.projectRoot;
        else {
            m_learnBtn->setEnabled(false);
            m_statusLbl->setText(tr("Learning %1...").arg(QDir(m_session.projectRoot).dirName()));
            m_index.build(m_session.projectRoot);
        }
    }
    emit statusMessage(tr("Resumed assistant session %1").arg(m_session.id));
    return true;
}

void AssistantPanel::populateHistoryMenu()
{
    QMenu *menu = m_historyBtn->menu();
    menu->clear();
    menu->addAction(tr("Open llm.log"), this, [] {
        LlmLog::write(QStringLiteral("panel"), QStringLiteral("log opened"));
        QDesktopServices::openUrl(QUrl::fromLocalFile(LlmLog::path()));
    });
    menu->addSeparator();
    const auto sessions = AssistantSession::recent();
    if (sessions.isEmpty()) {
        menu->addAction(tr("No saved sessions"))->setEnabled(false);
        return;
    }
    for (const AssistantSession &s : sessions) {
        const QString label = QStringLiteral("%1  %2  -  %3")
            .arg(s.id, s.updated.toString(QStringLiteral("dd MMM HH:mm")), s.title());
        QAction *a = menu->addAction(label, this, [this, id = s.id] {
            QString err;
            if (!resumeSession(id, &err)) m_statusLbl->setText(err);
        });
        a->setEnabled(s.id != m_session.id);
    }
}

void AssistantPanel::updateSessionLabel()
{
    m_sessionLbl->setText(tr("Session <code>%1</code>").arg(m_session.id));
    m_sessionLbl->setToolTip(tr("Saved on exit. Reopen with:\n  KayteIDE --resume %1\n"
                                "Right-click to copy.").arg(m_session.id));
}

// ─────────────────────────────────────────────────────────────────────────────
// Transcript rendering
// ─────────────────────────────────────────────────────────────────────────────

void AssistantPanel::scheduleRender()
{
    if (!m_renderTimer->isActive()) m_renderTimer->start();
}

void AssistantPanel::render()
{
    QString md;
    if (m_session.messages.isEmpty() && !m_busy) {
        md = tr("### Kayte Assistant\n\n"
                "A local LLM (via Ollama) that reads your project's **C, C++, Kayte, VB, "
                "BASIC and CMake** code and answers questions about it.\n\n"
                "1. Open a project or press **Learn**.\n"
                "2. Pick one of the suggested prompts, or ask your own.\n"
                "3. When you quit, the session is saved - reopen it with "
                "`KayteIDE --resume %1`.").arg(m_session.id);
    }
    for (const ChatMessage &m : std::as_const(m_session.messages)) {
        md += m.role == QLatin1String("user") ? tr("#### You\n\n") : tr("#### Assistant\n\n");
        md += m.content + QStringLiteral("\n\n");
    }
    if (m_busy)
        md += tr("#### Assistant\n\n") +
              (m_streaming.isEmpty() ? tr("*thinking...*") : m_streaming) + QStringLiteral("\n");

    QScrollBar *sb = m_transcript->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 8;
    m_transcript->setMarkdown(md);
    if (atBottom || m_busy) sb->setValue(sb->maximum());
}

} // namespace Kayte::Llm
