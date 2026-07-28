#include "property_grid.hpp"

#include <exception>

#include <QFont>
#include <QHBoxLayout>
#include <QSignalBlocker>

#include <atp/runtime/property_override.hpp>

namespace atp::studio::ui {

namespace {

// Editor text → the JSON scalar the document stores (the reverse of scalar_to_string). Garbage in a
// number becomes a config_error; JSON that parses but is not a number ("true", "[1]") is rejected as
// well, since otherwise a value of a foreign type would reach the config and only trip up at the
// next build.
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

std::string property_editor::text() const {
    if (combo != nullptr) {
        return combo->currentText().toStdString();  // the item text came from to_string
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
        // findText returns -1 when the document is out of step with the module description; the
        // first option stays selected rather than a foreign item being introduced.
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
    // Driven by count() rather than by takeAt returning nullptr: unlike a box layout, QFormLayout
    // warns on an out-of-range index instead of quietly reporting the end.
    while (form_->count() > 0) {
        QLayoutItem* old = form_->takeAt(0);
        if (QWidget* w = old->widget()) {
            w->hide();
            w->deleteLater();  // rebuild can be reached from a signal of one of these widgets
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
            // Session-only lives in the tooltip and in italics: spelling it out in the label made
            // the name column twice as wide as the values.
            QFont f = row->label->font();
            f.setItalic(true);
            row->label->setFont(f);
            row->label->setToolTip("session only: not written to the document");
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
        // The focused editor is what the user is typing into right now — leave it be.
        if (!row->editor->widget->hasFocus()) {
            row->editor->set_text(value);
        }
        mark(*row, value);
    }
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
    if (state_.run.running()) {
        if (atp::group* root = state_.run.live_root()) {
            try {
                value =
                    runtime::find_property(*root, detail::full_path(group_path_, child_), row.info.name).to_string();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch)
                // Document out of step with the run — show the document's value.
            }
        }
    }
    return value;
}

void property_grid::apply(property_row& row) {
    const std::string value = row.editor->text();
    try {
        if (state_.run.running()) {
            state_.run.set_property({detail::full_path(group_path_, child_), row.info.name, value});
        }
        if (row.info.persistent) {
            state_.doc.set_property(group_path_, child_, row.info.name, editor_to_json(*row.editor));
        }
    } catch (const std::exception& e) {
        style::mark_error(row.editor->widget, QString::fromStdString(e.what()));
        row.editor->set_text(current_value(row));  // the rejected value never reaches the document
        return;
    }
    style::clear_error(row.editor->widget);
    mark(row, value);
    callbacks_.document_changed();
}

void property_grid::reset(property_row& row) {
    try {
        if (state_.run.running()) {
            // The live module learns about the rollback through the same write channel.
            state_.run.set_property({detail::full_path(group_path_, child_), row.info.name, row.info.default_value});
        }
        state_.doc.clear_property(group_path_, child_, row.info.name);
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("property: ") + e.what()));
        return;
    }
    style::clear_error(row.editor->widget);
    row.editor->set_text(row.info.default_value);
    mark(row, row.info.default_value);
    callbacks_.document_changed();
}

void property_grid::mark(property_row& row, const std::string& value) {
    const bool changed = value != row.info.default_value;
    QFont f = row.label->font();
    f.setBold(changed);
    row.label->setFont(f);
    row.reset->setVisible(changed);
}

}  // namespace atp::studio::ui
