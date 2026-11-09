#pragma once
// cmakeautoadd.h - KayteIDE
// Keeps CMakeLists.txt in step with the project: when a new C/C++ source,
// header, .ui or .qrc file appears in a folder the project's CMake files
// already list sources from - saved from the editor, made in the terminal or
// by another tool - it is added to the list that holds its neighbours, in
// the same style (path prefix / ${VAR}, quoting, indentation, sort order).
//
// Files already listed, files a file(GLOB ...) picks up, and generated files
// (moc_*, ui_*, qrc_*, build folders) are left alone.

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

class QFileSystemWatcher;

namespace CMakeAuto {

// One argument of a CMake command, with where it sits in the file.
struct Token
{
    QString raw;      // as written (quotes included)
    QString value;    // unquoted
    int     start = 0;
    int     end = 0;  // one past the last character
    bool    quoted = false;
};

struct Command
{
    QString name;            // lower case
    int     start = 0;       // the command name
    int     closeParen = 0;  // offset of ")"
    QList<Token> args;
};

// The commands of a CMakeLists.txt (comments, quoted and bracket arguments
// handled). Commands inside if()/foreach() blocks are included.
QList<Command> parse(const QString &text);

// A text insertion that adds a file to a CMakeLists.txt.
struct Edit
{
    QString cmakeFile;      // absolute path
    QString file;           // the file being added (absolute)
    int     offset = -1;    // where to insert (-1 = nothing to do)
    QString insert;         // text to insert
    QString listName;       // "set(PROJECT_SOURCES)", "add_library(KayteLlmAssistant)" ...
    QString entry;          // the new entry as written
    QString reason;         // why nothing was done, when offset is -1
};

// Where `file` goes in the CMake file `cmakeFile` whose text is `text`.
// `topDir` is the top-level project folder (for ${CMAKE_SOURCE_DIR}).
Edit planAdd(const QString &cmakeFile, const QString &text, const QString &topDir, const QString &file);

// Whether `file` is something CMake lists (by extension, not generated).
bool isListable(const QString &file);

// The folders `cmakeFile` lists sources from, and the CMakeLists.txt files
// its add_subdirectory() calls bring into the build.
QStringList sourceDirs(const QString &cmakeFile, const QString &text, const QString &topDir);
QStringList subdirectories(const QString &cmakeFile, const QString &text, const QString &topDir);

} // namespace CMakeAuto

// Watches the project's source folders and adds new files as they appear.
class CMakeAutoAdder : public QObject
{
    Q_OBJECT
public:
    explicit CMakeAutoAdder(QObject *parent = nullptr);

    void setProjectRoot(const QString &dir);   // empty / no CMakeLists.txt = off
    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }

    // Adds `file` now (used right after the editor saves a new file).
    // Returns true when a CMakeLists.txt was changed.
    bool addFile(const QString &file);

    // Applies an edit. The IDE installs one that edits an open editor tab
    // (keeping undo); otherwise the file on disk is changed.
    std::function<bool(const CMakeAuto::Edit &)> applyEdit;

signals:
    void fileAdded(const QString &file, const QString &cmakeFile, const QString &listName);
    void message(const QString &text);

private:
    void rescan();                       // CMake files, watched folders, folder snapshots
    void onDirectoryChanged(const QString &dir);
    void processPending();
    QStringList cmakeFilesFor(const QString &file) const;   // nearest enclosing first, then the rest

    QString m_root;
    bool m_enabled = true;
    QFileSystemWatcher *m_watcher = nullptr;
    QHash<QString, QSet<QString>> m_snapshot;   // folder -> listable files seen there
    QStringList m_cmakeFiles;                   // only those the build uses (root + add_subdirectory)
    QSet<QString> m_pendingDirs;
    QTimer m_debounce;
};
