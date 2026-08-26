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
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QString>
#include <QTimer>
#include <QToolBar>

namespace atp::studio::ui {

/// Main window: menus and docks. All the logic lives in the core (project/session/manager) and the
/// window only routes actions and rebuilds the widgets.
class main_window final : public QMainWindow {
   public:
    explicit main_window(app_state& state);

    /// Rebuilds every widget from the current project and selection.
    void refresh_all();

    /// Puts the docks back where a fresh profile has them. Public because it is the only way to see
    /// a changed default: a saved window_state wins over apply_default_layout on every start, and a
    /// profile that has one keeps its old proportions until it is told to let go.
    void reset_layout();

    /// Appends a line of the studio's own to the Log dock, rendered like a module's: the current
    /// time, the level in brackets, then the message. One shape for every line in the dock is what
    /// lets a reader scan the column rather than parse each row, so a message must not name its
    /// own severity in words — the bracket already does.
    ///
    /// Lines drained from modules do not come this way — they arrive already rendered, carrying
    /// the moment their own thread wrote them, which is the moment worth showing.
    /// @param text what to say
    /// @param level severity, deciding both the bracket and the colour
    void report(const QString& text, atp::log_level level = atp::log_level::info);

    /// Appends an exception to the Log dock, prefixed with the context it happened in. Always
    /// an error — that is what an exception reaching this window is.
    /// @param context what was being attempted
    /// @param e the exception
    void report(const std::string& context, const std::exception& e);

    /// Moves whatever the running modules have said into the Log dock.
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

    /// Raises the toolbar over the menus. Every action on it already exists as a menu entry — the
    /// bar names no behaviour of its own, it only puts the handful a person reaches for within one
    /// glance of the canvas instead of two menus down.
    void build_toolbar();

    /// Raises the status bar under the docks: what the pipeline is doing, and which file is open.
    /// The run state is shown twice on purpose — here and in the Runtime dock — because the dock is
    /// closeable and the answer to "is it running" must not be.
    void build_status_bar();

    void build_view_menu(QMenu* view);

    void build_theme_menu(QMenu* view);

    void build_style_menu(QMenu* view);

    void build_help_menu(QMenu* help);

    void show_about();

    void apply_theme();

    void apply_style();

    void store_settings();

    void apply_default_layout();

    /// Gives the docks their share of the window. Split out of apply_default_layout and repeated
    /// after restore_layout for a profile that has a geometry but no dock state: the constructor
    /// lays the docks out before the saved geometry is applied, so at that point the height is the
    /// hardcoded one and a share of it would be a share of the wrong window.
    void size_docks();

    /// Puts the window back where the profile left it.
    /// @return whether the dock state was restored — false both when there is none saved and when
    ///         restoreState refused the blob it was given, and in either case the docks are still at
    ///         the sizes apply_default_layout gave them against the pre-geometry window, so the
    ///         caller owes them a size_docks
    [[nodiscard]] bool restore_layout();

    void rebuild_recent_menu();

    QDockWidget* build_log_dock();

    void open_dialog();

    void new_script_module();

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
    log_widget* log_ = nullptr;
    QMenu* recent_menu_ = nullptr;
    QToolBar* toolbar_ = nullptr;
    QLabel* status_run_ = nullptr;
    QLabel* status_path_ = nullptr;
    QAction* new_action_ = nullptr;
    QAction* open_action_ = nullptr;
    QAction* run_action_ = nullptr;
    QAction* stop_action_ = nullptr;
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
    QDockWidget* log_dock_ = nullptr;
};

}  // namespace atp::studio::ui

#endif
