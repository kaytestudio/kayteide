#ifndef FILEHISTORYDIALOG_H
#define FILEHISTORYDIALOG_H

#include <QDialog>
#include <QPlainTextEdit>
#include <QVector>

class QLabel;
class QTreeWidget;
class QToolButton;

// One side of the side-by-side diff: read-only text with a gutter showing the
// file's own line numbers (blank on padding rows) and tinted changed lines.
class DiffPane : public QPlainTextEdit
{
    Q_OBJECT
public:
    enum RowKind { Same, Removed, Added, Padding };
    struct Row { int lineNo = 0; RowKind kind = Same; };

    explicit DiffPane(QWidget *parent = nullptr);
    void setRows(const QStringList &text, const QVector<Row> &rows);
    int gutterWidth() const;
    int topRow() const;
    void paintGutter(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QWidget *m_gutter;
    QVector<Row> m_rows;
};

// Lists the git commits that touched a file and shows, side by side, what
// each one changed (or, for two selected commits, everything in between).
class FileHistoryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FileHistoryDialog(const QString &filePath, QWidget *parent = nullptr);

private:
    struct Commit {
        QString hash, parent, author, date, subject;
        QString path, oldPath;   // repo-relative, at this commit / its parent
    };

    void loadHistory();
    void onSelectionChanged();
    void showDiff(const QString &fromRev, const QString &fromPath,
                  const QString &toRev, const QString &toPath,
                  const QString &title);
    void jumpToChange(int direction);
    int visibleRows() const;
    QString git(const QStringList &args, bool *ok = nullptr) const;

    QString m_filePath;
    QString m_repoRoot;
    QVector<Commit> m_commits;   // newest first
    bool m_hasLocalChanges = false;

    QTreeWidget *m_list;
    QLabel      *m_title;
    DiffPane    *m_left;
    DiffPane    *m_right;
    QVector<int> m_changeStarts;  // first row of each changed block
};

#endif // FILEHISTORYDIALOG_H
