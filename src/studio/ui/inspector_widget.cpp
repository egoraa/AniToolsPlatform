#include "inspector_widget.hpp"

#include <chrono>
#include <exception>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>

namespace atp::studio::ui {

inspector_widget::inspector_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    auto* outer = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    body_ = new QWidget(scroll);
    body_layout_ = new QVBoxLayout(body_);
    body_layout_->setAlignment(Qt::AlignTop);
    scroll->setWidget(body_);
    outer->addWidget(scroll);
}

void inspector_widget::refresh() {
    // пересборка формы целиком: незачем синхронизировать поля точечно
    QLayoutItem* old = nullptr;
    while ((old = body_layout_->takeAt(0)) != nullptr) {
        delete old->widget();
        delete old;
    }
    const bool locked = state_.run.running();
    const runtime::group_node* g = state_.doc.group_at(state_.current_group);
    if (g != nullptr && !state_.selected_child.empty()) {
        for (const runtime::child_node& c : g->children) {
            const std::string& name = c.module ? c.module->name : c.group->name;
            if (name != state_.selected_child) {
                continue;
            }
            if (c.module) {
                build_module_section(*c.module);
            } else {
                build_group_section(name);
            }
            break;
        }
    }
    build_document_section();
    body_->setEnabled(!locked);  // документ read-only на ходу
}

void inspector_widget::add_header(const QString& text) {
    auto* label = new QLabel(QString("<b>%1</b>").arg(text), body_);
    body_layout_->addWidget(label);
}

QWidget* inspector_widget::add_row() {
    auto* row = new QWidget(body_);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    body_layout_->addWidget(row);
    return row;
}

void inspector_widget::guard(const char* context, const std::function<void()>& operation) {
    try {
        operation();
        callbacks_.document_changed();
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string(context) + ": " + e.what()));
    }
}

void inspector_widget::build_module_section(const runtime::module_node& m) {
    add_header(QString::fromStdString("module: " + m.factory));

    QWidget* row = add_row();
    auto* name_edit = new QLineEdit(QString::fromStdString(m.name), row);
    auto* rename = new QPushButton("Rename", row);
    row->layout()->addWidget(name_edit);
    row->layout()->addWidget(rename);
    const std::string old_name = m.name;
    QObject::connect(rename, &QPushButton::clicked, this, [this, old_name, name_edit] {
        guard("rename", [&] {
            const std::string next = name_edit->text().toStdString();
            state_.doc.rename_child(state_.current_group, old_name, next);
            state_.selected_child = next;
        });
    });

    // версия фиксируется при добавлении; смена версии — пересоздание
    // ребёнка: сохранность соединений при этом не гарантируется и вручную
    body_layout_->addWidget(new QLabel(
        QString::fromStdString("version: " + (m.factory_version ? m.factory_version->to_string() : "(latest)")),
        body_));

    auto* params = new QPlainTextEdit(QString::fromStdString(m.params), body_);
    params->setFixedHeight(80);
    body_layout_->addWidget(params);
    auto* apply = new QPushButton("Apply params", body_);
    body_layout_->addWidget(apply);
    QObject::connect(apply, &QPushButton::clicked, this, [this, old_name, params] {
        guard("params",
              [&] { state_.doc.set_params(state_.current_group, old_name, params->toPlainText().toStdString()); });
    });
}

void inspector_widget::build_group_section(const std::string& name) {
    add_header(QString::fromStdString("group: " + name));
    build_expose_editor(name, true);
    build_expose_editor(name, false);
}

void inspector_widget::build_expose_editor(const std::string& child, bool inputs) {
    const std::string group_path = detail::full_path(state_.current_group, child);
    const runtime::group_node* g = state_.doc.group_at(group_path);
    if (g == nullptr) {
        return;
    }
    add_header(inputs ? "expose inputs" : "expose outputs");
    const auto& map = inputs ? g->expose_inputs : g->expose_outputs;
    auto* list = new QListWidget(body_);
    list->setFixedHeight(70);
    for (const auto& [alias, path] : map) {
        list->addItem(QString::fromStdString(alias + " -> " + path));
    }
    body_layout_->addWidget(list);

    QWidget* controls = add_row();
    auto* remove = new QPushButton("Remove", controls);
    auto* alias_edit = new QLineEdit(controls);
    alias_edit->setPlaceholderText("alias");
    auto* path_edit = new QLineEdit(controls);
    path_edit->setPlaceholderText("child.port");
    auto* add = new QPushButton("Add", controls);
    controls->layout()->addWidget(remove);
    controls->layout()->addWidget(alias_edit);
    controls->layout()->addWidget(path_edit);
    controls->layout()->addWidget(add);

    QObject::connect(remove, &QPushButton::clicked, this, [this, group_path, inputs, list] {
        auto* item = list->currentItem();
        if (item == nullptr) {
            return;
        }
        const std::string alias = item->text().split(" -> ").first().toStdString();
        guard("expose", [&] {
            if (inputs) {
                state_.doc.remove_expose_input(group_path, alias);
            } else {
                state_.doc.remove_expose_output(group_path, alias);
            }
        });
    });
    QObject::connect(add, &QPushButton::clicked, this, [this, group_path, inputs, alias_edit, path_edit] {
        guard("expose", [&] {
            if (inputs) {
                state_.doc.set_expose_input(group_path, alias_edit->text().toStdString(),
                                            path_edit->text().toStdString());
            } else {
                state_.doc.set_expose_output(group_path, alias_edit->text().toStdString(),
                                             path_edit->text().toStdString());
            }
        });
    });
}

void inspector_widget::build_document_section() {
    add_header("Threads");
    auto* threads = new QListWidget(body_);
    threads->setFixedHeight(70);
    for (const runtime::thread_node& t : state_.doc.config().threads) {
        QString text = QString::fromStdString(t.name);
        switch (t.mode) {
            case thread_mode::on_demand:
                text += " (on_demand)";
                break;
            case thread_mode::throttled:
                text += QString(" (throttled, %1 ms)").arg(t.period.count());
                break;
            case thread_mode::spinning:
                text += " (spinning)";
                break;
        }
        threads->addItem(text);
    }
    body_layout_->addWidget(threads);

    QWidget* thread_controls = add_row();
    auto* thread_remove = new QPushButton("Remove", thread_controls);
    auto* thread_name = new QLineEdit(thread_controls);
    thread_name->setPlaceholderText("name");
    auto* mode = new QComboBox(thread_controls);
    mode->addItems({"on_demand", "throttled", "spinning"});
    auto* period = new QSpinBox(thread_controls);
    period->setRange(1, 60000);
    period->setValue(10);
    period->setSuffix(" ms");
    auto* thread_add = new QPushButton("Add", thread_controls);
    thread_controls->layout()->addWidget(thread_remove);
    thread_controls->layout()->addWidget(thread_name);
    thread_controls->layout()->addWidget(mode);
    thread_controls->layout()->addWidget(period);
    thread_controls->layout()->addWidget(thread_add);

    QObject::connect(thread_remove, &QPushButton::clicked, this, [this, threads] {
        auto* item = threads->currentItem();
        if (item == nullptr) {
            return;
        }
        const std::string name = item->text().split(" (").first().toStdString();
        guard("thread", [&] { state_.doc.remove_thread(name); });
    });
    QObject::connect(thread_add, &QPushButton::clicked, this, [this, thread_name, mode, period] {
        guard("thread", [&] {
            const thread_mode m = mode->currentIndex() == 0   ? thread_mode::on_demand
                                  : mode->currentIndex() == 1 ? thread_mode::throttled
                                                              : thread_mode::spinning;
            state_.doc.add_thread(thread_name->text().toStdString(), m,
                                  m == thread_mode::throttled ? std::chrono::milliseconds(period->value())
                                                              : std::chrono::milliseconds{});
        });
    });

    add_header("Assignments");
    auto* assigns = new QListWidget(body_);
    assigns->setFixedHeight(60);
    for (const auto& [path, thread] : state_.doc.config().assignments) {
        assigns->addItem(QString::fromStdString(path + " -> " + thread));
    }
    body_layout_->addWidget(assigns);

    QWidget* assign_controls = add_row();
    auto* assign_remove = new QPushButton("Remove", assign_controls);
    auto* assign_path = new QLineEdit(assign_controls);
    assign_path->setPlaceholderText("group path");
    auto* assign_thread = new QLineEdit(assign_controls);
    assign_thread->setPlaceholderText("thread");
    auto* assign_add = new QPushButton("Assign", assign_controls);
    assign_controls->layout()->addWidget(assign_remove);
    assign_controls->layout()->addWidget(assign_path);
    assign_controls->layout()->addWidget(assign_thread);
    assign_controls->layout()->addWidget(assign_add);

    QObject::connect(assign_remove, &QPushButton::clicked, this, [this, assigns] {
        auto* item = assigns->currentItem();
        if (item == nullptr) {
            return;
        }
        const std::string path = item->text().split(" -> ").first().toStdString();
        guard("assign", [&] { state_.doc.clear_assignment(path); });
    });
    QObject::connect(assign_add, &QPushButton::clicked, this, [this, assign_path, assign_thread] {
        guard("assign", [&] {
            state_.doc.set_assignment(assign_path->text().toStdString(), assign_thread->text().toStdString());
        });
    });
}

}  // namespace atp::studio::ui
