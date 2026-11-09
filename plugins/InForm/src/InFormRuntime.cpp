// InFormRuntime.cpp - KayteIDE InForm plugin
#include "InFormRuntime.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleValidator>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSlider>

#include <cmath>

namespace InForm {

QString eventName(Event e)
{
    switch (e) {
    case Event::Click:        return QStringLiteral("Click");
    case Event::MouseEnter:   return QStringLiteral("MouseEnter");
    case Event::MouseLeave:   return QStringLiteral("MouseLeave");
    case Event::FocusIn:      return QStringLiteral("FocusIn");
    case Event::FocusOut:     return QStringLiteral("FocusOut");
    case Event::MouseDown:    return QStringLiteral("MouseDown");
    case Event::MouseUp:      return QStringLiteral("MouseUp");
    case Event::KeyPress:     return QStringLiteral("KeyPress");
    case Event::TextChanged:  return QStringLiteral("TextChanged");
    case Event::ValueChanged: return QStringLiteral("ValueChanged");
    }
    return {};
}

namespace {

// InForm's ToggleSwitch: an on/off pill, drawn the way InForm's theme does.
class ToggleSwitch : public QAbstractButton
{
public:
    explicit ToggleSwitch(QWidget *parent) : QAbstractButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
    }
    QSize sizeHint() const override { return { 40, 17 }; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        const qreal radius = r.height() / 2;
        const QColor on  = palette().color(QPalette::Highlight);
        const QColor off = palette().color(QPalette::Mid);
        p.setPen(Qt::NoPen);
        p.setBrush(isEnabled() ? (isChecked() ? on : off) : off.lighter(130));
        p.drawRoundedRect(r, radius, radius);
        const qreal d = r.height() - 4;
        const qreal x = isChecked() ? r.right() - d - 2 : r.left() + 2;
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(x, r.top() + 2, d, d));
    }
};

bool isSymbol(const Control &c, const char *prop, const char *symbol)
{
    return c.symbol(QLatin1String(prop)).compare(QLatin1String(symbol), Qt::CaseInsensitive) == 0;
}

Qt::Alignment alignmentOf(const Control &c)
{
    Qt::Alignment a = Qt::AlignLeft;
    if (isSymbol(c, "Align", kCenter)) a = Qt::AlignHCenter;
    else if (isSymbol(c, "Align", kRight)) a = Qt::AlignRight;
    if (isSymbol(c, "VAlign", kMiddle)) a |= Qt::AlignVCenter;
    else if (isSymbol(c, "VAlign", kBottom)) a |= Qt::AlignBottom;
    else a |= Qt::AlignTop;
    return a;
}

// Menu captions: a trailing '-' means "separator after this item".
QString menuCaption(const QString &caption, bool *separatorAfter = nullptr)
{
    const bool sep = caption.endsWith(QLatin1Char('-'));
    if (separatorAfter) *separatorAfter = sep;
    return sep ? caption.chopped(1) : caption;
}

// InForm's xp.uitheme colours (__UI_DefaultColor). Forms are designed against
// them, so a dark desktop theme must not leak in (white text on white boxes).
QPalette themePalette(const Control &form)
{
    const QColor fg = form.properties.contains(QStringLiteral("ForeColor"))
                          ? form.color(QStringLiteral("ForeColor")) : QColor(0, 0, 0);
    const QColor bg = form.properties.contains(QStringLiteral("BackColor"))
                          ? form.color(QStringLiteral("BackColor")) : QColor(235, 233, 237);
    QPalette pal(bg, bg); // derives light / mid / dark shades from the face colour
    for (auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                       QPalette::PlaceholderText, QPalette::ToolTipText })
        pal.setColor(role, fg);
    pal.setColor(QPalette::PlaceholderText, QColor(128, 128, 128));
    pal.setColor(QPalette::Base, Qt::white);           // TextBox, ListBox, DropdownList
    pal.setColor(QPalette::AlternateBase, bg);
    pal.setColor(QPalette::Highlight, QColor(78, 150, 216));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, QColor(255, 255, 225));
    pal.setColor(QPalette::Mid, QColor(167, 166, 170)); // BorderColor
    return pal;
}

double sliderScale(const Control &c)
{
    const double minInterval = c.number(QStringLiteral("MinInterval"));
    if (minInterval > 0) return 1.0 / minInterval;
    for (const char *p : { "Min", "Max", "Interval", "Value" }) {
        const double v = c.number(QLatin1String(p));
        if (v != std::floor(v)) return 100.0;
    }
    return 1.0;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

Runtime::Runtime(QObject *parent) : QObject(parent) {}

Runtime::~Runtime()
{
    // The caller owns the form widget; only drop our watchers.
    for (auto it = m_objectIndex.cbegin(); it != m_objectIndex.cend(); ++it)
        if (it.key()) it.key()->removeEventFilter(this);
}

Runtime::Entry *Runtime::entry(const QString &name)
{
    const auto it = m_index.constFind(name.toLower());
    return it == m_index.cend() ? nullptr : &m_entries[*it];
}

const Runtime::Entry *Runtime::entry(const QString &name) const
{
    return const_cast<Runtime *>(this)->entry(name);
}

QWidget *Runtime::widget(const QString &name) const
{
    const Entry *e = entry(name);
    return e ? e->widget.data() : nullptr;
}

QAction *Runtime::action(const QString &name) const
{
    const Entry *e = entry(name);
    return e ? e->action.data() : nullptr;
}

QString Runtime::resolvePath(const QString &path) const
{
    if (path.isEmpty() || QFileInfo(path).isAbsolute() || m_baseDir.isEmpty()) return path;
    return QDir(m_baseDir).filePath(path);
}

QFont Runtime::resolveFont(const Control &c, const QFont &base) const
{
    QFont f = base;
    // InForm lists fallbacks separated by '?': the first file that exists wins.
    for (const QString &file : c.fontFiles.split(QLatin1Char('?'), Qt::SkipEmptyParts)) {
        const QString path = resolvePath(file);
        if (!QFileInfo::exists(path)) continue;
        static QHash<QString, QString> loaded; // path -> family, fonts load once per process
        auto it = loaded.find(path);
        if (it == loaded.end()) {
            const int id = QFontDatabase::addApplicationFont(path);
            const QStringList fam = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
            it = loaded.insert(path, fam.value(0));
        }
        if (!it->isEmpty()) { f.setFamily(*it); break; }
    }
    if (c.fontSize > 0) f.setPixelSize(c.fontSize);
    return f;
}

// ─── Build ───────────────────────────────────────────────────────────────────

QWidget *Runtime::build(const Document &doc, QWidget *parent)
{
    for (auto it = m_objectIndex.cbegin(); it != m_objectIndex.cend(); ++it)
        if (it.key()) it.key()->removeEventFilter(this);
    m_entries.clear();
    m_index.clear();
    m_objectIndex.clear();
    m_defaultButton.clear();
    m_doc = doc;

    for (const Control &c : doc.controls()) {
        if (c.type == ControlType::MenuPanel || c.type == ControlType::Font) continue;
        m_index.insert(c.name.toLower(), int(m_entries.size()));
        m_entries.append(Entry { c, {}, {}, {}, 1 });
    }

    const Control *formCtl = doc.form();
    if (!formCtl) { m_form.clear(); return nullptr; }

    const int formIdx = m_index.value(formCtl->name.toLower());
    m_form = createWidget(formIdx, parent);

    // Frames first so their children have somewhere to live.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < m_entries.size(); ++i) {
            const ControlType t = m_entries[i].control.type;
            if (t == ControlType::Form || t == ControlType::MenuBar ||
                t == ControlType::MenuItem || t == ControlType::ContextMenu) continue;
            if ((t == ControlType::Frame) != (pass == 0)) continue;

            QWidget *host = m_form;
            const QString &pn = m_entries[i].control.parentName;
            if (!pn.isEmpty())
                if (QWidget *pw = widget(pn)) host = pw;
            createWidget(i, host);
        }
    }

    buildMenus(m_form);

    for (int i = 0; i < m_entries.size(); ++i) {
        applyCommon(i);
        wireSignals(i);
    }
    applyPlatformConditionals();
    return m_form;
}

// QB64 metacommand conditions as they evaluate on this machine.
static bool qb64Condition(QString cond)
{
    cond = cond.trimmed().toUpper();
    if (cond.startsWith(QLatin1String("NOT "))) return !qb64Condition(cond.mid(4));
#if defined(Q_OS_WIN)
    const bool win = true, mac = false, linux_ = false;
#elif defined(Q_OS_MACOS)
    const bool win = false, mac = true, linux_ = false;
#else
    const bool win = false, mac = false, linux_ = true;
#endif
    if (cond == QLatin1String("WIN") || cond == QLatin1String("WINDOWS")) return win;
    if (cond == QLatin1String("MAC") || cond == QLatin1String("MACOSX")) return mac;
    if (cond == QLatin1String("LINUX")) return linux_;
    if (cond == QLatin1String("64BIT")) return sizeof(void *) == 8;
    if (cond == QLatin1String("32BIT")) return sizeof(void *) == 4;
    return false;
}

void Runtime::applyPlatformConditionals()
{
    // The .frm reader keeps $IF ... $END IF blocks verbatim in each control's
    // extra lines, and a block can span controls (a control created inside a
    // branch). Walk them in file order, as QB64's precompiler would.
    struct Level { bool active; bool taken; };
    QList<Level> stack;
    const auto active = [&stack] {
        for (const Level &l : stack) if (!l.active) return false;
        return true;
    };
    static const QRegularExpression reIf(QStringLiteral(R"(^\$(IF|ELSEIF)\s+(.*?)\s+THEN$)"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reElse(QStringLiteral(R"(^\$ELSE$)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reEnd(QStringLiteral(R"(^\$END\s*IF$)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reStmt(
        QStringLiteral(R"re(^(SetCaption\s+__UI_NewID\s*,|ToolTip\s*\(\s*__UI_NewID\s*\)\s*=|Text\s*\(\s*__UI_NewID\s*\)\s*=)\s*"(.*)"$)re"),
        QRegularExpression::CaseInsensitiveOption);

    for (const Control &c : m_doc.controls()) {
        if (!active()) setHidden(c.name, true); // created in a branch that doesn't apply here
        for (const QString &line : c.extraLines) {
            if (const auto m = reIf.match(line); m.hasMatch()) {
                if (m.captured(1).compare(QLatin1String("IF"), Qt::CaseInsensitive) == 0) {
                    const bool on = qb64Condition(m.captured(2));
                    stack << Level { on, on };
                } else if (!stack.isEmpty()) {
                    Level &l = stack.last();
                    l.active = !l.taken && qb64Condition(m.captured(2));
                    l.taken = l.taken || l.active;
                }
            } else if (reElse.match(line).hasMatch()) {
                if (!stack.isEmpty()) { stack.last().active = !stack.last().taken; stack.last().taken = true; }
            } else if (reEnd.match(line).hasMatch()) {
                if (!stack.isEmpty()) stack.removeLast();
            } else if (active()) {
                const auto m = reStmt.match(line);
                if (!m.hasMatch()) continue;
                const QString value = decodeBasicString(m.captured(2));
                const QString what = m.captured(1).left(4).toLower();
                if (what == QLatin1String("setc")) setCaption(c.name, value);
                else if (what == QLatin1String("tool")) setProperty(c.name, QStringLiteral("ToolTip"), value);
                else setText(c.name, value);
            }
        }
    }
}

QWidget *Runtime::createWidget(int index, QWidget *parent)
{
    Entry &e = m_entries[index];
    const Control &c = e.control;
    QWidget *w = nullptr;

    switch (c.type) {
    case ControlType::Form: {
        w = new QWidget(parent);
        w->setPalette(themePalette(c));
        w->setAutoFillBackground(true);
        w->setWindowTitle(c.caption);
        if (c.flag(QStringLiteral("CanResize"))) {
            w->resize(c.geometry.size());
            w->setMinimumSize(c.geometry.size() / 2);
        } else {
            w->setFixedSize(c.geometry.size());
        }
        if (!c.text.isEmpty()) w->setWindowIcon(QIcon(resolvePath(c.text)));
        break;
    }
    case ControlType::Frame: {
        auto *g = new QGroupBox(c.caption, parent);
        g->setFlat(c.properties.contains(QStringLiteral("HasBorder")) && !c.flag(QStringLiteral("HasBorder")));
        w = g;
        break;
    }
    case ControlType::Button: {
        auto *b = new QPushButton(c.caption, parent);
        if (!c.text.isEmpty()) b->setIcon(QIcon(resolvePath(c.text)));
        if (c.isDefaultButton) { b->setDefault(true); m_defaultButton = b; }
        w = b;
        break;
    }
    case ControlType::Label: {
        auto *l = new QLabel(c.caption, parent);
        l->setAlignment(alignmentOf(c));
        l->setWordWrap(c.flag(QStringLiteral("WordWrap")));
        if (c.flag(QStringLiteral("HasBorder"))) l->setFrameShape(QFrame::Box);
        w = l;
        break;
    }
    case ControlType::CheckBox: {
        auto *cb = new QCheckBox(c.caption, parent);
        cb->setChecked(c.number(QStringLiteral("Value")) != 0);
        w = cb;
        break;
    }
    case ControlType::ToggleSwitch: {
        auto *ts = new ToggleSwitch(parent);
        ts->setText(c.caption);
        ts->setChecked(c.number(QStringLiteral("Value")) != 0);
        w = ts;
        break;
    }
    case ControlType::RadioButton: {
        auto *rb = new QRadioButton(c.caption, parent);
        rb->setChecked(c.number(QStringLiteral("Value")) != 0);
        w = rb;
        break;
    }
    case ControlType::TextBox: {
        if (c.flag(QStringLiteral("Multiline"))) {
            auto *te = new QPlainTextEdit(parent);
            te->setPlainText(c.text);
            te->setPlaceholderText(c.caption);
            te->setLineWrapMode(c.flag(QStringLiteral("WordWrap")) ? QPlainTextEdit::WidgetWidth
                                                                     : QPlainTextEdit::NoWrap);
            if (c.properties.contains(QStringLiteral("HasBorder")) && !c.flag(QStringLiteral("HasBorder")))
                te->setFrameShape(QFrame::NoFrame);
            w = te;
        } else {
            auto *le = new QLineEdit(parent);
            le->setPlaceholderText(c.caption);
            if (!c.mask.isEmpty()) le->setInputMask(c.mask);
            le->setText(c.text);
            if (c.flag(QStringLiteral("PasswordField"))) le->setEchoMode(QLineEdit::Password);
            if (c.properties.contains(QStringLiteral("HasBorder")) && !c.flag(QStringLiteral("HasBorder")))
                le->setFrame(false);
            const QVariant numeric = c.properties.value(QStringLiteral("NumericOnly"));
            if (numeric.isValid()) {
                auto *v = new QDoubleValidator(le);
                v->setNotation(QDoubleValidator::StandardNotation);
                if (isSymbol(c, "NumericOnly", kNumericWithBounds))
                    v->setRange(c.number(QStringLiteral("Min")), c.number(QStringLiteral("Max")));
                if (numeric.toBool() || isSymbol(c, "NumericOnly", kNumericWithBounds))
                    le->setValidator(v);
            }
            le->setAlignment(alignmentOf(c) & Qt::AlignHorizontal_Mask);
            w = le;
        }
        break;
    }
    case ControlType::ProgressBar: {
        auto *pb = new QProgressBar(parent);
        pb->setRange(int(c.number(QStringLiteral("Min"))), int(c.number(QStringLiteral("Max"), 100)));
        pb->setValue(int(c.number(QStringLiteral("Value"))));
        pb->setTextVisible(c.flag(QStringLiteral("ShowPercentage")) || !c.caption.isEmpty());
        if (!c.caption.isEmpty()) pb->setFormat(c.caption);
        w = pb;
        break;
    }
    case ControlType::ListBox: {
        auto *lw = new QListWidget(parent);
        lw->addItems(c.items);
        const int v = int(c.number(QStringLiteral("Value")));
        if (v > 0) lw->setCurrentRow(v - 1);
        if (c.properties.contains(QStringLiteral("HasBorder")) && !c.flag(QStringLiteral("HasBorder")))
            lw->setFrameShape(QFrame::NoFrame);
        w = lw;
        break;
    }
    case ControlType::DropdownList: {
        auto *cb = new QComboBox(parent);
        cb->addItems(c.items);
        cb->setCurrentIndex(int(c.number(QStringLiteral("Value"))) - 1);
        w = cb;
        break;
    }
    case ControlType::PictureBox: {
        auto *l = new QLabel(parent);
        l->setAlignment(alignmentOf(c));
        l->setScaledContents(c.flag(QStringLiteral("Stretch")));
        if (c.flag(QStringLiteral("HasBorder"))) l->setFrameShape(QFrame::Box);
        if (!c.text.isEmpty()) {
            const QString path = resolvePath(c.text);
            if (c.animatedGif) {
                auto *movie = new QMovie(path, QByteArray(), l);
                l->setMovie(movie);
                if (c.autoPlayGif) movie->start();
                else movie->jumpToFrame(0);
            } else {
                l->setPixmap(QPixmap(path));
            }
        }
        w = l;
        break;
    }
    case ControlType::TrackBar: {
        auto *s = new QSlider(Qt::Horizontal, parent);
        e.scale = sliderScale(c);
        s->setRange(int(std::lround(c.number(QStringLiteral("Min")) * e.scale)),
                    int(std::lround(c.number(QStringLiteral("Max"), 10) * e.scale)));
        const double interval = c.number(QStringLiteral("Interval"), 1);
        s->setTickPosition(QSlider::TicksBelow);
        s->setTickInterval(qMax(1, int(std::lround(interval * e.scale))));
        s->setPageStep(qMax(1, int(std::lround(interval * e.scale))));
        s->setValue(int(std::lround(c.number(QStringLiteral("Value")) * e.scale)));
        w = s;
        break;
    }
    default:
        return nullptr;
    }

    if (c.type != ControlType::Form) w->setGeometry(c.geometry);
    w->setObjectName(c.name);
    e.widget = w;
    track(w, index);
    if (auto *lw = qobject_cast<QListWidget *>(w)) track(lw->viewport(), index);
    if (auto *te = qobject_cast<QPlainTextEdit *>(w)) track(te->viewport(), index);
    return w;
}

void Runtime::buildMenus(QWidget *form)
{
    QMenuBar *bar = nullptr;

    for (int i = 0; i < m_entries.size(); ++i) {
        Entry &e = m_entries[i];
        const Control &c = e.control;

        if (c.type == ControlType::MenuBar) {
            if (!bar) {
                bar = new QMenuBar(form);
                bar->setNativeMenuBar(false); // the bar belongs to the form, as in InForm
                bar->setGeometry(0, 0, form->width(), qMax(22, c.geometry.height()));
            }
            e.menu = bar->addMenu(menuCaption(c.caption));
            e.action = e.menu->menuAction();
            e.menu->setObjectName(c.name);
            track(e.menu, i);
        } else if (c.type == ControlType::ContextMenu) {
            e.menu = new QMenu(c.caption, form);
            e.menu->setObjectName(c.name);
            track(e.menu, i);
        } else if (c.type == ControlType::MenuItem) {
            Entry *parent = entry(c.parentName);
            if (!parent || !parent->menu) continue; // orphan item - nothing to attach to
            bool sepAfter = false;
            const QString caption = menuCaption(c.caption, &sepAfter);
            if (!m_doc.childrenOf(c.name).isEmpty()) {
                e.menu = parent->menu->addMenu(caption);
                e.menu->setObjectName(c.name);
                e.action = e.menu->menuAction();
            } else {
                e.action = parent->menu->addAction(caption);
                if (c.properties.contains(QStringLiteral("Value")) || isSymbol(c, "BulletStyle", kBullet)) {
                    e.action->setCheckable(true);
                    e.action->setChecked(c.number(QStringLiteral("Value")) != 0);
                }
            }
            if (!c.text.isEmpty()) e.action->setIcon(QIcon(resolvePath(c.text)));
            e.action->setObjectName(c.name);
            if (sepAfter) parent->menu->addSeparator();
        }
    }
}

void Runtime::applyCommon(int index)
{
    Entry &e = m_entries[index];
    const Control &c = e.control;

    if (e.action && c.type == ControlType::MenuItem) {
        e.action->setToolTip(c.toolTip);
        e.action->setEnabled(!c.flag(QStringLiteral("Disabled")));
        e.action->setVisible(!c.flag(QStringLiteral("Hidden")));
    }

    QWidget *w = e.widget;
    if (!w) return;

    if (!c.toolTip.isEmpty()) w->setToolTip(c.toolTip);
    if (c.flag(QStringLiteral("Disabled"))) w->setEnabled(false);
    if (c.flag(QStringLiteral("Hidden")) && c.type != ControlType::Form) w->hide();
    if (c.flag(QStringLiteral("CanHaveFocus")) || c.type == ControlType::TextBox)
        w->setFocusPolicy(Qt::StrongFocus);
    else if (c.type != ControlType::Form && c.properties.contains(QStringLiteral("CanHaveFocus")))
        w->setFocusPolicy(Qt::NoFocus);

    if (!c.fontFiles.isEmpty() || c.fontSize > 0)
        w->setFont(resolveFont(c, w->font()));

    if (c.type != ControlType::Form) { // the form's colours come from themePalette()
        QPalette pal = w->palette();
        const QColor fg = c.color(QStringLiteral("ForeColor"));
        const QColor bg = c.color(QStringLiteral("BackColor"));
        if (fg.isValid())
            for (auto role : { QPalette::WindowText, QPalette::ButtonText, QPalette::Text })
                pal.setColor(role, fg);
        if (bg.isValid())
            for (auto role : { QPalette::Window, QPalette::Button, QPalette::Base })
                pal.setColor(role, bg);
        const QColor selFg = c.color(QStringLiteral("SelectedForeColor"));
        const QColor selBg = c.color(QStringLiteral("SelectedBackColor"));
        if (selFg.isValid()) pal.setColor(QPalette::HighlightedText, selFg);
        if (selBg.isValid()) pal.setColor(QPalette::Highlight, selBg);
        w->setPalette(pal);
        w->setAutoFillBackground(bg.isValid() && !isSymbol(c, "BackStyle", kTransparent));
    }

    if (!c.contextMenu.isEmpty()) {
        if (Entry *m = entry(c.contextMenu); m && m->menu) {
            QPointer<QMenu> menu = m->menu;
            w->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(w, &QWidget::customContextMenuRequested, this, [w, menu](const QPoint &pos) {
                if (menu) menu->popup(w->mapToGlobal(pos));
            });
        }
    }
}

void Runtime::track(QObject *obj, int index)
{
    m_objectIndex.insert(obj, index);
    obj->installEventFilter(this);
    // Widgets owned by the caller may die before we do.
    connect(obj, &QObject::destroyed, this, [this, obj] { m_objectIndex.remove(obj); });
}

void Runtime::wireSignals(int index)
{
    const Entry &e = m_entries[index];
    const QString name = e.control.name;
    auto value = [this, name] { emitEvent(Event::ValueChanged, name); };
    auto text  = [this, name] { emitEvent(Event::TextChanged, name); };
    auto click = [this, name] { emitEvent(Event::Click, name); };

    if (e.action && e.control.type == ControlType::MenuItem && !e.menu) {
        connect(e.action, &QAction::triggered, this, click);
        connect(e.action, &QAction::hovered, this, [this, name] { emitEvent(Event::MouseEnter, name); });
        return;
    }

    QWidget *w = e.widget;
    if (!w) return;

    if (auto *b = qobject_cast<QAbstractButton *>(w)) {
        connect(b, &QAbstractButton::clicked, this, click);
        if (b->isCheckable()) connect(b, &QAbstractButton::toggled, this, value);
    } else if (auto *le = qobject_cast<QLineEdit *>(w)) {
        connect(le, &QLineEdit::textChanged, this, text);
    } else if (auto *te = qobject_cast<QPlainTextEdit *>(w)) {
        connect(te, &QPlainTextEdit::textChanged, this, text);
    } else if (auto *lw = qobject_cast<QListWidget *>(w)) {
        connect(lw, &QListWidget::currentRowChanged, this, value);
    } else if (auto *cb = qobject_cast<QComboBox *>(w)) {
        connect(cb, &QComboBox::currentIndexChanged, this, value);
    } else if (auto *s = qobject_cast<QSlider *>(w)) {
        connect(s, &QSlider::valueChanged, this, value);
    } else if (auto *pb = qobject_cast<QProgressBar *>(w)) {
        connect(pb, &QProgressBar::valueChanged, this, value);
    }
}

void Runtime::emitEvent(Event ev, const QString &name)
{
    emit event(ev, name);
    switch (ev) {
    case Event::Click:        emit clicked(name); break;
    case Event::ValueChanged: emit valueChanged(name); propagateBindings(name); break;
    case Event::TextChanged:  emit textChanged(name);  propagateBindings(name); break;
    default: break;
    }
}

bool Runtime::eventFilter(QObject *watched, QEvent *ev)
{
    const auto it = m_objectIndex.constFind(watched);
    if (it == m_objectIndex.cend()) return QObject::eventFilter(watched, ev);
    const Entry &e = m_entries[*it];
    const QString &name = e.control.name;
    auto *w = qobject_cast<QWidget *>(watched);

    switch (ev->type()) {
    case QEvent::Enter:     emitEvent(Event::MouseEnter, name); break;
    case QEvent::Leave:     emitEvent(Event::MouseLeave, name); break;
    case QEvent::FocusIn:   if (watched == e.widget) emitEvent(Event::FocusIn, name);  break;
    case QEvent::FocusOut:  if (watched == e.widget) emitEvent(Event::FocusOut, name); break;
    case QEvent::MouseButtonPress:
        if (static_cast<QMouseEvent *>(ev)->button() == Qt::LeftButton) emitEvent(Event::MouseDown, name);
        break;
    case QEvent::MouseButtonRelease: {
        auto *me = static_cast<QMouseEvent *>(ev);
        if (me->button() != Qt::LeftButton) break;
        emitEvent(Event::MouseUp, name);
        // Buttons and menu items report Click through their own signals.
        if (w && !qobject_cast<QAbstractButton *>(e.widget) && !qobject_cast<QMenu *>(watched) &&
            w->rect().contains(me->position().toPoint()))
            emitEvent(Event::Click, name);
        break;
    }
    case QEvent::KeyPress: {
        auto *ke = static_cast<QKeyEvent *>(ev);
        if (w && w->hasFocus()) emitEvent(Event::KeyPress, name);
        // InForm's default button answers Enter anywhere on the form.
        if (watched == m_form && m_defaultButton && m_defaultButton->isEnabled() &&
            (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)) {
            if (auto *b = qobject_cast<QAbstractButton *>(m_defaultButton.data())) {
                b->animateClick();
                return true;
            }
        }
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, ev);
}

// ─── InForm-style accessors ─────────────────────────────────────────────────

QString Runtime::caption(const QString &name) const
{
    const Entry *e = entry(name);
    return e ? e->control.caption : QString();
}

void Runtime::setCaption(const QString &name, const QString &caption)
{
    Entry *e = entry(name);
    if (!e) return;
    e->control.caption = caption;
    QWidget *w = e->widget;

    if (e->control.type == ControlType::Form && w)          w->setWindowTitle(caption);
    else if (auto *g  = qobject_cast<QGroupBox *>(w))        g->setTitle(caption);
    else if (auto *b  = qobject_cast<QAbstractButton *>(w))  b->setText(caption);
    else if (auto *l  = qobject_cast<QLabel *>(w))           { if (e->control.type == ControlType::Label) l->setText(caption); }
    else if (auto *le = qobject_cast<QLineEdit *>(w))        le->setPlaceholderText(caption);
    else if (auto *te = qobject_cast<QPlainTextEdit *>(w))   te->setPlaceholderText(caption);
    else if (auto *pb = qobject_cast<QProgressBar *>(w))     { pb->setFormat(caption); pb->setTextVisible(true); }
    else if (e->menu)                                        e->menu->setTitle(menuCaption(caption));
    else if (e->action)                                      e->action->setText(menuCaption(caption));
    propagateBindings(name);
}

QString Runtime::text(const QString &name) const
{
    const Entry *e = entry(name);
    if (!e) return {};
    QWidget *w = e->widget;
    if (auto *le = qobject_cast<QLineEdit *>(w))      return le->text();
    if (auto *te = qobject_cast<QPlainTextEdit *>(w)) return te->toPlainText();
    if (auto *lw = qobject_cast<QListWidget *>(w)) {
        QStringList items;
        for (int i = 0; i < lw->count(); ++i) items << lw->item(i)->text();
        return items.join(QLatin1Char('\n'));
    }
    if (auto *cb = qobject_cast<QComboBox *>(w)) {
        QStringList items;
        for (int i = 0; i < cb->count(); ++i) items << cb->itemText(i);
        return items.join(QLatin1Char('\n'));
    }
    return e->control.text;
}

void Runtime::setText(const QString &name, const QString &text)
{
    Entry *e = entry(name);
    if (!e) return;
    e->control.text = text;
    QWidget *w = e->widget;
    if (auto *le = qobject_cast<QLineEdit *>(w))      le->setText(text);
    else if (auto *te = qobject_cast<QPlainTextEdit *>(w)) te->setPlainText(text);
    else if (e->control.type == ControlType::PictureBox) {
        if (auto *l = qobject_cast<QLabel *>(w)) l->setPixmap(QPixmap(resolvePath(text)));
    } else if (e->control.type == ControlType::ListBox || e->control.type == ControlType::DropdownList) {
        resetList(name);
        for (const QString &item : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) addItem(name, item);
    }
}

double Runtime::value(const QString &name) const
{
    const Entry *e = entry(name);
    if (!e) return 0;
    QWidget *w = e->widget;
    if (e->action && e->action->isCheckable()) return e->action->isChecked() ? -1 : 0;
    if (auto *b  = qobject_cast<QAbstractButton *>(w); b && b->isCheckable()) return b->isChecked() ? -1 : 0;
    if (auto *lw = qobject_cast<QListWidget *>(w))  return lw->currentRow() + 1;
    if (auto *cb = qobject_cast<QComboBox *>(w))    return cb->currentIndex() + 1;
    if (auto *s  = qobject_cast<QSlider *>(w))      return s->value() / e->scale;
    if (auto *pb = qobject_cast<QProgressBar *>(w)) return pb->value();
    return e->control.number(QStringLiteral("Value"));
}

void Runtime::setValue(const QString &name, double v)
{
    Entry *e = entry(name);
    if (!e) return;
    e->control.properties.insert(QStringLiteral("Value"), v);
    QWidget *w = e->widget;
    if (e->action && e->action->isCheckable()) e->action->setChecked(v != 0);
    else if (auto *b  = qobject_cast<QAbstractButton *>(w); b && b->isCheckable()) b->setChecked(v != 0);
    else if (auto *lw = qobject_cast<QListWidget *>(w))  lw->setCurrentRow(int(v) - 1);
    else if (auto *cb = qobject_cast<QComboBox *>(w))    cb->setCurrentIndex(int(v) - 1);
    else if (auto *s  = qobject_cast<QSlider *>(w))      s->setValue(int(std::lround(v * e->scale)));
    else if (auto *pb = qobject_cast<QProgressBar *>(w)) pb->setValue(int(v));
}

void Runtime::addItem(const QString &name, const QString &item)
{
    Entry *e = entry(name);
    if (!e) return;
    e->control.items << item;
    if (auto *lw = qobject_cast<QListWidget *>(e->widget.data())) {
        lw->addItem(item);
        if (e->control.flag(QStringLiteral("AutoScroll"))) lw->scrollToBottom();
    } else if (auto *cb = qobject_cast<QComboBox *>(e->widget.data())) {
        cb->addItem(item);
    }
}

void Runtime::resetList(const QString &name)
{
    Entry *e = entry(name);
    if (!e) return;
    e->control.items.clear();
    if (auto *lw = qobject_cast<QListWidget *>(e->widget.data())) lw->clear();
    else if (auto *cb = qobject_cast<QComboBox *>(e->widget.data())) cb->clear();
}

void Runtime::setDisabled(const QString &name, bool disabled)
{
    Entry *e = entry(name);
    if (!e) return;
    e->control.properties.insert(QStringLiteral("Disabled"), disabled);
    if (e->widget) e->widget->setEnabled(!disabled);
    if (e->action) e->action->setEnabled(!disabled);
}

void Runtime::setHidden(const QString &name, bool hidden)
{
    Entry *e = entry(name);
    if (!e) return;
    e->control.properties.insert(QStringLiteral("Hidden"), hidden);
    if (e->widget) e->widget->setVisible(!hidden);
    if (e->action) e->action->setVisible(!hidden);
}

QVariant Runtime::property(const QString &name, const QString &prop) const
{
    const Entry *e = entry(name);
    if (!e) return {};
    const QString p = prop.toLower();
    if (p == QLatin1String("caption")) return caption(name);
    if (p == QLatin1String("text"))    return text(name);
    if (p == QLatin1String("value"))   return value(name);
    if (p == QLatin1String("tooltip")) return e->widget ? e->widget->toolTip() : e->control.toolTip;
    if (e->widget) {
        if (p == QLatin1String("left"))   return e->widget->x();
        if (p == QLatin1String("top"))    return e->widget->y();
        if (p == QLatin1String("width"))  return e->widget->width();
        if (p == QLatin1String("height")) return e->widget->height();
    }
    for (auto it = e->control.properties.cbegin(); it != e->control.properties.cend(); ++it)
        if (it.key().compare(prop, Qt::CaseInsensitive) == 0) return it.value();
    return {};
}

void Runtime::setProperty(const QString &name, const QString &prop, const QVariant &v)
{
    Entry *e = entry(name);
    if (!e || !v.isValid()) return;
    const QString p = prop.toLower();
    QWidget *w = e->widget;

    // A bound Caption/Text shows numbers the way QB64's STR$ would (no ".0").
    const auto asText = [&v] {
        return v.typeId() == QMetaType::Double ? QString::number(v.toDouble(), 'g', 15) : v.toString();
    };

    if (p == QLatin1String("caption"))       setCaption(name, asText());
    else if (p == QLatin1String("text"))     setText(name, asText());
    else if (p == QLatin1String("value"))    setValue(name, v.toDouble());
    else if (p == QLatin1String("disabled")) setDisabled(name, v.toBool());
    else if (p == QLatin1String("hidden"))   setHidden(name, v.toBool());
    else if (p == QLatin1String("tooltip"))  { e->control.toolTip = v.toString(); if (w) w->setToolTip(e->control.toolTip); }
    else if (w && p == QLatin1String("left"))   w->move(v.toInt(), w->y());
    else if (w && p == QLatin1String("top"))    w->move(w->x(), v.toInt());
    else if (w && p == QLatin1String("width"))  w->resize(v.toInt(), w->height());
    else if (w && p == QLatin1String("height")) w->resize(w->width(), v.toInt());
    else {
        e->control.properties.insert(prop, v);
        const double d = v.toDouble();
        if (auto *s = qobject_cast<QSlider *>(w)) {
            if (p == QLatin1String("min")) s->setMinimum(int(std::lround(d * e->scale)));
            if (p == QLatin1String("max")) s->setMaximum(int(std::lround(d * e->scale)));
            if (p == QLatin1String("interval")) s->setTickInterval(qMax(1, int(std::lround(d * e->scale))));
        } else if (auto *pb = qobject_cast<QProgressBar *>(w)) {
            if (p == QLatin1String("min")) pb->setMinimum(int(d));
            if (p == QLatin1String("max")) pb->setMaximum(int(d));
        }
    }
}

void Runtime::propagateBindings(const QString &name)
{
    if (m_inBinding) return;
    m_inBinding = true;
    for (const Binding &b : m_doc.bindings()) {
        if (b.source.compare(name, Qt::CaseInsensitive) == 0)
            setProperty(b.target, b.targetProperty, property(b.source, b.sourceProperty));
        else if (b.target.compare(name, Qt::CaseInsensitive) == 0)
            setProperty(b.source, b.sourceProperty, property(b.target, b.targetProperty));
    }
    m_inBinding = false;
}

} // namespace InForm
