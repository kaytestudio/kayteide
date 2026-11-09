#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace Kayte::Llm {

// Changes the assistant makes to the project's files.
//
// Local models (Ollama) answer in search/replace blocks, which are parsed and
// applied here:
//
//     src/window.cpp
//     <<<<<<< SEARCH
//     exact lines from the file
//     =======
//     the lines that replace them
//     >>>>>>> REPLACE
//
// An empty SEARCH creates a new file. Claude / Codex edit the files
// themselves; a ProjectSnapshot taken before they run tells what they changed.
// Either way every change keeps the file's previous content, so the whole set
// can be undone.

struct FileEdit
{
    QString path;      // as the model wrote it (relative to the project)
    QString search;
    QString replace;
    bool    wholeFile = false;   // `replace` is the complete new file
};

// The changes in an answer:
//  - search/replace blocks (also inside ``` fences);
//  - whole files - a ``` block right after a path line, or a SEARCH block
//    missing its ======= (small local models often answer that way);
//  - when there is nothing else, a single ``` block that is a full new
//    version of `openFile` (the file open in the editor, relative path).
// Whole files are only applied when they really are the whole file (see
// planEdits), so a fragment never replaces a file.
QList<FileEdit> parseEdits(const QString &answer, const QString &openFile = QString());

struct FileChange
{
    QString path;                       // absolute
    std::optional<QByteArray> before;   // nullopt: the file did not exist
    std::optional<QByteArray> after;    // nullopt: the file was deleted
    int added = 0;
    int removed = 0;
    bool undoable = true;               // false: too large / binary to have been kept
};

struct EditPlan
{
    QList<FileChange> changes;   // one per file, all blocks for it applied in order
    QStringList errors;          // blocks that could not be placed
};

// Current text of a file: what the editor shows when it is open (unsaved
// edits included), else the file on disk; nullopt if it does not exist.
using TextProvider = std::function<std::optional<QString>(const QString &absolutePath)>;

// Works out the new contents. Nothing is written. Paths must stay inside
// `root` (no "..", no .git).
EditPlan planEdits(const QList<FileEdit> &edits, const QString &root, const TextProvider &current);

// Writes `text` (nullopt = delete). The IDE installs one that goes through an
// open editor so Edit > Undo works there too; the default writes the file.
using FileWriter = std::function<bool(const QString &absolutePath, const std::optional<QString> &text, QString *error)>;
bool writeFileDirect(const QString &absolutePath, const std::optional<QString> &text, QString *error);

// Line counts for a change summary.
void countLines(FileChange &change);

// The project's files before an agent runs, to tell afterwards what it changed
// (and to undo it). Build folders, dependencies and .git are skipped; content
// is kept for text files up to 2 MB (64 MB in all).
class ProjectSnapshot
{
public:
    void take(const QString &root);
    QList<FileChange> changesSince() const;   // compares with the disk now
    bool isEmpty() const { return m_files.isEmpty(); }

private:
    struct Entry { qint64 size = 0; qint64 mtime = 0; std::optional<QByteArray> content; };
    QString m_root;
    QHash<QString, Entry> m_files;   // absolute path -> entry
};

// Markdown for the transcript: changed files with +/- counts.
QString changeSummary(const QList<FileChange> &changes, const QString &root, const QString &undoLink);

} // namespace Kayte::Llm
