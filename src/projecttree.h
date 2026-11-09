#pragma once
// projecttree.h - KayteIDE
// The Project panel's view of the project folder, and keeping it current:
//
//  - ProjectTreeFilter - every file and folder (dot-folders such as .vscode,
//    .qtcreator and .clang-tidy included), folders first; hides only
//    repository internals, OS clutter and VS Code's files.exclude patterns.
//  - ProjectLoader     - loads the whole project tree up front instead of a
//    folder at a time; QFileSystemModel watches every folder it has loaded,
//    so files created, deleted or renamed anywhere show up live.
//  - OpenFileWatcher   - notices when a file open in an editor changes or
//    disappears on disk.

#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTimer>

class ProjectTreeFilter : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setRoot(const QString &dir);
    // VS Code files.exclude globs ("**/*.o", "build/", "tests/*.json").
    void setExcludeGlobs(const QStringList &globs);

    // Hidden for good: version-control internals and OS clutter.
    static bool isNoise(const QString &name);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_root;
    QList<QRegularExpression> m_excludes;
};

class ProjectLoader : public QObject
{
    Q_OBJECT
public:
    ProjectLoader(QFileSystemModel *model, ProjectTreeFilter *filter, QObject *parent = nullptr);

    void start(const QString &root);
    bool isLoading() const { return m_loading; }

    // Folders listed but not read ahead (they load when expanded): build
    // output, dependencies, repository internals.
    static bool skipPreload(const QString &path);

    static constexpr int kMaxFolders = 4000;   // stay well inside OS watch limits

signals:
    void finished(int folders, int files, bool capped);

private:
    void onDirectoryLoaded(const QString &path);

    QFileSystemModel  *m_model;
    ProjectTreeFilter *m_filter;
    QString m_root;
    QSet<QString> m_requested;
    int  m_folders = 0;
    int  m_files = 0;
    bool m_loading = false;
    bool m_capped = false;
    QTimer m_settle;   // "finished" once no folder has arrived for a moment
};

class OpenFileWatcher : public QObject
{
    Q_OBJECT
public:
    explicit OpenFileWatcher(QObject *parent = nullptr);

    void setFiles(const QStringList &files);   // the files open in editors

signals:
    void changedOnDisk(const QString &file);
    void removedFromDisk(const QString &file);

private:
    void onFileChanged(const QString &file);

    QFileSystemWatcher m_watcher;
    QSet<QString> m_files;
    QSet<QString> m_pending;
    QTimer m_debounce;   // editors and tools often write a file in several steps
};
