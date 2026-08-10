// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_LANGUAGES_HPP
#define ATP_STUDIO_LANGUAGES_HPP

#include <array>
#include <span>
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

}  // namespace atp::studio

#endif
