// InFormPreview.cpp - KayteIDE InForm plugin
#include "InFormPreview.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTime>
#include <QVBoxLayout>

namespace InForm {

PreviewDialog::PreviewDialog(const Document &doc, const QString &baseDir, QWidget *parent)
    : QDialog(parent)
    , m_runtime(new Runtime(this))
{
    const Control *formCtl = doc.form();
    setWindowTitle(tr("InForm Preview - %1")
                       .arg(formCtl ? (formCtl->caption.isEmpty() ? formCtl->name : formCtl->caption)
                                    : tr("(no form)")));
    setAttribute(Qt::WA_DeleteOnClose);

    auto *lay = new QVBoxLayout(this);

    m_runtime->setBaseDirectory(baseDir);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(false);
    scroll->setAlignment(Qt::AlignCenter);
    if (QWidget *form = m_runtime->build(doc)) {
        // The window frame comes from this dialog; the form is shown as its body.
        form->setWindowFlags(Qt::Widget);
        scroll->setWidget(form);
    } else {
        scroll->setWidget(new QLabel(tr("This document has no Form control."), scroll));
    }
    lay->addWidget(scroll, 3);

    auto *logHeader = new QHBoxLayout;
    logHeader->addWidget(new QLabel(tr("Events"), this));
    auto *showMouse = new QCheckBox(tr("Mouse enter/leave"), this);
    logHeader->addStretch();
    logHeader->addWidget(showMouse);
    lay->addLayout(logHeader);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setFont(QFont(QStringLiteral("monospace")));
    lay->addWidget(m_log, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *clear = buttons->addButton(tr("Clear log"), QDialogButtonBox::ResetRole);
    connect(clear, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);

    connect(m_runtime, &Runtime::event, this, [this, showMouse](Event e, const QString &name) {
        if ((e == Event::MouseEnter || e == Event::MouseLeave) && !showMouse->isChecked()) return;
        QString line = QStringLiteral("%1  __UI_%2(%3)")
                           .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                eventName(e), name);
        if (e == Event::ValueChanged)
            line += QStringLiteral("  Value = %1").arg(m_runtime->value(name));
        else if (e == Event::TextChanged)
            line += QStringLiteral("  Text = \"%1\"").arg(m_runtime->text(name));
        m_log->appendPlainText(line);
    });

    if (formCtl)
        resize(qMax(420, formCtl->geometry.width() + 60), formCtl->geometry.height() + 260);
}

} // namespace InForm
