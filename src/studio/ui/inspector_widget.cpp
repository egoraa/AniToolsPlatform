#include "inspector_widget.hpp"

#include <algorithm>
#include <exception>
#include <string>

#include <QComboBox>
#include <QLabel>
#include <QScrollArea>

#include <atp/studio/thread_resolve.hpp>

namespace atp::studio::ui {

inspector_widget::inspector_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    auto* outer = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    // No horizontal scrolling at all: every row is required to shrink or wrap, and a row that
    // cannot is a layout bug to fix rather than a scrollbar to live with.
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
    // The factory and version take part because a different factory means a different set of
    // property rows, while the child name alone could stay the same; the cache generation covers the
    // case where the same factory came back from a reloaded plugin with different properties.
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
    // The nested editors leave together with the body.
    properties_ = nullptr;
    expose_inputs_ = nullptr;
    expose_outputs_ = nullptr;
    property_rows_.clear();

    const runtime::group_node* g = state_.doc.group_at(state_.current_group);
    if (g == nullptr) {
        return;
    }
    if (state_.selected_child.empty()) {
        // Nothing picked on the canvas means the group itself is what is being looked at: an empty
        // inspector says nothing, and the group's own thread and exports have to live somewhere.
        build_group_section(state_.current_group, g->name, /*renameable=*/false);
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
            build_group_section(detail::full_path(state_.current_group, name), name, /*renameable=*/true);
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
    // The document structure is read-only while running, properties being the exception — editing
    // them on the fly is the whole point. So it is not body_ that gets disabled but each of its
    // blocks separately: in Qt setEnabled(true) on a child of a disabled parent is a no-op, and the
    // property rows could never be re-enabled.
    const bool locked = state_.run.running();
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
    // deleteLater rather than delete: refresh() can be reached from a signal of one of these very
    // widgets, and destroying a widget inside its own handler crashes Qt.
    QLayoutItem* old = nullptr;
    while ((old = body_layout_->takeAt(0)) != nullptr) {
        if (QWidget* w = old->widget()) {
            w->hide();  // the widget outlives this loop until the event loop gets to it
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
        callbacks_.document_changed();
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
        // The rejected name never reaches the document, so the field goes back to what is there.
        style::mark_error(edit, QString::fromStdString(e.what()));
        edit->setText(QString::fromStdString(old_name));
        return;
    }
    style::clear_error(edit);
    callbacks_.document_changed();
}

void inspector_widget::build_module_section(const runtime::module_node& m) {
    const style::section s = add_section("module");

    // The factory is a row rather than the section title: a title carrying a value is a title that
    // has to be read twice.
    s.form->addRow("factory", new QLabel(QString::fromStdString(m.factory), body_));

    auto* name_edit = new QLineEdit(QString::fromStdString(m.name), body_);
    s.form->addRow("name", name_edit);
    const std::string old_name = m.name;
    QObject::connect(name_edit, &QLineEdit::editingFinished, this,
                     [this, old_name, name_edit] { commit_rename(old_name, name_edit); });

    // The version is fixed when the module is added; changing it means recreating the child, and
    // the connections are not guaranteed to survive that even by hand.
    s.form->addRow(
        "version",
        new QLabel(QString::fromStdString(m.factory_version ? m.factory_version->to_string() : "(latest)"), body_));

    // A module is never assigned itself: it runs wherever its group does.
    const resolved_thread r = resolve_thread(state_.doc.config(), state_.current_group);
    s.form->addRow("runs on", new QLabel(QString::fromStdString(r.name + (r.inherited ? " (inherited)" : "")), body_));

    // The property rows come last: apply_lock re-enables them after locking the rest of the form.
    const style::section props = add_section("properties");
    properties_ = new property_grid(state_, callbacks_, body_);
    properties_->rebuild(state_.current_group, m.name, m);
    props.form->addRow(properties_);
    // A module without properties leaves an empty block on screen for nothing.
    props.box->setVisible(!properties_->empty());
    property_rows_.push_back(props.box);
}

void inspector_widget::build_group_section(const std::string& group_path, const std::string& name, bool renameable) {
    const style::section s = add_section("group");
    QFormLayout* form = s.form;

    if (renameable) {
        auto* name_edit = new QLineEdit(QString::fromStdString(name), body_);
        form->addRow("name", name_edit);
        const std::string old_name = name;
        QObject::connect(name_edit, &QLineEdit::editingFinished, this,
                         [this, old_name, name_edit] { commit_rename(old_name, name_edit); });
    } else {
        form->addRow("name", new QLabel(QString::fromStdString(name), body_));
    }

    if (group_path.empty()) {
        // The root cannot be assigned — document::set_assignment rejects an empty path — so what it
        // runs on is shown, not chosen: an unassigned root goes to the first declared thread.
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
        // An unassigned group runs inline in its nearest assigned ancestor; saying so beats leaving
        // "(none)" to be read as "nowhere".
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
