// informintegration.cpp - KayteIDE
#include "informintegration.h"
#include "uicanvaswidget.h"

#include "InFormConvert.hpp"
#include "InFormKayte.hpp"
#include "InFormWeb.hpp"
#include "InFormPreview.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStatusBar>
#include <QUrl>

using InForm::ControlType;

// InForm fields the canvas has no column for ride along in
// CanvasItem::properties under '@' keys; plain keys are InForm properties.
namespace {

const QString kText       = QStringLiteral("@Text");
const QString kToolTip    = QStringLiteral("@ToolTip");
const QString kMask       = QStringLiteral("@Mask");
const QString kItems      = QStringLiteral("@Items");
const QString kFontFiles  = QStringLiteral("@FontFiles");
const QString kFontSize   = QStringLiteral("@FontSize");
const QString kContext    = QStringLiteral("@ContextMenu");
const QString kDefault    = QStringLiteral("@Default");
const QString kGif        = QStringLiteral("@Gif");
const QString kAutoGif    = QStringLiteral("@AutoPlayGif");
const QString kExtra      = QStringLiteral("@Extra");
const QString kKfmExtra   = QStringLiteral("@KfmExtra");
// A control the canvas cannot show where it really sits (menu entries, hidden
// off-form controls) is parked elsewhere: @Placed is where it was parked,
// @Geometry its real .frm geometry, restored on export if it was not moved.
const QString kPlaced     = QStringLiteral("@Placed");
const QString kGeometry   = QStringLiteral("@Geometry");
// Offset of a control inside its Frame at import. InForm lets children overflow
// their frame, so containment alone would hand them to a neighbouring frame.
const QString kLocal      = QStringLiteral("@Local");

constexpr int kMenuColumnWidth = 170;
constexpr int kMenuRowHeight   = 22;

bool isMenuType(ControlType t)
{
    return t == ControlType::MenuBar || t == ControlType::MenuItem || t == ControlType::ContextMenu;
}

ControlType controlTypeOf(const CanvasItem &ci)
{
    if (InForm::isPaletteKey(ci.widgetType)) return InForm::typeFromPaletteKey(ci.widgetType);
    return InForm::typeFromQtClass(ci.widgetType);
}

bool hasCaption(ControlType t)
{
    switch (t) {
    case ControlType::ListBox: case ControlType::DropdownList: case ControlType::TrackBar:
    case ControlType::PictureBox: case ControlType::ToggleSwitch: case ControlType::ContextMenu:
        return false;
    default:
        return true;
    }
}

} // namespace

InFormIntegration::InFormIntegration(UiCanvasWidget *canvas, QMainWindow *window)
    : QObject(window)
    , m_canvas(canvas)
    , m_window(window)
{
}

// ─── Document -> canvas ───────────────────────────────────────────────────────

void InFormIntegration::loadDocument(const InForm::Document &doc)
{
    const InForm::Control *form = doc.form();
    m_formMeta = form ? *form : InForm::Control {};
    m_bindings = doc.bindings();
    m_docExtra = doc.extraLines();

    QSize bounds;
    const QList<CanvasItem> items = itemsFromDocument(doc, &bounds);
    m_canvas->setItems(items);
    m_canvas->resize(bounds);
}

void InFormIntegration::updateFormFromDocument(const InForm::Document &doc)
{
    const InForm::Control *form = doc.form();
    if (!form) return;
    if (m_formMeta.type != ControlType::Form) m_formMeta = *form;
    m_formMeta.name     = form->name;
    m_formMeta.caption  = form->caption;
    m_formMeta.geometry = form->geometry;
}

QList<CanvasItem> InFormIntegration::itemsFromDocument(const InForm::Document &doc,
                                                       QSize *canvasSize) const
{
    const InForm::Control *form = doc.form();
    const QSize formSize = form ? form->geometry.size() : QSize(640, 480);
    const QRect formRect(QPoint(0, 0), formSize);

    // Where each control lands on the canvas, in form coordinates.
    QHash<QString, QPoint> placed;
    QHash<QString, int>    menuColumn;   // root menu -> column index
    QHash<QString, int>    menuRows;     // root menu -> items placed so far
    int offFormY = formSize.height() + 24;

    auto rootMenuOf = [&](const InForm::Control &c, int *depth) {
        const InForm::Control *cur = &c;
        *depth = 0;
        while (cur->type == ControlType::MenuItem) {
            const InForm::Control *p = doc.find(cur->parentName);
            if (!p) break;
            cur = p;
            ++*depth;
        }
        return cur->name;
    };

    QList<CanvasItem> items;
    for (const InForm::Control &c : doc.controls()) {
        if (c.type == ControlType::Form || c.type == ControlType::MenuPanel || c.type == ControlType::Font)
            continue;

        CanvasItem ci;
        ci.widgetType = InForm::paletteKey(c.type);
        ci.objectName = c.name;
        ci.labelText  = c.caption;
        ci.parentName = c.parentName;
        ci.properties = c.properties;

        QRect rect;
        bool parked = false;
        if (c.type == ControlType::MenuItem || c.type == ControlType::ContextMenu) {
            // Menus have no place on the form: lay each one out as a column
            // to the right of it, the way it looks when opened.
            int depth = 0;
            const QString root = c.type == ControlType::ContextMenu ? c.name : rootMenuOf(c, &depth);
            if (!menuColumn.contains(root)) menuColumn.insert(root, int(menuColumn.size()));
            const int row = menuRows.value(root, c.type == ControlType::ContextMenu ? 0 : 1);
            menuRows[root] = row + 1;
            const int indent = qMax(0, depth - 1) * 12; // submenu entries step right
            rect = QRect(formSize.width() + 24 + menuColumn[root] * kMenuColumnWidth + indent,
                         8 + row * kMenuRowHeight, kMenuColumnWidth - 16 - indent, 18);
            parked = true;
        } else {
            const InForm::Control *parent = c.parentName.isEmpty() ? nullptr : doc.find(c.parentName);
            const QPoint origin = parent && parent->type == ControlType::Frame
                                      ? placed.value(parent->name.toLower()) : QPoint();
            rect = QRect(origin + c.geometry.topLeft(), c.geometry.size());
            if (parent && parent->type == ControlType::Frame)
                ci.properties.insert(kLocal, c.geometry.topLeft());
            // Top-level controls InForm keeps off-form (e.g. at -600,-600 until
            // shown) go below the form so they can still be selected.
            if (!parent && !isMenuType(c.type) && !formRect.intersects(rect)) {
                rect.moveTopLeft(QPoint(8, offFormY));
                offFormY += rect.height() + 16;
                parked = true;
            }
        }
        placed.insert(c.name.toLower(), rect.topLeft());
        ci.geometry = rect;
        if (parked) {
            ci.properties.insert(kPlaced, rect);
            ci.properties.insert(kGeometry, c.geometry);
        }

        if (!c.text.isEmpty())        ci.properties.insert(kText, c.text);
        if (!c.toolTip.isEmpty())     ci.properties.insert(kToolTip, c.toolTip);
        if (!c.mask.isEmpty())        ci.properties.insert(kMask, c.mask);
        if (!c.items.isEmpty())       ci.properties.insert(kItems, c.items);
        if (!c.fontFiles.isEmpty())   ci.properties.insert(kFontFiles, c.fontFiles);
        if (c.fontSize > 0)           ci.properties.insert(kFontSize, c.fontSize);
        if (!c.contextMenu.isEmpty()) ci.properties.insert(kContext, c.contextMenu);
        if (c.isDefaultButton)        ci.properties.insert(kDefault, true);
        if (c.animatedGif)            ci.properties.insert(kGif, true);
        if (c.autoPlayGif)            ci.properties.insert(kAutoGif, true);
        if (!c.extraLines.isEmpty())  ci.properties.insert(kExtra, c.extraLines);
        if (!c.kfmExtra.isEmpty())    ci.properties.insert(kKfmExtra, c.kfmExtra);
        items << ci;
    }

    // Grow the canvas to hold the form plus the menu columns / parked controls.
    QRect bounds(formRect);
    for (const CanvasItem &ci : items) bounds |= ci.geometry;
    if (canvasSize) *canvasSize = QSize(bounds.width() + 24, bounds.height() + 24);
    return items;
}

// ─── Canvas -> document ───────────────────────────────────────────────────────

InForm::Document InFormIntegration::documentFromCanvas(int *skipped) const
{
    InForm::Document doc;
    const QList<CanvasItem> &items = m_canvas->items();
    int skippedCount = 0;

    // The form: what was imported, or a fresh one sized around the content.
    InForm::Control form = m_formMeta;
    if (form.type != ControlType::Form) {
        form = InForm::Control {};
        form.type = ControlType::Form;
        form.name = QStringLiteral("Form1");
        form.caption = QStringLiteral("Form1");
        QRect bounds(0, 0, 320, 240);
        for (const CanvasItem &ci : items)
            if (!ci.properties.contains(kPlaced) && !isMenuType(controlTypeOf(ci)))
                bounds |= ci.geometry;
        form.geometry = QRect(0, 0, bounds.right() + 16, bounds.bottom() + 16);
    }
    doc.controls() << form;
    doc.bindings() = m_bindings;
    doc.extraLines() = m_docExtra;

    // Frames on the canvas, to work out which one each control sits in.
    struct FrameRef { QString name; QRect rect; };
    QList<FrameRef> frames;
    for (const CanvasItem &ci : items)
        if (controlTypeOf(ci) == ControlType::Frame) frames << FrameRef { ci.objectName, ci.geometry };

    for (const CanvasItem &ci : items) {
        const ControlType type = controlTypeOf(ci);
        if (type == ControlType::None || type == ControlType::Form) { ++skippedCount; continue; }

        InForm::Control c;
        c.type = type;
        c.name = ci.objectName;
        if (hasCaption(type)) c.caption = ci.labelText;

        const QVariantMap &p = ci.properties;
        for (auto it = p.cbegin(); it != p.cend(); ++it)
            if (!it.key().startsWith(QLatin1Char('@'))) c.properties.insert(it.key(), it.value());
        if (!InForm::isPaletteKey(ci.widgetType)) c.properties = InForm::defaultProperties(type);

        c.text        = p.value(kText).toString();
        c.toolTip     = p.value(kToolTip).toString();
        c.mask        = p.value(kMask).toString();
        c.items       = p.value(kItems).toStringList();
        c.fontFiles   = p.value(kFontFiles).toString();
        c.fontSize    = p.value(kFontSize).toInt();
        c.contextMenu = p.value(kContext).toString();
        c.isDefaultButton = p.value(kDefault).toBool();
        c.animatedGif = p.value(kGif).toBool();
        c.autoPlayGif = p.value(kAutoGif).toBool();
        c.extraLines  = p.value(kExtra).toStringList();
        c.kfmExtra    = p.value(kKfmExtra).toStringList();

        const bool unmoved = p.contains(kPlaced) && p.value(kPlaced).toRect() == ci.geometry;

        if (type == ControlType::MenuItem) {
            c.parentName = ci.parentName;
            c.geometry = unmoved ? p.value(kGeometry).toRect()
                                 : QRect(0, 22, ci.geometry.width(), 18); // InForm lays menus out itself
        } else if (type == ControlType::ContextMenu) {
            c.geometry = unmoved ? p.value(kGeometry).toRect() : QRect(QPoint(), ci.geometry.size());
        } else if (type == ControlType::MenuBar || type == ControlType::Frame) {
            c.geometry = unmoved ? p.value(kGeometry).toRect() : ci.geometry;
        } else if (unmoved) {
            c.parentName = ci.parentName;
            c.geometry = p.value(kGeometry).toRect();
        } else {
            // Keep the imported frame while the control sits where it did inside
            // it; otherwise take the innermost frame containing its centre.
            const FrameRef *owner = nullptr;
            for (const FrameRef &f : frames)
                if (f.name == ci.parentName && p.contains(kLocal) &&
                    ci.geometry.topLeft() - f.rect.topLeft() == p.value(kLocal).toPoint())
                    owner = &f;
            const QPoint centre = ci.geometry.center();
            for (const FrameRef &f : frames) {
                if (owner && owner->name == ci.parentName) break;
                if (!f.rect.contains(centre)) continue;
                if (f.name == ci.parentName) { owner = &f; break; }
                if (!owner || f.rect.width() * f.rect.height() < owner->rect.width() * owner->rect.height())
                    owner = &f;
            }
            c.parentName = owner ? owner->name : QString();
            c.geometry = owner ? ci.geometry.translated(-owner->rect.topLeft()) : ci.geometry;
        }
        doc.controls() << c;
    }

    if (skipped) *skipped = skippedCount;
    return doc;
}

// ─── Slots ───────────────────────────────────────────────────────────────────

void InFormIntegration::onImportFrm()
{
    if (m_canvas->itemCount() > 0 &&
        QMessageBox::question(m_window, tr("Import InForm Form"),
                              tr("Replace the widgets on the canvas with the imported form?"),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    const QString path = QFileDialog::getOpenFileName(
        m_window, tr("Import InForm Form"),
        m_lastPath.isEmpty() ? QDir::homePath() : QFileInfo(m_lastPath).absolutePath(),
        tr("InForm Forms (*.frm);;All Files (*)"));
    if (path.isEmpty()) return;

    // VB-DOS forms share the .frm extension - convert those instead.
    QFile probe(path);
    if (probe.open(QIODevice::ReadOnly | QIODevice::Text) &&
        InForm::isVbDosForm(QString::fromLatin1(probe.read(256)))) {
        probe.close();
        InForm::Document doc;
        QString error;
        if (!InForm::convertVbDosFile(path, &doc, &error)) {
            QMessageBox::critical(m_window, tr("Conversion Failed"),
                                  tr("Could not convert %1:\n%2").arg(QFileInfo(path).fileName(), error));
            return;
        }
        finishVbDosConversion(path, doc);
        return;
    }

    InForm::Document doc;
    QString error;
    if (!doc.loadFrm(path, &error)) {
        QMessageBox::critical(m_window, tr("Import Failed"),
                              tr("Could not read %1:\n%2").arg(QFileInfo(path).fileName(), error));
        return;
    }
    m_lastPath = path;
    loadDocument(doc);
    m_window->statusBar()->showMessage(
        tr("Imported InForm form \"%1\" - %2 controls").arg(m_formMeta.name).arg(doc.controls().size() - 1),
        5000);
}

void InFormIntegration::onConvertVbDos()
{
    if (m_canvas->itemCount() > 0 &&
        QMessageBox::question(m_window, tr("Convert VB-DOS Form"),
                              tr("Replace the widgets on the canvas with the converted form?"),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    const QString path = QFileDialog::getOpenFileName(
        m_window, tr("Convert VB-DOS Form"),
        m_lastPath.isEmpty() ? QDir::homePath() : QFileInfo(m_lastPath).absolutePath(),
        tr("VB-DOS Forms (*.frm *.FRM);;All Files (*)"));
    if (path.isEmpty()) return;

    InForm::Document doc;
    QString error;
    if (!InForm::convertVbDosFile(path, &doc, &error)) {
        QMessageBox::critical(m_window, tr("Conversion Failed"),
                              tr("Could not convert %1:\n%2").arg(QFileInfo(path).fileName(), error));
        return;
    }
    finishVbDosConversion(path, doc);
}

void InFormIntegration::finishVbDosConversion(const QString &sourcePath, const InForm::Document &doc)
{
    loadDocument(doc);
    m_lastPath.clear(); // the VB-DOS source must not be overwritten by Export .frm

    // vbdos2inform writes <name>_InForm.frm and <name>_InForm.bas next to the source.
    const QFileInfo src(sourcePath);
    const QString suggestion = src.absolutePath() + QLatin1Char('/') + src.completeBaseName() +
                               QStringLiteral("_InForm.frm");
    const QString frmPath = QFileDialog::getSaveFileName(
        m_window, tr("Save Converted InForm Form"), suggestion, tr("InForm Forms (*.frm)"));
    if (frmPath.isEmpty()) {
        m_window->statusBar()->showMessage(
            tr("Converted VB-DOS form \"%1\" onto the canvas (not saved)").arg(src.fileName()), 6000);
        return;
    }

    QString error;
    if (!doc.saveFrm(frmPath, &error)) {
        QMessageBox::critical(m_window, tr("Save Failed"), tr("Could not write %1:\n%2").arg(frmPath, error));
        return;
    }
    m_lastPath = frmPath;

    const QFileInfo frm(frmPath);
    const QString basPath = frm.absolutePath() + QLatin1Char('/') + frm.completeBaseName() + QStringLiteral(".bas");
    QString basNote;
    if (QFileInfo::exists(basPath)) {
        basNote = tr("; %1 already exists and was left untouched").arg(QFileInfo(basPath).fileName());
    } else {
        QFile bas(basPath);
        if (bas.open(QIODevice::WriteOnly | QIODevice::Text))
            bas.write(InForm::eventSkeleton(doc, frm.fileName()).toLatin1());
        else
            basNote = tr("; could not write %1").arg(QFileInfo(basPath).fileName());
    }
    m_window->statusBar()->showMessage(
        tr("Converted %1 -> %2 and %3%4").arg(src.fileName(), frm.fileName(),
                                             QFileInfo(basPath).fileName(), basNote), 8000);
}

void InFormIntegration::onImportKfm()
{
    if (m_canvas->itemCount() > 0 &&
        QMessageBox::question(m_window, tr("Import Kayte Form"),
                              tr("Replace the widgets on the canvas with the Kayte form?"),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    const QString path = QFileDialog::getOpenFileName(
        m_window, tr("Import Kayte Form"),
        m_lastKaytePath.isEmpty() ? QDir::homePath() : QFileInfo(m_lastKaytePath).absolutePath(),
        tr("Kayte Forms (*.kfm);;All Files (*)"));
    if (path.isEmpty()) return;

    InForm::Document doc;
    QString error;
    if (!InForm::loadKfm(path, &doc, &error)) {
        QMessageBox::critical(m_window, tr("Import Failed"),
                              tr("Could not read %1:\n%2").arg(QFileInfo(path).fileName(), error));
        return;
    }
    loadDocument(doc);
    m_lastPath.clear();
    m_lastKaytePath = path;
    m_window->statusBar()->showMessage(
        tr("Imported Kayte form \"%1\" - %2 controls").arg(m_formMeta.name).arg(doc.controls().size() - 1), 5000);
}

void InFormIntegration::onExportKayte()
{
    // The program file picks the language; the .kfm goes next to it.
    QString suggestion = m_lastKaytePath;
    if (suggestion.endsWith(QLatin1String(".kfm"), Qt::CaseInsensitive))
        suggestion = suggestion.chopped(4) + QStringLiteral(".kayte");
    if (suggestion.isEmpty())
        suggestion = QDir::homePath() + QLatin1Char('/') +
                     (m_formMeta.name.isEmpty() ? QStringLiteral("Form1") : m_formMeta.name) +
                     QStringLiteral(".kayte");
    QString filter = tr("Kayte (*.kayte)");
    const QString path = QFileDialog::getSaveFileName(
        m_window, tr("Export Kayte Program and Form"), suggestion,
        tr("Kayte (*.kayte);;Kayte JS (*.kjs);;Kayte BASIC (*.bas)"), &filter,
        QFileDialog::DontConfirmOverwrite); // an existing program is kept - asked below
    if (path.isEmpty()) return;

    QString program = path;
    if (QFileInfo(program).suffix().isEmpty())
        program += filter.contains(QLatin1String("kjs")) ? QStringLiteral(".kjs")
                 : filter.contains(QLatin1String("bas")) ? QStringLiteral(".bas") : QStringLiteral(".kayte");
    const InForm::KayteLanguage lang = InForm::kayteLanguageForFile(program);
    const QFileInfo pi(program);
    const QString kfmPath = pi.absolutePath() + QLatin1Char('/') + pi.completeBaseName() + QStringLiteral(".kfm");

    int skipped = 0;
    const InForm::Document doc = documentFromCanvas(&skipped);
    QStringList warnings;
    QString error;
    if (!InForm::saveKfm(doc, kfmPath, &warnings, &error)) {
        QMessageBox::critical(m_window, tr("Export Failed"), tr("Could not write %1:\n%2").arg(kfmPath, error));
        return;
    }

    // Never overwrite code already written: an existing program only gets
    // stubs for handlers it is missing, unless the user asks to replace it.
    QString programNote;
    QFile existing(program);
    bool writeFresh = !existing.exists();
    if (!writeFresh) {
        QMessageBox box(QMessageBox::Question, tr("Export Kayte Program"),
                        tr("%1 already exists.").arg(pi.fileName()), QMessageBox::NoButton, m_window);
        box.setInformativeText(tr("The form (%1) has been updated. Keep your code and only add the "
                                  "missing event handlers, or replace the program?")
                                   .arg(QFileInfo(kfmPath).fileName()));
        QPushButton *keep = box.addButton(tr("Keep Code, Add Handlers"), QMessageBox::AcceptRole);
        QPushButton *replace = box.addButton(tr("Replace Program"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(keep);
        box.exec();
        if (box.clickedButton() == replace) {
            writeFresh = true;
        } else if (box.clickedButton() == keep) {
            if (!existing.open(QIODevice::ReadWrite | QIODevice::Text)) {
                QMessageBox::critical(m_window, tr("Export Failed"),
                                      tr("Could not open %1:\n%2").arg(program, existing.errorString()));
                return;
            }
            const QString source = QString::fromUtf8(existing.readAll());
            const QString added = InForm::missingKayteHandlers(doc, source, lang);
            if (!added.isEmpty()) {
                existing.seek(existing.size());
                existing.write(added.toUtf8());
                programNote = tr("added missing handlers to %1").arg(pi.fileName());
            } else {
                programNote = tr("%1 already has every handler").arg(pi.fileName());
            }
            existing.close();
        } else {
            programNote = tr("%1 left unchanged").arg(pi.fileName());
        }
    }
    if (writeFresh) {
        QSaveFile out(program);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text) ||
            out.write(InForm::kayteProgram(doc, QFileInfo(kfmPath).fileName(), lang).toUtf8()) < 0 ||
            !out.commit()) {
            QMessageBox::critical(m_window, tr("Export Failed"), tr("Could not write %1").arg(program));
            return;
        }
        programNote = tr("wrote %1").arg(pi.fileName());
    }
    m_lastKaytePath = program;

    if (skipped > 0)
        warnings << tr("%n canvas widget(s) with no InForm equivalent were left out", nullptr, skipped);
    m_window->statusBar()->showMessage(
        tr("Kayte export: wrote %1, %2").arg(QFileInfo(kfmPath).fileName(), programNote), 8000);
    if (!warnings.isEmpty())
        QMessageBox::information(m_window, tr("Kayte Export"),
                                 tr("The form was exported with these limits of Kayte forms:\n\n- %1")
                                     .arg(warnings.join(QStringLiteral("\n- "))));
}

void InFormIntegration::onExportWeb()
{
    // The web app runs a .kjs program - the same file the desktop runs through
    // libkayte_qt6 - so pick (or create) that program first.
    QString suggestion = m_lastKaytePath;
    if (!suggestion.isEmpty())
        suggestion = QFileInfo(suggestion).absolutePath() + QLatin1Char('/') +
                     QFileInfo(suggestion).completeBaseName() + QStringLiteral(".kjs");
    else
        suggestion = QDir::homePath() + QLatin1Char('/') +
                     (m_formMeta.name.isEmpty() ? QStringLiteral("Form1") : m_formMeta.name) + QStringLiteral(".kjs");
    QString program = QFileDialog::getSaveFileName(
        m_window, tr("Web App - Choose the .kjs Program"), suggestion,
        tr("Kayte JS (*.kjs)"), nullptr, QFileDialog::DontConfirmOverwrite); // existing code is kept
    if (program.isEmpty()) return;
    if (!program.endsWith(QLatin1String(".kjs"), Qt::CaseInsensitive)) program += QStringLiteral(".kjs");

    const QFileInfo pi(program);
    const QString kfmName = pi.completeBaseName() + QStringLiteral(".kfm");
    const QString kfmPath = pi.absolutePath() + QLatin1Char('/') + kfmName;

    int skipped = 0;
    const InForm::Document doc = documentFromCanvas(&skipped);
    QStringList warnings;
    QString error;

    // Keep the desktop side in step: the same .kfm and .kjs run there.
    QStringList kfmWarnings;
    if (!InForm::saveKfm(doc, kfmPath, &kfmWarnings, &error)) {
        QMessageBox::critical(m_window, tr("Export Failed"), tr("Could not write %1:\n%2").arg(kfmPath, error));
        return;
    }
    QString programNote;
    QFile existing(program);
    if (!existing.exists()) {
        QSaveFile out(program);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text) ||
            out.write(InForm::kayteProgram(doc, kfmName, InForm::KayteLanguage::Kjs).toUtf8()) < 0 || !out.commit()) {
            QMessageBox::critical(m_window, tr("Export Failed"), tr("Could not write %1").arg(program));
            return;
        }
        programNote = tr("wrote %1").arg(pi.fileName());
    } else if (existing.open(QIODevice::ReadWrite | QIODevice::Text)) {
        const QString added = InForm::missingKayteHandlers(doc, QString::fromUtf8(existing.readAll()),
                                                           InForm::KayteLanguage::Kjs);
        if (!added.isEmpty()) {
            existing.seek(existing.size());
            existing.write(added.toUtf8());
            programNote = tr("added missing handlers to %1").arg(pi.fileName());
        } else {
            programNote = tr("kept %1").arg(pi.fileName());
        }
        existing.close();
    }
    m_lastKaytePath = program;

    const QString outDir = pi.absolutePath() + QLatin1Char('/') + pi.completeBaseName() + QStringLiteral("_web");
    InForm::WebExport web;
    if (!InForm::exportWebApp(doc, program, kfmName, outDir,
                              m_lastPath.isEmpty() ? pi.absolutePath() : QFileInfo(m_lastPath).absolutePath(),
                              &web, &error)) {
        QMessageBox::critical(m_window, tr("Export Failed"), error);
        return;
    }
    warnings << web.warnings;
    if (skipped > 0)
        warnings << tr("%n canvas widget(s) with no InForm equivalent were left out", nullptr, skipped);
    if (!kfmWarnings.isEmpty())
        warnings << tr("Desktop only (the web page shows every control): %1").arg(kfmWarnings.join(QStringLiteral("; ")));

    m_window->statusBar()->showMessage(
        tr("Web app: %1 (%2, %3 updated)").arg(QDir::toNativeSeparators(web.indexPath), programNote, kfmName), 8000);

    QMessageBox box(QMessageBox::Information, tr("Web App Exported"),
                    tr("The web app is in %1.").arg(QDir::toNativeSeparators(outDir)), QMessageBox::NoButton, m_window);
    QString info = tr("It runs %1 in the browser through kayte-web.js. Open index.html directly, or serve "
                      "the folder from any web server.").arg(pi.fileName());
    if (!warnings.isEmpty())
        info += tr("\n\nNotes:\n- %1").arg(warnings.join(QStringLiteral("\n- ")));
    box.setInformativeText(info);
    QPushButton *open = box.addButton(tr("Open in Browser"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Close);
    box.setDefaultButton(open);
    box.exec();
    if (box.clickedButton() == open)
        QDesktopServices::openUrl(QUrl::fromLocalFile(web.indexPath));
}

void InFormIntegration::onExportFrm()
{
    QString suggestion = m_lastPath;
    if (suggestion.isEmpty())
        suggestion = QDir::homePath() + QLatin1Char('/') +
                     (m_formMeta.name.isEmpty() ? QStringLiteral("Form1") : m_formMeta.name) +
                     QStringLiteral(".frm");
    const QString path = QFileDialog::getSaveFileName(
        m_window, tr("Export InForm Form"), suggestion, tr("InForm Forms (*.frm);;All Files (*)"));
    if (path.isEmpty()) return;

    int skipped = 0;
    const InForm::Document doc = documentFromCanvas(&skipped);
    QString error;
    if (!doc.saveFrm(path, &error)) {
        QMessageBox::critical(m_window, tr("Export Failed"), tr("Could not write %1:\n%2").arg(path, error));
        return;
    }
    m_lastPath = path;

    // Like InForm's File > Save: the .frm comes with a .bas main program. An
    // existing .bas holds the user's code, so it is never touched.
    const QFileInfo frm(path);
    const QString basPath = frm.absolutePath() + QLatin1Char('/') + frm.completeBaseName() + QStringLiteral(".bas");
    QString basNote;
    if (QFileInfo::exists(basPath)) {
        basNote = tr(" (%1 kept as is)").arg(QFileInfo(basPath).fileName());
    } else {
        QSaveFile bas(basPath);
        if (bas.open(QIODevice::WriteOnly | QIODevice::Text) &&
            bas.write(InForm::eventSkeleton(doc, frm.fileName()).toLatin1()) >= 0 && bas.commit())
            basNote = tr(" + %1").arg(QFileInfo(basPath).fileName());
        else
            basNote = tr(" (could not write %1)").arg(QFileInfo(basPath).fileName());
    }

    QString msg = tr("Exported InForm form: %1%2").arg(path, basNote);
    if (skipped > 0) msg += tr(" (%n widget(s) with no InForm equivalent left out)", nullptr, skipped);
    m_window->statusBar()->showMessage(msg, 6000);
}

void InFormIntegration::onPreview()
{
    int skipped = 0;
    const InForm::Document doc = documentFromCanvas(&skipped);
    auto *dlg = new InForm::PreviewDialog(
        doc, m_lastPath.isEmpty() ? QString() : QFileInfo(m_lastPath).absolutePath(), m_window);
    dlg->show();
    if (skipped > 0)
        m_window->statusBar()->showMessage(
            tr("Preview: %n widget(s) with no InForm equivalent are not shown", nullptr, skipped), 5000);
}
