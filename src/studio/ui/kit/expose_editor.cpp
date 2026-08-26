// SPDX-License-Identifier: Apache-2.0
#include "kit/expose_editor.hpp"

#include <algorithm>
#include <exception>
#include <optional>
#include <vector>

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>

#include <atp/studio/port_types.hpp>

namespace atp::studio::ui {

namespace {

constexpr int alias_column = 0;
constexpr int port_column = 1;
constexpr int old_alias_role = Qt::UserRole + 1;

}  // namespace

expose_editor::expose_editor(app_state& state, ui_callbacks& callbacks, bool inputs, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks), inputs_(inputs) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({"alias", "port"});
    tree_->setRootIsDecorated(false);
    tree_->header()->setSectionResizeMode(QHeaderView::Stretch);
    style::embed_view(tree_);
    style::set_placeholder(tree_, "No port of this group is exported yet.");
    layout->addWidget(tree_, 1);

    const style::button_bar bar = style::make_button_bar(this);
    add_ = style::tool_button(style::glyph::add, "export the next child port", bar.box);
    remove_ = style::tool_button(style::glyph::drop, "drop the selected export", bar.box);
    bar.row->addWidget(add_);
    bar.row->addWidget(remove_);
    bar.row->addStretch(1);
    layout->addWidget(bar.box);

    QObject::connect(add_, &QToolButton::clicked, this, [this] { add_export(); });
    QObject::connect(remove_, &QToolButton::clicked, this, [this] { remove_selected(); });
    QObject::connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (!filling_ && column == alias_column) {
            commit_alias(item);
        }
    });
}

void expose_editor::sync() {
    QMetaObject::invokeMethod(this, [this] { rebuild(group_path_); }, Qt::QueuedConnection);
}

void expose_editor::rebuild(const std::string& group_path) {
    group_path_ = group_path;
    filling_ = true;
    tree_->clear();
    const runtime::group_node* g = state_.doc.group_at(group_path_);
    if (g == nullptr) {
        filling_ = false;
        return;
    }
    const describe_fn describe = [this](const std::string& factory, const std::optional<version>& ver) {
        return state_.describe_cached(factory, ver);
    };
    const std::vector<std::string> candidates = expose_candidates(*g, inputs_, describe);
    for (const auto& [alias, path] : inputs_ ? g->expose_inputs : g->expose_outputs) {
        auto* item = new QTreeWidgetItem(tree_);
        item->setText(alias_column, QString::fromStdString(alias));
        item->setData(alias_column, old_alias_role, QString::fromStdString(alias));
        item->setFlags(item->flags() | Qt::ItemIsEditable);

        auto* combo = new QComboBox(tree_);
        for (const std::string& candidate : candidates) {
            combo->addItem(QString::fromStdString(candidate));
        }
        if (std::ranges::find(candidates, path) == candidates.end()) {
            combo->addItem(QString::fromStdString(path));
        }
        combo->setCurrentText(QString::fromStdString(path));
        tree_->setItemWidget(item, port_column, combo);
        QObject::connect(combo, &QComboBox::currentTextChanged, this, [this, item](const QString& text) {
            if (!filling_) {
                commit_port(item, text.toStdString());
            }
        });
    }
    filling_ = false;
}

void expose_editor::add_export() {
    const runtime::group_node* g = state_.doc.group_at(group_path_);
    if (g == nullptr) {
        return;
    }
    const describe_fn describe = [this](const std::string& factory, const std::optional<version>& ver) {
        return state_.describe_cached(factory, ver);
    };
    const auto& exported = inputs_ ? g->expose_inputs : g->expose_outputs;
    for (const std::string& candidate : expose_candidates(*g, inputs_, describe)) {
        if (std::ranges::none_of(exported, [&](const auto& e) { return e.second == candidate; })) {
            guard("expose", [&] { (void)expose_port(state_.doc, group_path_, candidate, !inputs_); });
            return;
        }
    }
    callbacks_.error(inputs_ ? "expose: every child input is already exported"
                             : "expose: every child output is already exported");
}

void expose_editor::remove_selected() {
    QTreeWidgetItem* item = tree_->currentItem();
    if (item == nullptr) {
        return;
    }
    const std::string alias = item->data(alias_column, old_alias_role).toString().toStdString();
    guard("expose", [&] {
        if (inputs_) {
            state_.doc.remove_expose_input(group_path_, alias);
        } else {
            state_.doc.remove_expose_output(group_path_, alias);
        }
    });
}

void expose_editor::commit_alias(QTreeWidgetItem* item) {
    const std::string old_alias = item->data(alias_column, old_alias_role).toString().toStdString();
    const std::string next = item->text(alias_column).toStdString();
    if (next == old_alias) {
        return;
    }
    guard("expose", [&] {
        if (inputs_) {
            state_.doc.rename_expose_input(group_path_, old_alias, next);
        } else {
            state_.doc.rename_expose_output(group_path_, old_alias, next);
        }
    });
}

void expose_editor::commit_port(QTreeWidgetItem* item, const std::string& port_path) {
    const std::string alias = item->data(alias_column, old_alias_role).toString().toStdString();
    guard("expose", [&] {
        if (inputs_) {
            state_.doc.set_expose_input(group_path_, alias, port_path);
        } else {
            state_.doc.set_expose_output(group_path_, alias, port_path);
        }
    });
}

void expose_editor::guard(const char* context, const std::function<void()>& operation) {
    try {
        operation();
        callbacks_.project_changed();
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string(context) + ": " + e.what()));
        sync();
    }
}

}  // namespace atp::studio::ui
