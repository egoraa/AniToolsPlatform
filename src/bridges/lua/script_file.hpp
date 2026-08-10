// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_LUA_SCRIPT_FILE_HPP
#define ANITOOLSPLATFORM_BRIDGES_LUA_SCRIPT_FILE_HPP

#include <filesystem>
#include <string>
#include <string_view>

#include "lua_api.hpp"

namespace atp::lua_bridge {

/// A path as the bytes that cross the C boundary: UTF-8, always.
///
/// Never `path::string()`. On Windows that converts through the process code page and **throws**
/// `filesystem_error` for anything the page cannot represent — measured, `No mapping for the Unicode
/// character exists in the target multi-byte code page` for a Cyrillic folder under a 1252 machine.
/// The throw is not merely a failure: `discover()` is reached through `atp_module_count`, an
/// `extern "C"` entry point that plugin_c.h requires to be exception-free, so a module folder with a
/// non-ASCII name used to take the whole bridge down and every module in it with it.
/// @param path the path to encode
/// @return its UTF-8 bytes
[[nodiscard]] std::string to_utf8(const std::filesystem::path& path);

/// The inverse, for bytes that arrived as UTF-8.
[[nodiscard]] std::filesystem::path from_utf8(std::string_view text);

/// Loads a script as a Lua chunk, leaving it on the stack.
///
/// The file is read here rather than handed to `luaL_loadfile`, and that is the second half of the
/// same problem: `luaL_loadfile` opens the name with `fopen`, which on Windows decodes a narrow path
/// through the process code page, so a UTF-8 path would not open and an ANSI one could not represent
/// the name. `std::ifstream` takes the path itself and uses the wide API underneath. The chunk is
/// named `@<utf-8 path>` so tracebacks and error messages still read as file positions.
/// @param state the interpreter to load into
/// @param file the script
/// @param error filled in when the answer is false
/// @return true with the chunk on the stack, false with nothing pushed
[[nodiscard]] bool load_chunk(lua_State* state, const std::filesystem::path& file, std::string& error);

}  // namespace atp::lua_bridge

#endif
