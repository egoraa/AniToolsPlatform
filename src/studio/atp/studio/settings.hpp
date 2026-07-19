#ifndef ATP_STUDIO_SETTINGS_HPP
#define ATP_STUDIO_SETTINGS_HPP

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace atp::studio {

// Пользовательские настройки studio (не конфиг пайплайна): живут в профиле
// пользователя, документов не касаются.
struct studio_settings {
    std::vector<std::string> search_dirs;  // папки поиска модулей
};

// Каталог настроек: %APPDATA%/atp_studio (Windows) или
// $XDG_CONFIG_HOME/atp_studio, при отсутствии — ~/.config/atp_studio.
// Последний фолбэк — рядом с бинарём: работать надо и в голом окружении.
[[nodiscard]] inline std::filesystem::path default_settings_path() {
    if (const char* appdata = std::getenv("APPDATA")) {
        return std::filesystem::path(appdata) / "atp_studio" / "settings.json";
    }
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg) / "atp_studio" / "settings.json";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".config" / "atp_studio" / "settings.json";
    }
    return std::filesystem::path("atp_studio_settings.json");
}

// Отсутствующий или битый файл — настройки по умолчанию: первый запуск
// и повреждённый профиль не должны валить приложение.
[[nodiscard]] inline studio_settings load_settings(const std::filesystem::path& file) {
    studio_settings s;
    std::ifstream in(file);
    if (!in) {
        return s;
    }
    try {
        const nlohmann::json doc = nlohmann::json::parse(in);
        for (const nlohmann::json& d : doc.value("search_dirs", nlohmann::json::array())) {
            if (d.is_string()) {
                s.search_dirs.push_back(d.get<std::string>());
            }
        }
    } catch (const nlohmann::json::parse_error&) {  // NOLINT(bugprone-empty-catch)
    }
    return s;
}

inline void save_settings(const studio_settings& s, const std::filesystem::path& file) {
    std::filesystem::create_directories(file.parent_path());
    nlohmann::json doc;
    doc["search_dirs"] = s.search_dirs;
    std::ofstream out(file);
    if (!out) {
        throw std::runtime_error("cannot write settings '" + file.string() + "'");
    }
    out << doc.dump(4) << '\n';
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_SETTINGS_HPP
