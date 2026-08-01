#ifndef ATP_STUDIO_UI_MANAGER_WIDGET_HPP
#define ATP_STUDIO_UI_MANAGER_WIDGET_HPP

#include "kit/icons.hpp"
#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

#include <QIcon>
#include <QListWidget>
#include <QPoint>
#include <QTreeWidget>
#include <QWidget>

namespace atp::studio::ui {

/// Modules panel: the plugin search directories and the plugins found in them, with their load
/// errors.
class manager_widget final : public QWidget {
   public:
    manager_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the directory and plugin lists from the manager.
    void refresh();

   private:
    void sync_settings();

    void show_plugin_menu(const QPoint& pos, const QPoint& global);

    static constexpr int path_role = Qt::UserRole;

    app_state& state_;
    ui_callbacks& callbacks_;
    QListWidget* dirs_ = nullptr;
    QTreeWidget* plugins_ = nullptr;

    QIcon directory_icon_ = icons::directory();
    QIcon plugin_icon_ = icons::plugin();
    QIcon module_icon_ = icons::module();
};

}  // namespace atp::studio::ui

#endif
