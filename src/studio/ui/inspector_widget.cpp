#include "inspector_widget.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>

#include <atp/runtime/property_override.hpp>

namespace atp::studio::ui {

namespace {

property_editor* make_editor(const property_info& p, const QString& current, QWidget* parent) {
    auto* editor = new property_editor;  // владение — у вектора инспектора
    editor->kind = p.kind;
    if (!p.options.empty()) {
        editor->combo = new QComboBox(parent);
        for (const std::string& option : p.options) {
            editor->combo->addItem(QString::fromStdString(option));
        }
        // setCurrentText на редактируемом комбобоксе завёл бы левый пункт;
        // findText отдаёт -1 на рассинхроне документа с описанием модуля —
        // тогда остаётся первый вариант, а Set перезапишет значение явно.
        const int index = editor->combo->findText(current);
        editor->combo->setCurrentIndex(index < 0 ? 0 : index);
        editor->widget = editor->combo;
    } else if (p.kind == io::property_kind::boolean) {
        editor->check = new QCheckBox(parent);
        editor->check->setChecked(current == "true");
        editor->widget = editor->check;
    } else {
        editor->line = new QLineEdit(current, parent);
        editor->widget = editor->line;
    }
    return editor;
}

// Текст редактора → JSON-скаляр для документа (обратная сторона
// scalar_to_string). Мусор в number — config_error через guard: разобравшийся,
// но нечисловой JSON («true», «[1]») отвергается тоже, иначе в конфиг уехало бы
// значение чужого типа и споткнулось бы только при следующей сборке.
nlohmann::json editor_to_json(const property_editor& e) {
    const std::string text = e.text();
    switch (e.kind) {
        case io::property_kind::number:
            try {
                nlohmann::json parsed = nlohmann::json::parse(text);
                if (!parsed.is_number()) {
                    throw runtime::config_error("'" + text + "' is not a number");
                }
                return parsed;
            } catch (const nlohmann::json::parse_error&) {
                throw runtime::config_error("'" + text + "' is not a number");
            }
        case io::property_kind::boolean:
            return nlohmann::json(text == "true");
        case io::property_kind::text:
            break;
    }
    return nlohmann::json(text);
}

}  // namespace

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
    property_editors_.clear();  // виджеты уже мертвы — их connect'ы вместе с ними
    property_rows_.clear();
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
    // Структура документа read-only на ходу, проперти — исключение (их правят
    // именно на ходу). Поэтому запирается не body_ целиком, а каждый его блок
    // по отдельности: setEnabled(true) на ребёнке запрещённого родителя в Qt —
    // no-op, и включить строки пропертей обратно было бы уже нельзя.
    for (int i = 0; i < body_layout_->count(); ++i) {
        QWidget* block = body_layout_->itemAt(i)->widget();
        if (block == nullptr) {
            continue;
        }
        const bool is_property_block = std::ranges::find(property_rows_, block) != property_rows_.end();
        block->setEnabled(!locked || is_property_block);
    }
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

    // Строки пропертей — последними в секции: refresh вернёт им доступность
    // после общего запирания формы на ходу.
    property_rows_ = build_property_rows(m);
}

std::vector<QWidget*> inspector_widget::build_property_rows(const runtime::module_node& m) {
    std::vector<QWidget*> rows;
    const module_info* info = state_.describe_cached(m.factory, m.factory_version);
    if (info == nullptr || info->properties.empty()) {
        return rows;
    }
    add_header("properties");
    rows.push_back(body_layout_->itemAt(body_layout_->count() - 1)->widget());  // заголовок не гасим вместе с формой
    const bool running = state_.run.running();
    const std::string module_path = detail::full_path(state_.current_group, m.name);
    for (const property_info& p : info->properties) {
        QWidget* row = add_row();
        rows.push_back(row);
        auto* label = new QLabel(QString::fromStdString(p.name), row);
        if (!p.persistent) {
            label->setText(label->text() + " (на время сеанса)");
            QFont f = label->font();
            f.setItalic(true);
            label->setFont(f);
        }
        row->layout()->addWidget(label);

        // Текущее значение: на ходу — с живого модуля, иначе из документа;
        // нет в документе — дефолт из описания. Повторный резолв на refresh
        // дешёв: формы малы.
        QString current = QString::fromStdString(p.default_value);
        for (const auto& [pname, pvalue] : m.properties) {
            if (pname == p.name) {
                current = QString::fromStdString(runtime::detail::scalar_to_string(pvalue));
            }
        }
        if (running) {
            if (atp::group* root = state_.run.live_root()) {
                try {
                    current = QString::fromStdString(runtime::find_property(*root, module_path, p.name).to_string());
                } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch)
                    // рассинхрон документа и запуска — показываем документное значение
                }
            }
        }

        property_editors_.push_back(std::unique_ptr<property_editor>(make_editor(p, current, row)));
        property_editor* editor = property_editors_.back().get();
        row->layout()->addWidget(editor->widget);
        auto* apply = new QPushButton("Set", row);
        row->layout()->addWidget(apply);
        auto* clear = new QPushButton("Reset", row);
        row->layout()->addWidget(clear);

        const std::string prop_name = p.name;
        const std::string child_name = m.name;
        const bool persistent = p.persistent;
        QObject::connect(
            apply, &QPushButton::clicked, this, [this, module_path, child_name, prop_name, persistent, editor] {
                guard("property", [&] {
                    if (state_.run.running()) {
                        state_.run.set_property({module_path, prop_name, editor->text()});
                    }
                    if (persistent) {
                        state_.doc.set_property(state_.current_group, child_name, prop_name, editor_to_json(*editor));
                    }
                });
            });
        const std::string default_value = p.default_value;
        QObject::connect(clear, &QPushButton::clicked, this, [this, module_path, child_name, prop_name, default_value] {
            guard("property", [&] {
                if (state_.run.running()) {
                    // живой модуль узнаёт об откате тем же каналом записи
                    state_.run.set_property({module_path, prop_name, default_value});
                }
                state_.doc.clear_property(state_.current_group, child_name, prop_name);
            });
        });
    }
    return rows;
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
            state_.doc.add_thread(
                thread_name->text().toStdString(), m,
                m == thread_mode::throttled ? std::chrono::milliseconds(period->value()) : std::chrono::milliseconds{});
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
