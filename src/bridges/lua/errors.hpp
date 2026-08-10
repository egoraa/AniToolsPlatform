// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_LUA_ERRORS_HPP
#define ANITOOLSPLATFORM_BRIDGES_LUA_ERRORS_HPP

#include <atp/plugin_c.h>

#include "lua_api.hpp"

namespace atp::lua_bridge {

/// Message handler for every lua_pcall the bridge makes, adding a traceback to the error.
///
/// It has to be a handler rather than something done afterwards: the stack the error happened on is
/// gone by the time lua_pcall returns, and a bare Lua error names only the line error() was called
/// on. A module that fails three calls deep is exactly the case worth diagnosing, and the script's
/// own path is what tells its author which file to open.
/// @param state the interpreter, with the error on top
/// @return one, the enriched message left on the stack
int add_traceback(lua_State* state);

/// Turns the error on top of a Lua stack into the failure text of a lifecycle call.
///
/// The boundary is exception-free in both directions (plugin_c.h), and an error left on the stack
/// would be neither: it would sit under the next call's arguments and be read as one. This hands the
/// text to set_error and pops it.
/// @param api the host's callbacks
/// @param ctx the failing instance's context
/// @param state the interpreter, with the error on top
/// @param where the entry point that failed, named first in the message
void report_error(const atp_api& api, atp_ctx* ctx, lua_State* state, const char* where);

}  // namespace atp::lua_bridge

#endif
