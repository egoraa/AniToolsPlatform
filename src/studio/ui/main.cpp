// SPDX-License-Identifier: Apache-2.0
#include "kit/icons.hpp"
#include "model/app_state.hpp"
#include "shell/main_window.hpp"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setWindowIcon(atp::studio::ui::icons::brand());

    atp::studio::ui::app_state state;
    state.settings = atp::studio::load_settings(state.settings_file);
    for (const std::string& dir : state.settings.search_dirs) {
        state.manager.add_search_dir(dir);
    }
    state.manager.rescan();

    atp::studio::ui::main_window window(state);
    if (!state.settings.recent_projects.empty()) {
        window.open_path(state.settings.recent_projects.front());
    }
    window.show();
    const int code = QApplication::exec();

    atp::studio::save_settings(state.settings, state.settings_file);
    return code;
}
