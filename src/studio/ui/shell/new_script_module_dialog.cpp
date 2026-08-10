// SPDX-License-Identifier: Apache-2.0
#include "shell/new_script_module_dialog.hpp"

#include <filesystem>
#include <string>

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>

#include <atp/studio/script_modules.hpp>

namespace atp::studio::ui {
namespace {

QString to_q(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}  // namespace

new_script_module_dialog::new_script_module_dialog(const QString& directory,
                                                   const module_registry& registry,
                                                   std::string_view initial_language,
                                                   QWidget* parent)
    : QDialog(parent), registry_(registry) {
    setWindowTitle(QStringLiteral("New module"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    language_ = new QComboBox(this);
    language_->setObjectName(QStringLiteral("language"));
    for (const script_language& lang : studio::languages()) {
        language_->addItem(to_q(lang.label), to_q(lang.id));
    }
    const script_language* initial = studio::language_by_id(initial_language);
    language_->setCurrentIndex(
        language_->findData(to_q(initial == nullptr ? studio::languages().front().id : initial->id)));
    form->addRow(QStringLiteral("Language"), language_);

    name_ = new QLineEdit(to_q(language().name_prefix), this);
    name_->setObjectName(QStringLiteral("module_name"));
    directory_ = new QLineEdit(directory, this);
    directory_->setObjectName(QStringLiteral("directory"));
    auto* browse = new QToolButton(this);
    browse->setText(QStringLiteral("..."));
    auto* row = new QHBoxLayout();
    row->addWidget(directory_, 1);
    row->addWidget(browse);
    form->addRow(QStringLiteral("Module name"), name_);
    form->addRow(QStringLiteral("Directory"), row);
    layout->addLayout(form);

    note_ = new QLabel(this);
    note_->setObjectName(QStringLiteral("note"));
    note_->setWordWrap(true);
    layout->addWidget(note_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(QStringLiteral("Create"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    QObject::connect(name_, &QLineEdit::textChanged, this, [this] { revalidate(); });
    QObject::connect(directory_, &QLineEdit::textChanged, this, [this] { revalidate(); });
    QObject::connect(language_, &QComboBox::currentIndexChanged, this, [this] {
        for (const script_language& lang : studio::languages()) {
            if (name_->text() == to_q(lang.name_prefix)) {
                name_->setText(to_q(language().name_prefix));
                break;
            }
        }
        revalidate();
    });
    QObject::connect(browse, &QToolButton::clicked, this, [this] {
        const QString chosen =
            QFileDialog::getExistingDirectory(this, QStringLiteral("Script directory"), directory_->text());
        if (!chosen.isEmpty()) {
            directory_->setText(chosen);
        }
    });
    revalidate();

    const QSize hint = sizeHint();
    resize(hint.width() * 2, hint.height());
}

QString new_script_module_dialog::module_name() const {
    return name_->text().trimmed();
}

QString new_script_module_dialog::directory() const {
    return directory_->text().trimmed();
}

const script_language& new_script_module_dialog::language() const {
    const script_language* chosen = studio::language_by_id(language_->currentData().toString().toStdString());
    return chosen == nullptr ? studio::languages().front() : *chosen;
}

void new_script_module_dialog::revalidate() {
    const script_language& lang = language();
    const std::string name = module_name().toStdString();
    const QString dir = directory();
    if (!lang.name_valid(name)) {
        note_->setText(
            QStringLiteral("A name starts with a letter or an underscore and holds letters, digits and underscores."));
        ok_->setEnabled(false);
        return;
    }
    if (registry_.find(name) != nullptr) {
        note_->setText(QStringLiteral("A module named '%1' is already registered.").arg(module_name()));
        ok_->setEnabled(false);
        return;
    }
    if (dir.isEmpty()) {
        note_->setText(QStringLiteral("Choose a directory to write the script into."));
        ok_->setEnabled(false);
        return;
    }
    const std::filesystem::path file =
        studio::scripts_dir(std::filesystem::path(dir.toStdWString()), lang) / studio::script_file_name(name, lang);
    if (std::filesystem::exists(file)) {
        note_->setText(QStringLiteral("'%1' already exists.").arg(QString::fromStdWString(file.wstring())));
        ok_->setEnabled(false);
        return;
    }
    note_->setText(QString::fromStdString(lang.creation_note(file, name)));
    ok_->setEnabled(true);
}

}  // namespace atp::studio::ui
