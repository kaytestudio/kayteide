#include "codesymbols.h"

#include <QFileInfo>
#include <algorithm>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {

using Kind = CodeSymbol::Kind;

// Languages ending a symbol at the next one of the same or lower level
// (everything but the brace languages) get their end lines here.
void closeByLevel(QVector<CodeSymbol> &symbols, int lastLine)
{
    for (int i = 0; i < symbols.size(); ++i) {
        symbols[i].endLine = lastLine;
        for (int j = i + 1; j < symbols.size(); ++j) {
            if (symbols[j].level <= symbols[i].level) {
                symbols[i].endLine = qMax(symbols[i].line, symbols[j].line - 1);
                break;
            }
        }
    }
}

// ── C, C++, Objective-C, JavaScript ─────────────────────────────────────────
// Tracks braces (outside strings and comments) so a symbol ends where its
// body closes. Functions are only looked for at file, namespace or class
// scope, which keeps calls and control statements out.
QVector<CodeSymbol> scanBraces(const QStringList &lines, bool js)
{
    static const QSet<QString> notFunctions = {
        "if", "for", "while", "switch", "return", "else", "do", "case", "catch", "sizeof",
        "new", "delete", "throw", "using", "typedef", "emit", "Q_EMIT", "foreach", "Q_FOREACH",
        "defined", "alignof", "decltype", "static_assert", "Q_ASSERT", "Q_UNUSED",
    };
    static const QRegularExpression typeRe(QStringLiteral(
        R"(^\s*(?:template\s*<.*>\s*)?(?:export\s+)?(class|struct|union|namespace|enum(?:\s+class)?|interface)\b)"
        R"((?:\s+(?:Q_DECL_EXPORT|Q_DECL_IMPORT|[A-Z_]+_EXPORT|alignas\([^)]*\)))*\s*(\w*))"));
    static const QRegularExpression cFunctionRe(QStringLiteral(
        R"(^\s*(?:[\w:<>,\*&~\[\]\s]*?[\s\*&>])?((?:\w+::)*~?\w+|operator\s*\S+)\s*\()"));
    static const QRegularExpression jsFunctionRe(QStringLiteral(
        R"(^\s*(?:export\s+)?(?:default\s+)?(?:async\s+)?function\s*\*?\s*(\w+)\s*\()"));
    static const QRegularExpression jsMethodRe(QStringLiteral(
        R"(^\s*(?:static\s+|async\s+|get\s+|set\s+)*(\w+)\s*\([^)]*\)\s*\{)"));

    QVector<CodeSymbol> out;
    struct Open { int symbol; bool scope; };   // scope: class / namespace body
    QVector<Open> stack;
    int pending = -1;          // symbol whose '{' hasn't come yet
    int pendingLine = 0;
    bool inBlockComment = false;

    for (int n = 0; n < lines.size(); ++n) {
        const QString &raw = lines[n];

        // Code with strings, chars and comments blanked out.
        QString code;
        code.reserve(raw.size());
        for (int i = 0; i < raw.size(); ++i) {
            const QChar c = raw[i];
            if (inBlockComment) {
                if (c == QLatin1Char('*') && i + 1 < raw.size() && raw[i + 1] == QLatin1Char('/')) {
                    inBlockComment = false;
                    ++i;
                }
                continue;
            }
            if (c == QLatin1Char('/') && i + 1 < raw.size() && raw[i + 1] == QLatin1Char('/')) break;
            if (c == QLatin1Char('/') && i + 1 < raw.size() && raw[i + 1] == QLatin1Char('*')) {
                inBlockComment = true;
                ++i;
                continue;
            }
            if (c == QLatin1Char('"') || c == QLatin1Char('\'') || (js && c == QLatin1Char('`'))) {
                int j = i + 1;
                while (j < raw.size() && raw[j] != c) j += raw[j] == QLatin1Char('\\') ? 2 : 1;
                code += QStringLiteral("\"\"");
                i = j;
                continue;
            }
            code += c;
        }
        const QString trimmed = code.trimmed();
        if (trimmed.startsWith(QLatin1Char('#'))) continue;   // preprocessor

        const bool atScope = stack.isEmpty() || stack.last().scope;
        // A constructor's initializer list keeps its '{' coming.
        if (pending >= 0 && (trimmed.startsWith(QLatin1Char(':')) || trimmed.startsWith(QLatin1Char(','))))
            pendingLine = n;
        if (pending >= 0 && n - pendingLine > 3) pending = -1;   // never opened

        if (atScope && !trimmed.isEmpty() && !trimmed.endsWith(QLatin1Char(';'))) {
            int found = -1;
            if (const auto m = typeRe.match(code); m.hasMatch()) {
                const QString word = m.captured(1);
                CodeSymbol s;
                s.kind = word == QLatin1String("namespace") ? Kind::Namespace : Kind::Class;
                s.name = m.captured(2).isEmpty() ? QStringLiteral("(anonymous)") : m.captured(2);
                if (word.startsWith(QLatin1String("enum"))) s.name = QStringLiteral("enum ") + s.name;
                s.line = n + 1;
                out << s;
                found = int(out.size()) - 1;
            } else if (js) {
                auto m = jsFunctionRe.match(code);
                if (!m.hasMatch() && !stack.isEmpty()) m = jsMethodRe.match(code);
                if (m.hasMatch() && !notFunctions.contains(m.captured(1))) {
                    out << CodeSymbol{m.captured(1), Kind::Function, n + 1, 0, 0};
                    found = int(out.size()) - 1;
                }
            } else if (code.contains(QLatin1Char('('))) {
                const auto m = cFunctionRe.match(code);
                const QString name = m.captured(1);
                const QString bare = name.section(QStringLiteral("::"), -1);
                const QString first = trimmed.section(QRegularExpression(QStringLiteral("[\\s(]")), 0, 0);
                if (m.hasMatch() && !notFunctions.contains(bare) && !notFunctions.contains(first)
                    && !trimmed.startsWith(QLatin1Char('}'))
                    && !trimmed.startsWith(QLatin1Char(':')) && !trimmed.startsWith(QLatin1Char(','))   // init lists
                    && !trimmed.left(trimmed.indexOf(QLatin1Char('('))).contains(QLatin1Char('='))
                    && !(bare.size() > 1 && bare == bare.toUpper())) {   // MACRO(...)
                    out << CodeSymbol{name, Kind::Function, n + 1, 0, 0};
                    found = int(out.size()) - 1;
                }
            }
            if (found >= 0) {
                out[found].level = int(std::count_if(stack.cbegin(), stack.cend(),
                                                     [](const Open &o) { return o.symbol >= 0; }));
                pending = found;
                pendingLine = n;
            }
        }

        for (const QChar c : code) {
            if (c == QLatin1Char('{')) {
                if (pending >= 0) {
                    stack << Open{pending, out[pending].kind != Kind::Function};
                    pending = -1;
                } else {
                    stack << Open{-1, false};
                }
            } else if (c == QLatin1Char('}')) {
                if (stack.isEmpty()) continue;
                const Open o = stack.takeLast();
                if (o.symbol >= 0) out[o.symbol].endLine = n + 1;
            } else if (c == QLatin1Char(';') && pending >= 0 && pendingLine != n) {
                pending = -1;   // a declaration after all
            }
        }
        if (pending >= 0 && trimmed.endsWith(QLatin1Char(';'))) pending = -1;
    }

    // A file that stops mid-body: those symbols run to its end. Ones that
    // never got a body were declarations, not definitions.
    for (const Open &o : std::as_const(stack))
        if (o.symbol >= 0) out[o.symbol].endLine = int(lines.size());
    QVector<CodeSymbol> kept;
    for (const CodeSymbol &s : std::as_const(out))
        if (s.endLine > 0) kept << s;
    return kept;
}

// ── COBOL ────────────────────────────────────────────────────────────────────
QVector<CodeSymbol> scanCobol(const QStringList &lines)
{
    static const QRegularExpression division(QStringLiteral(R"(^\s*([A-Z0-9-]+)\s+DIVISION\b)"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression section(QStringLiteral(R"(^\s*([A-Z0-9][A-Z0-9-]*)\s+SECTION\s*\.)"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression paragraph(QStringLiteral(R"(^\s*([A-Z0-9][A-Z0-9-]*)\s*\.\s*$)"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QSet<QString> statements = {
        "EXIT", "GOBACK", "CONTINUE", "STOP", "ELSE", "END", "NEXT", "DECLARATIVES",
    };

    QVector<CodeSymbol> out;
    bool procedure = false;
    bool inSection = false;
    for (int n = 0; n < lines.size(); ++n) {
        QString code = lines[n];
        // Fixed format: drop the sequence area; * or / in column 7 is a comment.
        bool fixedLine = code.size() >= 7;
        for (int i = 0; fixedLine && i < 6; ++i) fixedLine = code[i] == QLatin1Char(' ') || code[i].isDigit();
        if (fixedLine) {
            if (code[6] == QLatin1Char('*') || code[6] == QLatin1Char('/')) continue;
            code = code.mid(7, 65);
        }
        if (const int c = code.indexOf(QStringLiteral("*>")); c >= 0) code.truncate(c);
        if (code.trimmed().isEmpty() || code.trimmed().startsWith(QStringLiteral(">>"))) continue;

        if (const auto m = division.match(code); m.hasMatch()) {
            procedure = m.captured(1).compare(QLatin1String("PROCEDURE"), Qt::CaseInsensitive) == 0;
            inSection = false;
            out << CodeSymbol{m.captured(1).toUpper() + QStringLiteral(" DIVISION"), Kind::Division, n + 1, 0, 0};
        } else if (const auto s = section.match(code); s.hasMatch()) {
            inSection = procedure;
            out << CodeSymbol{s.captured(1).toUpper() + QStringLiteral(" SECTION"), Kind::Section, n + 1, 0, 1};
        } else if (const auto p = paragraph.match(code); procedure && p.hasMatch()) {
            const QString name = p.captured(1).toUpper();
            if (statements.contains(name) || name.startsWith(QLatin1String("END-"))) continue;
            out << CodeSymbol{name, Kind::Paragraph, n + 1, 0, inSection ? 2 : 1};
        }
    }
    closeByLevel(out, int(lines.size()));
    return out;
}

// ── Line-oriented languages ──────────────────────────────────────────────────
struct LineRule { QRegularExpression re; Kind kind; int nameGroup; };

QVector<CodeSymbol> scanLines(const QStringList &lines, const QVector<LineRule> &rules, bool indentLevels)
{
    QVector<CodeSymbol> out;
    for (int n = 0; n < lines.size(); ++n) {
        for (const LineRule &r : rules) {
            const auto m = r.re.match(lines[n]);
            if (!m.hasMatch()) continue;
            int level = 0;
            if (indentLevels) {
                int indent = 0;
                for (const QChar c : lines[n]) {
                    if (c == QLatin1Char(' ')) ++indent;
                    else if (c == QLatin1Char('\t')) indent += 4;
                    else break;
                }
                level = indent / 4;
            }
            out << CodeSymbol{m.captured(r.nameGroup), r.kind, n + 1, 0, level};
            break;
        }
    }
    closeByLevel(out, int(lines.size()));
    return out;
}

QVector<CodeSymbol> scanMarkdown(const QStringList &lines)
{
    static const QRegularExpression heading(QStringLiteral(R"(^(#{1,6})\s+(.+?)\s*#*\s*$)"));
    QVector<CodeSymbol> out;
    bool fence = false;
    for (int n = 0; n < lines.size(); ++n) {
        const QString t = lines[n].trimmed();
        if (t.startsWith(QLatin1String("```")) || t.startsWith(QLatin1String("~~~"))) { fence = !fence; continue; }
        if (fence) continue;
        if (const auto m = heading.match(lines[n]); m.hasMatch())
            out << CodeSymbol{m.captured(2), Kind::Heading, n + 1, 0, int(m.captured(1).size()) - 1};
    }
    closeByLevel(out, int(lines.size()));
    return out;
}

QRegularExpression ci(const char *pattern)
{
    return QRegularExpression(QString::fromLatin1(pattern), QRegularExpression::CaseInsensitiveOption);
}

} // namespace

namespace CodeSymbols {

QVector<CodeSymbol> scan(const QString &text, const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    const QStringList lines = text.split(QLatin1Char('\n'));

    static const QStringList cLike = {"c", "h", "cpp", "cxx", "cc", "hpp", "hxx", "hh", "m", "mm",
                                      "java", "cs", "swift", "rs", "go"};
    static const QStringList jsLike = {"js", "mjs", "ts", "kjs"};
    if (cLike.contains(suffix)) return scanBraces(lines, false);
    if (jsLike.contains(suffix)) return scanBraces(lines, true);
    if (suffix == QLatin1String("cob") || suffix == QLatin1String("cbl") || suffix == QLatin1String("cpy")
        || suffix == QLatin1String("cobol"))
        return scanCobol(lines);
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) return scanMarkdown(lines);

    if (suffix == QLatin1String("pas") || suffix == QLatin1String("pp") || suffix == QLatin1String("lpr")
        || suffix == QLatin1String("dpr")) {
        static const QVector<LineRule> pascal = {
            {ci(R"(^\s*(?:class\s+)?(?:procedure|function|constructor|destructor|operator)\s+([\w.]+))"), Kind::Function, 1},
            {ci(R"(^\s*(\w+)\s*=\s*(?:packed\s+)?(?:class|record|object|interface)\b(?!\s*;))"), Kind::Class, 1},
        };
        return scanLines(lines, pascal, false);
    }
    if (suffix == QLatin1String("kayte") || suffix == QLatin1String("kyt") || suffix == QLatin1String("bas")
        || suffix == QLatin1String("vb") || suffix == QLatin1String("frm")) {
        static const QVector<LineRule> basic = {
            {ci(R"(^\s*(?:(?:PUBLIC|PRIVATE|STATIC|FRIEND)\s+)*(?:SUB|FUNCTION)\s+(\w+))"), Kind::Function, 1},
            {ci(R"(^\s*(?:(?:PUBLIC|PRIVATE)\s+)?(?:CLASS|TYPE|MODULE)\s+(\w+)\s*$)"), Kind::Class, 1},
        };
        return scanLines(lines, basic, false);
    }
    if (suffix == QLatin1String("py")) {
        static const QVector<LineRule> python = {
            {QRegularExpression(QStringLiteral(R"(^\s*(?:async\s+)?def\s+(\w+))")), Kind::Function, 1},
            {QRegularExpression(QStringLiteral(R"(^\s*class\s+(\w+))")), Kind::Class, 1},
        };
        return scanLines(lines, python, true);
    }
    return {};
}

QVector<int> pathAt(const QVector<CodeSymbol> &symbols, int line)
{
    QVector<int> path;
    for (int i = 0; i < symbols.size(); ++i)
        if (symbols[i].line <= line && line <= symbols[i].endLine) path << i;
    return path;
}

QString glyph(CodeSymbol::Kind kind)
{
    switch (kind) {
    case Kind::Namespace: return QStringLiteral("{}");
    case Kind::Class:     return QStringLiteral("◆");   // ◆
    case Kind::Function:  return QStringLiteral("ƒ");   // ƒ
    case Kind::Division:
    case Kind::Section:   return QStringLiteral("§");   // §
    case Kind::Paragraph: return QStringLiteral("¶");   // ¶
    case Kind::Heading:   return QStringLiteral("#");
    }
    return {};
}

} // namespace CodeSymbols
