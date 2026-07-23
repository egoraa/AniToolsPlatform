#include "app_state.hpp"
#include "main_window.hpp"

#include <QApplication>

// Тонкая точка входа: настройки → менеджер → окно; логика — в ядре.
int main(int argc, char** argv) {
    QApplication app(argc, argv);

    atp::studio::ui::app_state state;
    state.settings = atp::studio::load_settings(state.settings_file);
    for (const std::string& dir : state.settings.search_dirs) {
        state.manager.add_search_dir(dir);
    }
    state.manager.rescan();

    atp::studio::ui::main_window window(state);
    // автооткрытие последнего проекта: неудача — строка в Errors-док,
    // приложение стартует с пустым документом, без модальных диалогов
    if (!state.settings.recent_projects.empty()) {
        window.open_path(state.settings.recent_projects.front());
    }
    window.show();
    const int code = QApplication::exec();

    atp::studio::save_settings(state.settings, state.settings_file);
    return code;
}
