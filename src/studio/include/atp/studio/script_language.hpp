// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_SCRIPT_LANGUAGE_HPP
#define ATP_STUDIO_SCRIPT_LANGUAGE_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace atp::studio {

namespace detail {

inline void replace_all(std::string& text, std::string_view token, std::string_view value) {
    for (std::size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size())) {
        text.replace(at, token.size(), value);
    }
}

[[nodiscard]] inline bool name_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

[[nodiscard]] inline bool name_digit(char c) {
    return c >= '0' && c <= '9';
}

}  // namespace detail

/// Everything the studio has to know about one scripting language in order to author a module in it.
///
/// The type exists because the studio's Python support turned out to be two different things wearing
/// one name: a handful of conventions any bridge would have — a file name, a subdirectory, a search
/// variable — and a few rules that are genuinely CPython's. Splitting them is what lets a second
/// language be a value rather than a second copy of six hundred lines, and, more to the point, keeps
/// the two sets of rules from drifting apart once there are two.
///
/// Everything here is a constant of the language and not of the session: an instance is `constexpr`
/// and there is exactly one per language in the whole process.
struct script_language {
    /// Stable key, written into the profile and into log lines. Never shown as a heading.
    std::string_view id;
    /// What a person is shown: "Python", "Lua".
    std::string_view label;
    /// File name of the bridge, extension aside — the one plugin whose contents depend on files the
    /// studio itself writes.
    std::string_view bridge_stem;
    /// Directory the bridge scans beside itself, and therefore the one a script belongs in.
    ///
    /// Not a name the studio is free to choose: the bridge appends exactly this to its own directory
    /// and looks for its package in the same place.
    std::string_view scripts_subdir;
    /// The package every script imports, as it is named inside `scripts_subdir` — a directory for
    /// Python, a single file for Lua.
    std::string_view package_entry;
    /// Whether `package_entry` is a directory. Decides both how it is copied and how its age is
    /// measured.
    bool package_is_directory;
    /// Extension of a script, dot included. Also what freshness is measured over: a language whose
    /// runtime writes caches beside the sources must not let a cache make a stale package look new.
    std::string_view file_extension;
    /// Prefix the new-module dialog offers, and the one a derived symbol drops.
    std::string_view name_prefix;
    /// Environment variable the bridge reads its extra scan directories from.
    std::string_view path_variable;
    /// What to add when a load failure reads as a missing dependency, or empty when this language
    /// cannot have one. Both platforms word such a failure about the plugin rather than about the
    /// library that is actually absent, so the hint is the only thing pointing at the real cause.
    std::string_view missing_dependency_hint;

    /// Whether a module name can be written into a script of this language.
    bool (*name_valid)(std::string_view name);
    /// The whole content of a new script: a module that already runs, so the palette entry is
    /// reachable and can be connected before a single line is edited.
    std::string (*render_skeleton)(std::string_view module_name);
    /// The sentence the dialog shows under its fields, naming the file and whatever else the gesture
    /// will produce.
    std::string (*creation_note)(const std::filesystem::path& file, std::string_view module_name);
};

}  // namespace atp::studio

#endif
