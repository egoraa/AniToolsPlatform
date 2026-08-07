// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_MAIN_WINDOW_HPP
#define ATP_STUDIO_UI_MAIN_WINDOW_HPP

#include "canvas/canvas_widget.hpp"
#include "model/app_state.hpp"
#include "panels/inspector_widget.hpp"
#include "panels/log_widget.hpp"
#include "panels/manager_widget.hpp"
#include "panels/palette_widget.hpp"
#include "panels/project_tree.hpp"
#include "panels/runtime_widget.hpp"

#include <exception>
#include <filesystem>
#include <string>

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QString>
#include <QTimer>

namespace atp::studio::ui {

/// Main window: menus and docks. All the logic lives in the core (project/session/manager) and the
/// window only routes actions and rebuilds the widgets.
class main_window final : public QMainWindow {
   public:
    explicit main_window(app_state& state);

    /// Rebuilds every widget from the current project and selection.
    void refresh_all();

    /// Appends a line to the Errors dock.
    void report(const QString& text);

    /// Appends an exception to the Errors dock, prefixed with the context it happened in.
    void report(const std::string& context, const std::exception& e);

    /// Moves whatever the running modules have said into the Errors dock.
    ///
    /// Called from poll(), and public because a test that had to wait for a timer tick instead
    /// would be a test that fails on a busy machine.
    void drain_logs();

    /// Opens a project by path — the shared entry point for the dialog, the Recent menu and the
    /// auto-open at startup. Any error goes to the log and leaves the current project untouched.
    void open_path(const std::filesystem::path& path);

   protected:
    /// Captures the layout into the settings on the way out. The file itself is written once the
    /// event loop returns, so dragging a dock does not reach the disk.
    void closeEvent(QCloseEvent* event) override;

   private:
    void build_menus();

    void build_view_menu(QMenu* view);

    void build_theme_menu(QMenu* view);

    void build_style_menu(QMenu* view);

    void build_help_menu(QMenu* help);

    void show_about();

    void apply_theme();

    void apply_style();

    void store_settings();

    void apply_default_layout();

    void restore_layout();

    void reset_layout();

    void rebuild_recent_menu();

    QDockWidget* build_errors_dock();

    void open_dialog();

    void attach_dialog_flow();

    void detach(const QString& reason);

    void save(bool ask_path);

    void poll();

    void update_title();

    app_state& state_;
    ui_callbacks callbacks_;
    QString default_style_;
    project_tree* tree_ = nullptr;
    manager_widget* manager_ = nullptr;
    palette_widget* palette_ = nullptr;
    canvas_widget* canvas_ = nullptr;
    inspector_widget* inspector_ = nullptr;
    runtime_widget* runtime_ = nullptr;
    log_widget* errors_ = nullptr;
    QMenu* recent_menu_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* save_as_action_ = nullptr;
    QAction* attach_action_ = nullptr;
    QAction* detach_action_ = nullptr;
    QAction* refresh_mirror_action_ = nullptr;
    QAction* stop_remote_action_ = nullptr;
    QTimer* poll_timer_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QAction* new_group_action_ = nullptr;
    QDockWidget* tree_dock_ = nullptr;
    QDockWidget* palette_dock_ = nullptr;
    QDockWidget* manager_dock_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* runtime_dock_ = nullptr;
    QDockWidget* errors_dock_ = nullptr;
};

}  // namespace atp::studio::ui

#endif
