#include "shell/attach_dialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace atp::studio::ui {

attach_dialog::attach_dialog(const QString& host, int port, QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Attach to a running host"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    host_ = new QLineEdit(host, this);
    port_ = new QSpinBox(this);
    port_->setRange(1, 65535);
    port_->setValue(port > 0 ? port : 7777);
    form->addRow(QStringLiteral("Host"), host_);
    form->addRow(QStringLiteral("Port"), port_);
    layout->addLayout(form);

    auto* note =
        new QLabel(QStringLiteral("The host must have been started with --control &lt;port&gt;. The pipeline is shown "
                                  "read-only; properties can still be edited on the fly."),
                   this);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Attach"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString attach_dialog::host() const {
    return host_->text().trimmed();
}

int attach_dialog::port() const {
    return port_->value();
}

}  // namespace atp::studio::ui
