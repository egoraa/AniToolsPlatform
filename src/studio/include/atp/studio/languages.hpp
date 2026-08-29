// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_LANGUAGES_HPP
#define ATP_STUDIO_LANGUAGES_HPP

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include <atp/studio/languages/lua.hpp>
#include <atp/studio/languages/python.hpp>
#include <atp/studio/script_language.hpp>

namespace atp::studio {

/// Every language the studio can author a module in, in the order they are offered.
///
/// This is the one place a language is added. Everything else — provisioning, the scan paths, the
/// dialog, the startup checks — walks this list rather than naming a language, which is what keeps
/// "we support two" from meaning "two sets of rules that must be kept in step by hand".
[[nodiscard]] inline std::span<const script_language> languages() {
    static constexpr std::array<script_language, 2> all{python_language, lua_language};
    return all;
}

/// The language with this id, or nullptr.
///
/// A profile naming a language this build does not have must produce nothing rather than a wrong
/// default chosen silently: the caller knows what to fall back to, this function does not.
/// @param id the stable key of a language
/// @return the language, or nullptr
[[nodiscard]] inline const script_language* language_by_id(std::string_view id) {
    for (const script_language& lang : languages()) {
        if (lang.id == id) {
            return &lang;
        }
    }
    return nullptr;
}

namespace detail {

[[nodiscard]] inline std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

}  // namespace detail

/// The language a module declared in this file is written in, or nullptr when none claims it.
///
/// What the studio knows about a module's origin is the file the plugin named it in — a script for a
/// bridge, nothing at all for an ordinary plugin — so the extension is the whole of the evidence.
/// It is matched against languages() rather than against a list written here, which is what keeps a
/// language added in one place from having to be added in a second.
///
/// The file is handled as the bytes it is, and never through std::filesystem::path: on Windows that
/// conversion goes through the process code page and throws for a name it cannot represent, and a
/// module folder named in Cyrillic must not cost the canvas its mark. Case is folded over ASCII
/// alone for the same kind of reason — an extension is ASCII, and towlower would answer differently
/// under a different locale.
/// @param source the file a module was declared in, empty when the plugin named none
/// @return the language, or nullptr for a binary module and for an extension no language claims
[[nodiscard]] inline const script_language* language_of_source(std::string_view source) {
    const std::size_t dot = source.find_last_of('.');
    if (dot == std::string_view::npos) {
        return nullptr;
    }
    const std::size_t separator = source.find_last_of("/\\");
    if (separator != std::string_view::npos && dot < separator) {
        return nullptr;
    }
    const std::string extension = detail::ascii_lower(source.substr(dot));
    for (const script_language& lang : languages()) {
        if (extension == lang.file_extension) {
            return &lang;
        }
    }
    return nullptr;
}

}  // namespace atp::studio

#endif
