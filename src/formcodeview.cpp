#include "formcodeview.h"

#include "uicanvaswidget.h"
#include "informintegration.h"
#include "InFormKayte.hpp"
#include "InFormCobol.hpp"
#include "cobolsyntaxhighlighter.h"
#include "cppsyntaxhighlighter.h"
#include "kaytesyntaxhighlighter.h"
#include "vbsyntaxhighlighter.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QTextBlock>
#include <QTimer>
#include <QVBoxLayout>
#include <QXmlStreamReader>

#include <algorithm>

namespace {

// A C++ string literal for `text`.
QString cppString(const QString &text)
{
    QString out;
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '\\': out += QLatin1String("\\\\"); break;
        case '"':  out += QLatin1String("\\\""); break;
        case '\n': out += QLatin1String("\\n");  break;
        case '\t': out += QLatin1String("\\t");  break;
        default:   out += c;
        }
    }
    return QLatin1Char('"') + out + QLatin1Char('"');
}

// The text of a C++ string literal (quotes included) - the inverse of cppString.
QString cppUnescape(const QString &literal)
{
    const QString body = literal.mid(1, literal.size() - 2);
    QString out;
    for (int i = 0; i < body.size(); ++i) {
        if (body[i] != QLatin1Char('\\') || i + 1 == body.size()) { out += body[i]; continue; }
        const QChar e = body[++i];
        out += e == QLatin1Char('n') ? QChar('\n') : e == QLatin1Char('t') ? QChar('\t') : e;
    }
    return out;
}

const char *kLanguageKey = "UiDesigner/codeLanguage";

// A widget read back from edited code. Fields the code didn't mention keep
// their value from the canvas.
struct Parsed {
    CanvasItem item;
    bool hasText     = false;
    bool hasGeometry = false;
};

// Qt Designer XML: the form's direct child widgets, as toUiXml() writes them.
bool parseUi(const QString &code, QList<Parsed> *out, QString *error)
{
    QXmlStreamReader xml(code);
    int depth = 0;          // nesting of <widget>; 1 = the form, 2 = its children
    QString property;
    Parsed cur;
    while (!xml.atEnd()) {
        xml.readNext();
        const QStringView name = xml.name();
        if (xml.isStartElement()) {
            if (name == QLatin1String("widget")) {
                if (++depth == 2) {
                    cur = Parsed {};
                    cur.item.widgetType = xml.attributes().value(QLatin1String("class")).toString();
                    cur.item.objectName = xml.attributes().value(QLatin1String("name")).toString();
                }
            } else if (depth == 2 && name == QLatin1String("property")) {
                property = xml.attributes().value(QLatin1String("name")).toString();
            } else if (depth == 2 && property == QLatin1String("geometry")) {
                QRect &g = cur.item.geometry;
                if (name == QLatin1String("x"))           g.moveLeft(xml.readElementText().toInt());
                else if (name == QLatin1String("y"))      g.moveTop(xml.readElementText().toInt());
                else if (name == QLatin1String("width"))  g.setWidth(xml.readElementText().toInt());
                else if (name == QLatin1String("height")) g.setHeight(xml.readElementText().toInt());
                cur.hasGeometry = true;
            } else if (depth == 2 && name == QLatin1String("string")
                       && (property == QLatin1String("text") || property == QLatin1String("title")
                           || property == QLatin1String("placeholderText"))) {
                cur.item.labelText = xml.readElementText();
                cur.hasText = true;
            }
        } else if (xml.isEndElement()) {
            if (name == QLatin1String("widget")) {
                if (depth == 2) out->append(cur);
                --depth;
            } else if (depth == 2 && name == QLatin1String("property")) {
                property.clear();
            }
        }
    }
    if (xml.hasError()) {
        *error = QObject::tr("Line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
        return false;
    }
    return true;
}

// Qt C++ in the shape qtCpp() writes: `name = new Class(Form);` plus
// setObjectName / setGeometry / setText-style calls on it.
bool parseCpp(const QString &code, QList<Parsed> *out, QString *error)
{
    static const QString str = QStringLiteral("(\"(?:[^\"\\\\]|\\\\.)*\")");
    static const QRegularExpression newRe(
        QStringLiteral("\\b(\\w+)\\s*=\\s*new\\s+(\\w+)\\s*\\(\\s*Form\\s*\\)\\s*;"));
    static const QRegularExpression nameRe(
        QStringLiteral("\\b(\\w+)\\s*->\\s*setObjectName\\s*\\(\\s*") + str + QStringLiteral("\\s*\\)"));
    static const QRegularExpression geomRe(QStringLiteral(
        "\\b(\\w+)\\s*->\\s*setGeometry\\s*\\(\\s*QRect\\s*\\(\\s*(-?\\d+)\\s*,\\s*(-?\\d+)\\s*,"
        "\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)\\s*\\)"));
    static const QRegularExpression textRe(
        QStringLiteral("\\b(\\w+)\\s*->\\s*set(?:Text|Title|PlaceholderText)\\s*\\(\\s*") + str
        + QStringLiteral("\\s*\\)"));

    QHash<QString, int> byVar;
    for (auto it = newRe.globalMatch(code); it.hasNext();) {
        const auto m = it.next();
        if (byVar.contains(m.captured(1))) {
            *error = QObject::tr("'%1' is created twice").arg(m.captured(1));
            return false;
        }
        Parsed p;
        p.item.widgetType = m.captured(2);
        p.item.objectName = m.captured(1);
        byVar.insert(m.captured(1), int(out->size()));
        out->append(p);
    }
    for (auto it = nameRe.globalMatch(code); it.hasNext();) {
        const auto m = it.next();
        if (byVar.contains(m.captured(1)))
            (*out)[byVar[m.captured(1)]].item.objectName = cppUnescape(m.captured(2));
    }
    for (auto it = geomRe.globalMatch(code); it.hasNext();) {
        const auto m = it.next();
        if (!byVar.contains(m.captured(1))) continue;
        Parsed &p = (*out)[byVar[m.captured(1)]];
        p.item.geometry = QRect(m.captured(2).toInt(), m.captured(3).toInt(),
                                m.captured(4).toInt(), m.captured(5).toInt());
        p.hasGeometry = true;
    }
    for (auto it = textRe.globalMatch(code); it.hasNext();) {
        const auto m = it.next();
        if (!byVar.contains(m.captured(1))) continue;
        Parsed &p = (*out)[byVar[m.captured(1)]];
        p.item.labelText = cppUnescape(m.captured(2));
        p.hasText = true;
    }
    return true;
}

bool sameItem(const CanvasItem &a, const CanvasItem &b)
{
    return a.widgetType == b.widgetType && a.objectName == b.objectName
        && a.labelText == b.labelText && a.geometry == b.geometry
        && a.parentName == b.parentName && a.properties == b.properties;
}

} // namespace

FormCodeView::FormCodeView(UiCanvasWidget *canvas, InFormIntegration *inform, QWidget *parent)
    : QWidget(parent), m_canvas(canvas), m_inform(inform)
{
    m_language = new QComboBox(this);
    m_language->addItem(tr("Kayte program (.kayte)"), KayteProgram);
    m_language->addItem(tr("Kayte JS program (.kjs)"), KjsProgram);
    m_language->addItem(tr("Kayte BASIC program (.bas)"), BasicProgram);
    m_language->addItem(tr("GnuCOBOL program (.cob)"), CobolProgram);
    m_language->addItem(tr("Kayte form (.kfm)"), KayteForm);
    m_language->addItem(tr("InForm form (.frm)"), InFormForm);
    m_language->addItem(tr("Qt C++ (setupUi)"), QtCpp);
    m_language->addItem(tr("Qt Designer (.ui)"), QtUi);
    m_language->setCurrentIndex(qMax(0, m_language->findData(
        QSettings().value(kLanguageKey, int(KayteProgram)).toInt())));

    m_note = new QLabel(this);
    m_note->setTextFormat(Qt::PlainText);
    setNote(QString());

    m_reset = new QPushButton(tr("Regenerate"), this);
    m_reset->setToolTip(tr("Throw away your edits to this program and generate it again"));
    m_reset->hide();
    auto *copy = new QPushButton(tr("Copy"), this);
    copy->setToolTip(tr("Copy the code to the clipboard"));
    auto *save = new QPushButton(tr("Save As..."), this);
    save->setToolTip(tr("Save the code to a file and open it"));

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    bar->addWidget(new QLabel(tr("Language:"), this));
    bar->addWidget(m_language);
    bar->addWidget(m_note, 1);
    bar->addWidget(m_reset);
    bar->addWidget(copy);
    bar->addWidget(save);

    m_editor = new QPlainTextEdit(this);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setTabStopDistance(4 * m_editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addLayout(bar);
    lay->addWidget(m_editor, 1);

    // Drops, moves and property edits come in bursts; regenerate once after.
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(150);
    connect(m_refreshTimer, &QTimer::timeout, this, &FormCodeView::refresh);
    connect(m_canvas, &UiCanvasWidget::canvasModified, this, [this] {
        if (isVisible() && !m_applying) m_refreshTimer->start();
    });
    connect(m_canvas, &UiCanvasWidget::itemSelectionChanged, this, [this] {
        if (isVisible() && !m_applying) highlightSelected();
    });

    // Typing: push the code to the canvas once the user pauses.
    m_syncTimer = new QTimer(this);
    m_syncTimer->setSingleShot(true);
    m_syncTimer->setInterval(500);
    connect(m_syncTimer, &QTimer::timeout, this, &FormCodeView::flushPendingSync);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (m_settingText) return;
        m_unsynced = true;
        m_syncTimer->start();
    });

    connect(m_language, &QComboBox::currentIndexChanged, this, [this] {
        flushPendingSync();   // the pending edit is in the language being left
        QSettings().setValue(kLanguageKey, m_language->currentData().toInt());
        m_settingText = true;
        m_editor->clear();   // a new language starts at the top
        m_settingText = false;
        refresh();
    });

    connect(m_reset, &QPushButton::clicked, this, [this] {
        m_programEdits.remove(currentLanguage());
        refresh();
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_editor->toPlainText());
    });
    connect(save, &QPushButton::clicked, this, &FormCodeView::onSaveAs);
}

FormCodeView::Language FormCodeView::currentLanguage() const
{
    return Language(m_language->currentData().toInt());
}

void FormCodeView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

void FormCodeView::setNote(const QString &text, bool error)
{
    m_note->setText(text);
    m_note->setStyleSheet(error ? QStringLiteral("color: #d64545;") : QStringLiteral("color: gray;"));
}

// Sends typed-but-unsynced code to the canvas, in the language it was written in.
void FormCodeView::flushPendingSync()
{
    m_syncTimer->stop();
    if (m_unsynced) syncToCanvas(m_shownLanguage);
    m_unsynced = false;
}

void FormCodeView::refresh()
{
    flushPendingSync();   // never overwrite typing that hasn't reached the canvas

    const Language lang = currentLanguage();
    QString note;
    m_note->setToolTip(QString());
    const QString code = generate(lang, &note);
    setNote(note);
    m_reset->setVisible(isProgram(lang) && m_programEdits.contains(lang));

    // The widgets this text describes (see m_baseNames).
    m_baseNames.clear();
    if (!isProgram(lang)) {
        QList<Parsed> parsed;
        QString error;
        bool ok = false;
        if (lang == QtUi)       ok = parseUi(code, &parsed, &error);
        else if (lang == QtCpp) ok = parseCpp(code, &parsed, &error);
        else {
            InForm::Document doc;
            ok = lang == KayteForm ? InForm::readKfm(code, &doc, &error) : doc.parseFrm(code, &error);
            if (ok)
                for (const CanvasItem &ci : m_inform->itemsFromDocument(doc))
                    parsed.append({ci, true, true});
        }
        if (ok)
            for (const Parsed &p : parsed) m_baseNames.insert(p.item.objectName);
    }

    if (code == m_editor->toPlainText()) { m_shownLanguage = lang; return; }

    // Keep the reader's place while the form changes under them.
    const int v = m_editor->verticalScrollBar()->value();
    const int h = m_editor->horizontalScrollBar()->value();
    const int pos = m_editor->textCursor().position();
    m_settingText = true;
    if (lang != m_shownLanguage || !m_highlighter) setHighlighter(lang);
    m_editor->setPlainText(code);
    m_settingText = false;
    m_shownLanguage = lang;
    QTextCursor cur = m_editor->textCursor();
    cur.setPosition(qMin(pos, code.size()));
    m_editor->setTextCursor(cur);
    m_editor->verticalScrollBar()->setValue(v);
    m_editor->horizontalScrollBar()->setValue(h);
    if (!m_editor->hasFocus()) highlightSelected();
}

QString FormCodeView::generate(Language lang, QString *note)
{
    if (lang == QtUi)  return m_canvas->toUiXml();
    if (lang == QtCpp) return qtCpp();

    int skipped = 0;
    const InForm::Document doc = m_inform->documentFromCanvas(&skipped);
    if (skipped > 0)
        *note = tr("%n widget(s) with no InForm equivalent left out", nullptr, skipped);

    QString formName = QStringLiteral("Form1");
    for (const InForm::Control &c : doc.controls())
        if (c.type == InForm::ControlType::Form) { formName = c.name; break; }
    const QString kfm = formName + QStringLiteral(".kfm");

    if (lang == CobolProgram) {
        QStringList warnings;
        if (!m_programEdits.contains(lang)) {
            const QString out = InForm::cobolProgram(doc, &warnings);
            if (!warnings.isEmpty()) {
                if (!note->isEmpty()) *note += QStringLiteral(" | ");
                *note += tr("%n control(s) a terminal screen can't show", nullptr, int(warnings.size()));
                m_note->setToolTip(warnings.join(QLatin1Char('\n')));
            }
            return out;
        }
        QString &source = m_programEdits[lang];
        source += InForm::missingCobolHandlers(doc, source);
        *note = tr("Your edits are kept; new buttons get handlers added. "
                   "Regenerate to pick up layout changes.");
        return source;
    }

    if (isProgram(lang)) {
        const InForm::KayteLanguage kl = lang == KjsProgram   ? InForm::KayteLanguage::Kjs
                                       : lang == BasicProgram ? InForm::KayteLanguage::Basic
                                                              : InForm::KayteLanguage::Kayte;
        if (!m_programEdits.contains(lang)) return InForm::kayteProgram(doc, kfm, kl);

        // An edited program is the user's: only add handlers for new controls.
        QString &source = m_programEdits[lang];
        source += InForm::missingKayteHandlers(doc, source, kl);
        *note = tr("Your edits are kept; new controls get handlers added. "
                   "Layout changes go in the form (.kfm).");
        return source;
    }

    switch (lang) {
    case KayteForm: {
        QStringList warnings;
        const QString out = InForm::writeKfm(doc, &warnings);
        if (!warnings.isEmpty()) {
            if (!note->isEmpty()) *note += QStringLiteral(" | ");
            *note += tr("%n control(s) Kayte forms can't hold", nullptr, int(warnings.size()));
            m_note->setToolTip(warnings.join(QLatin1Char('\n')));
        }
        return out;
    }
    case InFormForm:   return doc.toFrm();
    default:           return QString();
    }
}

// Applies the edited code to the canvas as one undoable step. Widgets are
// matched by name: changed ones are updated, new ones added, removed ones
// deleted. Widgets the format can't express (outside m_baseNames) stay put.
void FormCodeView::syncToCanvas(Language lang)
{
    const QString code = m_editor->toPlainText();

    if (isProgram(lang)) {
        m_programEdits[lang] = code;
        m_reset->show();
        setNote(lang == CobolProgram
                    ? tr("Your edits are kept; new buttons get handlers added. "
                         "Regenerate to pick up layout changes.")
                    : tr("Your edits are kept; new controls get handlers added. "
                         "Layout changes go in the form (.kfm)."));
        return;
    }

    QList<Parsed> parsed;
    QString error;
    InForm::Document doc;
    bool ok = false;
    if (lang == QtUi)       ok = parseUi(code, &parsed, &error);
    else if (lang == QtCpp) ok = parseCpp(code, &parsed, &error);
    else {
        ok = lang == KayteForm ? InForm::readKfm(code, &doc, &error) : doc.parseFrm(code, &error);
        if (ok)
            for (const CanvasItem &ci : m_inform->itemsFromDocument(doc))
                parsed.append({ci, true, true});
    }
    if (!ok) {
        setNote(tr("Not synced - %1").arg(error), true);
        return;
    }

    QHash<QString, int> byName;
    for (int i = 0; i < parsed.size(); ++i) {
        const QString &n = parsed[i].item.objectName;
        if (n.isEmpty()) {
            setNote(tr("Not synced - a %1 has no name").arg(parsed[i].item.widgetType), true);
            return;
        }
        if (byName.contains(n)) {
            setNote(tr("Not synced - two widgets are named '%1'").arg(n), true);
            return;
        }
        byName.insert(n, i);
    }

    const bool informFormat = lang == KayteForm || lang == InFormForm;
    const QList<CanvasItem> before = m_canvas->items();
    QList<CanvasItem> after;
    QSet<QString> matched;
    for (const CanvasItem &old : before) {
        if (!m_baseNames.contains(old.objectName)) { after << old; continue; }
        const int i = byName.value(old.objectName, -1);
        if (i < 0) continue;   // deleted from the code
        const Parsed &p = parsed[i];
        CanvasItem ci = p.item;
        // Same kind of widget: keep the canvas's own type (a QPushButton stays
        // one when edited as an InForm Button, and the other way round).
        if (UiCanvasWidget::qtClassOf(old) == UiCanvasWidget::qtClassOf(p.item)) {
            ci.widgetType = old.widgetType;
            if (!informFormat || !InForm::isPaletteKey(old.widgetType)) {
                ci.properties = old.properties;
                ci.parentName = old.parentName;
            }
        }
        if (!p.hasText)     ci.labelText = old.labelText;
        if (!p.hasGeometry) ci.geometry  = old.geometry;
        after << ci;
        matched.insert(old.objectName);
    }
    for (const Parsed &p : parsed) {
        if (matched.contains(p.item.objectName)) continue;
        CanvasItem ci = p.item;
        if (!p.hasGeometry || ci.geometry.isEmpty())
            ci.geometry = QRect(16, 16, qMax(ci.geometry.width(), 100), qMax(ci.geometry.height(), 30));
        after << ci;
    }

    m_baseNames.clear();
    for (const Parsed &p : parsed) m_baseNames.insert(p.item.objectName);
    if (informFormat) m_inform->updateFormFromDocument(doc);

    const bool changed = before.size() != after.size()
        || !std::equal(before.cbegin(), before.cend(), after.cbegin(), sameItem);
    setNote(changed ? tr("Synced to the canvas") : QString());
    if (!changed) return;

    m_applying = true;
    m_canvas->undoStack()->push(new ReplaceItemsCmd(m_canvas, before, after));
    m_applying = false;

    // Make room for widgets moved past the canvas edge.
    QRect bounds(QPoint(0, 0), m_canvas->size());
    for (const CanvasItem &ci : after) bounds |= ci.geometry;
    if (bounds.size() != m_canvas->size())
        m_canvas->resize(bounds.width() + 24, bounds.height() + 24);
}

// The canvas as a uic-style Ui_ class: every widget parented to the form at
// its canvas position, matching the .ui export.
QString FormCodeView::qtCpp() const
{
    const QStringList layoutTypes = {
        QStringLiteral("QVBoxLayout"), QStringLiteral("QHBoxLayout"),
        QStringLiteral("QGridLayout"), QStringLiteral("QFormLayout"),
        QStringLiteral("QSpacerItem_H"), QStringLiteral("QSpacerItem_V"),
    };
    const QStringList textWidgets = {
        QStringLiteral("QPushButton"), QStringLiteral("QToolButton"),
        QStringLiteral("QLabel"),      QStringLiteral("QCheckBox"),
        QStringLiteral("QRadioButton"),QStringLiteral("QCommandLinkButton"),
    };

    struct Entry { QString cls; const CanvasItem *item; };
    QList<Entry> entries;
    QStringList includes;
    for (const CanvasItem &ci : m_canvas->items()) {
        const QString cls = UiCanvasWidget::qtClassOf(ci);
        if (cls.isEmpty() || layoutTypes.contains(cls)) continue;
        entries.append({cls, &ci});
        if (!includes.contains(cls)) includes << cls;
    }
    includes.sort();

    QString s;
    s += QStringLiteral("// Generated by the KayteIDE UI Designer - edits here are not read back.\n");
    s += QStringLiteral("#pragma once\n\n#include <QWidget>\n");
    for (const QString &inc : includes)
        if (inc != QLatin1String("QWidget")) s += QStringLiteral("#include <%1>\n").arg(inc);

    s += QStringLiteral("\nclass Ui_Form\n{\npublic:\n");
    for (const Entry &e : entries)
        s += QStringLiteral("    %1 *%2;\n").arg(e.cls, e.item->objectName);
    if (!entries.isEmpty()) s += QLatin1Char('\n');

    s += QStringLiteral("    void setupUi(QWidget *Form)\n    {\n");
    s += QStringLiteral("        if (Form->objectName().isEmpty())\n"
                        "            Form->setObjectName(\"Form\");\n");
    s += QStringLiteral("        Form->resize(%1, %2);\n").arg(m_canvas->width()).arg(m_canvas->height());
    for (const Entry &e : entries) {
        const CanvasItem &ci = *e.item;
        const QString &n = ci.objectName;
        const QRect &g = ci.geometry;
        s += QStringLiteral("\n        %1 = new %2(Form);\n").arg(n, e.cls);
        s += QStringLiteral("        %1->setObjectName(%2);\n").arg(n, cppString(n));
        s += QStringLiteral("        %1->setGeometry(QRect(%2, %3, %4, %5));\n")
                 .arg(n).arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
        if (ci.labelText.isEmpty()) {
        } else if (e.cls == QLatin1String("QGroupBox")) {
            s += QStringLiteral("        %1->setTitle(%2);\n").arg(n, cppString(ci.labelText));
        } else if (e.cls == QLatin1String("QLineEdit")) {
            s += QStringLiteral("        %1->setPlaceholderText(%2);\n").arg(n, cppString(ci.labelText));
        } else if (textWidgets.contains(e.cls)) {
            s += QStringLiteral("        %1->setText(%2);\n").arg(n, cppString(ci.labelText));
        }
        if (e.cls == QLatin1String("QSlider"))
            s += QStringLiteral("        %1->setOrientation(Qt::Horizontal);\n").arg(n);
    }
    s += QStringLiteral("\n        Form->setWindowTitle(\"Form\");\n    }\n};\n\n");
    s += QStringLiteral("namespace Ui {\n    class Form : public Ui_Form {};\n}\n");
    return s;
}

QString FormCodeView::fileSuffix(Language lang) const
{
    switch (lang) {
    case KayteProgram: return QStringLiteral("kayte");
    case KjsProgram:   return QStringLiteral("kjs");
    case BasicProgram: return QStringLiteral("bas");
    case CobolProgram: return QStringLiteral("cob");
    case KayteForm:    return QStringLiteral("kfm");
    case InFormForm:   return QStringLiteral("frm");
    case QtCpp:        return QStringLiteral("h");
    case QtUi:         return QStringLiteral("ui");
    }
    return QString();
}

void FormCodeView::setHighlighter(Language lang)
{
    delete m_highlighter;
    m_highlighter = nullptr;
    QTextDocument *doc = m_editor->document();
    switch (lang) {
    case KayteProgram: m_highlighter = new KayteSyntaxHighlighter(doc); break;
    case KjsProgram:
    case QtCpp:        m_highlighter = new CppSyntaxHighlighter(doc);   break;
    case BasicProgram:
    case InFormForm:   m_highlighter = new VBSyntaxHighlighter(doc);    break;
    case CobolProgram: m_highlighter = new CobolSyntaxHighlighter(doc);  break;
    case KayteForm:
    case QtUi:         break;
    }
}

// Tints the first line that mentions the widget selected on the canvas.
void FormCodeView::highlightSelected()
{
    QList<QTextEdit::ExtraSelection> extras;
    const int idx = m_canvas->selectedIndex();
    if (idx >= 0 && idx < m_canvas->items().size()) {
        const QString name = m_canvas->items().at(idx).objectName;
        QTextCursor found = m_editor->document()->find(
            name, 0, QTextDocument::FindCaseSensitively | QTextDocument::FindWholeWords);
        if (!found.isNull()) {
            QTextEdit::ExtraSelection sel;
            sel.cursor = QTextCursor(found.block());
            sel.format.setBackground(palette().color(QPalette::Highlight).lighter(170));
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            extras << sel;
            m_editor->setTextCursor(QTextCursor(found.block()));
            m_editor->ensureCursorVisible();
        }
    }
    m_editor->setExtraSelections(extras);
}

void FormCodeView::onSaveAs()
{
    const auto lang = Language(m_language->currentData().toInt());
    const QString suffix = fileSuffix(lang);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Generated Code"),
        QStringLiteral("Form1.") + suffix,
        tr("%1 files (*.%2);;All files (*)").arg(suffix.toUpper(), suffix));
    if (path.isEmpty()) return;

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text) ||
        out.write(m_editor->toPlainText().toUtf8()) < 0 || !out.commit()) {
        QMessageBox::critical(this, tr("Save Failed"), tr("Could not write %1").arg(path));
        return;
    }
    emit fileSaved(path);
}
