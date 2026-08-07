// SPDX-License-Identifier: Apache-2.0
#include "kit/property_grid.hpp"

#include <exception>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QMenu>
#include <QSignalBlocker>
#include <QStringList>

#include <atp/runtime/property_override.hpp>
#include <atp/studio/node_ref.hpp>

namespace atp::studio::ui {

namespace {

nlohmann::json editor_to_json(const property_editor& e) {
    std::string text = e.text();
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
            return text == "true";
        case io::property_kind::text:
            break;
    }
    return text;
}

}  // namespace

std::string property_editor::text() const {
    if (combo != nullptr) {
        return combo->currentText().toStdString();
    }
    if (check != nullptr) {
        return check->isChecked() ? "true" : "false";
    }
    return line->text().toStdString();
}

void property_editor::set_text(const std::string& value) {
    const QString text = QString::fromStdString(value);
    if (combo != nullptr) {
        const QSignalBlocker block(combo);
        const int index = combo->findText(text);
        combo->setCurrentIndex(index < 0 ? 0 : index);
        return;
    }
    if (check != nullptr) {
        const QSignalBlocker block(check);
        check->setChecked(value == "true");
        return;
    }
    const QSignalBlocker block(line);
    line->setText(text);
}

property_grid::property_grid(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    form_ = new QFormLayout(this);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form_->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form_->setLabelAlignment(Qt::AlignLeft);
}

void property_grid::rebuild(const std::string& group_path, const std::string& child, const runtime::module_node& m) {
    while (form_->count() > 0) {
        QLayoutItem* old = form_->takeAt(0);
        if (QWidget* w = old->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete old;
    }
    rows_.clear();
    group_path_ = group_path;
    child_ = child;

    const module_info* info = state_.describe_cached(m.factory, m.factory_version);
    if (info == nullptr) {
        return;
    }
    for (const property_info& p : info->properties) {
        auto row = std::make_unique<property_row>();
        row->info = p;
        row->label = new QLabel(QString::fromStdString(p.name), this);
        if (!p.persistent) {
            QFont f = row->label->font();
            f.setItalic(true);
            row->label->setFont(f);
            row->label->setToolTip("session only: not written to the project");
        }

        auto editor = std::make_unique<property_editor>();
        editor->kind = p.kind;
        if (!p.options.empty()) {
            editor->combo = new QComboBox(this);
            for (const std::string& option : p.options) {
                editor->combo->addItem(QString::fromStdString(option));
            }
            editor->widget = editor->combo;
        } else if (p.kind == io::property_kind::boolean) {
            editor->check = new QCheckBox(this);
            editor->widget = editor->check;
        } else {
            editor->line = new QLineEdit(this);
            editor->widget = editor->line;
        }
        editor->widget->setContextMenuPolicy(Qt::NoContextMenu);

        row->editor = std::move(editor);
        const std::string value = current_value(*row);
        row->editor->set_text(value);

        row->reset = style::tool_button(style::glyph::reset, "back to the default", this);

        auto* field = new QWidget(this);
        auto* field_layout = new QHBoxLayout(field);
        field_layout->setContentsMargins(0, 0, 0, 0);
        field_layout->addWidget(row->editor->widget, 1);
        field_layout->addWidget(row->reset);
        form_->addRow(row->label, field);

        property_row* raw = row.get();
        mark(*raw, value);
        if (raw->editor->line != nullptr) {
            QObject::connect(raw->editor->line, &QLineEdit::editingFinished, this, [this, raw] { apply(*raw); });
        } else if (raw->editor->check != nullptr) {
            QObject::connect(raw->editor->check, &QCheckBox::toggled, this, [this, raw] { apply(*raw); });
        } else {
            QObject::connect(raw->editor->combo, &QComboBox::currentIndexChanged, this, [this, raw] { apply(*raw); });
        }
        QObject::connect(raw->reset, &QToolButton::clicked, this, [this, raw] { reset(*raw); });
        rows_.push_back(std::move(row));
    }
}

void property_grid::sync() {
    for (const std::unique_ptr<property_row>& row : rows_) {
        const std::string value = current_value(*row);
        if (!row->editor->widget->hasFocus()) {
            row->editor->set_text(value);
        }
        mark(*row, value);
    }
}

QString property_grid::value_text(const std::string& name) const {
    for (const std::unique_ptr<property_row>& row : rows_) {
        if (row->info.name == name) {
            return QString::fromStdString(current_value(*row));
        }
    }
    return {};
}

QString property_grid::all_text() const {
    QStringList lines;
    for (const std::unique_ptr<property_row>& row : rows_) {
        lines << QString("%1 = %2").arg(QString::fromStdString(row->info.name),
                                        QString::fromStdString(current_value(*row)));
    }
    lines.sort();
    return lines.join(QLatin1Char('\n'));
}

void property_grid::copy_value(const std::string& name) const {
    const QString text = value_text(name);
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void property_grid::copy_all() const {
    const QString text = all_text();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

const property_row* property_grid::row_at(const QPoint& pos) const {
    const QWidget* hit = childAt(pos);
    while (hit != nullptr && hit != this) {
        for (const std::unique_ptr<property_row>& row : rows_) {
            if (hit == row->label || hit == row->editor->widget || hit == row->reset) {
                return row.get();
            }
        }
        hit = hit->parentWidget();
    }
    return nullptr;
}

void property_grid::contextMenuEvent(QContextMenuEvent* event) {
    if (rows_.empty()) {
        return;
    }
    const property_row* row = row_at(event->pos());

    QMenu menu;
    QAction* value = nullptr;
    if (row != nullptr) {
        value = menu.addAction(QString("Copy value of '%1'").arg(QString::fromStdString(row->info.name)));
    }
    QAction* all = menu.addAction(QStringLiteral("Copy all properties"));

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen != nullptr && chosen == value) {
        copy_value(row->info.name);
    } else if (chosen == all) {
        copy_all();
    }
    event->accept();
}

std::string property_grid::current_value(const property_row& row) const {
    std::string value = row.info.default_value;
    const runtime::group_node* g = state_.doc.group_at(group_path_);
    if (g != nullptr) {
        for (const runtime::child_node& c : g->modules) {
            if (c.module && c.module->name == child_) {
                for (const auto& [name, node] : c.module->properties) {
                    if (name == row.info.name) {
                        value = runtime::detail::scalar_to_string(node);
                    }
                }
            }
        }
    }
    if (state_.view->running()) {
        for (const live_property& p : state_.view->live_properties(node_ref{group_path_, child_}.full())) {
            if (p.info.name == row.info.name) {
                value = p.value;
            }
        }
    }
    return value;
}

void property_grid::apply(property_row& row) {
    const std::string value = row.editor->text();
    try {
        if (state_.view->running()) {
            state_.view->set_property({node_ref{group_path_, child_}.full(), row.info.name, value});
        }
        if (row.info.persistent) {
            state_.doc.set_property(group_path_, child_, row.info.name, editor_to_json(*row.editor));
        }
    } catch (const std::exception& e) {
        style::mark_error(row.editor->widget, QString::fromStdString(e.what()));
        row.editor->set_text(current_value(row));
        return;
    }
    style::clear_error(row.editor->widget);
    mark(row, value);
    callbacks_.project_changed();
}

void property_grid::reset(property_row& row) {
    try {
        if (state_.view->running()) {
            state_.view->set_property({node_ref{group_path_, child_}.full(), row.info.name, row.info.default_value});
        }
        state_.doc.clear_property(group_path_, child_, row.info.name);
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("property: ") + e.what()));
        return;
    }
    style::clear_error(row.editor->widget);
    row.editor->set_text(row.info.default_value);
    mark(row, row.info.default_value);
    callbacks_.project_changed();
}

void property_grid::mark(property_row& row, const std::string& value) {
    const bool changed = value != row.info.default_value;
    QFont f = row.label->font();
    f.setBold(changed);
    row.label->setFont(f);
    row.reset->setVisible(changed);
}

}  // namespace atp::studio::ui
