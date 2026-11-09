#include "editorstatusbar.h"

#include "editortabwidget.h"

#include <QAccessible>
#include <QActionGroup>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSettings>
#include <QStatusBar>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>

namespace {
const char *kScreenReaderKey = "editor/screenReaderOptimized";
}

EditorStatusBar::EditorStatusBar(QStatusBar *bar)
    : QWidget(bar)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 0, 4, 0);
    lay->setSpacing(2);

    m_branch = addItem(tr("Git branch - click to open the Git panel"));
    m_problems = addItem(tr("Errors and warnings from the last build - click to show them"));
    m_message = new QLabel(this);
    m_message->setTextFormat(Qt::PlainText);
    m_message->setContentsMargins(8, 0, 8, 0);
    m_message->setMinimumWidth(0);
    m_message->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_screenReaderItem = addItem(tr("Screen reader mode is on - click to change"));
    m_screenReaderItem->setText(tr("Screen Reader Optimized"));
    m_cursor   = addItem(tr("Go to Line/Column"));
    m_indent   = addItem(tr("Select Indentation"));
    m_encoding = addItem(tr("File encoding"));
    m_eol      = addItem(tr("Line endings of the file on disk"));
    m_language = addItem(tr("Language of the file"));

    // Same size as the items (macOS gives tool buttons a smaller font).
    m_message->setFont(QApplication::font(m_branch));
    lay->addWidget(m_branch);
    lay->addWidget(m_problems);
    lay->addWidget(m_message, 1);
    for (QToolButton *b : {m_screenReaderItem, m_cursor, m_indent, m_encoding, m_eol, m_language})
        lay->addWidget(b);

    // Fill the bar and show its messages ourselves, so they never hide items.
    bar->addPermanentWidget(this, 1);
    connect(bar, &QStatusBar::messageChanged, m_message, &QLabel::setText);

    connect(m_problems, &QToolButton::clicked, this, &EditorStatusBar::problemsClicked);
    connect(m_branch,   &QToolButton::clicked, this, &EditorStatusBar::branchClicked);
    connect(m_cursor,   &QToolButton::clicked, this, &EditorStatusBar::goToLine);
    connect(m_indent,   &QToolButton::clicked, this, &EditorStatusBar::chooseIndentation);
    connect(m_screenReaderItem, &QToolButton::clicked, this, [this] {
        QMenu menu(this);
        menu.addAction(tr("Turn Off Screen Reader Optimized"), this,
                       [this] { setScreenReaderOptimized(false); });
        menu.exec(m_screenReaderItem->mapToGlobal(QPoint(0, 0)) - QPoint(0, menu.sizeHint().height()));
    });
    for (QToolButton *b : {m_encoding, m_eol, m_language}) b->setEnabled(false);

    // Branches change outside the IDE too (a terminal checkout).
    m_branchTimer = new QTimer(this);
    m_branchTimer->setInterval(10000);
    connect(m_branchTimer, &QTimer::timeout, this, &EditorStatusBar::refreshBranch);
    m_branchTimer->start();

    m_screenReader = QSettings().value(kScreenReaderKey, false).toBool();
    m_screenReaderItem->setVisible(m_screenReader);
    m_branch->hide();
    setProblemCounts(0, 0);
    setEditor(nullptr);
}

QToolButton *EditorStatusBar::addItem(const QString &toolTip)
{
    auto *b = new QToolButton(this);
    b->setAutoRaise(true);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolTip(toolTip);
    b->setAccessibleDescription(toolTip);
    // Disabled items are information only: keep them readable.
    b->setStyleSheet(QStringLiteral("QToolButton { padding: 0 6px; border: none; }"
                                    "QToolButton:disabled { color: palette(window-text); }"
                                    "QToolButton:hover:enabled { background: palette(midlight); }"));
    return b;
}

void EditorStatusBar::setEditor(EditorTabWidget *tab)
{
    for (const auto &c : std::as_const(m_editorConnections)) disconnect(c);
    m_editorConnections.clear();
    m_tab = tab;
    m_lastLine = -1;

    const bool hasEditor = tab && tab->getPlainTextEdit();
    for (QToolButton *b : {m_cursor, m_indent, m_encoding, m_eol, m_language}) b->setVisible(hasEditor);
    if (!hasEditor) return;

    QPlainTextEdit *ed = tab->getPlainTextEdit();
    m_editorConnections << connect(ed, &QPlainTextEdit::cursorPositionChanged, this, &EditorStatusBar::updateCursor)
                        << connect(ed, &QPlainTextEdit::selectionChanged, this, &EditorStatusBar::updateCursor)
                        << connect(tab, &EditorTabWidget::titleChanged, this, &EditorStatusBar::updateFileInfo);
    updateFileInfo();
    updateCursor();
}

void EditorStatusBar::setFolder(const QString &dir)
{
    if (dir == m_folder) return;
    m_folder = dir;
    refreshBranch();
}

void EditorStatusBar::setProblemCounts(int errors, int warnings)
{
    if (errors == m_errors && warnings == m_warnings) return;
    const bool first = m_errors < 0;
    m_errors = errors;
    m_warnings = warnings;
    // ⊗ / ⚠ as in VS Code; the accessible name spells them out.
    m_problems->setText(QStringLiteral("⊗ %1  ⚠ %2").arg(errors).arg(warnings));
    const QString spoken = tr("%n error(s)", nullptr, errors) + QStringLiteral(", ")
                         + tr("%n warning(s)", nullptr, warnings);
    m_problems->setAccessibleName(spoken);
    if (!first) announce(spoken);
}

void EditorStatusBar::setIndentation(int width, bool spaces)
{
    m_indentWidth = width > 0 ? width : 4;
    m_indentSpaces = spaces;
    m_indent->setText(spaces ? tr("Spaces: %1").arg(m_indentWidth) : tr("Tab Size: %1").arg(m_indentWidth));
}

void EditorStatusBar::setScreenReaderOptimized(bool on)
{
    if (on == m_screenReader) return;
    m_screenReader = on;
    QSettings().setValue(kScreenReaderKey, on);
    m_screenReaderItem->setVisible(on);
    updateFileInfo();
    announce(on ? tr("Screen reader optimized on") : tr("Screen reader optimized off"));
    emit screenReaderOptimizedChanged(on);
}

void EditorStatusBar::updateCursor()
{
    if (!m_tab || !m_tab->getPlainTextEdit()) return;
    const QTextCursor c = m_tab->getPlainTextEdit()->textCursor();
    const int line = c.blockNumber() + 1;
    QString text = tr("Ln %1, Col %2").arg(line).arg(c.positionInBlock() + 1);
    if (c.hasSelection()) {
        const int chars = int(c.selectionEnd() - c.selectionStart());
        text += QStringLiteral(" ") + tr("(%1 selected)").arg(chars);
    }
    m_cursor->setText(text);
    m_cursor->setAccessibleName(text);
    m_lastLine = line;
}

void EditorStatusBar::updateFileInfo()
{
    if (!m_tab) return;
    const QString path = m_tab->filePath();

    // What is on disk: the editor reads UTF-8 and normalises line endings.
    QByteArray head;
    QFile f(path);
    if (!path.isEmpty() && f.open(QIODevice::ReadOnly)) head = f.read(64 * 1024);
    m_encoding->setText(head.startsWith("\xEF\xBB\xBF") ? tr("UTF-8 with BOM") : tr("UTF-8"));
    m_eol->setText(head.contains("\r\n") ? QStringLiteral("CRLF") : QStringLiteral("LF"));

    const QString language = languageName(path);
    m_language->setText(language);

    // Name the editor so a screen reader says which file has focus.
    if (QPlainTextEdit *ed = m_tab->getPlainTextEdit()) {
        const QString file = path.isEmpty() ? tr("Untitled") : QFileInfo(path).fileName();
        ed->setAccessibleName(m_screenReader ? tr("Editor, %1, %2").arg(file, language) : QString());
    }
}

QString EditorStatusBar::languageName(const QString &filePath)
{
    static const QHash<QString, QString> names = {
        {"c", "C"}, {"h", "C++"}, {"cpp", "C++"}, {"cxx", "C++"}, {"cc", "C++"}, {"hpp", "C++"},
        {"hxx", "C++"}, {"m", "Objective-C"}, {"mm", "Objective-C++"},
        {"kayte", "Kayte"}, {"kyt", "Kayte"}, {"kjs", "Kayte JS"}, {"kfm", "Kayte Form"},
        {"bas", "BASIC"}, {"vb", "Visual Basic"}, {"frm", "InForm Form"},
        {"pas", "Pascal"}, {"pp", "Pascal"}, {"lpr", "Pascal"}, {"dpr", "Delphi"}, {"dfm", "Delphi Form"},
        {"cob", "COBOL"}, {"cbl", "COBOL"}, {"cpy", "COBOL Copybook"}, {"cobol", "COBOL"},
        {"md", "Markdown"}, {"markdown", "Markdown"}, {"ui", "Qt Designer"},
        {"txt", "Plain Text"}, {"json", "JSON"}, {"xml", "XML"}, {"html", "HTML"}, {"css", "CSS"},
        {"js", "JavaScript"}, {"ts", "TypeScript"}, {"py", "Python"}, {"sh", "Shell Script"},
        {"cmake", "CMake"}, {"yml", "YAML"}, {"yaml", "YAML"}, {"swift", "Swift"}, {"rs", "Rust"},
        {"go", "Go"}, {"java", "Java"},
    };
    const QFileInfo fi(filePath);
    if (fi.fileName() == QLatin1String("CMakeLists.txt")) return QStringLiteral("CMake");
    if (fi.fileName().compare(QLatin1String("Makefile"), Qt::CaseInsensitive) == 0) return QStringLiteral("Makefile");
    return names.value(fi.suffix().toLower(), QStringLiteral("Plain Text"));
}

void EditorStatusBar::refreshBranch()
{
    if (m_folder.isEmpty()) { m_branch->hide(); return; }
    if (m_git) return;   // one at a time
    m_git = new QProcess(this);
    m_git->setWorkingDirectory(m_folder);
    connect(m_git, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString branch = QString::fromUtf8(m_git->readAllStandardOutput()).trimmed();
        m_git->deleteLater();
        m_git = nullptr;
        const bool ok = status == QProcess::NormalExit && code == 0 && !branch.isEmpty();
        m_branch->setVisible(ok);
        if (ok) {
            m_branch->setText(QStringLiteral("⎇ ") + branch);   // ⎇
            m_branch->setAccessibleName(tr("Git branch %1").arg(branch));
        }
    });
    connect(m_git, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        m_git->deleteLater();
        m_git = nullptr;
        m_branch->hide();
    });
    m_git->start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                                         QStringLiteral("HEAD")});
}

void EditorStatusBar::goToLine()
{
    if (!m_tab || !m_tab->getPlainTextEdit()) return;
    QPlainTextEdit *ed = m_tab->getPlainTextEdit();
    bool ok = false;
    const int line = QInputDialog::getInt(this, tr("Go to Line"),
                                          tr("Line (1 - %1):").arg(ed->blockCount()),
                                          ed->textCursor().blockNumber() + 1, 1, ed->blockCount(), 1, &ok);
    if (!ok) return;
    QTextCursor c(ed->document()->findBlockByNumber(line - 1));
    ed->setTextCursor(c);
    ed->centerCursor();
    ed->setFocus();
    announce(tr("Line %1").arg(line));
}

void EditorStatusBar::chooseIndentation()
{
    QMenu menu(this);
    auto *kind = new QActionGroup(&menu);
    QAction *spaces = menu.addAction(tr("Indent Using Spaces"));
    QAction *tabs   = menu.addAction(tr("Indent Using Tabs"));
    for (QAction *a : {spaces, tabs}) { a->setCheckable(true); kind->addAction(a); }
    (m_indentSpaces ? spaces : tabs)->setChecked(true);
    menu.addSeparator();
    auto *widths = new QActionGroup(&menu);
    for (int w : {2, 4, 8}) {
        QAction *a = menu.addAction(tr("Width: %1").arg(w));
        a->setCheckable(true);
        a->setChecked(w == m_indentWidth);
        a->setData(w);
        widths->addAction(a);
    }
    QAction *chosen = menu.exec(m_indent->mapToGlobal(QPoint(0, 0)) - QPoint(0, menu.sizeHint().height()));
    if (!chosen) return;
    const bool useSpaces = chosen == tabs ? false : chosen == spaces ? true : m_indentSpaces;
    const int width = chosen->data().isValid() ? chosen->data().toInt() : m_indentWidth;
    setIndentation(width, useSpaces);
    emit indentationChosen(width, useSpaces);
}

void EditorStatusBar::announce(const QString &message)
{
    if (!m_screenReader) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAccessibleAnnouncementEvent event(this, message);
    QAccessible::updateAccessibility(&event);
#else
    Q_UNUSED(message);
#endif
}
