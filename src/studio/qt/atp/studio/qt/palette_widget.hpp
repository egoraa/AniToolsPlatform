#ifndef ATP_STUDIO_QT_PALETTE_WIDGET_HPP
#define ATP_STUDIO_QT_PALETTE_WIDGET_HPP

#include <filesystem>
#include <optional>
#include <string>

#include <QTreeWidget>

#include <atp/studio/add_module.hpp>
#include <atp/studio/qt/app_state.hpp>

namespace atp::studio::qt {

class palette_widget final : public QTreeWidget {
   public:
    palette_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr)
        : QTreeWidget(parent), state_(state), callbacks_(callbacks) {
        setHeaderLabel("modules (double-click to add)");
        QObject::connect(this, &QTreeWidget::itemDoubleClicked, this,
                         [this](QTreeWidgetItem* item, int) { add_from_item(item); });
    }

    void refresh() {
        clear();
        for (const plugin_info& p : state_.manager.plugins()) {
            if (!p.loaded) {
                continue;  // отказы видны в панели Modules
            }
            auto* root = new QTreeWidgetItem(this);
            root->setText(0, QString::fromStdWString(p.path.filename().wstring()));
            for (const module_info& m : p.modules) {
                auto* item = new QTreeWidgetItem(root);
                item->setText(0, QString::fromStdString(m.name + " " + m.ver.to_string()));
                item->setData(0, Qt::UserRole, QString::fromStdString(m.name));
                item->setData(0, Qt::UserRole + 1, QString::fromStdString(m.ver.to_string()));
                item->setData(0, Qt::UserRole + 2, QString::fromStdWString(p.path.wstring()));
                if (m.broken) {
                    item->setDisabled(true);
                    item->setToolTip(0, QString::fromStdString(m.error));
                }
            }
        }
        expandAll();
        setEnabled(!state_.run.running());  // документ read-only на ходу
    }

   private:
    void add_from_item(QTreeWidgetItem* item) {
        if (item == nullptr || item->parent() == nullptr || item->isDisabled() || state_.run.running()) {
            return;  // даблклик по заголовку плагина — не добавление
        }
        add_module_from_item(item, std::nullopt);
    }

    // Общий путь для двойного клика (position = nullopt → auto_layout) и
    // сброса на канвас (position = точка сброса).
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
            callbacks_.document_changed();
        } catch (const std::exception& e) {
            callbacks_.error(QString::fromStdString(std::string("add module: ") + e.what()));
        }
    }

    app_state& state_;
    ui_callbacks& callbacks_;
};

}  // namespace atp::studio::qt

#endif  // ATP_STUDIO_QT_PALETTE_WIDGET_HPP
