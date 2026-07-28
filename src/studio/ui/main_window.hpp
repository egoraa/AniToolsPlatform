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

/// Main window: menus and docks. All the logic lives in the core (document/session/manager) and the
/// window only routes actions and rebuilds the widgets.
class main_window final : public QMainWindow {
   public:
    explicit main_window(app_state& state);

    /// Rebuilds every widget from the current document and selection.
    void refresh_all();

    /// Appends a line to the Errors dock.
    void report(const QString& text);

    /// Appends an exception to the Errors dock, prefixed with the context it happened in.
    void report(const std::string& context, const std::exception& e);

    /// Opens a document by path — the shared entry point for the dialog, the Recent menu and the
    /// auto-open at startup. Any error goes to the log and leaves the current document untouched.
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
    QAction* new_group_action_ = nullptr;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_MAIN_WINDOW_HPP
