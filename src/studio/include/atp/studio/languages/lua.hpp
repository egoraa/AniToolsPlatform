// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_LANGUAGES_LUA_HPP
#define ATP_STUDIO_LANGUAGES_LUA_HPP

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

#include <atp/studio/script_language.hpp>

namespace atp::studio {

/// Whether a module name can be written into a Lua script.
///
/// One check shorter than Python's, and the difference is real rather than an oversight: nothing is
/// derived from the name here — the skeleton keeps the module in a local variable — so `_1` is a
/// perfectly good name, while Python has to refuse it because the class it would derive is `1`.
///
/// `atp` is refused for the same reason as there and by a different mechanism: the package is
/// `atp.lua` in the very directory the script is written into, so a module of that name would *be*
/// the package file.
/// @param name candidate name
/// @return true if the name is usable
[[nodiscard]] inline bool lua_name_valid(std::string_view name) {
    if (name.empty() || !detail::name_letter(name.front())) {
        return false;
    }
    if (name == "atp") {
        return false;
    }
    return std::ranges::all_of(name, [](const char c) { return detail::name_letter(c) || detail::name_digit(c); });
}

/// The skeleton script: a module that already runs, so the palette entry is reachable and can be
/// connected before a single line is edited.
/// @param module_name the platform name of the module
/// @return the whole file content
[[nodiscard]] inline std::string render_lua_module(std::string_view module_name) {
    static constexpr std::string_view skeleton =
        R"LUA(--- A platform module written in Lua, created by atp_studio.
---
--- The bridge reads this file when it loads and builds real platform ports from the declarations
--- below, so nothing here is compiled and nothing links the SDK.
---
--- Expensive work belongs in initialize rather than at the top level: every instance of this module
--- gets an interpreter of its own and runs this file again, which is what lets two of them iterate on
--- two threads at once. Values at the top level are therefore not shared between instances.
---
--- The order of the assignments below is the order the host sees the ports in, so do not shuffle them
--- once a config names them.
local atp = require("atp")

local M = atp.module("@NAME@", { 1, 0 })

M.value = atp.input(atp.i32)
M.result = atp.output(atp.i32)

M.factor = atp.property(atp.i32, 2)

function M:initialize()
    self:log("@NAME@ ready")
end

function M:iterate()
    local value = self.value:take()
    if value == nil then
        return atp.IDLE
    end
    self.result:write(value * self.factor:get())
    return atp.BUSY
end

return M
)LUA";
    std::string text(skeleton);
    detail::replace_all(text, "@NAME@", module_name);
    return text;
}

/// What the dialog promises for this language.
///
/// It names no derived symbol, because there is none: the module is the table the script returns.
/// @param file the script that will be written
/// @return one sentence
[[nodiscard]] inline std::string lua_creation_note(const std::filesystem::path& file, std::string_view) {
    return "Creates " + file.string() + ", and puts a bridge and the atp package in the folder if it has none.";
}

/// Lua as the studio knows it.
///
/// `missing_dependency_hint` is empty because this bridge carries its interpreter inside the plugin
/// file and therefore cannot have an absent runtime — a hint with nothing to say is worse than none.
/// That is the only field where it differs from Python: a second loaded copy is dropped here too, and
/// for the reason that applies to any bridge rather than for CPython's.
inline constexpr script_language lua_language{
    .id = "lua",
    .label = "Lua",
    .bridge_stem = "atp_lua_bridge",
    .scripts_subdir = "lua",
    .package_entry = "atp.lua",
    .package_is_directory = false,
    .file_extension = ".lua",
    .name_prefix = "lua_",
    .path_variable = "ATP_LUA_PATH",
    .missing_dependency_hint = "",
    .name_valid = &lua_name_valid,
    .render_skeleton = &render_lua_module,
    .creation_note = &lua_creation_note,
};

}  // namespace atp::studio

#endif
