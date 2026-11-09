// projecttree.cpp - KayteIDE
#include "projecttree.h"

#include <QDir>
#include <QFileInfo>

// ─── ProjectTreeFilter ───────────────────────────────────────────────────────

bool ProjectTreeFilter::isNoise(const QString &name)
{
    static const QSet<QString> noise {
        QStringLiteral(".git"), QStringLiteral(".svn"), QStringLiteral(".hg"), QStringLiteral(".DS_Store"),
        QStringLiteral("Thumbs.db"), QStringLiteral("desktop.ini"), QStringLiteral(".localized"),
    };
    return noise.contains(name) || name.startsWith(QLatin1String("._"));   // macOS resource forks
}

void ProjectTreeFilter::setRoot(const QString &dir)
{
    m_root = QDir(dir).absolutePath();
    invalidateFilter();
}

void ProjectTreeFilter::setExcludeGlobs(const QStringList &globs)
{
    m_excludes.clear();
    for (QString g : globs) {
        g = g.trimmed();
        if (g.isEmpty()) continue;
        if (g.endsWith(QLatin1Char('/'))) g.chop(1);
        // A glob without a "/" matches at any depth, as in VS Code.
        if (!g.contains(QLatin1Char('/'))) g.prepend(QStringLiteral("**/"));
        QString rx;
        int braces = 0;   // inside {a,b}: "," separates alternatives
        for (int i = 0; i < g.size(); ++i) {
            const QChar c = g[i];
            if (c == QLatin1Char('*') && i + 1 < g.size() && g[i + 1] == QLatin1Char('*')) {
                ++i;
                if (i + 1 < g.size() && g[i + 1] == QLatin1Char('/')) { ++i; rx += QStringLiteral("(?:.*/)?"); }
                else rx += QStringLiteral(".*");
            } else if (c == QLatin1Char('*')) {
                rx += QStringLiteral("[^/]*");
            } else if (c == QLatin1Char('?')) {
                rx += QStringLiteral("[^/]");
            } else if (c == QLatin1Char('{')) {
                ++braces;
                rx += QStringLiteral("(?:");
            } else if (c == QLatin1Char('}') && braces > 0) {
                --braces;
                rx += QLatin1Char(')');
            } else if (c == QLatin1Char(',') && braces > 0) {
                rx += QLatin1Char('|');
            } else {
                rx += QRegularExpression::escape(QString(c));
            }
        }
        m_excludes << QRegularExpression(QStringLiteral("^") + rx + QStringLiteral("$"));
    }
    invalidateFilter();
}

bool ProjectTreeFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
    if (!fs) return true;
    const QModelIndex idx = fs->index(row, 0, parent);
    const QString path = fs->filePath(idx);
    // Only judge what lies inside the project; the folders above it stay visible.
    if (m_root.isEmpty() || !(path.startsWith(m_root + QLatin1Char('/')))) return true;
    if (isNoise(fs->fileName(idx))) return false;
    if (!m_excludes.isEmpty()) {
        const QString rel = path.mid(m_root.size() + 1);
        for (const QRegularExpression &rx : m_excludes)
            if (rx.match(rel).hasMatch()) return false;
    }
    return true;
}

bool ProjectTreeFilter::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
    if (fs) {
        const bool ld = fs->isDir(left), rd = fs->isDir(right);
        if (ld != rd) return ld;   // folders first
        return fs->fileName(left).compare(fs->fileName(right), Qt::CaseInsensitive) < 0;
    }
    return QSortFilterProxyModel::lessThan(left, right);
}

// ─── ProjectLoader ───────────────────────────────────────────────────────────

ProjectLoader::ProjectLoader(QFileSystemModel *model, ProjectTreeFilter *filter, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_filter(filter)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(600);
    connect(&m_settle, &QTimer::timeout, this, [this] {
        if (!m_loading) return;
        m_loading = false;
        emit finished(m_folders, m_files, m_capped);
    });
    connect(m_model, &QFileSystemModel::directoryLoaded, this, &ProjectLoader::onDirectoryLoaded);
}

bool ProjectLoader::skipPreload(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    static const QSet<QString> skip {
        QStringLiteral(".git"), QStringLiteral(".svn"), QStringLiteral(".hg"), QStringLiteral("node_modules"),
        QStringLiteral("_deps"), QStringLiteral("CMakeFiles"), QStringLiteral("__pycache__"), QStringLiteral(".cache"),
    };
    if (skip.contains(name) || name.endsWith(QLatin1String("_autogen")) || name.endsWith(QLatin1String(".app")) ||
        name.endsWith(QLatin1String(".xcodeproj")))
        return true;
    // A CMake / qmake build folder.
    return QFileInfo::exists(QDir(path).filePath(QStringLiteral("CMakeCache.txt"))) ||
           QFileInfo::exists(QDir(path).filePath(QStringLiteral(".qmake.stash")));
}

void ProjectLoader::start(const QString &root)
{
    m_root = QDir(root).absolutePath();
    m_requested.clear();
    m_folders = m_files = 0;
    m_capped = false;
    m_loading = true;
    m_requested.insert(m_root);
    const QModelIndex idx = m_model->index(m_root);
    if (m_model->canFetchMore(idx)) m_model->fetchMore(idx);
    else onDirectoryLoaded(m_root);   // already loaded (same root as before)
    m_settle.start();
}

void ProjectLoader::onDirectoryLoaded(const QString &path)
{
    if (!m_loading || !(path == m_root || path.startsWith(m_root + QLatin1Char('/')))) return;
    ++m_folders;
    const QModelIndex parent = m_model->index(path);
    const int rows = m_model->rowCount(parent);
    for (int r = 0; r < rows; ++r) {
        const QModelIndex child = m_model->index(r, 0, parent);
        const QString childPath = m_model->filePath(child);
        if (!m_model->isDir(child)) { ++m_files; continue; }
        if (m_requested.contains(childPath) || ProjectTreeFilter::isNoise(m_model->fileName(child)) ||
            skipPreload(childPath) || QFileInfo(childPath).isSymLink())
            continue;
        if (m_requested.size() >= kMaxFolders) { m_capped = true; continue; }
        m_requested.insert(childPath);
        if (m_model->canFetchMore(child)) m_model->fetchMore(child);   // loads (and watches) the folder
    }
    m_settle.start();
}

// ─── OpenFileWatcher ─────────────────────────────────────────────────────────

OpenFileWatcher::OpenFileWatcher(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &OpenFileWatcher::onFileChanged);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        const QSet<QString> files = std::exchange(m_pending, {});
        for (const QString &f : files) {
            if (!m_files.contains(f)) continue;
            if (QFileInfo::exists(f)) {
                if (!m_watcher.files().contains(f)) m_watcher.addPath(f);   // replaced by an atomic save
                emit changedOnDisk(f);
            } else {
                emit removedFromDisk(f);
            }
        }
    });
}

void OpenFileWatcher::setFiles(const QStringList &files)
{
    QSet<QString> want;
    for (const QString &f : files)
        if (!f.isEmpty()) want.insert(QFileInfo(f).absoluteFilePath());
    for (const QString &f : m_watcher.files())
        if (!want.contains(f)) m_watcher.removePath(f);
    for (const QString &f : want)
        if (QFileInfo::exists(f) && !m_watcher.files().contains(f)) m_watcher.addPath(f);
    m_files = want;
}

void OpenFileWatcher::onFileChanged(const QString &file)
{
    m_pending.insert(file);
    m_debounce.start();
}
