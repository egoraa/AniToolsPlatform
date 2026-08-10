// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_LUA_SELF_MODULE_HPP
#define ANITOOLSPLATFORM_BRIDGES_LUA_SELF_MODULE_HPP

#include <filesystem>

namespace atp::lua_bridge {

/// Directory this library was loaded from, which is where the atp package and the scripts live.
///
/// There is deliberately no counterpart to the Python bridge's pin_self(): nothing here outlives a
/// load. Every interpreter belongs to one call or one module instance and is closed with it, so by
/// the time module_loader may unload this library there is no interpreter left holding a pointer into
/// its code — which is the whole reason a second copy of this bridge in one process is harmless.
/// @return the directory, or an empty path if the platform would not say
[[nodiscard]] std::filesystem::path self_directory();

}  // namespace atp::lua_bridge

#endif
