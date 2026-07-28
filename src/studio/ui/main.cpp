#include "app_state.hpp"
#include "icons.hpp"
#include "main_window.hpp"

#include <QApplication>

// Thin entry point: settings → manager → window; the logic lives in the core.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Set on the application rather than on the window: dialogs and message boxes are windows of
    // their own and would otherwise show up under the platform's default icon.
    QApplication::setWindowIcon(atp::studio::ui::icons::brand());

    atp::studio::ui::app_state state;
    state.settings = atp::studio::load_settings(state.settings_file);
    for (const std::string& dir : state.settings.search_dirs) {
        state.manager.add_search_dir(dir);
    }
    state.manager.rescan();

    atp::studio::ui::main_window window(state);
    // Auto-open the last project: on failure a line goes into the Errors dock and the application
    // starts with an empty document, without any modal dialogs.
    if (!state.settings.recent_projects.empty()) {
        window.open_path(state.settings.recent_projects.front());
    }
    window.show();
    const int code = QApplication::exec();

    atp::studio::save_settings(state.settings, state.settings_file);
    return code;
}
