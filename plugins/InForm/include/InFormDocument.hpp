#pragma once
// InFormDocument.hpp - KayteIDE InForm plugin
// A whole InForm form: its controls and bindings. Reads and writes the .frm
// files InForm's UiEditor generates (QB64 source containing SUB __UI_LoadForm),
// so forms round-trip between KayteIDE and the original QB64 tool chain.

#include "InFormTypes.hpp"

#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>

namespace InForm {

class Document
{
public:
    // ── .frm I/O ──────────────────────────────────────────────────────────────
    bool    loadFrm(const QString &filePath, QString *error = nullptr);
    bool    parseFrm(const QString &source, QString *error = nullptr);
    bool    saveFrm(const QString &filePath, QString *error = nullptr) const;
    QString toFrm() const;

    // ── Controls ──────────────────────────────────────────────────────────────
    QList<Control>       &controls()       { return m_controls; }
    const QList<Control> &controls() const { return m_controls; }
    QList<Binding>       &bindings()       { return m_bindings; }
    const QList<Binding> &bindings() const { return m_bindings; }

    Control       *find(const QString &name);
    const Control *find(const QString &name) const;
    Control       *form();               // the single Form control (nullptr if none)
    const Control *form() const;
    QStringList    childrenOf(const QString &name) const;

    // Top-left of a control in form coordinates (parent frame offsets added).
    QPoint absolutePos(const QString &name) const;

    void clear();

    // Statements outside any control (e.g. $EXEICON is kept on the Form instead).
    QStringList       &extraLines()       { return m_extraLines; }
    const QStringList &extraLines() const { return m_extraLines; }

private:
    QList<Control> m_controls;
    QList<Binding> m_bindings;
    QStringList    m_extraLines;
};

// QB64 string helpers shared with the IDE bridge.
QString encodeBasicString(const QString &text);  // -> "a\10;b"  (InForm escape codes)
QString decodeBasicString(const QString &text);  // "a\10;b" (no quotes) -> "a\nb"

} // namespace InForm
