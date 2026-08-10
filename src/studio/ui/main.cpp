// SPDX-License-Identifier: Apache-2.0
#include "kit/icons.hpp"
#include "model/app_state.hpp"
#include "shell/main_window.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QApplication>
#include <QCoreApplication>
#include <QString>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setWindowIcon(atp::studio::ui::icons::brand());

    atp::studio::ui::app_state state;
    state.settings = atp::studio::load_settings(state.settings_file);
    for (const std::string& dir : state.settings.search_dirs) {
        state.manager.add_search_dir(dir);
    }
    state.script_env.apply(state.manager.search_dirs());
    state.manager.rescan();

    const std::filesystem::path exe_dir(QCoreApplication::applicationDirPath().toStdWString());
    std::vector<std::pair<QString, atp::log_level>> startup_notes;
    for (const atp::studio::script_language& lang : atp::studio::languages()) {
        for (const std::filesystem::path& dropped : atp::studio::keep_one_bridge(state.manager, lang)) {
            startup_notes.emplace_back(QString::fromStdString(atp::studio::dropped_bridge_note(dropped, lang)),
                                       atp::log_level::info);
        }
        const atp::studio::bridge_source ours = atp::studio::find_bridge_source(state.manager, exe_dir, lang);
        if (const std::optional<std::filesystem::path> stale =
                atp::studio::stale_loaded_bridge(state.manager, ours, lang)) {
            startup_notes.emplace_back(QString::fromStdString(atp::studio::stale_bridge_note(*stale, ours.bridge)),
                                       atp::log_level::warning);
        }
    }

    atp::studio::ui::main_window window(state);
    for (const std::pair<QString, atp::log_level>& note : startup_notes) {
        window.report(note.first, note.second);
    }
    if (!state.settings.recent_projects.empty()) {
        window.open_path(state.settings.recent_projects.front());
    }
    window.show();
    const int code = QApplication::exec();

    atp::studio::save_settings(state.settings, state.settings_file);
    return code;
}
