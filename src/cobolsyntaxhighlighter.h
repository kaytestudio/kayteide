#ifndef COBOLSYNTAXHIGHLIGHTER_H
#define COBOLSYNTAXHIGHLIGHTER_H

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

// Highlights COBOL (GnuCOBOL dialect) in fixed and free source format:
// divisions and sections, verbs, data clauses and PICTURE strings, level
// numbers, literals, figurative constants, intrinsic functions, compiler
// directives and comments. Fixed-format lines get their sequence area
// (columns 1-6), indicator (column 7) and, after `>>SOURCE FIXED`, the
// identification area (columns 73+) dimmed. Colours follow the editor
// palette, so they suit light and dark themes.
class CobolSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit CobolSyntaxHighlighter(QTextDocument *parent = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    // Source format carried from line to line; set by >>SOURCE directives.
    // Auto decides per line whether it looks like fixed format.
    enum BlockState { Auto = 0, Fixed = 1, Free = 2 };

    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
        int group = 0;   // capture group to format (0 = whole match)
    };
    QVector<Rule> m_rules;

    QRegularExpression m_sourceFormat;
    QRegularExpression m_paragraph;

    QTextCharFormat m_comment;
    QTextCharFormat m_string;
    QTextCharFormat m_number;
    QTextCharFormat m_label;
    QTextCharFormat m_margin;
    QTextCharFormat m_directive;

    void highlightCode(const QString &text, int start, int end);
};

#endif // COBOLSYNTAXHIGHLIGHTER_H
