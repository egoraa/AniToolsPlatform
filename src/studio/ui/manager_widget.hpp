#ifndef ATP_STUDIO_UI_MANAGER_WIDGET_HPP
#define ATP_STUDIO_UI_MANAGER_WIDGET_HPP

#include "app_state.hpp"
#include "ui_style.hpp"

#include <QListWidget>
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
    // The settings mirror the manager and are saved at once: the directory list has to survive an
    // application crash too.
    void sync_settings();

    app_state& state_;
    ui_callbacks& callbacks_;
    QListWidget* dirs_ = nullptr;
    QTreeWidget* plugins_ = nullptr;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_MANAGER_WIDGET_HPP
