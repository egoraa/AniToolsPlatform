// SPDX-License-Identifier: Apache-2.0
#include "panels/inspector_widget.hpp"

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTextDocument>

#include <atp/config/node.hpp>
#include <atp/runtime/config_file.hpp>
#include <atp/runtime/json_codec.hpp>
#include <atp/studio/node_ref.hpp>
#include <atp/studio/thread_resolve.hpp>

namespace atp::studio::ui {

namespace {

const QString inline_source = QStringLiteral("(inline)");

}  // namespace

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
                if (c.module->config && c.module->config->is_string()) {
                    key += '\0' + c.module->config->as_string();
                }
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
    config_tree_ = nullptr;
    config_edit_ = nullptr;
    config_source_ = nullptr;
    share_name_ = nullptr;
    config_declined_.clear();
    config_schema_.reset();
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
    sync_config();
    if (config_tree_ != nullptr) {
        if (!config_tree_->sync(shown_config())) {
            rebuild();
        }
        return;
    }
    if (config_declined_.isEmpty() || config_schema_ == nullptr || config_edit_ == nullptr) {
        return;
    }
    if (!config_edit_->document()->isModified() && config_misfits(*config_schema_, shown_config()).empty()) {
        rebuild();
    }
}

atp::config::node inspector_widget::shown_config() const {
    const atp::config::node* stored = effective_config();
    return stored == nullptr ? atp::config::node(atp::config::node::object_type{}) : *stored;
}

void inspector_widget::sync_config() {
    if (config_edit_ == nullptr || config_edit_->document()->isModified() || !config_file_.empty()) {
        return;
    }
    const atp::config::node* stored = effective_config();
    const QString text = QString::fromStdString(stored == nullptr ? std::string() : runtime::json_dump(*stored, 4));
    if (text == config_edit_->toPlainText()) {
        return;
    }
    config_edit_->setPlainText(text);
    config_edit_->document()->setModified(false);
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

bool inspector_widget::guard(const char* context, const std::function<void()>& operation) {
    try {
        operation();
        callbacks_.project_changed();
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string(context) + ": " + e.what()));
        return false;
    }
    return true;
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

    build_config_section(m);
}

void inspector_widget::build_config_section(const runtime::module_node& m) {
    const module_info* info = state_.describe_cached(m.factory, m.factory_version);
    const bool declared = info != nullptr && info->config_schema != nullptr && !info->config_schema->entries().empty();

    const style::section s = add_section("config");
    config_group_ = state_.current_group;
    config_module_ = m.name;
    const std::string written = m.config && m.config->is_string() ? m.config->as_string() : std::string();
    const bool from_file = written.starts_with(runtime::config_file_prefix);
    config_file_ = from_file ? written : std::string();
    shared_name_ = from_file ? std::string() : written;
    if (!shared_name_.empty()) {
        const atp::config::node* block = state_.doc.shared_config(shared_name_);
        if (block != nullptr && block->is_string() && block->as_string().starts_with(runtime::config_file_prefix)) {
            config_file_ = block->as_string();
        }
    }

    config_source_ = new QComboBox(body_);
    config_source_->setObjectName("config_source");
    config_source_->addItem(inline_source);
    for (const std::string& name : state_.doc.config_names()) {
        config_source_->addItem(QString::fromStdString(name));
    }
    const std::string current = shared_name_.empty() ? config_file_ : shared_name_;
    if (!current.empty() && config_source_->findText(QString::fromStdString(current)) < 0) {
        config_source_->addItem(QString::fromStdString(current));
    }
    config_source_->setCurrentText(current.empty() ? inline_source : QString::fromStdString(current));
    s.form->addRow("source", config_source_);
    QObject::connect(config_source_, &QComboBox::currentIndexChanged, this,
                     [this](int) { change_config_source(config_source_->currentText()); });

    if (declared) {
        config_schema_ = info->config_schema;
    }
    if (declared && shared_name_.empty() && config_file_.empty()) {
        const atp::config::node empty(atp::config::node::object_type{});
        auto* rows = new config_tree(state_, callbacks_, body_);
        if (rows->rebuild(config_group_, config_module_, m.config ? *m.config : empty, config_schema_)) {
            config_tree_ = rows;
            s.form->addRow(config_tree_);
            build_share_row(s);
            return;
        }
        config_declined_ = rows->declined();
        delete rows;
    }

    config_edit_ = new QPlainTextEdit(body_);
    config_edit_->setPlaceholderText(shared_name_.empty() ? "an object, or nothing at all"
                                                          : "an object; this block is not declared yet");
    config_edit_->setFixedHeight(config_editor_height);
    if (config_file_.empty()) {
        const atp::config::node* shown = effective_config();
        config_edit_->setPlainText(
            QString::fromStdString(shown == nullptr ? std::string() : runtime::json_dump(*shown, 4)));
    } else {
        config_edit_->setReadOnly(true);
        config_edit_->setPlainText(QString::fromStdString(config_file_preview()));
    }
    config_edit_->document()->setModified(false);
    if (!config_declined_.isEmpty()) {
        auto* why = new QLabel(config_declined_, body_);
        why->setObjectName("config_problem");
        why->setWordWrap(true);
        style::error_text(why);
        s.form->addRow(why);
        style::mark_error(config_edit_, config_declined_);
    }
    s.form->addRow(config_edit_);

    config_edit_->installEventFilter(this);
    QPlainTextEdit* mine = config_edit_;
    QObject::connect(config_edit_, &QPlainTextEdit::destroyed, this, [this, mine] {
        if (config_edit_ == mine) {
            config_edit_ = nullptr;
        }
    });

    build_share_row(s);
}

void inspector_widget::build_share_row(const style::section& s) {
    if (!shared_name_.empty() || !config_file_.empty()) {
        return;
    }
    auto* row = new QWidget(body_);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    share_name_ = new QLineEdit(row);
    share_name_->setObjectName("config_share_name");
    share_name_->setPlaceholderText("name for a shared block");
    auto* button = new QPushButton("Share", row);
    button->setObjectName("config_share");
    layout->addWidget(share_name_);
    layout->addWidget(button);
    s.form->addRow(row);
    QObject::connect(button, &QPushButton::clicked, this, [this] { share_config(); });
}

std::string inspector_widget::config_file_preview() const {
    try {
        const runtime::config_source src = runtime::load_config_source(
            std::string_view(config_file_).substr(runtime::config_file_prefix.size()), state_.saved_dir());
        return src.text;
    } catch (const std::exception& e) {
        return e.what();
    }
}

const atp::config::node* inspector_widget::effective_config() const {
    if (!shared_name_.empty()) {
        return state_.doc.shared_config(shared_name_);
    }
    try {
        return state_.doc.config_of(config_group_, config_module_);
    } catch (const std::exception&) {
        return nullptr;
    }
}

void inspector_widget::change_config_source(const QString& choice) {
    const std::string next = choice == inline_source ? std::string() : choice.toStdString();
    if (next == (shared_name_.empty() ? config_file_ : shared_name_)) {
        return;
    }
    QPlainTextEdit* editing = std::exchange(config_edit_, nullptr);
    bool done = false;
    if (next.empty()) {
        const atp::config::node* block = state_.doc.shared_config(shared_name_);
        done = guard("config", [this, block] {
            if (block == nullptr) {
                state_.doc.clear_config(config_group_, config_module_);
            } else {
                state_.doc.set_config(config_group_, config_module_, *block);
            }
        });
    } else {
        done = guard("config",
                     [this, &next] { state_.doc.set_config(config_group_, config_module_, atp::config::node(next)); });
    }
    if (!done) {
        config_edit_ = editing;
    }
    refresh();
}

void inspector_widget::share_config() {
    if (share_name_ == nullptr) {
        return;
    }
    const std::string name = share_name_->text().trimmed().toStdString();
    if (state_.doc.shared_config(name) != nullptr) {
        callbacks_.error(QString("config: '%1' is already declared; pick it as the source instead")
                             .arg(QString::fromStdString(name)));
        return;
    }
    const atp::config::node* shown = effective_config();
    std::optional<atp::config::node> parsed;
    if (config_edit_ == nullptr) {
        parsed = shown == nullptr ? atp::config::node(atp::config::node::object_type{}) : *shown;
    } else {
        parsed = runtime::try_json_parse(config_edit_->toPlainText().trimmed().toStdString());
    }
    if (!parsed) {
        callbacks_.error("config: not valid JSON");
        return;
    }
    QPlainTextEdit* editing = std::exchange(config_edit_, nullptr);
    if (!guard("config", [this, &name, &parsed] {
            state_.doc.set_shared_config(name, *parsed);
            state_.doc.set_config(config_group_, config_module_, atp::config::node(name));
        })) {
        config_edit_ = editing;
    }
    refresh();
}

bool inspector_widget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == config_edit_ && event->type() == QEvent::FocusOut) {
        commit_config();
    }
    return QWidget::eventFilter(watched, event);
}

void inspector_widget::commit_config() {
    if (config_edit_ == nullptr || !config_file_.empty()) {
        return;
    }
    const std::string text = config_edit_->toPlainText().trimmed().toStdString();
    const atp::config::node* stored = effective_config();
    if (text == (stored == nullptr ? std::string() : runtime::json_dump(*stored, 4))) {
        config_edit_->document()->setModified(false);
        return;
    }
    if (text.empty()) {
        guard("config", [this] { state_.doc.clear_config(config_group_, config_module_); });
        if (config_edit_ != nullptr) {
            config_edit_->document()->setModified(false);
        }
        return;
    }
    const std::optional<atp::config::node> parsed = runtime::try_json_parse(text);
    if (!parsed) {
        callbacks_.error("config: not valid JSON");
        return;
    }
    guard("config", [this, &parsed] {
        if (shared_name_.empty()) {
            state_.doc.set_config(config_group_, config_module_, *parsed);
        } else {
            state_.doc.set_shared_config(shared_name_, *parsed);
        }
    });
    config_edit_->document()->setModified(false);
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
