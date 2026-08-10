// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_LUA_LUA_API_HPP
#define ANITOOLSPLATFORM_BRIDGES_LUA_LUA_API_HPP

/// @file
/// The one place the interpreter's headers are included.
///
/// They are included **without** an extern "C" block, and that is not an oversight: the vendored
/// interpreter is compiled as C++ (cmake/BuildLua.cmake says why), so its symbols carry C++ linkage
/// and wrapping the headers here would ask the linker for names that do not exist. Keeping the
/// include in one file is what makes that coupling checkable — the day the interpreter goes back to
/// being built as C, exactly one file changes.

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#endif
