#ifndef CODESYMBOLS_H
#define CODESYMBOLS_H

#include <QString>
#include <QVector>

// The outline of a source file - classes, functions, COBOL divisions /
// sections / paragraphs, Markdown headings ... - found with light per-language
// scanning (no compiler or language server), for the breadcrumb bar and
// Go to Symbol.
struct CodeSymbol
{
    enum Kind { Namespace, Class, Function, Division, Section, Paragraph, Heading };
    QString name;
    Kind    kind = Function;
    int     line = 0;      // 1-based first line
    int     endLine = 0;   // 1-based last line (inclusive)
    int     level = 0;     // nesting depth, 0 = top level
};

namespace CodeSymbols {

// The symbols of `text`, in line order; the language comes from the file's
// suffix. Empty for languages it doesn't know.
QVector<CodeSymbol> scan(const QString &text, const QString &filePath);

// Indexes into `symbols` of the ones enclosing `line`, outermost first.
QVector<int> pathAt(const QVector<CodeSymbol> &symbols, int line);

// A short glyph for the kind (ƒ, ◆, §, ¶, #, {}).
QString glyph(CodeSymbol::Kind kind);

} // namespace CodeSymbols

#endif // CODESYMBOLS_H
