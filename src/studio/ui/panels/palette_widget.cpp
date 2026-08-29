// SPDX-License-Identifier: Apache-2.0
#include "panels/palette_widget.hpp"

#include "kit/icons.hpp"
#include "model/create_group.hpp"
#include "model/drag_payloads.hpp"

#include <filesystem>
#include <optional>
#include <string>

#include <QIcon>
#include <QList>
#include <QMimeData>
#include <QStringList>
#include <QVBoxLayout>

#include <atp/studio/add_module.hpp>

namespace atp::studio::ui {
namespace {

constexpr int group_role = Qt::UserRole + 3;

}  // namespace

class palette_tree final : public QTreeWidget {
   public:
    palette_tree(app_state& state, ui_callbacks& callbacks, QWidget* parent)
        : QTreeWidget(parent), state_(state), callbacks_(callbacks) {
        setHeaderLabel("module");
        setToolTip("double-click or drag onto the canvas to add");
        style::embed_tree(this);
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
        QObject::connect(this, &QTreeWidget::itemDoubleClicked, this,
                         [this](QTreeWidgetItem* item, int) { add_from_item(item); });
    }

    void rebuild() {
        clear();
        auto* group = new QTreeWidgetItem(this);
        group->setText(0, "group");
        group->setIcon(0, group_icon_);
        group->setData(0, group_role, true);
        group->setToolTip(0, "an empty subgroup of the group shown on the canvas");
        for (const plugin_info& p : state_.manager.plugins()) {
            if (!p.loaded) {
                continue;
            }
            auto* root = new QTreeWidgetItem(this);
            root->setText(0, QString::fromStdWString(p.path.filename().wstring()));
            root->setIcon(0, plugin_icon_);
            for (const module_info& m : p.modules) {
                auto* item = new QTreeWidgetItem(root);
                item->setText(0, QString::fromStdString(m.name + " " + m.ver.to_string()));
                item->setIcon(0, module_icons_.of_source(m.source));
                item->setData(0, Qt::UserRole, QString::fromStdString(m.name));
                item->setData(0, Qt::UserRole + 1, QString::fromStdString(m.ver.to_string()));
                item->setData(0, Qt::UserRole + 2, QString::fromStdWString(p.path.wstring()));
                if (m.broken) {
                    item->setDisabled(true);
                    item->setText(0, item->text(0) + " (failed)");
                    item->setToolTip(0, QString::fromStdString(m.error));
                }
            }
        }
        expandAll();
        setEnabled(!state_.view->running());
    }

   protected:
    [[nodiscard]] QStringList mimeTypes() const override {
        return {QString::fromLatin1(module_mime_type), QString::fromLatin1(group_mime_type)};
    }

    [[nodiscard]] QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
        if (items.size() != 1 || state_.view->running()) {
            return nullptr;
        }
        QTreeWidgetItem* item = items.front();
        if (item->data(0, group_role).toBool()) {
            return encode_group_mime();
        }
        if (item->parent() == nullptr || item->isDisabled()) {
            return nullptr;
        }
        return encode_module_mime({item->data(0, Qt::UserRole).toString(), item->data(0, Qt::UserRole + 1).toString(),
                                   item->data(0, Qt::UserRole + 2).toString()});
    }

   private:
    void add_from_item(QTreeWidgetItem* item) {
        if (item == nullptr || state_.view->running()) {
            return;
        }
        if (item->data(0, group_role).toBool()) {
            create_group(state_, callbacks_, std::nullopt);
            return;
        }
        if (item->parent() == nullptr || item->isDisabled()) {
            return;
        }
        add_module_from_item(item, std::nullopt);
    }

    void add_module_from_item(QTreeWidgetItem* item, std::optional<node_position> position) {
        studio::add_module_request request;
        request.group_path = state_.current_group;
        request.factory = item->data(0, Qt::UserRole).toString().toStdString();
        request.factory_version = try_parse_version(item->data(0, Qt::UserRole + 1).toString().toStdString());
        request.plugin = std::filesystem::path(item->data(0, Qt::UserRole + 2).toString().toStdWString());
        request.config_dir = state_.config_dir();
        request.position = position;
        try {
            const studio::add_module_result result = studio::add_module(state_.doc, request);
            if (!result.warning.empty()) {
                callbacks_.error(QString::fromStdString("warning: " + result.warning));
            }
            callbacks_.project_changed();
        } catch (const std::exception& e) {
            callbacks_.error(QString::fromStdString(std::string("add module: ") + e.what()));
        }
    }

    app_state& state_;
    ui_callbacks& callbacks_;

    QIcon group_icon_ = icons::group();
    QIcon plugin_icon_ = icons::plugin();
    icons::module_icons module_icons_;
};

palette_widget::palette_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(style::row_spacing);

    filter_ = new QLineEdit(this);
    filter_->setObjectName("palette.filter");
    filter_->setPlaceholderText("filter");
    filter_->setClearButtonEnabled(true);
    layout->addWidget(filter_);

    tree_ = new palette_tree(state_, callbacks_, this);
    layout->addWidget(tree_, 1);

    QObject::connect(filter_, &QLineEdit::textChanged, this, [this](const QString&) { apply_filter(); });
}

void palette_widget::refresh() {
    tree_->rebuild();
    apply_filter();
}

void palette_widget::set_filter(const QString& text) {
    filter_->setText(text);
}

QTreeWidget& palette_widget::tree() {
    return *tree_;
}

void palette_widget::apply_filter() {
    const QString needle = filter_->text().trimmed();
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree_->topLevelItem(i);
        if (top->data(0, group_role).toBool()) {
            top->setHidden(false);
            continue;
        }
        int shown = 0;
        for (int j = 0; j < top->childCount(); ++j) {
            QTreeWidgetItem* child = top->child(j);
            const bool match = needle.isEmpty() || child->text(0).contains(needle, Qt::CaseInsensitive);
            child->setHidden(!match);
            shown += match ? 1 : 0;
        }
        top->setHidden(!needle.isEmpty() && shown == 0);
    }
}

}  // namespace atp::studio::ui
