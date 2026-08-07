// SPDX-License-Identifier: Apache-2.0
#include "panels/inspector_widget.hpp"

#include <algorithm>
#include <exception>
#include <string>

#include <QComboBox>
#include <QLabel>
#include <QScrollArea>

#include <atp/studio/node_ref.hpp>
#include <atp/studio/thread_resolve.hpp>

namespace atp::studio::ui {

inspector_widget::inspector_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    auto* outer = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body_ = new QWidget(scroll);
    body_layout_ = new QVBoxLayout(body_);
    body_layout_->setAlignment(Qt::AlignTop);
    body_layout_->setSpacing(style::section_spacing);
    scroll->setWidget(body_);
    outer->addWidget(scroll);
}

void inspector_widget::refresh() {
    const std::string key = form_key();
    if (key != form_key_ || body_layout_->isEmpty()) {
        form_key_ = key;
        rebuild();
    } else {
        sync();
    }
    apply_lock();
}

std::string inspector_widget::form_key() const {
    std::string key =
        std::to_string(state_.describe_generation) + '\0' + state_.current_group + '\0' + state_.selected_child;
    const runtime::group_node* g = state_.doc.group_at(state_.current_group);
    if (g != nullptr && !state_.selected_child.empty()) {
        for (const runtime::child_node& c : g->modules) {
            if (c.module && c.module->name == state_.selected_child) {
                key += '\0' + c.module->factory + '@' +
                       (c.module->factory_version ? c.module->factory_version->to_string() : "latest");
            }
        }
    }
    return key;
}

void inspector_widget::rebuild() {
    clear_body();
    properties_ = nullptr;
    expose_inputs_ = nullptr;
    expose_outputs_ = nullptr;
    property_rows_.clear();

    const runtime::group_node* g = state_.doc.group_at(state_.current_group);
    if (g == nullptr) {
        return;
    }
    if (state_.selected_child.empty()) {
        build_group_section(state_.current_group, g->name, false);
        return;
    }
    for (const runtime::child_node& c : g->modules) {
        const std::string& name = c.module ? c.module->name : c.group->name;
        if (name != state_.selected_child) {
            continue;
        }
        if (c.module) {
            build_module_section(*c.module);
        } else {
            build_group_section(node_ref{state_.current_group, name}.full(), name, true);
        }
        return;
    }
}

void inspector_widget::sync() {
    if (properties_ != nullptr) {
        properties_->sync();
    }
    if (expose_inputs_ != nullptr) {
        expose_inputs_->sync();
    }
    if (expose_outputs_ != nullptr) {
        expose_outputs_->sync();
    }
}

void inspector_widget::apply_lock() {
    const bool locked = state_.view->running();
    for (int i = 0; i < body_layout_->count(); ++i) {
        QWidget* block = body_layout_->itemAt(i)->widget();
        if (block == nullptr) {
            continue;
        }
        const bool is_property_block = std::ranges::find(property_rows_, block) != property_rows_.end();
        block->setEnabled(!locked || is_property_block);
    }
}

void inspector_widget::clear_body() {
    QLayoutItem* old = nullptr;
    while ((old = body_layout_->takeAt(0)) != nullptr) {
        if (QWidget* w = old->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete old;
    }
}

style::section inspector_widget::add_section(const QString& title) {
    const style::section s = style::make_section(title, body_);
    body_layout_->addWidget(s.box);
    return s;
}

void inspector_widget::guard(const char* context, const std::function<void()>& operation) {
    try {
        operation();
        callbacks_.project_changed();
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string(context) + ": " + e.what()));
    }
}

void inspector_widget::commit_rename(const std::string& old_name, QLineEdit* edit) {
    const std::string next = edit->text().toStdString();
    if (next == old_name) {
        return;
    }
    try {
        state_.doc.rename_child(state_.current_group, old_name, next);
        state_.selected_child = next;
    } catch (const std::exception& e) {
        style::mark_error(edit, QString::fromStdString(e.what()));
        edit->setText(QString::fromStdString(old_name));
        return;
    }
    style::clear_error(edit);
    callbacks_.project_changed();
}

void inspector_widget::build_module_section(const runtime::module_node& m) {
    const style::section s = add_section("module");

    s.form->addRow("factory", new QLabel(QString::fromStdString(m.factory), body_));

    auto* name_edit = new QLineEdit(QString::fromStdString(m.name), body_);
    s.form->addRow("name", name_edit);
    const std::string old_name = m.name;
    QObject::connect(name_edit, &QLineEdit::editingFinished, this,
                     [this, old_name, name_edit] { commit_rename(old_name, name_edit); });

    s.form->addRow(
        "version",
        new QLabel(QString::fromStdString(m.factory_version ? m.factory_version->to_string() : "(latest)"), body_));

    const resolved_thread r = resolve_thread(state_.doc.config(), state_.current_group);
    s.form->addRow("runs on", new QLabel(QString::fromStdString(r.name + (r.inherited ? " (inherited)" : "")), body_));

    const style::section props = add_section("properties");
    properties_ = new property_grid(state_, callbacks_, body_);
    properties_->rebuild(state_.current_group, m.name, m);
    props.form->addRow(properties_);
    props.box->setVisible(!properties_->empty());
    property_rows_.push_back(props.box);
}

void inspector_widget::build_group_section(const std::string& group_path, const std::string& name, bool renameable) {
    const style::section s = add_section("group");
    QFormLayout* form = s.form;

    if (renameable) {
        auto* name_edit = new QLineEdit(QString::fromStdString(name), body_);
        form->addRow("name", name_edit);
        const std::string& old_name = name;
        QObject::connect(name_edit, &QLineEdit::editingFinished, this,
                         [this, old_name, name_edit] { commit_rename(old_name, name_edit); });
    } else {
        form->addRow("name", new QLabel(QString::fromStdString(name), body_));
    }

    if (group_path.empty()) {
        const resolved_thread r = resolve_thread(state_.doc.config(), group_path);
        form->addRow("runs on", new QLabel(QString::fromStdString(r.name + " (implicit)"), body_));
    } else {
        auto* thread = new QComboBox(body_);
        thread->addItem("(none)");
        for (const runtime::thread_node& t : state_.doc.config().threads) {
            thread->addItem(QString::fromStdString(t.name));
        }
        QString current = "(none)";
        for (const auto& [path, name_of_thread] : state_.doc.config().assignments) {
            if (path == group_path) {
                current = QString::fromStdString(name_of_thread);
            }
        }
        thread->setCurrentText(current);
        form->addRow("thread", thread);
        QObject::connect(thread, &QComboBox::currentIndexChanged, this, [this, group_path, thread](int index) {
            guard("assign", [&] {
                if (index == 0) {
                    state_.doc.clear_assignment(group_path);
                } else {
                    state_.doc.set_assignment(group_path, thread->currentText().toStdString());
                }
            });
        });
        if (current == "(none)") {
            const resolved_thread r = resolve_thread(state_.doc.config(), group_path);
            form->addRow("runs on", new QLabel(QString::fromStdString(r.name + " (inherited)"), body_));
        }
    }

    expose_inputs_ = new expose_editor(state_, callbacks_, true, body_);
    expose_inputs_->rebuild(group_path);
    add_section("expose inputs").form->addRow(expose_inputs_);

    expose_outputs_ = new expose_editor(state_, callbacks_, false, body_);
    expose_outputs_->rebuild(group_path);
    add_section("expose outputs").form->addRow(expose_outputs_);
}

}  // namespace atp::studio::ui
