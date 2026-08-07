// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_SETTINGS_HPP
#define ATP_STUDIO_SETTINGS_HPP

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace atp::studio {

/// Colour scheme studio asks Qt for. `system` follows the desktop, and is what a fresh profile gets:
/// an application that ignores the desktop's own choice is the one that looks out of place.
enum class app_theme { system, light, dark };

/// The word an app_theme is written as in the settings file. A word rather than a number, because the
/// file is meant to be readable and editable by hand.
/// @param theme the scheme
/// @return its token
[[nodiscard]] inline std::string_view theme_name(app_theme theme) {
    switch (theme) {
        case app_theme::light:
            return "light";
        case app_theme::dark:
            return "dark";
        case app_theme::system:
            break;
    }
    return "system";
}

/// The reverse. A word this build does not know yields nothing rather than a guess — what to do with
/// such a profile is the caller's decision, not this function's.
/// @param name token from the settings file
/// @return the scheme, or nullopt if the token is not one of them
[[nodiscard]] inline std::optional<app_theme> theme_from_name(std::string_view name) {
    if (name == "system") {
        return app_theme::system;
    }
    if (name == "light") {
        return app_theme::light;
    }
    if (name == "dark") {
        return app_theme::dark;
    }
    return std::nullopt;
}

/// User settings of studio, as opposed to a pipeline config: they live in the user profile and
/// never touch projects.
struct studio_settings {
    std::vector<std::string> search_dirs;
    std::vector<std::string> recent_projects;
    app_theme theme = app_theme::system;
    std::string style;
    std::string window_geometry;
    std::string window_state;

    /// Endpoint the Attach dialog offers first. A person watches the same host over and over, and
    /// retyping a port every time is the kind of friction that stops people from looking.
    std::string attach_host = "127.0.0.1";
    int attach_port = 0;
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
///
/// std::getenv is kept despite the MSVC CRT deprecating it in favour of _dupenv_s: the standard
/// deprecates nothing here, the lookup runs once at startup before the application has a second
/// thread, and the result is only read — so neither the reentrancy nor the buffer-ownership concern
/// behind that deprecation applies, and the alternative would be non-portable.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
[[nodiscard]] inline std::filesystem::path default_settings_path() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (const char* appdata = std::getenv("APPDATA")) {
        return std::filesystem::path(appdata) / "atp_studio" / "settings.json";
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg) / "atp_studio" / "settings.json";
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".config" / "atp_studio" / "settings.json";
    }
    return {"atp_studio_settings.json"};
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

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
        if (const auto theme = doc.find("theme"); theme != doc.end() && theme->is_string()) {
            if (const std::optional<app_theme> parsed = theme_from_name(theme->get<std::string>())) {
                s.theme = *parsed;
            }
        }
        if (const auto it = doc.find("style"); it != doc.end() && it->is_string()) {
            s.style = it->get<std::string>();
        }
        if (const auto it = doc.find("window_geometry"); it != doc.end() && it->is_string()) {
            s.window_geometry = it->get<std::string>();
        }
        if (const auto it = doc.find("attach_host"); it != doc.end() && it->is_string()) {
            s.attach_host = it->get<std::string>();
        }
        if (const auto it = doc.find("attach_port"); it != doc.end() && it->is_number_integer()) {
            s.attach_port = it->get<int>();
        }
        if (const auto it = doc.find("window_state"); it != doc.end() && it->is_string()) {
            s.window_state = it->get<std::string>();
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
    doc["theme"] = std::string(theme_name(s.theme));
    doc["style"] = s.style;
    doc["window_geometry"] = s.window_geometry;
    doc["window_state"] = s.window_state;
    doc["attach_host"] = s.attach_host;
    doc["attach_port"] = s.attach_port;
    std::ofstream out(file);
    if (!out) {
        throw std::runtime_error("cannot write settings '" + file.string() + "'");
    }
    out << doc.dump(4) << '\n';
}

}  // namespace atp::studio

#endif
