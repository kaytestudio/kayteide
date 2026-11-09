#include "cobolsyntaxhighlighter.h"

#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QStringList>

namespace {
bool darkTheme()
{
    return QGuiApplication::palette().color(QPalette::Base).lightness() < 128;
}

QTextCharFormat colored(const QColor &c, bool bold = false, bool italic = false)
{
    QTextCharFormat f;
    f.setForeground(c);
    if (bold) f.setFontWeight(QFont::Bold);
    if (italic) f.setFontItalic(true);
    return f;
}

// COBOL words contain hyphens, so `MOVE` must not match inside `WS-MOVE-CNT`.
QString words(const QStringList &list)
{
    return QStringLiteral("(?<![\\w-])(?:") + list.join(QLatin1Char('|'))
           + QStringLiteral(")(?![\\w-])");
}

// Fixed format: columns 1-6 blank or a sequence number, column 7 an indicator.
bool looksFixed(const QString &text)
{
    const int n = qMin(6, int(text.size()));
    for (int i = 0; i < n; ++i) {
        const QChar c = text.at(i);
        if (!(c == QLatin1Char(' ') || c.isDigit())) return false;
    }
    if (text.size() <= 6) return true;
    return QStringLiteral(" *-/dD").contains(text.at(6));
}

bool isLevelNumber(int n)
{
    return (n >= 1 && n <= 49) || n == 66 || n == 77 || n == 88;
}
} // namespace

CobolSyntaxHighlighter::CobolSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    const bool dark = darkTheme();
    const QColor keyword  = dark ? QColor(0x6c, 0xb6, 0xff) : QColor(0x05, 0x50, 0xae);
    const QColor type     = dark ? QColor(0x7e, 0xc6, 0x99) : QColor(0x1a, 0x7f, 0x37);
    const QColor literal  = dark ? QColor(0xe6, 0xa5, 0x6b) : QColor(0xa3, 0x40, 0x0c);
    const QColor number   = dark ? QColor(0x56, 0xd4, 0xdd) : QColor(0x0a, 0x7e, 0x8c);
    const QColor accent   = dark ? QColor(0xd2, 0x8f, 0xe8) : QColor(0x82, 0x50, 0xdf);
    const QColor dim      = dark ? QColor(0x8b, 0x94, 0x9e) : QColor(0x6e, 0x77, 0x81);

    m_comment   = colored(dim, false, true);
    m_string    = colored(literal);
    m_number    = colored(number);
    m_label     = colored(accent);
    m_margin    = colored(dim);
    m_directive = colored(accent, false, true);

    const auto add = [this](const QString &pattern, const QTextCharFormat &format, int group = 0) {
        m_rules.append({QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption),
                        format, group});
    };

    // Later rules win where they overlap.
    add(QStringLiteral("(?<!\\w)(?<!\\w-)\\d+(?:\\.\\d+)?(?![\\w-])"), m_number);

    add(words({
        "ACCEPT", "ACCESS", "ADD", "ADDRESS", "ADVANCING", "AFTER", "ALL", "ALLOCATE",
        "ALSO", "ALTER", "AND", "ANY", "ARE", "AREAS?", "ASCENDING", "ASSIGN", "AT",
        "AUTHOR", "BASED", "BEFORE", "BY", "CALL", "CANCEL", "CHARACTERS?", "CLASS",
        "CLOSE", "CODE", "COLLATING", "COMMON", "COMPUTE", "CONFIGURATION", "CONTINUE",
        "CONVERTING", "COPY", "CORR", "CORRESPONDING", "COUNT", "CURRENCY",
        "DATE-COMPILED", "DATE-WRITTEN", "DECIMAL-POINT", "DECLARATIVES", "DEFAULT",
        "DELETE", "DELIMITED", "DELIMITER", "DEPENDING", "DESCENDING", "DISPLAY",
        "DIVIDE", "DOWN", "DUPLICATES", "DYNAMIC", "ELSE", "END", "END-[A-Z]+", "ENTRY",
        "EQUAL", "ERROR", "EVALUATE", "EXCEPTION", "EXIT", "EXTEND", "EXTERNAL", "FD",
        "FILE", "FILE-CONTROL", "FIRST", "FOR", "FREE", "FROM", "FUNCTION", "GENERATE",
        "GIVING", "GLOBAL", "GO", "GOBACK", "GREATER", "I-O", "I-O-CONTROL", "IF", "IN",
        "INDEXED", "INITIAL", "INITIALIZE", "INPUT", "INPUT-OUTPUT", "INSPECT",
        "INSTALLATION", "INTO", "INVALID", "INVOKE", "IS", "KEY", "LENGTH", "LESS",
        "LIMIT", "LINES?", "LINKAGE", "LOCAL-STORAGE", "LOCK", "MERGE", "MODE", "MOVE",
        "MULTIPLY", "NEXT", "NO", "NOT", "OBJECT-COMPUTER", "OCCURS", "OF", "OFF", "ON",
        "OPEN", "OPTIONAL", "OR", "ORDER", "ORGANIZATION", "OTHER", "OUTPUT", "OVERFLOW",
        "PERFORM", "PROCEED", "PROGRAM", "PROGRAM-ID", "RAISE", "RANDOM", "READ",
        "RECORDS?", "REDEFINES", "REFERENCE", "RELATIVE", "RELEASE", "REMAINDER",
        "RENAMES", "REPLACE", "REPLACING", "REPORT", "REPOSITORY", "RESERVE", "RETURN",
        "RETURNING", "REWIND", "REWRITE", "ROUNDED", "RUN", "SCREEN", "SD", "SEARCH",
        "SECURITY", "SELECT", "SENTENCE", "SEQUENCE", "SEQUENTIAL", "SET", "SIZE",
        "SORT", "SOURCE-COMPUTER", "SPECIAL-NAMES", "STANDARD", "START", "STATUS",
        "STOP", "STRING", "SUBTRACT", "SUPPRESS", "TALLYING", "THAN", "THEN",
        "THROUGH", "THRU", "TIMES", "TO", "UNSTRING", "UNTIL", "UP", "UPON", "USING",
        "VALUES?", "VARYING", "WHEN", "WITH", "WORKING-STORAGE", "WRITE"}),
        colored(keyword, true));

    add(words({
        "PIC", "PICTURE", "USAGE", "COMP(?:-[1-6NX])?", "COMPUTATIONAL(?:-[1-6NX])?",
        "BINARY(?:-CHAR|-SHORT|-LONG|-DOUBLE)?", "PACKED-DECIMAL", "FLOAT-SHORT",
        "FLOAT-LONG", "NATIONAL", "POINTER", "PROGRAM-POINTER", "PROCEDURE-POINTER",
        "INDEX", "SIGN", "LEADING", "TRAILING", "SEPARATE", "JUST", "JUSTIFIED",
        "BLANK", "SYNC", "SYNCHRONIZED", "FILLER"}),
        colored(type));

    add(words({
        "ZERO", "ZEROS", "ZEROES", "SPACES?", "HIGH-VALUES?", "LOW-VALUES?", "QUOTES?",
        "NULLS?", "TRUE", "FALSE", "RETURN-CODE", "TALLY", "WHEN-COMPILED"}),
        m_number);

    // PIC X(10), PIC S9(5)V99 - the picture string up to the sentence period.
    add(QStringLiteral("(?<![\\w-])PIC(?:TURE)?(?:\\s+IS)?\\s+(\\S+?)(?=\\.?(?:\\s|$))"),
        colored(type), 1);
    add(QStringLiteral("(?<![\\w-])FUNCTION\\s+([\\w-]+)"), m_label, 1);
    add(QStringLiteral("(?<![\\w-])[\\w-]+\\s+(?:DIVISION|SECTION)(?![\\w-])"),
        colored(accent, true));
    add(QStringLiteral(">>\\s*[A-Z-]+.*$"), m_directive);
    add(QStringLiteral("\\$\\s*SET\\b.*$"), m_directive);

    m_sourceFormat = QRegularExpression(
        QStringLiteral(">>\\s*SOURCE(?:\\s+FORMAT)?(?:\\s+IS)?\\s+(FREE|FIXED|VARIABLE)"
                       "|\\$\\s*SET\\s+SOURCEFORMAT\\s*\\(?\\s*[\"']?(FREE|FIXED|VARIABLE)"),
        QRegularExpression::CaseInsensitiveOption);
    // A paragraph name alone on its line: `MAIN-PARA.`
    m_paragraph = QRegularExpression(QStringLiteral("^\\s*([\\w-]+)\\.\\s*$"));
}

void CobolSyntaxHighlighter::highlightBlock(const QString &text)
{
    int state = previousBlockState() < 0 ? Auto : previousBlockState();
    const QRegularExpressionMatch fmt = m_sourceFormat.match(text);
    int nextState = state;
    if (fmt.hasMatch()) {
        const QString f = (fmt.captured(1) + fmt.captured(2)).toUpper();
        nextState = f == QLatin1String("FREE") ? Free : Fixed;
    }
    setCurrentBlockState(nextState);

    const bool fixed = state == Fixed || (state == Auto && looksFixed(text));
    if (!fixed) {
        highlightCode(text, 0, text.size());
        return;
    }

    setFormat(0, qMin(6, int(text.size())), m_margin);
    if (text.size() <= 6) return;

    const QChar indicator = text.at(6);
    if (indicator == QLatin1Char('*') || indicator == QLatin1Char('/')) {
        setFormat(6, text.size() - 6, m_comment);
        return;
    }
    if (indicator != QLatin1Char(' ')) setFormat(6, 1, m_directive);   // -, D

    // Columns 73+ are the identification area; only dimmed once the source
    // says it is fixed format, since free-format lines may run past 72.
    const int end = state == Fixed ? qMin(72, int(text.size())) : text.size();
    highlightCode(text, 7, end);
    if (end < text.size()) setFormat(end, text.size() - end, m_margin);
}

void CobolSyntaxHighlighter::highlightCode(const QString &text, int start, int end)
{
    const QString code = text.left(end);

    const QRegularExpressionMatch para = m_paragraph.match(code.mid(start));
    if (para.hasMatch())
        setFormat(start + para.capturedStart(1), para.capturedLength(1), m_label);

    for (const Rule &rule : std::as_const(m_rules)) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(code, start);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            setFormat(m.capturedStart(rule.group), m.capturedLength(rule.group), rule.format);
        }
    }

    // Level number opening a data description entry: 01, 05, 77, 88 ...
    int i = start;
    while (i < end && code.at(i).isSpace()) ++i;
    int j = i;
    while (j < end && code.at(j).isDigit()) ++j;
    if (j > i && j - i <= 2 && (j == end || code.at(j).isSpace())
        && isLevelNumber(code.mid(i, j - i).toInt()))
        setFormat(i, j - i, m_label);

    // Literals and `*>` comments last, so nothing inside them is highlighted.
    // Quotes are escaped by doubling; X"..", N"..", Z".." prefixes included.
    for (int k = start; k < end; ++k) {
        const QChar c = code.at(k);
        if (c == QLatin1Char('*') && k + 1 < end && code.at(k + 1) == QLatin1Char('>')) {
            setFormat(k, end - k, m_comment);
            return;
        }
        if (c != QLatin1Char('"') && c != QLatin1Char('\'')) continue;
        int from = k;
        if (k > start && code.at(k - 1).isLetter()
            && (k - 1 == start || !(code.at(k - 2).isLetterOrNumber() || code.at(k - 2) == QLatin1Char('-'))))
            from = k - 1;
        int m = k + 1;
        while (m < end) {
            if (code.at(m) == c) {
                if (m + 1 < end && code.at(m + 1) == c) { m += 2; continue; }
                break;
            }
            ++m;
        }
        const int stop = qMin(m + 1, end);
        setFormat(from, stop - from, m_string);
        k = stop - 1;
    }
}
