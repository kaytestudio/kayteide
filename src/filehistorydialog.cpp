#include "filehistorydialog.h"

#include <algorithm>
#include <utility>

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// git's well-known empty tree: the "parent" of a root commit.
const QString kEmptyTree = QStringLiteral("4b825dc642cb6eb9a060e54bf8d69288fbe4904b");

bool isDarkPalette(const QPalette &p)
{
    return p.color(QPalette::Base).lightness() < 128;
}

class DiffGutter : public QWidget
{
public:
    explicit DiffGutter(DiffPane *pane) : QWidget(pane), m_pane(pane) {}
    QSize sizeHint() const override { return QSize(m_pane->gutterWidth(), 0); }
protected:
    void paintEvent(QPaintEvent *event) override { m_pane->paintGutter(event); }
private:
    DiffPane *m_pane;
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// DiffPane
// ─────────────────────────────────────────────────────────────────────────────
DiffPane::DiffPane(QWidget *parent)
    : QPlainTextEdit(parent), m_gutter(new DiffGutter(this))
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        if (dy) m_gutter->scroll(0, dy);
        else    m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
    });
}

int DiffPane::gutterWidth() const
{
    int maxLine = 1;
    for (const Row &r : m_rows) maxLine = qMax(maxLine, r.lineNo);
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9'))
                * QString::number(maxLine).size();
}

int DiffPane::topRow() const
{
    return firstVisibleBlock().blockNumber();
}

void DiffPane::setRows(const QStringList &text, const QVector<Row> &rows)
{
    m_rows = rows;
    clear();
    setViewportMargins(gutterWidth(), 0, 0, 0);

    const bool dark = isDarkPalette(palette());
    const QColor removed = dark ? QColor(0x4b, 0x1c, 0x1c) : QColor(0xff, 0xeb, 0xe9);
    const QColor added   = dark ? QColor(0x1b, 0x3d, 0x26) : QColor(0xe6, 0xff, 0xec);
    const QColor padding = dark ? QColor(0x2b, 0x2b, 0x2b) : QColor(0xf2, 0xf2, 0xf2);

    QTextCursor cur(document());
    cur.beginEditBlock();
    for (int i = 0; i < rows.size(); ++i) {
        QTextBlockFormat fmt;
        switch (rows[i].kind) {
        case Removed: fmt.setBackground(removed); break;
        case Added:   fmt.setBackground(added);   break;
        case Padding: fmt.setBackground(padding); break;
        case Same:    break;
        }
        if (i == 0) cur.setBlockFormat(fmt);
        else        cur.insertBlock(fmt);
        cur.insertText(text.value(i));
    }
    cur.endEditBlock();
    moveCursor(QTextCursor::Start);
}

void DiffPane::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

void DiffPane::paintGutter(QPaintEvent *event)
{
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), palette().color(QPalette::AlternateBase));
    painter.setPen(palette().color(QPalette::PlaceholderText));

    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    const int h = fontMetrics().height();
    while (block.isValid() && top <= event->rect().bottom()) {
        const int n = block.blockNumber();
        if (block.isVisible() && n < m_rows.size() && m_rows[n].lineNo > 0)
            painter.drawText(0, top, m_gutter->width() - 6, h, Qt::AlignRight,
                             QString::number(m_rows[n].lineNo));
        top += qRound(blockBoundingRect(block).height());
        block = block.next();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FileHistoryDialog
// ─────────────────────────────────────────────────────────────────────────────
FileHistoryDialog::FileHistoryDialog(const QString &filePath, QWidget *parent)
    : QDialog(parent), m_filePath(QFileInfo(filePath).canonicalFilePath())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("History - %1").arg(QFileInfo(filePath).fileName()));
    resize(1280, 820);

    m_list = new QTreeWidget(this);
    m_list->setHeaderLabels({tr("Commit"), tr("Date"), tr("Author"), tr("Message")});
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setToolTip(tr("Select one commit to see what it changed, "
                          "or two (⌘/Ctrl-click) to compare them."));
    m_list->header()->setStretchLastSection(true);

    m_title = new QLabel(this);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_title->setWordWrap(true);

    auto *prev = new QToolButton(this);
    prev->setText(tr("▲ Previous change"));
    prev->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    auto *next = new QToolButton(this);
    next->setText(tr("▼ Next change"));
    next->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
    connect(prev, &QToolButton::clicked, this, [this] { jumpToChange(-1); });
    connect(next, &QToolButton::clicked, this, [this] { jumpToChange(+1); });

    auto *bar = new QHBoxLayout;
    bar->addWidget(m_title, 1);
    bar->addWidget(prev);
    bar->addWidget(next);

    m_left  = new DiffPane(this);
    m_right = new DiffPane(this);
    // Both panes always hold the same number of rows, so mirror the scrolling.
    for (auto [a, b] : {std::pair{m_left, m_right}, std::pair{m_right, m_left}}) {
        connect(a->verticalScrollBar(), &QScrollBar::valueChanged,
                b->verticalScrollBar(), &QScrollBar::setValue);
        connect(a->horizontalScrollBar(), &QScrollBar::valueChanged,
                b->horizontalScrollBar(), &QScrollBar::setValue);
    }

    auto *panes = new QSplitter(Qt::Horizontal, this);
    panes->addWidget(m_left);
    panes->addWidget(m_right);

    auto *diffArea = new QWidget(this);
    auto *diffLay = new QVBoxLayout(diffArea);
    diffLay->setContentsMargins(0, 0, 0, 0);
    diffLay->addLayout(bar);
    diffLay->addWidget(panes, 1);

    auto *split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_list);
    split->addWidget(diffArea);
    split->setStretchFactor(1, 1);
    split->setSizes({220, 600});

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(split);

    connect(m_list, &QTreeWidget::itemSelectionChanged,
            this, &FileHistoryDialog::onSelectionChanged);

    loadHistory();
}

QString FileHistoryDialog::git(const QStringList &args, bool *ok) const
{
    QProcess p;
    p.setWorkingDirectory(m_repoRoot.isEmpty() ? QFileInfo(m_filePath).absolutePath()
                                               : m_repoRoot);
    p.start(QStringLiteral("git"), args);
    const bool done = p.waitForFinished(30000);
    if (ok) *ok = done && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return QString::fromUtf8(p.readAllStandardOutput());
}

void FileHistoryDialog::loadHistory()
{
    bool ok = false;
    m_repoRoot = git({"rev-parse", "--show-toplevel"}, &ok).trimmed();
    if (!ok || m_repoRoot.isEmpty()) {
        m_repoRoot.clear();
        m_title->setText(tr("<b>%1</b> is not inside a git repository (or git is not installed).")
                             .arg(m_filePath.toHtmlEscaped()));
        return;
    }
    m_repoRoot = QFileInfo(m_repoRoot).canonicalFilePath();
    const QString rel = QDir(m_repoRoot).relativeFilePath(m_filePath);

    const QString log = git({"log", "--follow", "--name-status", "--date=format:%Y-%m-%d %H:%M",
                             "--format=%x1e%H%x1f%P%x1f%an%x1f%ad%x1f%s", "--", rel});

    // Walk newest -> oldest, following the file back through renames.
    QString currentPath = rel;
    for (const QString &record : log.split(QChar(0x1e), Qt::SkipEmptyParts)) {
        const QStringList lines = record.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QStringList f = lines.value(0).split(QChar(0x1f));
        if (f.size() < 5) continue;

        Commit c;
        c.hash    = f[0];
        c.parent  = f[1].section(QLatin1Char(' '), 0, 0);
        c.author  = f[2];
        c.date    = f[3];
        c.subject = f[4];
        c.path = c.oldPath = currentPath;
        for (int i = 1; i < lines.size(); ++i) {
            const QStringList st = lines[i].split(QLatin1Char('\t'));
            if (st.size() >= 3 && (st[0].startsWith('R') || st[0].startsWith('C'))) {
                c.oldPath = st[1];
                c.path    = st[2];
            } else if (st.size() == 2) {
                c.path = c.oldPath = st[1];
            }
        }
        if (c.parent.isEmpty()) c.parent = kEmptyTree;
        currentPath = c.oldPath;
        m_commits.append(c);
    }

    m_hasLocalChanges = !git({"status", "--porcelain", "--", rel}).trimmed().isEmpty();

    if (m_hasLocalChanges) {
        auto *item = new QTreeWidgetItem(m_list, {tr("Working copy"), QString(), QString(),
                                                  tr("Uncommitted changes")});
        item->setData(0, Qt::UserRole, -1);
        QFont f = item->font(0);
        f.setItalic(true);
        for (int col = 0; col < 4; ++col) item->setFont(col, f);
    }
    for (int i = 0; i < m_commits.size(); ++i) {
        const Commit &c = m_commits[i];
        auto *item = new QTreeWidgetItem(m_list, {c.hash.left(8), c.date, c.author, c.subject});
        item->setData(0, Qt::UserRole, i);
        item->setToolTip(3, c.path == c.oldPath ? c.subject
                             : tr("%1\n(renamed from %2)").arg(c.subject, c.oldPath));
    }
    for (int col = 0; col < 3; ++col) m_list->resizeColumnToContents(col);

    if (m_list->topLevelItemCount() == 0) {
        m_title->setText(tr("<b>%1</b> has no git history yet (untracked or never committed).")
                             .arg(rel.toHtmlEscaped()));
        return;
    }
    m_list->setCurrentItem(m_list->topLevelItem(0));
}

void FileHistoryDialog::onSelectionChanged()
{
    QList<QTreeWidgetItem *> sel = m_list->selectedItems();
    if (sel.isEmpty()) return;
    const QString rel = QDir(m_repoRoot).relativeFilePath(m_filePath);

    // Rev "" means the working tree.
    auto stateOf = [&](int idx, QString *rev, QString *path, QString *label) {
        if (idx < 0) { *rev = QString(); *path = rel; *label = tr("working copy"); return; }
        const Commit &c = m_commits[idx];
        *rev = c.hash; *path = c.path; *label = c.hash.left(8);
    };

    if (sel.size() == 1) {
        const int idx = sel[0]->data(0, Qt::UserRole).toInt();
        if (idx < 0) {
            showDiff(QStringLiteral("HEAD"), rel, QString(), rel,
                     tr("<b>Uncommitted changes</b> - HEAD vs. working copy"));
            return;
        }
        const Commit &c = m_commits[idx];
        showDiff(c.parent, c.oldPath, c.hash, c.path,
                 tr("<b>%1</b> %2 <span style='color:gray'>- %3, %4</span>")
                     .arg(c.hash.left(8), c.subject.toHtmlEscaped(),
                          c.author.toHtmlEscaped(), c.date));
        return;
    }

    // Two or more selected: compare the oldest against the newest. Rows are
    // listed newest first, so a larger row index is older.
    std::sort(sel.begin(), sel.end(), [this](QTreeWidgetItem *a, QTreeWidgetItem *b) {
        return m_list->indexOfTopLevelItem(a) < m_list->indexOfTopLevelItem(b);
    });
    QString newRev, newPath, newLabel, oldRev, oldPath, oldLabel;
    stateOf(sel.first()->data(0, Qt::UserRole).toInt(), &newRev, &newPath, &newLabel);
    stateOf(sel.last()->data(0, Qt::UserRole).toInt(),  &oldRev, &oldPath, &oldLabel);
    showDiff(oldRev, oldPath, newRev, newPath,
             tr("<b>%1</b> ↔ <b>%2</b>").arg(oldLabel, newLabel));
}

void FileHistoryDialog::showDiff(const QString &fromRev, const QString &fromPath,
                                 const QString &toRev, const QString &toPath,
                                 const QString &title)
{
    m_title->setText(title);
    m_changeStarts.clear();

    // Full-context diff, so the panes show the whole file with changes inline.
    QStringList args{"diff", "--no-color", "--no-ext-diff", "-M", "-U100000000", fromRev};
    if (!toRev.isEmpty()) args << toRev;
    args << "--" << fromPath;
    if (toPath != fromPath) args << toPath;
    const QString diff = git(args);

    QStringList leftText, rightText;
    QVector<DiffPane::Row> leftRows, rightRows;
    QStringList pendDel, pendAdd;
    int oldNo = 0, newNo = 0;
    bool inHunk = false, binary = false;

    auto flush = [&] {
        if (pendDel.isEmpty() && pendAdd.isEmpty()) return;
        m_changeStarts.append(leftRows.size());
        const int n = qMax(pendDel.size(), pendAdd.size());
        for (int i = 0; i < n; ++i) {
            if (i < pendDel.size()) {
                leftText << pendDel[i];
                leftRows.append({++oldNo, DiffPane::Removed});
            } else {
                leftText << QString();
                leftRows.append({0, DiffPane::Padding});
            }
            if (i < pendAdd.size()) {
                rightText << pendAdd[i];
                rightRows.append({++newNo, DiffPane::Added});
            } else {
                rightText << QString();
                rightRows.append({0, DiffPane::Padding});
            }
        }
        pendDel.clear();
        pendAdd.clear();
    };

    static const QRegularExpression hunkRe(QStringLiteral("^@@ -(\\d+)(?:,\\d+)? \\+(\\d+)"));
    for (const QString &line : diff.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("@@"))) {
            flush();
            const auto m = hunkRe.match(line);
            if (m.hasMatch()) {
                // Counters are pre-incremented; an empty side reports line 0.
                oldNo = qMax(0, m.captured(1).toInt() - 1);
                newNo = qMax(0, m.captured(2).toInt() - 1);
            }
            inHunk = true;
            continue;
        }
        if (!inHunk) {
            if (line.startsWith(QLatin1String("Binary files"))) binary = true;
            continue;
        }
        if (line.startsWith(QLatin1Char('-')))      pendDel << line.mid(1);
        else if (line.startsWith(QLatin1Char('+'))) pendAdd << line.mid(1);
        else if (line.startsWith(QLatin1Char(' '))) {
            flush();
            leftText  << line.mid(1);
            rightText << line.mid(1);
            leftRows.append({++oldNo, DiffPane::Same});
            rightRows.append({++newNo, DiffPane::Same});
        }
        // "\ No newline at end of file" and the trailing empty split are skipped.
    }
    flush();

    if (binary) {
        leftText = rightText = QStringList{tr("(binary file - no text diff)")};
        leftRows = rightRows = {{0, DiffPane::Padding}};
    } else if (leftRows.isEmpty()) {
        // Nothing changed in the text (e.g. a mode change): just show the file.
        const QString content = toRev.isEmpty()
            ? QString() : git({"show", toRev + QLatin1Char(':') + toPath});
        QStringList lines = content.split(QLatin1Char('\n'));
        if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
        for (int i = 0; i < lines.size(); ++i) {
            leftRows.append({i + 1, DiffPane::Same});
            rightRows.append({i + 1, DiffPane::Same});
        }
        leftText = rightText = lines;
        m_title->setText(title + tr("  <i>(no text changes)</i>"));
    }

    m_left->setRows(leftText, leftRows);
    m_right->setRows(rightText, rightRows);
    // Start at the first change, not at line 1.
    m_left->verticalScrollBar()->setValue(
        m_changeStarts.isEmpty() ? 0 : qMax(0, m_changeStarts.first() - visibleRows() / 3));
}

int FileHistoryDialog::visibleRows() const
{
    return qMax(1, m_left->viewport()->height() / qMax(1, m_left->fontMetrics().height()));
}

void FileHistoryDialog::jumpToChange(int direction)
{
    if (m_changeStarts.isEmpty()) return;
    const int top = m_left->topRow();
    const int visible = visibleRows();
    const int anchor = top + visible / 3;   // where a change lands after a jump

    int target = -1;
    if (direction > 0) {
        for (int s : m_changeStarts)
            if (s > anchor) { target = s; break; }
    } else {
        for (auto it = m_changeStarts.crbegin(); it != m_changeStarts.crend(); ++it)
            if (*it < anchor) { target = *it; break; }
    }
    if (target < 0) return;
    m_left->verticalScrollBar()->setValue(qMax(0, target - visible / 3));
}
