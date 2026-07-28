#ifndef ATP_STUDIO_SETTINGS_HPP
#define ATP_STUDIO_SETTINGS_HPP

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace atp::studio {

/// User settings of studio, as opposed to a pipeline config: they live in the user profile and
/// never touch documents.
struct studio_settings {
    std::vector<std::string> search_dirs;      // module search directories
    std::vector<std::string> recent_projects;  // MRU list of projects, freshest first
};

/// How many projects the MRU list keeps.
inline constexpr std::size_t recent_limit = 10;

/// Moves a project to the head of the MRU list, dropping a duplicate and trimming the tail. The
/// path is made absolute and normalised but not canonical: it may have just appeared (Save As) or
/// be temporarily gone (a network drive), and it belongs in the list either way.
inline void note_recent(studio_settings& s, const std::filesystem::path& file) {
    const std::string path = std::filesystem::absolute(file).lexically_normal().string();
    std::erase(s.recent_projects, path);
    s.recent_projects.insert(s.recent_projects.begin(), path);
    if (s.recent_projects.size() > recent_limit) {
        s.recent_projects.resize(recent_limit);
    }
}

/// Default settings file: %APPDATA%/atp_studio on Windows, otherwise $XDG_CONFIG_HOME/atp_studio or
/// ~/.config/atp_studio. The last fallback is next to the binary — studio has to work in a bare
/// environment too.
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

/// Reads the settings. A missing or corrupt file yields the defaults: neither a first run nor a
/// damaged profile should take the application down.
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
        for (const nlohmann::json& d : doc.value("recent_projects", nlohmann::json::array())) {
            if (d.is_string()) {
                s.recent_projects.push_back(d.get<std::string>());
            }
        }
    } catch (const nlohmann::json::parse_error&) {  // NOLINT(bugprone-empty-catch)
    }
    return s;
}

/// Writes the settings, creating the parent directory if needed.
/// @throws std::runtime_error if the file cannot be written
inline void save_settings(const studio_settings& s, const std::filesystem::path& file) {
    std::filesystem::create_directories(file.parent_path());
    nlohmann::json doc;
    doc["search_dirs"] = s.search_dirs;
    doc["recent_projects"] = s.recent_projects;
    std::ofstream out(file);
    if (!out) {
        throw std::runtime_error("cannot write settings '" + file.string() + "'");
    }
    out << doc.dump(4) << '\n';
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_SETTINGS_HPP
