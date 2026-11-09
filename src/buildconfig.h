#ifndef BUILDCONFIG_H
#define BUILDCONFIG_H

#include <QObject>
#include <QString>
#include <QVector>

class QJsonObject;

// One named way of building and running a project (Debug, Release, ...).
// Commands are run by /bin/sh in the project folder and may use
// ${ProjectDir}, ${ProjectName}, ${BuildDir}, ${Config}, ${KayteTools},
// ${File} and ${FileBaseName} (the file open in the editor).
struct BuildConfiguration
{
    QString name;
    QString buildDir;   // relative to the project folder, or absolute
    QString build;
    QString clean;
    QString run;
    QString debug;
    // Run (and Debug) build first and start only if the build succeeded.
    // Off for run commands that build by themselves.
    bool buildBeforeRun = true;
    // Set for configurations imported from another tool ("VS Code",
    // "Qt Creator"): they are re-read from that tool's files and never saved
    // to build.json.
    QString origin;

    QJsonObject toJson() const;
    static BuildConfiguration fromJson(const QJsonObject &o);
};

// The build configurations of the current project. They are detected from
// the project's files until the user changes something; from then on they
// are stored in <project>/.kayteide/build.json.
class BuildConfigurations : public QObject
{
    Q_OBJECT
public:
    explicit BuildConfigurations(QObject *parent = nullptr);

    // Loads the project's saved configurations, or detects defaults.
    // `language` ("cobol", or empty for any) narrows a folder of loose files
    // to the language of the file being edited; such configurations are
    // stored apart (.kayteide/build-<language>.json) so one language's
    // commands are never run on another's files.
    void setProject(const QString &dir, const QString &name, const QString &language = QString());
    QString projectDir()  const { return m_dir; }
    QString projectName() const { return m_name; }
    QString language()    const { return m_language; }

    // The language setProject() takes for a loose file ("cobol"), or empty
    // when the folder's own detection applies.
    static QString fileLanguage(const QString &path);
    QString detectedKind() const { return m_kind; }   // "CMake", "Lazarus", ...

    const QVector<BuildConfiguration> &configurations() const { return m_configs; }
    int  activeIndex() const { return m_active; }
    BuildConfiguration active() const;

    // Both persist to build.json; they return false (with `error`) if it
    // could not be written - the change still applies for this session.
    bool setActiveIndex(int index, QString *error = nullptr);
    bool setConfigurations(const QVector<BuildConfiguration> &configs, int active,
                           QString *error = nullptr);

    // Absolute build directory of `config` (default: the active one).
    QString buildDirectory(const BuildConfiguration &config) const;
    // Substitutes the ${...} variables for `config`.
    QString expand(const QString &command, const BuildConfiguration &config) const;

    QString settingsFile() const;

    // Configurations imported from VS Code / Qt Creator for the project in
    // `dir`: appended after the project's own, kept in memory only. When the
    // project has no saved choice, `preferredActive` (by name) becomes active.
    void setImported(const QString &dir, const QVector<BuildConfiguration> &configs,
                     const QString &preferredActive = QString());

    // The file open in the editor, for ${File} / ${FileBaseName}.
    void    setCurrentFile(const QString &path) { m_currentFile = path; }
    QString currentFile() const { return m_currentFile; }

    // Default configurations for the project in `dir`.
    static QVector<BuildConfiguration> detect(const QString &dir, const QString &name,
                                              QString *kind = nullptr,
                                              const QString &language = QString());
    // Contents/tools of the app bundle (FPC, Lazarus, QEMU), or empty.
    static QString toolsDir();

signals:
    void changed();

private:
    bool save(QString *error);

    QString m_dir;
    QString m_name;
    QString m_language;
    QString m_kind;
    QString m_currentFile;
    QVector<BuildConfiguration> m_configs;
    int m_active = 0;
    QString m_savedActive;                     // the active name build.json recorded
    QString m_importedDir;
    QVector<BuildConfiguration> m_imported;
    QString m_importedPreferred;
    void appendImported();
};

#endif // BUILDCONFIG_H
