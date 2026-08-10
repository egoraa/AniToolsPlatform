// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_LUA_CTX_TYPE_HPP
#define ANITOOLSPLATFORM_BRIDGES_LUA_CTX_TYPE_HPP

#include <string>

#include <atp/plugin_c.h>

#include "lua_api.hpp"
#include "module_slot.hpp"

namespace atp::lua_bridge {

/// Opens the built-in `_atp` module, preloaded into every state before any script runs.
///
/// Built in rather than written in Lua because it is the one part of the package that cannot be:
/// its methods are the host's own callbacks. It is opened even for the state that only reads
/// declarations, so that a script requiring it at the top level behaves the same at discovery as at
/// creation.
/// @param state the interpreter being built
/// @return one, the module left on the stack
int open_ctx_module(lua_State* state);

/// Pushes the context object one module instance reaches the host through.
///
/// Ports are addressed by index, so the object carries the slot whose kind tables say what each index
/// means. The scratch string backs a text or blob on its way out and must outlive every call the
/// instance makes, which is why it belongs to the instance rather than to this object.
/// @param state the instance's interpreter
/// @param api the host's callbacks
/// @param ctx the instance's context
/// @param slot the described module this instance was created from
/// @param scratch storage for outgoing payloads
void push_ctx(lua_State* state, const atp_api* api, atp_ctx* ctx, const module_slot* slot, std::string* scratch);

}  // namespace atp::lua_bridge

#endif
