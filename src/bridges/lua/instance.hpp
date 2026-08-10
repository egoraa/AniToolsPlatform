// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_LUA_INSTANCE_HPP
#define ANITOOLSPLATFORM_BRIDGES_LUA_INSTANCE_HPP

#include <atp/plugin_c.h>

namespace atp::lua_bridge {

/// Builds the interpreter behind one module instance and asks the package for the module.
///
/// The script is executed here, a second time after discovery read it, because the interpreter is
/// this instance's own — that is what lets two instances iterate on two threads at once. The package
/// is told what the host was promised and refuses a file whose declarations no longer match.
/// @param api the host's callbacks, valid for as long as the instance lives
/// @param ctx this instance's context, passed back to every api call
/// @param user_data the module_slot this descriptor was built from
/// @return the instance state, or nullptr to refuse creation
extern "C" void* instance_create(const atp_api* api, atp_ctx* ctx, void* user_data);

/// Closes the interpreter. Called exactly once, after stop.
extern "C" void instance_destroy(void* self);

/// Calls initialize() if the module defines one.
extern "C" atp_status instance_initialize(void* self);

/// Calls start() if the module defines one.
extern "C" atp_status instance_start(void* self);

/// Calls stop() if the module defines one.
///
/// Pointed at unconditionally rather than only for modules that define the method, because the host
/// calls stop during a failed start's rollback and a missing entry point would make that rollback
/// silently partial.
extern "C" atp_status instance_stop(void* self);

/// One pass of the hot path: calls iterate() and translates what comes back.
extern "C" atp_work instance_iterate(void* self);

}  // namespace atp::lua_bridge

#endif
