#ifndef ATP_STUDIO_QT_PALETTE_WIDGET_HPP
#define ATP_STUDIO_QT_PALETTE_WIDGET_HPP

#include <filesystem>
#include <string>
#include <system_error>

#include <QTreeWidget>

#include <atp/studio/qt/app_state.hpp>

namespace atp::studio::qt {

namespace detail {

// Имя нового узла: имя фабрики, при занятости — числовой суффикс.
inline std::string unique_child_name(const app_state& state, const std::string& factory) {
    const app::group_node* g = state.doc.group_at(state.current_group);
    auto taken = [&](const std::string& name) {
        for (const app::child_node& c : g->children) {
            if ((c.module ? c.module->name : c.group->name) == name) {
                return true;
            }
        }
        return false;
    };
    if (!taken(factory)) {
        return factory;
    }
    for (int i = 2;; ++i) {
        const std::string candidate = factory + "_" + std::to_string(i);
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

}  // namespace detail

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
        const std::string factory = item->data(0, Qt::UserRole).toString().toStdString();
        const std::string ver_text = item->data(0, Qt::UserRole + 1).toString().toStdString();
        const std::filesystem::path plugin(item->data(0, Qt::UserRole + 2).toString().toStdWString());
        try {
            state_.doc.add_module(state_.current_group, factory, detail::unique_child_name(state_, factory),
                                  try_parse_version(ver_text));
            ensure_plugin_listed(plugin);
            callbacks_.document_changed();
        } catch (const std::exception& e) {
            callbacks_.error(QString::fromStdString(std::string("add module: ") + e.what()));
        }
    }

    // Модуль из DLL вне plugins конфига не запустится — путь дописывается
    // автоматически, относительно каталога документа где возможно.
    void ensure_plugin_listed(const std::filesystem::path& plugin) {
        std::error_code ec;
        const std::filesystem::path relative = std::filesystem::relative(plugin, state_.config_dir(), ec);
        std::string entry;
        if (!ec && !relative.empty()) {
            entry = relative.generic_string();
        } else {
            entry = plugin.generic_string();
            callbacks_.error(QString::fromStdString("warning: plugin path is absolute: " + entry));
        }
        state_.doc.add_plugin(entry);
    }

    app_state& state_;
    ui_callbacks& callbacks_;
};

}  // namespace atp::studio::qt

#endif  // ATP_STUDIO_QT_PALETTE_WIDGET_HPP
