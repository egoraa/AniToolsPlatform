// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_LUA_STATE_HPP
#define ANITOOLSPLATFORM_BRIDGES_LUA_STATE_HPP

#include <filesystem>
#include <string>
#include <vector>

#include "lua_api.hpp"

namespace atp::lua_bridge {

/// Creates an interpreter with the platform's package reachable and leaves the `atp` table on top of
/// its stack.
///
/// A state per caller and not one per process, which is this bridge's central decision. Lua has no
/// global interpreter and no lock around one, so a module instance owning its state runs on the
/// runner's thread with nothing to serialise against — the property the Python bridge cannot have,
/// and the reason none of its "one bridge per process" machinery is needed here. The price is that a
/// script is executed once per instance, which is why the package tells authors to keep expensive
/// work out of the top level.
/// @param error filled in with the reason when the answer is nullptr
/// @return the state, which the caller must close, or nullptr
[[nodiscard]] lua_State* new_state(std::string& error);

/// Directories to scan for scripts: ATP_LUA_PATH first, then `lua/` next to this library.
///
/// Paths and not strings, and on Windows the variable is read wide. A narrow read converts through
/// the process code page, so a directory the page cannot represent came back with its characters
/// replaced and the directory was silently not there — no error, no log line, the modules simply
/// absent from the palette.
[[nodiscard]] std::vector<std::filesystem::path> scan_paths();

}  // namespace atp::lua_bridge

#endif
