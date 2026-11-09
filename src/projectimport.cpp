// projectimport.cpp - KayteIDE
#include "projectimport.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVariantMap>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <functional>

// ═════════════════════════════════════════════════════════════════════════════
//  .xprj
// ═════════════════════════════════════════════════════════════════════════════

QString XprjImport::kindName(Kind k)
{
    switch (k) {
    case VSCode:    return QStringLiteral("VSCode");
    case QtCreator: return QStringLiteral("QtCreator");
    case ClangTidy: return QStringLiteral("ClangTidy");
    }
    return {};
}

QString XprjImport::label(Kind k)
{
    switch (k) {
    case VSCode:    return QStringLiteral("VS Code");
    case QtCreator: return QStringLiteral("Qt Creator");
    case ClangTidy: return QStringLiteral("clang-tidy");
    }
    return {};
}

bool XprjProject::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    *this = XprjProject {};
    filePath = QFileInfo(path).absoluteFilePath();

    QXmlStreamReader xml(&f);
    bool sawRoot = false;
    QString section;
    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::EndElement) {
            const auto n = xml.name();
            if (n == QLatin1String("BuildSettings") || n == QLatin1String("OpenFiles") || n == QLatin1String("Imports"))
                section.clear();
            continue;
        }
        if (tok != QXmlStreamReader::StartElement) continue;
        const QString tag = xml.name().toString();
        if (!sawRoot) {
            if (tag != QLatin1String("KayteIDEProject")) {
                if (error) *error = QStringLiteral("not a KayteIDE project (root element is <%1>)").arg(tag);
                return false;
            }
            sawRoot = true;
            continue;
        }
        if (tag == QLatin1String("BuildSettings") || tag == QLatin1String("OpenFiles")) { section = tag; continue; }
        if (tag == QLatin1String("Imports")) { section = tag; hasImportsElement = true; continue; }

        if (section == QLatin1String("Imports")) {
            XprjImport imp;
            if (tag == QLatin1String("VSCode"))         imp.kind = XprjImport::VSCode;
            else if (tag == QLatin1String("QtCreator")) imp.kind = XprjImport::QtCreator;
            else if (tag == QLatin1String("ClangTidy")) imp.kind = XprjImport::ClangTidy;
            else { xml.skipCurrentElement(); continue; }
            imp.enabled = xml.attributes().value(QLatin1String("enabled")) != QLatin1String("false");
            imp.path = xml.readElementText().trimmed();
            imports << imp;
            continue;
        }

        const QString text = xml.readElementText().trimmed();
        if (section == QLatin1String("OpenFiles")) {
            if (tag == QLatin1String("File") && !text.isEmpty()) openFiles << text;
        } else if (section == QLatin1String("BuildSettings")) {
            if (tag == QLatin1String("Configuration"))      activeConfiguration = text;
            else if (tag == QLatin1String("BuildCommand"))  buildCommand = text;
            else if (tag == QLatin1String("RunCommand"))    runCommand = text;
            else if (tag == QLatin1String("CleanCommand"))  cleanCommand = text;
            else if (tag == QLatin1String("DebugCommand"))  debugCommand = text;
        } else if (tag == QLatin1String("ProjectName")) {
            name = text;
        } else if (tag == QLatin1String("ProjectPath")) {
            projectPath = text;
        } else if (tag == QLatin1String("DevelopmentMode")) {
            developmentMode = text;
        }
    }
    if (xml.hasError()) {
        if (error) *error = QStringLiteral("line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
        return false;
    }
    if (!sawRoot) {
        if (error) *error = QStringLiteral("empty project file");
        return false;
    }
    if (name.isEmpty()) name = QFileInfo(path).completeBaseName();
    if (!hasImportsElement) imports = detectImports(projectDir());
    return true;
}

bool XprjProject::save(const QString &path, QString *error) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    QXmlStreamWriter xml(&f);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(4);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("KayteIDEProject"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("2"));
    xml.writeTextElement(QStringLiteral("ProjectName"), name);
    xml.writeTextElement(QStringLiteral("ProjectPath"), projectPath);
    xml.writeTextElement(QStringLiteral("DevelopmentMode"), developmentMode);

    xml.writeStartElement(QStringLiteral("BuildSettings"));
    xml.writeTextElement(QStringLiteral("Configuration"), activeConfiguration);
    xml.writeTextElement(QStringLiteral("BuildCommand"), buildCommand);
    xml.writeTextElement(QStringLiteral("RunCommand"), runCommand);
    xml.writeTextElement(QStringLiteral("CleanCommand"), cleanCommand);
    xml.writeTextElement(QStringLiteral("DebugCommand"), debugCommand);
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("OpenFiles"));
    for (const QString &file : openFiles) xml.writeTextElement(QStringLiteral("File"), file);
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("Imports"));
    for (const XprjImport &imp : imports) {
        xml.writeStartElement(XprjImport::kindName(imp.kind));
        xml.writeAttribute(QStringLiteral("enabled"), imp.enabled ? QStringLiteral("true") : QStringLiteral("false"));
        xml.writeCharacters(imp.path);
        xml.writeEndElement();
    }
    xml.writeEndElement();

    xml.writeEndElement();
    xml.writeEndDocument();
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

QString XprjProject::projectDir() const
{
    if (!projectPath.isEmpty() && QDir(projectPath).exists()) return QDir(projectPath).absolutePath();
    return QFileInfo(filePath).absolutePath();
}

QString XprjProject::absolutePath(const XprjImport &imp) const
{
    if (imp.path.isEmpty()) return {};
    return QFileInfo(imp.path).isAbsolute() ? imp.path : QDir(projectDir()).absoluteFilePath(imp.path);
}

const XprjImport *XprjProject::import(XprjImport::Kind kind) const
{
    for (const XprjImport &imp : imports)
        if (imp.kind == kind) return &imp;
    return nullptr;
}

QList<XprjImport> XprjProject::detectImports(const QString &dir)
{
    QList<XprjImport> out;
    const QDir d(dir);
    if (d.exists(QStringLiteral(".vscode")))
        out << XprjImport { XprjImport::VSCode, QStringLiteral(".vscode"), true };
    const QString user = ProjectImporters::findQtCreatorUserFile(dir);
    if (!user.isEmpty())
        out << XprjImport { XprjImport::QtCreator, d.relativeFilePath(user), true };
    if (d.exists(QStringLiteral(".clang-tidy")))
        out << XprjImport { XprjImport::ClangTidy, QStringLiteral(".clang-tidy"), true };
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Helpers
// ═════════════════════════════════════════════════════════════════════════════

namespace ProjectImporters {

QByteArray jsoncToJson(const QByteArray &in)
{
    // Drop // and /* */ comments outside strings, then trailing commas.
    QByteArray out;
    out.reserve(in.size());
    bool inStr = false;
    for (int i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (inStr) {
            out += c;
            if (c == '\\' && i + 1 < in.size()) { out += in[++i]; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; out += c; continue; }
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            while (i < in.size() && in[i] != '\n') ++i;
            out += '\n';
            continue;
        }
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {
            i += 2;
            while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) ++i;
            ++i;
            continue;
        }
        out += c;
    }
    // Trailing commas: "," followed only by whitespace, then } or ].
    QByteArray res;
    res.reserve(out.size());
    inStr = false;
    for (int i = 0; i < out.size(); ++i) {
        const char c = out[i];
        if (inStr) {
            res += c;
            if (c == '\\' && i + 1 < out.size()) { res += out[++i]; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; res += c; continue; }
        if (c == ',') {
            int j = i + 1;
            while (j < out.size() && QChar(out[j]).isSpace()) ++j;
            if (j < out.size() && (out[j] == '}' || out[j] == ']')) continue;
        }
        res += c;
    }
    return res;
}

} // namespace ProjectImporters

namespace {

// "3 tasks" / "1 task" - summary lines are plain English.
QString plural(int n, const char *one, const char *many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : many));
}

QJsonObject readJsonc(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return {};
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(ProjectImporters::jsoncToJson(f.readAll()), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), pe.errorString());
        return {};
    }
    return doc.object();
}

QString platformKey()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("osx");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

// A task / launch configuration with its "osx" / "linux" / "windows" part applied.
QJsonObject forPlatform(QJsonObject o)
{
    const QJsonObject p = o.value(platformKey()).toObject();
    for (auto it = p.begin(); it != p.end(); ++it) {
        if (it.value().isObject() && o.value(it.key()).isObject()) {
            QJsonObject merged = o.value(it.key()).toObject();
            const QJsonObject over = it.value().toObject();
            for (auto j = over.begin(); j != over.end(); ++j) merged.insert(j.key(), j.value());
            o.insert(it.key(), merged);
        } else {
            o.insert(it.key(), it.value());
        }
    }
    return o;
}

QString shellQuote(const QString &s)
{
    if (s.isEmpty()) return QStringLiteral("''");
    // ${ProjectDir} etc. become real paths before the shell runs, so anything
    // containing them is double-quoted (paths may have spaces).
    static const QRegularExpression safe(QStringLiteral(R"(^[A-Za-z0-9_@%+=:,./-]+$)"));
    if (safe.match(s).hasMatch()) return s;
    QString q = s;
    q.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + q + QLatin1Char('"');   // double quotes keep ${ProjectDir} working
}

// VS Code's ${...} variables -> KayteIDE's (BuildConfigurations::expand) or shell.
QString vsVars(QString s, const QString &cmakeBuildDir, QStringList *notes)
{
    static const QList<QPair<QString, QString>> map = {
        { QStringLiteral("${workspaceFolder}"),          QStringLiteral("${ProjectDir}") },
        { QStringLiteral("${workspaceRoot}"),            QStringLiteral("${ProjectDir}") },
        { QStringLiteral("${cwd}"),                      QStringLiteral("${ProjectDir}") },
        { QStringLiteral("${workspaceFolderBasename}"),  QStringLiteral("${ProjectName}") },
        { QStringLiteral("${fileBasenameNoExtension}"),  QStringLiteral("${FileBaseName}") },
        { QStringLiteral("${fileBasename}"),             QStringLiteral("$(basename \"${File}\")") },
        { QStringLiteral("${fileDirname}"),              QStringLiteral("$(dirname \"${File}\")") },
        { QStringLiteral("${relativeFile}"),             QStringLiteral("${File}") },
        { QStringLiteral("${file}"),                     QStringLiteral("${File}") },
        { QStringLiteral("${userHome}"),                 QStringLiteral("${HOME}") },
        { QStringLiteral("${pathSeparator}"),            QStringLiteral("/") },
        { QStringLiteral("${command:cmake.buildDirectory}"), QStringLiteral("${CMakeBuildDir}") },
        { QStringLiteral("${config:cmake.buildDirectory}"),  QStringLiteral("${CMakeBuildDir}") },
    };
    for (const auto &[from, to] : map) s.replace(from, to);
    s.replace(QStringLiteral("${CMakeBuildDir}"), cmakeBuildDir);
    // ${env:NAME} -> ${NAME}: the shell expands it.
    static const QRegularExpression env(QStringLiteral(R"(\$\{env:([A-Za-z_][A-Za-z0-9_]*)\})"));
    s.replace(env, QStringLiteral("${\\1}"));
    static const QRegularExpression other(QStringLiteral(R"(\$\{(input|command|config):[^}]*\})"));
    for (auto it = other.globalMatch(s); it.hasNext();) {
        const QString v = it.next().captured(0);
        if (notes && !notes->contains(v)) *notes << QStringLiteral("variable %1 has no KayteIDE equivalent - left as is").arg(v);
    }
    return s;
}

QString joinArgs(const QJsonArray &args, const QString &cmakeBuildDir, QStringList *notes)
{
    QStringList out;
    for (const QJsonValue &a : args) {
        const QString v = a.isObject() ? a.toObject().value(QStringLiteral("value")).toString() : a.toVariant().toString();
        out << shellQuote(vsVars(v, cmakeBuildDir, notes));
    }
    return out.join(QLatin1Char(' '));
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
//  VS Code
// ═════════════════════════════════════════════════════════════════════════════

ProjectImport ProjectImporters::readVSCode(const QString &projectDir, const QString &vscodeDir)
{
    ProjectImport r;
    r.kind = XprjImport::VSCode;
    r.path = vscodeDir;
    const QString origin = XprjImport::label(XprjImport::VSCode);
    if (!QDir(vscodeDir).exists()) {
        r.error = QStringLiteral("%1 does not exist").arg(vscodeDir);
        return r;
    }
    const QDir d(vscodeDir);
    QString err;

    // settings.json - editor and CMake Tools settings.
    QString cmakeBuildDir = QStringLiteral("${ProjectDir}/build");
    if (d.exists(QStringLiteral("settings.json"))) {
        const QJsonObject s = readJsonc(d.filePath(QStringLiteral("settings.json")), &err);
        if (!err.isEmpty()) { r.notes << err; err.clear(); }
        if (s.contains(QStringLiteral("editor.tabSize"))) {
            r.editor.tabSize = s.value(QStringLiteral("editor.tabSize")).toInt();
            r.summary << QStringLiteral("settings.json: tab size %1").arg(r.editor.tabSize);
        }
        if (s.contains(QStringLiteral("editor.insertSpaces"))) {
            r.editor.insertSpacesSet = true;
            r.editor.insertSpaces = s.value(QStringLiteral("editor.insertSpaces")).toBool();
            r.summary << QStringLiteral("settings.json: indent with %1").arg(r.editor.insertSpaces ? "spaces" : "tabs");
        }
        const QJsonObject ex = s.value(QStringLiteral("files.exclude")).toObject();
        for (auto it = ex.begin(); it != ex.end(); ++it)
            if (it.value().toBool()) r.editor.excludeGlobs << it.key();
        if (!r.editor.excludeGlobs.isEmpty())
            r.summary << QStringLiteral("settings.json: ") + plural(int(r.editor.excludeGlobs.size()), "excluded file pattern", "excluded file patterns");
        if (s.contains(QStringLiteral("cmake.buildDirectory")))
            cmakeBuildDir = vsVars(s.value(QStringLiteral("cmake.buildDirectory")).toString(), QString(), &r.notes);
    }

    // c_cpp_properties.json - the configuration for this platform.
    if (d.exists(QStringLiteral("c_cpp_properties.json"))) {
        const QJsonObject c = readJsonc(d.filePath(QStringLiteral("c_cpp_properties.json")), &err);
        if (!err.isEmpty()) { r.notes << err; err.clear(); }
        const QJsonArray cfgs = c.value(QStringLiteral("configurations")).toArray();
#if defined(Q_OS_MACOS)
        const QStringList wanted { QStringLiteral("Mac"), QStringLiteral("macOS") };
#elif defined(Q_OS_WIN)
        const QStringList wanted { QStringLiteral("Win32"), QStringLiteral("Windows") };
#else
        const QStringList wanted { QStringLiteral("Linux") };
#endif
        QJsonObject pick = cfgs.isEmpty() ? QJsonObject() : cfgs.first().toObject();
        for (const QJsonValue &v : cfgs)
            if (wanted.contains(v.toObject().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive))
                pick = v.toObject();
        for (const QJsonValue &v : pick.value(QStringLiteral("includePath")).toArray()) {
            QString p = v.toString();
            if (p.endsWith(QLatin1String("/**"))) p.chop(3);   // VS Code's recursive marker
            r.includePaths << vsVars(p, cmakeBuildDir, nullptr);
        }
        for (const QJsonValue &v : pick.value(QStringLiteral("defines")).toArray()) r.defines << v.toString();
        r.cppStandard = pick.value(QStringLiteral("cppStandard")).toString();
        if (!pick.isEmpty())
            r.summary << QStringLiteral("c_cpp_properties.json: \"%1\" - %2 include path(s), %3 define(s)%4")
                             .arg(pick.value(QStringLiteral("name")).toString())
                             .arg(r.includePaths.size()).arg(r.defines.size())
                             .arg(r.cppStandard.isEmpty() ? QString() : QStringLiteral(", ") + r.cppStandard);
    }

    // tasks.json - by label; dependsOn chains run first.
    QHash<QString, QJsonObject> tasks;
    QStringList taskOrder, buildTasks;
    QString defaultBuild, cleanTask;
    if (d.exists(QStringLiteral("tasks.json"))) {
        const QJsonObject t = readJsonc(d.filePath(QStringLiteral("tasks.json")), &err);
        if (!err.isEmpty()) { r.notes << err; err.clear(); }
        for (const QJsonValue &v : t.value(QStringLiteral("tasks")).toArray()) {
            const QJsonObject task = forPlatform(v.toObject());
            const QString label = task.value(QStringLiteral("label")).toString();
            if (label.isEmpty()) continue;
            tasks.insert(label, task);
            taskOrder << label;
            const QJsonValue g = task.value(QStringLiteral("group"));
            const QString kind = g.isObject() ? g.toObject().value(QStringLiteral("kind")).toString() : g.toString();
            if (kind == QLatin1String("build")) {
                buildTasks << label;
                if (g.isObject() && g.toObject().value(QStringLiteral("isDefault")).toBool()) defaultBuild = label;
            }
            if (cleanTask.isEmpty() && label.contains(QLatin1String("clean"), Qt::CaseInsensitive) &&
                !label.contains(QLatin1String("build"), Qt::CaseInsensitive))
                cleanTask = label;
        }
        r.summary << QStringLiteral("tasks.json: ") + plural(int(taskOrder.size()), "task", "tasks")
                         + (defaultBuild.isEmpty() ? QString() : QStringLiteral(", default build \"%1\"").arg(defaultBuild));
    }

    // One task as a shell command (with its dependencies first).
    std::function<QString(const QString &, int)> taskCommand = [&](const QString &label, int depth) -> QString {
        if (!tasks.contains(label) || depth > 8) return {};
        const QJsonObject task = tasks.value(label);
        QStringList parts;
        const QJsonValue deps = task.value(QStringLiteral("dependsOn"));
        QStringList depList;
        if (deps.isString()) depList << deps.toString();
        for (const QJsonValue &dv : deps.toArray()) depList << (dv.isObject() ? dv.toObject().value(QStringLiteral("label")).toString() : dv.toString());
        for (const QString &dep : depList) {
            const QString c = taskCommand(dep, depth + 1);
            if (!c.isEmpty()) parts << c;
        }

        const QString type = task.value(QStringLiteral("type")).toString(QStringLiteral("shell"));
        QString cmd;
        if (type == QLatin1String("shell") || type == QLatin1String("process")) {
            const QJsonValue c = task.value(QStringLiteral("command"));
            cmd = c.isArray() ? joinArgs(c.toArray(), cmakeBuildDir, &r.notes)
                              : vsVars(c.toString(), cmakeBuildDir, &r.notes);
            const QString args = joinArgs(task.value(QStringLiteral("args")).toArray(), cmakeBuildDir, &r.notes);
            if (!args.isEmpty()) cmd += QLatin1Char(' ') + args;
        } else if (type == QLatin1String("cmake")) {            // CMake Tools task
            const QString what = task.value(QStringLiteral("command")).toString(QStringLiteral("build"));
            if (what == QLatin1String("configure"))
                cmd = QStringLiteral("cmake -S \"${ProjectDir}\" -B \"%1\"").arg(cmakeBuildDir);
            else if (what == QLatin1String("clean"))
                cmd = QStringLiteral("cmake --build \"%1\" --target clean").arg(cmakeBuildDir);
            else if (what == QLatin1String("install"))
                cmd = QStringLiteral("cmake --install \"%1\"").arg(cmakeBuildDir);
            else {
                cmd = QStringLiteral("cmake --build \"%1\"").arg(cmakeBuildDir);
                for (const QJsonValue &tv : task.value(QStringLiteral("targets")).toArray())
                    cmd += QStringLiteral(" --target ") + shellQuote(tv.toString());
            }
        } else if (type == QLatin1String("npm")) {
            cmd = QStringLiteral("npm run ") + shellQuote(task.value(QStringLiteral("script")).toString());
        } else {
            r.notes << QStringLiteral("task \"%1\": type \"%2\" is run by a VS Code extension - skipped").arg(label, type);
        }
        if (!cmd.isEmpty()) {
            const QJsonObject opts = task.value(QStringLiteral("options")).toObject();
            QStringList env;
            const QJsonObject e = opts.value(QStringLiteral("env")).toObject();
            for (auto it = e.begin(); it != e.end(); ++it)
                env << it.key() + QLatin1Char('=') + shellQuote(vsVars(it.value().toString(), cmakeBuildDir, &r.notes));
            if (!env.isEmpty()) cmd = QStringLiteral("env ") + env.join(QLatin1Char(' ')) + QLatin1Char(' ') + cmd;
            const QString cwd = opts.value(QStringLiteral("cwd")).toString();
            if (!cwd.isEmpty())
                cmd = QStringLiteral("(cd \"%1\" && %2)").arg(vsVars(cwd, cmakeBuildDir, &r.notes), cmd);
            parts << cmd;
        }
        return parts.join(QStringLiteral(" && "));
    };

    const QString cleanCmd = taskCommand(cleanTask, 0);

    // launch.json - each C/C++ launch configuration becomes a build configuration.
    if (d.exists(QStringLiteral("launch.json"))) {
        const QJsonObject l = readJsonc(d.filePath(QStringLiteral("launch.json")), &err);
        if (!err.isEmpty()) { r.notes << err; err.clear(); }
        int count = 0;
        for (const QJsonValue &v : l.value(QStringLiteral("configurations")).toArray()) {
            const QJsonObject c = forPlatform(v.toObject());
            const QString type = c.value(QStringLiteral("type")).toString();
            const QString name = c.value(QStringLiteral("name")).toString();
            if (c.value(QStringLiteral("request")).toString() == QLatin1String("attach")) {
                r.notes << QStringLiteral("launch \"%1\": attaching to a process is not supported - skipped").arg(name);
                continue;
            }
            QString program = vsVars(c.value(QStringLiteral("program")).toString(), cmakeBuildDir, &r.notes);
            if (program.isEmpty() && c.contains(QStringLiteral("cargo"))) program = QStringLiteral("cargo run");
            QString runner;
            if (type == QLatin1String("python") || type == QLatin1String("debugpy")) runner = QStringLiteral("python3 ");
            else if (type == QLatin1String("node") || type == QLatin1String("pwa-node")) runner = QStringLiteral("node ");
            else if (!(type == QLatin1String("cppdbg") || type == QLatin1String("lldb") || type == QLatin1String("cppvsdbg") ||
                       type == QLatin1String("gdb") || type == QLatin1String("lldb-dap"))) {
                r.notes << QStringLiteral("launch \"%1\": debugger type \"%2\" is not supported - skipped").arg(name, type);
                continue;
            }
            if (program.isEmpty()) {
                r.notes << QStringLiteral("launch \"%1\": no program - skipped").arg(name);
                continue;
            }
            const QString args = joinArgs(c.value(QStringLiteral("args")).toArray(), cmakeBuildDir, &r.notes);
            QStringList env;
            for (const QJsonValue &ev : c.value(QStringLiteral("environment")).toArray())   // cppdbg
                env << ev.toObject().value(QStringLiteral("name")).toString() + QLatin1Char('=') +
                           shellQuote(vsVars(ev.toObject().value(QStringLiteral("value")).toString(), cmakeBuildDir, &r.notes));
            const QJsonObject envObj = c.value(QStringLiteral("env")).toObject();          // lldb / python / node
            for (auto it = envObj.begin(); it != envObj.end(); ++it)
                env << it.key() + QLatin1Char('=') + shellQuote(vsVars(it.value().toString(), cmakeBuildDir, &r.notes));
            const QString envPrefix = env.isEmpty() ? QString() : QStringLiteral("env ") + env.join(QLatin1Char(' ')) + QLatin1Char(' ');
            const QString cwd = vsVars(c.value(QStringLiteral("cwd")).toString(), cmakeBuildDir, &r.notes);
            const QString exe = runner + shellQuote(program) + (args.isEmpty() ? QString() : QLatin1Char(' ') + args);
            const auto inCwd = [&cwd](const QString &cmd) {
                return cwd.isEmpty() ? cmd : QStringLiteral("cd \"%1\" && %2").arg(cwd, cmd);
            };

            BuildConfiguration bc;
            bc.name = origin + QStringLiteral(": ") + name;
            bc.origin = origin;
            bc.buildDir = QStringLiteral(".");
            const QString pre = c.value(QStringLiteral("preLaunchTask")).toString();
            bc.build = taskCommand(pre.isEmpty() ? defaultBuild : pre, 0);
            bc.clean = cleanCmd;
            bc.run = inCwd(envPrefix + exe);
            const bool gdb = c.value(QStringLiteral("MIMode")).toString() == QLatin1String("gdb") || type == QLatin1String("gdb");
            if (runner.isEmpty())
                bc.debug = inCwd(envPrefix + (gdb ? QStringLiteral("gdb --args ") : QStringLiteral("lldb -- ")) + exe);
            else
                bc.debug = bc.run;
            bc.buildBeforeRun = !bc.build.isEmpty();
            r.configs << bc;
            ++count;
        }
        r.summary << QStringLiteral("launch.json: ") + plural(count, "launch configuration", "launch configurations") + QStringLiteral(" imported");
    }

    // No launch configurations: each build task becomes a configuration.
    if (r.configs.isEmpty()) {
        for (const QString &label : buildTasks.isEmpty() ? taskOrder : buildTasks) {
            if (label == cleanTask) continue;
            BuildConfiguration bc;
            bc.name = origin + QStringLiteral(": ") + label;
            bc.origin = origin;
            bc.buildDir = QStringLiteral(".");
            bc.build = taskCommand(label, 0);
            bc.clean = cleanCmd;
            if (!bc.build.isEmpty()) r.configs << bc;
        }
        if (!defaultBuild.isEmpty()) r.preferredActive = origin + QStringLiteral(": ") + defaultBuild;
    } else {
        r.preferredActive = r.configs.first().name;
    }
    r.summary << QStringLiteral("-> ") + plural(int(r.configs.size()), "build configuration", "build configurations");
    r.ok = true;
    return r;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Qt Creator
// ═════════════════════════════════════════════════════════════════════════════

namespace {

// <valuemap> / <valuelist> / <value> -> QVariant
QVariant readQcValue(QXmlStreamReader &xml)
{
    const QString tag = xml.name().toString();
    if (tag == QLatin1String("valuemap")) {
        QVariantMap m;
        while (xml.readNextStartElement()) {
            const QString key = xml.attributes().value(QLatin1String("key")).toString();
            m.insert(key, readQcValue(xml));
        }
        return m;
    }
    if (tag == QLatin1String("valuelist")) {
        QVariantList l;
        while (xml.readNextStartElement()) l << readQcValue(xml);
        return l;
    }
    return xml.readElementText();
}

QString asciify(const QString &s)
{
    QString out;
    for (const QChar c : s)
        out += (c.isLetterOrNumber() && c.unicode() < 128) || c == QLatin1Char('-') || c == QLatin1Char('.') ? c : QLatin1Char('_');
    return out;
}

// A kit's file-system name as Qt Creator derives it from the display name:
// "Desktop (x86-darwin-generic-mach_o-32bit)" -> "Desktop_x86_darwin_generic_mach_o_32bit".
QString kitFileSystemName(const QString &kit)
{
    QString out;
    for (const QChar c : kit) {
        const bool keep = c.isLetterOrNumber() && c.unicode() < 128;
        if (keep) out += c;
        else if (!out.isEmpty() && !out.endsWith(QLatin1Char('_'))) out += QLatin1Char('_');
    }
    while (out.endsWith(QLatin1Char('_'))) out.chop(1);
    return out;
}

// Qt Creator's %{...} macros that matter for paths.
QString qcMacros(QString s, const QString &kit, const QString &config, const QString &project)
{
    s.replace(QLatin1String("%{Kit:FileSystemName}"), kitFileSystemName(kit));
    s.replace(QLatin1String("%{Kit:DisplayName}"), kit);
    s.replace(QLatin1String("%{BuildConfig:Name}"), config);
    s.replace(QLatin1String("%{Project:Name}"), project);
    s.replace(QLatin1String("%{CurrentProject:Name}"), project);
    s.replace(QLatin1String("%{JS: Util.asciify(\"build-%{Project:Name}-%{Kit:FileSystemName}-%{BuildConfig:Name}\")}"),
              asciify(QStringLiteral("build-%1-%2-%3").arg(project, kitFileSystemName(kit), config)));
    static const QRegularExpression asc(QStringLiteral(R"(%\{Asciify:([^{}]*)\})"));
    for (auto m = asc.match(s); m.hasMatch(); m = asc.match(s))
        s.replace(m.capturedStart(), m.capturedLength(), asciify(m.captured(1)));
    return s;
}

QVariantMap stepList(const QVariantMap &bc, int index)
{
    return bc.value(QStringLiteral("ProjectExplorer.BuildConfiguration.BuildStepList.%1").arg(index)).toMap();
}

QList<QVariantMap> steps(const QVariantMap &list)
{
    QList<QVariantMap> out;
    const int n = list.value(QStringLiteral("ProjectExplorer.BuildStepList.StepsCount")).toInt();
    for (int i = 0; i < n; ++i)
        out << list.value(QStringLiteral("ProjectExplorer.BuildStepList.Step.%1").arg(i)).toMap();
    return out;
}

} // namespace

QString ProjectImporters::findQtCreatorUserFile(const QString &dir)
{
    const QDir d(dir);
    QStringList candidates;
    for (const QString &sub : { QStringLiteral(".qtcreator"), QString() }) {
        const QDir base(sub.isEmpty() ? dir : d.filePath(sub));
        for (const QFileInfo &fi : base.entryInfoList({ QStringLiteral("CMakeLists.txt.user"), QStringLiteral("*.pro.user"),
                                                         QStringLiteral("*.qbs.user"), QStringLiteral("*.user") },
                                                       QDir::Files))
            candidates << fi.absoluteFilePath();
    }
    QString best;
    QDateTime newest;
    for (const QString &c : candidates) {
        const QDateTime m = QFileInfo(c).lastModified();
        if (best.isEmpty() || m > newest) { best = c; newest = m; }
    }
    return best;
}

ProjectImport ProjectImporters::readQtCreator(const QString &projectDir, const QString &pathOrDir)
{
    ProjectImport r;
    r.kind = XprjImport::QtCreator;
    const QString origin = XprjImport::label(XprjImport::QtCreator);
    const QString file = QFileInfo(pathOrDir).isDir() ? findQtCreatorUserFile(pathOrDir) : pathOrDir;
    r.path = file;
    QFile f(file);
    if (file.isEmpty() || !f.open(QIODevice::ReadOnly)) {
        r.error = file.isEmpty() ? QStringLiteral("no Qt Creator .user file in %1").arg(pathOrDir) : f.errorString();
        return r;
    }

    QVariantMap data;
    QXmlStreamReader xml(&f);
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("qtcreator")) {
        r.error = QStringLiteral("%1 is not a Qt Creator settings file").arg(QFileInfo(file).fileName());
        return r;
    }
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("data")) { xml.skipCurrentElement(); continue; }
        QString var;
        QVariant val;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("variable")) var = xml.readElementText();
            else val = readQcValue(xml);
        }
        data.insert(var, val);
    }
    if (xml.hasError()) {
        r.error = QStringLiteral("%1: %2").arg(QFileInfo(file).fileName(), xml.errorString());
        return r;
    }

    const QString project = QFileInfo(projectDir).fileName();
    const QString userName = QFileInfo(file).fileName();
    const bool isQmake = userName.endsWith(QLatin1String(".pro.user"));
    const QString proFile = isQmake ? QDir(projectDir).filePath(userName.chopped(5)) : QString();

    const QVariantMap editor = data.value(QStringLiteral("ProjectExplorer.Project.EditorSettings")).toMap();
    if (!editor.isEmpty() && !editor.value(QStringLiteral("EditorConfiguration.UseGlobal"), false).toBool() &&
        editor.contains(QStringLiteral("EditorConfiguration.TabSize"))) {
        r.editor.tabSize = editor.value(QStringLiteral("EditorConfiguration.TabSize")).toInt();
        r.editor.insertSpacesSet = editor.contains(QStringLiteral("EditorConfiguration.SpacesForTabs"));
        r.editor.insertSpaces = editor.value(QStringLiteral("EditorConfiguration.SpacesForTabs"), true).toBool();
        r.summary << QStringLiteral("editor: tab size %1, %2").arg(r.editor.tabSize).arg(r.editor.insertSpaces ? "spaces" : "tabs");
    }

    const int targetCount = data.value(QStringLiteral("ProjectExplorer.Project.TargetCount"), 1).toInt();
    const int activeTarget = data.value(QStringLiteral("ProjectExplorer.Project.ActiveTarget")).toInt();
    for (int t = 0; t < qMax(1, targetCount); ++t) {
        const QVariantMap target = data.value(QStringLiteral("ProjectExplorer.Project.Target.%1").arg(t)).toMap();
        if (target.isEmpty()) continue;
        const QString kit = target.value(QStringLiteral("ProjectExplorer.ProjectConfiguration.DisplayName")).toString();
        const int bcCount = target.value(QStringLiteral("ProjectExplorer.Target.BuildConfigurationCount")).toInt();
        const int activeBc = target.value(QStringLiteral("ProjectExplorer.Target.ActiveBuildConfiguration")).toInt();
        r.summary << QStringLiteral("kit \"%1\": ").arg(kit) + plural(bcCount, "build configuration", "build configurations");

        for (int b = 0; b < bcCount; ++b) {
            const QVariantMap bc = target.value(QStringLiteral("ProjectExplorer.Target.BuildConfiguration.%1").arg(b)).toMap();
            const QString bcName = bc.value(QStringLiteral("ProjectExplorer.ProjectConfiguration.DisplayName")).toString();
            QString buildDir = qcMacros(bc.value(QStringLiteral("ProjectExplorer.BuildConfiguration.BuildDirectory")).toString(),
                                        kit, bcName, project);
            if (QDir::isRelativePath(buildDir)) buildDir = QDir::cleanPath(QDir(projectDir).filePath(buildDir));
            // Not there? Use the build folder Qt Creator actually made for this
            // configuration ("...-Debug" next to where we looked), if there is one.
            if (!buildDir.contains(QLatin1String("%{")) && !QDir(buildDir).exists()) {
                const QDir parent = QFileInfo(buildDir).absoluteDir();
                const QStringList hits = parent.entryList({ QStringLiteral("*-") + bcName }, QDir::Dirs | QDir::NoDotAndDotDot);
                if (hits.size() == 1) buildDir = parent.filePath(hits.first());
            }
            if (buildDir.contains(QLatin1String("%{"))) {
                r.notes << QStringLiteral("%1: build directory %2 uses Qt Creator macros - using build/%1").arg(bcName, buildDir);
                buildDir = QDir(projectDir).filePath(QStringLiteral("build/") + asciify(bcName));
            }
            const QString bdRel = QDir(projectDir).relativeFilePath(buildDir);
            const QString bd = bdRel.startsWith(QLatin1String("..")) ? buildDir : QStringLiteral("${ProjectDir}/") + bdRel;

            auto stepCommand = [&](const QVariantMap &s) -> QString {
                const QString id = s.value(QStringLiteral("ProjectExplorer.ProjectConfiguration.Id")).toString();
                if (!s.value(QStringLiteral("ProjectExplorer.BuildStep.Enabled"), true).toBool()) return {};
                if (id == QLatin1String("CMakeProjectManager.MakeStep")) {
                    QString cmd = QStringLiteral("cmake --build \"%1\"").arg(bd);
                    for (const QVariant &tv : s.value(QStringLiteral("CMakeProjectManager.MakeStep.BuildTargets")).toList())
                        if (!tv.toString().isEmpty()) cmd += QStringLiteral(" --target ") + shellQuote(tv.toString());
                    const QString extra = s.value(QStringLiteral("CMakeProjectManager.MakeStep.AdditionalArguments")).toString();
                    if (!extra.isEmpty()) cmd += QLatin1Char(' ') + extra;
                    return cmd;
                }
                if (id == QLatin1String("QtProjectManager.QMakeBuildStep")) {
                    const QString args = s.value(QStringLiteral("QtProjectManager.QMakeBuildStep.QMakeArguments")).toString();
                    return QStringLiteral("mkdir -p \"%1\" && (cd \"%1\" && qmake \"%2\"%3)")
                        .arg(bd, proFile, args.isEmpty() ? QString() : QLatin1Char(' ') + args);
                }
                if (id == QLatin1String("Qt4ProjectManager.MakeStep") || id == QLatin1String("GenericProjectManager.GenericMakeStep")) {
                    const QString args = s.value(QStringLiteral("Qt4ProjectManager.MakeStep.MakeArguments")).toString() +
                                         s.value(QStringLiteral("GenericProjectManager.GenericMakeStep.MakeArguments")).toString();
                    return QStringLiteral("make -C \"%1\"%2").arg(bd, args.isEmpty() ? QString() : QLatin1Char(' ') + args);
                }
                if (id == QLatin1String("ProjectExplorer.ProcessStep")) {
                    QString cmd = s.value(QStringLiteral("ProjectExplorer.ProcessStep.Command")).toString();
                    const QString args = s.value(QStringLiteral("ProjectExplorer.ProcessStep.Arguments")).toString();
                    QString wd = s.value(QStringLiteral("ProjectExplorer.ProcessStep.WorkingDirectory")).toString();
                    wd.replace(QLatin1String("%{buildDir}"), bd).replace(QLatin1String("%{sourceDir}"), QStringLiteral("${ProjectDir}"));
                    cmd = qcMacros(cmd, kit, bcName, project) + (args.isEmpty() ? QString() : QLatin1Char(' ') + args);
                    cmd.replace(QLatin1String("%{buildDir}"), bd).replace(QLatin1String("%{sourceDir}"), QStringLiteral("${ProjectDir}"));
                    return wd.isEmpty() ? cmd : QStringLiteral("(cd \"%1\" && %2)").arg(wd, cmd);
                }
                if (!id.isEmpty())
                    r.notes << QStringLiteral("%1: build step %2 is specific to Qt Creator - skipped").arg(bcName, id);
                return {};
            };

            QStringList build, clean;
            for (const QVariantMap &s : steps(stepList(bc, 0))) if (const QString c = stepCommand(s); !c.isEmpty()) build << c;
            for (const QVariantMap &s : steps(stepList(bc, 1))) if (const QString c = stepCommand(s); !c.isEmpty()) clean << c;

            // CMake: Qt Creator configures by itself before building - do it when needed.
            if (bc.value(QStringLiteral("ProjectExplorer.ProjectConfiguration.Id")).toString()
                    .startsWith(QLatin1String("CMakeProjectManager"))) {
                QStringList params;
                for (const QString &line : bc.value(QStringLiteral("CMake.Initial.Parameters")).toString().split(QLatin1Char('\n')))
                    if (!line.trimmed().isEmpty() && !line.contains(QLatin1String("%{")))
                        params << shellQuote(line.trimmed());
                const QString type = bc.value(QStringLiteral("CMake.Build.Type")).toString();
                if (!type.isEmpty() && !params.join(QLatin1Char(' ')).contains(QLatin1String("CMAKE_BUILD_TYPE")))
                    params << QStringLiteral("-DCMAKE_BUILD_TYPE=") + type;
                build.prepend(QStringLiteral("(test -f \"%1/CMakeCache.txt\" || cmake -S \"${ProjectDir}\" -B \"%1\" %2)")
                                  .arg(bd, params.join(QLatin1Char(' '))));
                if (build.size() == 1) build << QStringLiteral("cmake --build \"%1\"").arg(bd);
            }

            // Run configuration: nested in the build configuration (Qt Creator 13+) or per target.
            QVariantMap rc;
            for (const QVariantMap &holder : { bc, target }) {
                const int active = holder.value(QStringLiteral("ProjectExplorer.Target.ActiveRunConfiguration")).toInt();
                rc = holder.value(QStringLiteral("ProjectExplorer.Target.RunConfiguration.%1").arg(active)).toMap();
                if (!rc.isEmpty()) break;
            }
            QString run, debug;
            if (!rc.isEmpty()) {
                const QString rcId = rc.value(QStringLiteral("ProjectExplorer.ProjectConfiguration.Id")).toString();
                QString exe = rc.value(QStringLiteral("ProjectExplorer.CustomExecutableRunConfiguration.Executable")).toString();
                QString buildKey = rc.value(QStringLiteral("ProjectExplorer.RunConfiguration.BuildKey")).toString();
                if (isQmake && buildKey.endsWith(QLatin1String(".pro"))) buildKey = QFileInfo(buildKey).completeBaseName();
                QString exeCmd;
                if (!exe.isEmpty()) {
                    exeCmd = shellQuote(qcMacros(exe, kit, bcName, project).replace(QLatin1String("%{buildDir}"), bd));
                } else if (!buildKey.isEmpty()) {
                    // The target's executable (a plain binary or inside a macOS .app), found at run time.
                    exeCmd = QStringLiteral("\"$(find \"%1\" -type f -perm -u+x -name %2 -not -path '*/CMakeFiles/*' | head -1)\"")
                                 .arg(bd, shellQuote(buildKey));
                } else {
                    r.notes << QStringLiteral("%1: run configuration %2 has no executable - no run command").arg(bcName, rcId);
                }
                if (!exeCmd.isEmpty()) {
                    const QString args = rc.value(QStringLiteral("RunConfiguration.Arguments")).toString();
                    const QString cmd = exeCmd + (args.isEmpty() ? QString() : QLatin1Char(' ') + args);
                    QString wd = rc.value(QStringLiteral("RunConfiguration.WorkingDirectory")).toString();
                    if (wd.contains(QLatin1String("%{"))) wd.clear();
                    const auto inWd = [&wd](const QString &c) { return wd.isEmpty() ? c : QStringLiteral("cd \"%1\" && %2").arg(wd, c); };
                    run = inWd(cmd);
#if defined(Q_OS_MACOS)
                    debug = inWd(QStringLiteral("lldb -- ") + cmd);
#else
                    debug = inWd(QStringLiteral("gdb --args ") + cmd);
#endif
                }
            }

            BuildConfiguration out;
            out.name = origin + QStringLiteral(": ") + bcName + (targetCount > 1 ? QStringLiteral(" (%1)").arg(kit) : QString());
            out.origin = origin;
            out.buildDir = buildDir;
            out.build = build.join(QStringLiteral(" && "));
            out.clean = clean.join(QStringLiteral(" && "));
            out.run = run;
            out.debug = debug;
            out.buildBeforeRun = !out.build.isEmpty();
            r.configs << out;
            if (t == activeTarget && b == activeBc) r.preferredActive = out.name;
        }
    }
    r.summary << QStringLiteral("-> ") + plural(int(r.configs.size()), "build configuration", "build configurations");
    r.ok = true;
    return r;
}

// ═════════════════════════════════════════════════════════════════════════════
//  clang-tidy
// ═════════════════════════════════════════════════════════════════════════════

ClangTidyConfig ProjectImporters::readClangTidy(const QString &file)
{
    ClangTidyConfig c;
    c.path = file;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        c.error = f.errorString();
        return c;
    }
    // A small reader for the keys clang-tidy configs use: a top-level key's
    // value runs until the next top-level key (YAML block or quoted scalar).
    QMap<QString, QString> values;
    QString key;
    static const QRegularExpression top(QStringLiteral(R"(^([A-Za-z][A-Za-z0-9]*):\s*(.*)$)"));
    for (const QString &raw : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        if (raw.trimmed().startsWith(QLatin1Char('#')) || raw.trimmed() == QLatin1String("---") ||
            raw.trimmed() == QLatin1String("...")) continue;
        const auto m = top.match(raw);
        if (m.hasMatch() && !raw.startsWith(QLatin1Char(' '))) {
            key = m.captured(1);
            values[key] = m.captured(2);
        } else if (!key.isEmpty()) {
            values[key] += QLatin1Char('\n') + raw;
        }
    }
    auto scalar = [](QString v) {
        v = v.trimmed();
        if (v.startsWith(QLatin1Char('>')) || v.startsWith(QLatin1Char('|'))) v = v.mid(1);
        v = v.trimmed();
        if (v.size() >= 2 && (v.startsWith(QLatin1Char('\'')) || v.startsWith(QLatin1Char('"')))) v = v.mid(1, v.size() - 2);
        return v;
    };
    auto list = [&](const QString &v) {
        QStringList out;
        for (QString s : scalar(v).split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts)) {
            s.remove(QLatin1Char('\'')).remove(QLatin1Char('"'));
            if (!s.isEmpty()) out << s;
        }
        return out;
    };
    c.checks = list(values.value(QStringLiteral("Checks")));
    c.warningsAsErrors = list(values.value(QStringLiteral("WarningsAsErrors")));
    c.headerFilter = scalar(values.value(QStringLiteral("HeaderFilterRegex")));
    c.inheritParent = scalar(values.value(QStringLiteral("InheritParentConfig"))) == QLatin1String("true");

    int enabled = 0, disabled = 0;
    for (const QString &ch : c.checks) (ch.startsWith(QLatin1Char('-')) ? disabled : enabled)++;
    c.summary << QStringLiteral("%1 check pattern(s) enabled, %2 disabled").arg(enabled).arg(disabled);
    if (!c.warningsAsErrors.isEmpty())
        c.summary << QStringLiteral("warnings as errors: %1").arg(c.warningsAsErrors.join(QStringLiteral(", ")));
    if (!c.headerFilter.isEmpty()) c.summary << QStringLiteral("header filter: %1").arg(c.headerFilter);
    if (values.contains(QStringLiteral("CheckOptions"))) c.summary << QStringLiteral("check options: kept in the file");
    c.ok = true;
    return c;
}

QString ProjectImporters::findClangTidy()
{
    QString exe = QStandardPaths::findExecutable(QStringLiteral("clang-tidy"));
    if (!exe.isEmpty()) return exe;
    for (const QString &p : { QStringLiteral("/opt/homebrew/opt/llvm/bin/clang-tidy"),
                              QStringLiteral("/usr/local/opt/llvm/bin/clang-tidy"),
                              QStringLiteral("/usr/bin/clang-tidy") })
        if (QFileInfo(p).isExecutable()) return p;
#if defined(Q_OS_MACOS)
    QProcess xcrun;
    xcrun.start(QStringLiteral("xcrun"), { QStringLiteral("--find"), QStringLiteral("clang-tidy") });
    if (xcrun.waitForFinished(3000) && xcrun.exitCode() == 0) {
        exe = QString::fromLocal8Bit(xcrun.readAllStandardOutput()).trimmed();
        if (QFileInfo(exe).isExecutable()) return exe;
    }
#endif
    return {};
}

QString ProjectImporters::findCompileCommands(const QString &projectDir, const QString &buildDir)
{
    QStringList dirs { buildDir, projectDir, QDir(projectDir).filePath(QStringLiteral("build")) };
    const QDir build(QDir(projectDir).filePath(QStringLiteral("build")));
    for (const QString &sub : build.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) dirs << build.filePath(sub);
    for (const QString &d : dirs)
        if (!d.isEmpty() && QFileInfo::exists(QDir(d).filePath(QStringLiteral("compile_commands.json"))))
            return QDir(d).filePath(QStringLiteral("compile_commands.json"));
    return {};
}

QStringList ProjectImporters::compileCommandsFiles(const QString &json, const QString &projectDir)
{
    if (json.isEmpty()) return {};
    QFile f(json);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList out;
    const QString root = QDir(projectDir).canonicalPath() + QLatin1Char('/');
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
        const QJsonObject o = v.toObject();
        QString file = o.value(QStringLiteral("file")).toString();
        if (QDir::isRelativePath(file)) file = QDir(o.value(QStringLiteral("directory")).toString()).filePath(file);
        file = QFileInfo(file).canonicalFilePath();
        if (!file.isEmpty() && file.startsWith(root) && !file.contains(QLatin1String("/_deps/")) &&
            !file.contains(QLatin1String("_autogen/")) && !out.contains(file))
            out << file;
    }
    return out;
}

QString ProjectImporters::clangTidyCommand(const ClangTidyConfig &config, const QString &exe,
                                           const QString &compileCommandsDir, const QStringList &files,
                                           const QStringList &fallbackFlags)
{
    QStringList cmd { shellQuote(exe) };
    if (config.ok && !config.path.isEmpty()) cmd << QStringLiteral("--config-file=") + shellQuote(config.path);
    if (!compileCommandsDir.isEmpty()) cmd << QStringLiteral("-p") << shellQuote(compileCommandsDir);
    for (const QString &f : files) cmd << shellQuote(f);
    if (compileCommandsDir.isEmpty()) {
        cmd << QStringLiteral("--");
        for (const QString &flag : fallbackFlags) cmd << shellQuote(flag);
    }
    return cmd.join(QLatin1Char(' '));
}
