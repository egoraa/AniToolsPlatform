#include <QApplication>

#include <atp/studio/qt/app_state.hpp>
#include <atp/studio/qt/main_window.hpp>

// Тонкая точка входа: настройки → менеджер → окно; логика — в ядре.
int main(int argc, char** argv) {
    QApplication app(argc, argv);

    atp::studio::qt::app_state state;
    state.settings = atp::studio::load_settings(state.settings_file);
    for (const std::string& dir : state.settings.search_dirs) {
        state.manager.add_search_dir(dir);
    }
    state.manager.rescan();

    atp::studio::qt::main_window window(state);
    window.show();
    const int code = QApplication::exec();

    atp::studio::save_settings(state.settings, state.settings_file);
    return code;
}
