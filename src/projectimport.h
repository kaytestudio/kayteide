#pragma once
// projectimport.h - KayteIDE
// The .xprj project file, and what it imports from other tools' project
// settings so one .xprj opens a folder the way VS Code or Qt Creator would:
//
//   .vscode/      tasks.json, launch.json  -> build configurations
//                 settings.json            -> tab size, excluded files
//                 c_cpp_properties.json    -> include paths / defines (clang-tidy)
//   .qtcreator/   CMakeLists.txt.user (or <dir>/CMakeLists.txt.user, *.pro.user)
//                 -> build configurations (CMake / qmake / custom steps),
//                   run configurations, tab size
//   .clang-tidy   -> checks run by Project > Run clang-tidy
//
// Imported build configurations are re-read whenever the project opens; they
// are never copied into .kayteide/build.json, so editing tasks.json (or
// Qt Creator's settings) is all it takes to change them.

#include "buildconfig.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

// ─── The .xprj file ──────────────────────────────────────────────────────────
//
//   <KayteIDEProject version="2">
//     <ProjectName>...</ProjectName>
//     <ProjectPath>...</ProjectPath>
//     <DevelopmentMode>RAD | Editor</DevelopmentMode>
//     <BuildSettings>...</BuildSettings>
//     <OpenFiles><File>...</File></OpenFiles>
//     <Imports>
//       <VSCode    enabled="true">.vscode</VSCode>
//       <QtCreator enabled="true">.qtcreator/CMakeLists.txt.user</QtCreator>
//       <ClangTidy enabled="true">.clang-tidy</ClangTidy>
//     </Imports>
//   </KayteIDEProject>
//
// Import paths are relative to the project folder. Files without <Imports>
// (written by older KayteIDE versions) get them detected on open.

struct XprjImport
{
    enum Kind { VSCode, QtCreator, ClangTidy };
    Kind    kind;
    QString path;      // as written in the file (relative to the project folder)
    bool    enabled = true;

    static QString kindName(Kind k);   // "VSCode", "QtCreator", "ClangTidy"
    static QString label(Kind k);      // "VS Code", "Qt Creator", "clang-tidy"
};

struct XprjProject
{
    QString filePath;              // the .xprj itself
    QString name;
    QString projectPath;           // <ProjectPath> as stored
    QString developmentMode;       // "RAD" or "Editor"
    QString activeConfiguration;
    QString buildCommand, runCommand, cleanCommand, debugCommand;
    QStringList openFiles;
    QList<XprjImport> imports;
    bool hasImportsElement = false;

    bool load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const;

    // The folder the project lives in: <ProjectPath> when it exists, else the
    // .xprj's own folder (so a moved / cloned project still opens).
    QString projectDir() const;
    QString absolutePath(const XprjImport &imp) const;
    const XprjImport *import(XprjImport::Kind kind) const;

    // The VS Code / Qt Creator / clang-tidy settings present in `dir`.
    static QList<XprjImport> detectImports(const QString &dir);
};

// ─── What an import produced ─────────────────────────────────────────────────

struct EditorSettings
{
    int  tabSize = 0;               // 0 = not set
    bool insertSpacesSet = false;
    bool insertSpaces = true;
    QStringList excludeGlobs;       // files.exclude patterns
};

struct ProjectImport
{
    XprjImport::Kind kind;
    QString path;                           // absolute
    bool ok = false;
    QString error;
    QVector<BuildConfiguration> configs;    // origin set to the tool's name
    QString preferredActive;                // default build task / Qt Creator's active config
    EditorSettings editor;
    QStringList includePaths, defines;      // c_cpp_properties.json
    QString cppStandard;
    QStringList summary;                    // human-readable lines for the Imports dialog
    QStringList notes;                      // what could not be carried over
};

struct ClangTidyConfig
{
    QString path;                           // the .clang-tidy file
    bool ok = false;
    QString error;
    QStringList checks;                     // as listed (including "-group-*" removals)
    QStringList warningsAsErrors;
    QString headerFilter;
    bool inheritParent = false;
    QStringList summary;
};

namespace ProjectImporters {

// tasks.json / launch.json / settings.json / c_cpp_properties.json in `vscodeDir`.
ProjectImport readVSCode(const QString &projectDir, const QString &vscodeDir);

// A Qt Creator .user file; `pathOrDir` may also be a .qtcreator folder.
ProjectImport readQtCreator(const QString &projectDir, const QString &pathOrDir);
QString       findQtCreatorUserFile(const QString &dir);

ClangTidyConfig readClangTidy(const QString &file);

// The clang-tidy executable (PATH, Homebrew LLVM, Xcode), or empty.
QString findClangTidy();
// compile_commands.json for the project (a build dir, the project root, build/...), or empty.
QString findCompileCommands(const QString &projectDir, const QString &buildDir);
// The shell command running clang-tidy on `files` with the project's settings.
QString clangTidyCommand(const ClangTidyConfig &config, const QString &executable,
                         const QString &compileCommandsDir, const QStringList &files,
                         const QStringList &fallbackFlags);
// Source files listed in compile_commands.json that live under `projectDir`.
QStringList compileCommandsFiles(const QString &compileCommandsJson, const QString &projectDir);

// JSON with comments and trailing commas (VS Code's JSONC) -> strict JSON.
QByteArray jsoncToJson(const QByteArray &jsonc);

} // namespace ProjectImporters
