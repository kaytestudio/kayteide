#include "LogWidget.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <QScrollBar>
#include <QTextCharFormat>

LogWidget::LogWidget(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setWordWrapMode(QTextOption::NoWrap);
    setFont(QFont("Menlo, Consolas, monospace", 10));

    QPalette p = palette();
    p.setColor(QPalette::Base,     QColor("#0d1117"));
    p.setColor(QPalette::Text,     QColor("#c9d1d9"));
    setPalette(p);

    setStyleSheet(R"(
        QPlainTextEdit {
            background-color: #0d1117;
            color: #c9d1d9;
            border: 1px solid #30363d;
            border-radius: 6px;
            padding: 6px;
            font-family: 'Menlo', 'Consolas', 'Courier New', monospace;
            font-size: 11px;
        }
        QScrollBar:vertical {
            background: #161b22;
            width: 8px;
        }
        QScrollBar::handle:vertical {
            background: #30363d;
            border-radius: 4px;
        }
    )");
}

namespace {
// Everything shown in the log is also appended to ~/Library/Logs/KayteIDEUpdater.log
// (~/.local/state/KayteIDEUpdater.log elsewhere), so an update that closed
// and reopened KayteIDE can be followed afterwards.
void writeLogFile(const QString &text)
{
#ifdef Q_OS_MACOS
    const QString path = QDir::homePath() + "/Library/Logs/KayteIDEUpdater.log";
#else
    const QString path = QDir::homePath() + "/.local/state/KayteIDEUpdater.log";
#endif
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write((QDateTime::currentDateTime().toString(Qt::ISODate) + "  " + text + "\n").toUtf8());
}
} // namespace

void LogWidget::appendLog(const QString &line, bool isError)
{
    writeLogFile((isError ? "! " : "  ") + line);
    QTextCharFormat fmt;
    if (isError)
        fmt.setForeground(QColor("#f85149")); // GitHub red
    else if (line.startsWith("▶"))
        fmt.setForeground(QColor("#58a6ff")); // blue for commands
    else if (line.startsWith("✔") || line.contains("success", Qt::CaseInsensitive))
        fmt.setForeground(QColor("#3fb950")); // green
    else if (line.contains("warning", Qt::CaseInsensitive))
        fmt.setForeground(QColor("#d29922")); // yellow
    else
        fmt.setForeground(QColor("#c9d1d9")); // default

    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(line + "\n", fmt);

    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void LogWidget::appendSection(const QString &title)
{
    writeLogFile("== " + title);
    QTextCharFormat fmt;
    fmt.setForeground(QColor("#8b949e"));
    fmt.setFontWeight(QFont::Bold);

    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);

    const QString separator = QString("─").repeated(60);
    cursor.insertText("\n" + separator + "\n", fmt);

    fmt.setForeground(QColor("#79c0ff"));
    cursor.insertText("  " + title + "\n", fmt);

    fmt.setForeground(QColor("#8b949e"));
    cursor.insertText(separator + "\n", fmt);

    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void LogWidget::clear()
{
    QPlainTextEdit::clear();
}
