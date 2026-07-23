#ifndef ATP_STUDIO_UI_MANAGER_WIDGET_HPP
#define ATP_STUDIO_UI_MANAGER_WIDGET_HPP

#include "app_state.hpp"

#include <QListWidget>
#include <QTreeWidget>
#include <QWidget>

namespace atp::studio::ui {

class manager_widget final : public QWidget {
   public:
    manager_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    void refresh();

   private:
    // Настройки — зеркало менеджера; сохраняются сразу: список папок
    // должен пережить и крах приложения.
    void sync_settings();

    app_state& state_;
    ui_callbacks& callbacks_;
    QListWidget* dirs_ = nullptr;
    QTreeWidget* plugins_ = nullptr;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_MANAGER_WIDGET_HPP
