// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_PYTHON_CTX_TYPE_HPP
#define ANITOOLSPLATFORM_BRIDGES_PYTHON_CTX_TYPE_HPP

#include <string>

#include <atp/plugin_c.h>

#include "module_slot.hpp"
#include "python_api.hpp"

namespace atp::bridge {

/// Creates the built-in _atp module, registered in the inittab before the interpreter starts.
///
/// Built in rather than imported from a file because it is the one part of the package that cannot
/// be written in Python: its methods are the host's own callbacks.
/// @return a new reference to the module, or nullptr with a Python error set
extern "C" PyObject* create_ctx_module();

/// Builds the context object one module instance reaches the host through.
///
/// Ports are addressed by index, so the object carries the slot whose kind tables say what each index
/// means. The scratch string backs a text or blob on its way out and must outlive every call the
/// instance makes, which is why it belongs to the instance rather than to this object.
/// @param api the host's callbacks
/// @param ctx the instance's context
/// @param slot the described module this instance was created from
/// @param scratch storage for outgoing payloads
/// @return a new reference, or nullptr with a Python error set
[[nodiscard]] PyObject* make_ctx(const atp_api* api, atp_ctx* ctx, const module_slot* slot, std::string* scratch);

}  // namespace atp::bridge

#endif
