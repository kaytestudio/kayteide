#ifndef EDITORSTATUSBAR_H
#define EDITORSTATUSBAR_H

#include <QPointer>
#include <QWidget>

class EditorTabWidget;
class QLabel;
class QProcess;
class QStatusBar;
class QTimer;
class QToolButton;

// The VS Code-style strip along the window's status bar:
//
//   ⎇ main  ⊗ 0 ⚠ 0  <message>      Screen Reader Optimized  Ln 5, Col 3  Spaces: 4  UTF-8  LF  COBOL
//
// It takes the whole status bar and shows QStatusBar::showMessage() text in
// its middle, so messages never hide the items. The editor items follow the
// current tab; clicking them goes to a line, changes the indentation or
// opens the Problems view.
class EditorStatusBar : public QWidget
{
    Q_OBJECT
public:
    explicit EditorStatusBar(QStatusBar *bar);

    // The tab whose cursor / file the bar describes (nullptr: no editor).
    void setEditor(EditorTabWidget *tab);
    // The folder whose git branch is shown (empty: none).
    void setFolder(const QString &dir);
    void setProblemCounts(int errors, int warnings);
    void setIndentation(int width, bool spaces);

    // Screen Reader Optimized: names the editor for assistive technology and
    // announces line jumps and build results. Saved in the settings.
    bool screenReaderOptimized() const { return m_screenReader; }
    void setScreenReaderOptimized(bool on);

    static QString languageName(const QString &filePath);

    // Asks for a line and moves the current editor there.
    void goToLine();

signals:
    void problemsClicked();
    void branchClicked();
    void indentationChosen(int width, bool spaces);
    void screenReaderOptimizedChanged(bool on);

private:
    QToolButton *addItem(const QString &toolTip);
    void updateCursor();
    void updateFileInfo();
    void refreshBranch();
    void chooseIndentation();
    void announce(const QString &message);

    QPointer<EditorTabWidget> m_tab;
    QString      m_folder;
    QProcess    *m_git { nullptr };
    QTimer      *m_branchTimer;
    int          m_errors { -1 };
    int          m_warnings { -1 };
    int          m_indentWidth { 4 };
    bool         m_indentSpaces { true };
    bool         m_screenReader { false };
    int          m_lastLine { -1 };

    QToolButton *m_branch;
    QToolButton *m_problems;
    QLabel      *m_message;
    QToolButton *m_screenReaderItem;
    QToolButton *m_cursor;
    QToolButton *m_indent;
    QToolButton *m_encoding;
    QToolButton *m_eol;
    QToolButton *m_language;
    QList<QMetaObject::Connection> m_editorConnections;
};

#endif // EDITORSTATUSBAR_H
