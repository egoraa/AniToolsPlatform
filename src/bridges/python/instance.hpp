// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_PYTHON_INSTANCE_HPP
#define ANITOOLSPLATFORM_BRIDGES_PYTHON_INSTANCE_HPP

#include <atp/plugin_c.h>

namespace atp::bridge {

/// Creates the Python object behind one module instance.
/// @param api the host's callbacks, valid for as long as the instance lives
/// @param ctx this instance's context, passed back to every api call
/// @param user_data the module_slot this descriptor was built from
/// @return the instance state, or nullptr to refuse creation
extern "C" void* instance_create(const atp_api* api, atp_ctx* ctx, void* user_data);

/// Drops the Python object. Called exactly once, after stop.
extern "C" void instance_destroy(void* self);

/// Calls initialize() if the class defines one.
extern "C" atp_status instance_initialize(void* self);

/// Calls start() if the class defines one.
extern "C" atp_status instance_start(void* self);

/// Calls stop() if the class defines one.
///
/// Pointed at unconditionally rather than only for classes that define the method, because the host
/// calls stop during a failed start's rollback and a missing entry point would make that rollback
/// silently partial.
extern "C" atp_status instance_stop(void* self);

/// One pass of the hot path: calls iterate() and translates what comes back.
extern "C" atp_work instance_iterate(void* self);

}  // namespace atp::bridge

#endif
