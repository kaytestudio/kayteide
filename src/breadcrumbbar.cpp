#include "breadcrumbbar.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStyle>
#include <QToolButton>

namespace {
QString g_projectRoot;
constexpr int kMaxMenuEntries = 400;

QToolButton *crumb(QWidget *parent, const QString &text, const QString &toolTip)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(toolTip);
    b->setAccessibleName(toolTip);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setStyleSheet(QStringLiteral("QToolButton { padding: 0 3px; border: none; color: palette(placeholder-text); }"
                                    "QToolButton:hover { color: palette(text); background: palette(midlight); }"));
    return b;
}
} // namespace

BreadcrumbBar::BreadcrumbBar(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(6, 1, 6, 1);
    m_layout->setSpacing(0);
    setAccessibleName(tr("Breadcrumbs"));
    setFont(QApplication::font("QToolButton"));
}

void BreadcrumbBar::setProjectRoot(const QString &dir) { g_projectRoot = dir; }
QString BreadcrumbBar::projectRoot() { return g_projectRoot; }

void BreadcrumbBar::setFile(const QString &path)
{
    m_file = path;
    m_path.clear();
    rebuild();
}

void BreadcrumbBar::setSymbols(const QVector<CodeSymbol> &symbols)
{
    m_symbols = symbols;
    m_path = CodeSymbols::pathAt(m_symbols, m_line);
    rebuild();
}

void BreadcrumbBar::setCursorLine(int line)
{
    m_line = line;
    const QVector<int> path = CodeSymbols::pathAt(m_symbols, line);
    if (path == m_path) return;
    m_path = path;
    rebuild();
}

void BreadcrumbBar::refresh() { rebuild(); }

void BreadcrumbBar::addSeparator()
{
    auto *sep = new QLabel(QStringLiteral("›"), this);   // ›
    sep->setContentsMargins(2, 0, 2, 0);
    sep->setForegroundRole(QPalette::PlaceholderText);
    m_layout->addWidget(sep);
}

void BreadcrumbBar::rebuild()
{
    // Deferred: a crumb's own click (its menu) may be what triggered this.
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *w = item->widget()) { w->hide(); w->deleteLater(); }
        delete item;
    }
    if (m_file.isEmpty()) {
        m_layout->addWidget(crumb(this, tr("Untitled"), tr("Not saved yet")));
        m_layout->addStretch(1);
        return;
    }

    // Folders from the project root (or the last few, outside it).
    const QFileInfo fi(m_file);
    QString dir = fi.absolutePath();
    QStringList parts;
    QString base;
    const QString root = g_projectRoot.isEmpty() ? QString() : QDir(g_projectRoot).absolutePath();
    if (!root.isEmpty() && (dir == root || dir.startsWith(root + QLatin1Char('/')))) {
        base = root;
        parts = QDir(root).relativeFilePath(dir).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        parts.removeAll(QStringLiteral("."));
    } else {
        parts = dir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const int keep = qMin(3, int(parts.size()));
        base = QLatin1Char('/') + parts.mid(0, parts.size() - keep).join(QLatin1Char('/'));
        parts = parts.mid(parts.size() - keep);
    }

    QString folder = base;
    for (const QString &part : std::as_const(parts)) {
        const QString parent = folder;
        folder = QDir(folder).filePath(part);
        auto *b = crumb(this, part, QDir::toNativeSeparators(folder));
        const QString target = folder;
        connect(b, &QToolButton::clicked, this, [this, b, parent, target] {
            QMenu menu(this);
            fillFolderMenu(&menu, parent, target);
            menu.exec(b->mapToGlobal(QPoint(0, b->height())));
        });
        m_layout->addWidget(b);
        addSeparator();
    }

    auto *file = crumb(this, fi.fileName(), QDir::toNativeSeparators(m_file));
    file->setStyleSheet(file->styleSheet() + QStringLiteral("QToolButton { color: palette(text); }"));
    connect(file, &QToolButton::clicked, this, [this, file] {
        QMenu menu(this);
        fillFolderMenu(&menu, QFileInfo(m_file).absolutePath(), m_file);
        menu.exec(file->mapToGlobal(QPoint(0, file->height())));
    });
    m_layout->addWidget(file);

    for (int idx : std::as_const(m_path)) {
        addSeparator();
        const CodeSymbol &s = m_symbols[idx];
        auto *b = crumb(this, CodeSymbols::glyph(s.kind) + QLatin1Char(' ') + s.name,
                        tr("%1 - line %2. Click for the file's outline").arg(s.name).arg(s.line));
        connect(b, &QToolButton::clicked, this, [this, b] { showOutline(b->mapToGlobal(QPoint(0, b->height()))); });
        m_layout->addWidget(b);
    }
    m_layout->addStretch(1);
}

// `dir`'s folders (as submenus, filled when opened) and files; `current` is
// checked.
void BreadcrumbBar::fillFolderMenu(QMenu *menu, const QString &dir, const QString &current)
{
    const QFileInfoList entries = QDir(dir).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    const QIcon folderIcon = style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon fileIcon   = style()->standardIcon(QStyle::SP_FileIcon);
    int count = 0;
    for (const QFileInfo &e : entries) {
        if (++count > kMaxMenuEntries) {
            menu->addAction(tr("..."))->setEnabled(false);
            break;
        }
        const QString path = e.absoluteFilePath();
        if (e.isDir()) {
            QMenu *sub = menu->addMenu(folderIcon, e.fileName());
            if (path == current) sub->menuAction()->setFont([&] { QFont f = menu->font(); f.setBold(true); return f; }());
            connect(sub, &QMenu::aboutToShow, this, [this, sub, path] {
                if (sub->isEmpty()) fillFolderMenu(sub, path, QString());
            });
        } else {
            QAction *a = menu->addAction(fileIcon, e.fileName());
            if (path == current) { a->setCheckable(true); a->setChecked(true); }
            connect(a, &QAction::triggered, this, [this, path] { emit openFileRequested(path); });
        }
    }
    if (entries.isEmpty()) menu->addAction(tr("(empty)"))->setEnabled(false);
}

void BreadcrumbBar::showOutline(const QPoint &pos)
{
    QMenu menu(this);
    if (m_symbols.isEmpty()) menu.addAction(tr("No symbols in this file"))->setEnabled(false);
    const int current = m_path.isEmpty() ? -1 : m_path.last();
    for (int i = 0; i < m_symbols.size() && i < kMaxMenuEntries; ++i) {
        const CodeSymbol &s = m_symbols[i];
        QAction *a = menu.addAction(QString(s.level * 4, QLatin1Char(' ')) + CodeSymbols::glyph(s.kind)
                                    + QLatin1Char(' ') + s.name);
        a->setToolTip(tr("Line %1").arg(s.line));
        if (i == current) { a->setCheckable(true); a->setChecked(true); menu.setActiveAction(a); }
        const int line = s.line;
        connect(a, &QAction::triggered, this, [this, line] { emit symbolActivated(line); });
    }
    menu.exec(pos);
}
