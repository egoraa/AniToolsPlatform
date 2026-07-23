#ifndef ATP_STUDIO_UI_MAIN_WINDOW_HPP
#define ATP_STUDIO_UI_MAIN_WINDOW_HPP

#include "app_state.hpp"
#include "canvas_widget.hpp"
#include "inspector_widget.hpp"
#include "manager_widget.hpp"
#include "palette_widget.hpp"
#include "runtime_widget.hpp"

#include <exception>
#include <filesystem>
#include <string>

#include <QAction>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>

namespace atp::studio::ui {

// Главное окно: меню + доки. Вся логика — в ядре (document/session/manager),
// окно только маршрутизирует действия и перестраивает виджеты.
class main_window final : public QMainWindow {
   public:
    explicit main_window(app_state& state);

    void refresh_all();

    void report(const QString& text);

    void report(const std::string& context, const std::exception& e);

    // Открытие по пути — общая точка для диалога, меню Recent и автооткрытия
    // при старте: любая ошибка уходит в журнал, текущий документ не трогается.
    void open_path(const std::filesystem::path& path);

   private:
    void build_menus();

    void rebuild_recent_menu();

    void build_errors_dock();

    void open_dialog();

    void save(bool ask_path);

    void poll();

    app_state& state_;
    ui_callbacks callbacks_;
    manager_widget* manager_ = nullptr;
    palette_widget* palette_ = nullptr;
    canvas_widget* canvas_ = nullptr;
    inspector_widget* inspector_ = nullptr;
    runtime_widget* runtime_ = nullptr;
    QListWidget* errors_ = nullptr;
    QMenu* recent_menu_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* save_as_action_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_MAIN_WINDOW_HPP
