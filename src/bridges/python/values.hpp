// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_PYTHON_VALUES_HPP
#define ANITOOLSPLATFORM_BRIDGES_PYTHON_VALUES_HPP

#include <cstdint>
#include <string>

#include <atp/plugin_c.h>

#include "python_api.hpp"

namespace atp::bridge {

/// Copies a value the host handed over into a fresh Python object.
///
/// A copy and not a view, and that is what keeps the host's lifetime rule off the script's back: the
/// buffer behind a text or a blob is valid only until the next read on the same context, while the
/// object made here may be kept for as long as its author likes.
/// @param value the host's value, whose kind decides what is built
/// @return a new reference, or nullptr with a Python error set
[[nodiscard]] PyObject* to_python(const atp_value& value);

/// Reads a Python object into a value of the port's declared kind.
///
/// The conversion is chosen by the declared kind and never by the type of the object, so a script
/// that passes the wrong thing gets an error naming the expectation rather than a reinterpreted
/// payload.
/// @param kind the port's declared kind
/// @param object what the script passed
/// @param out filled in only on success
/// @param scratch storage for a text or blob payload; must outlive the call that consumes @p out
/// @return false with a Python error set — a wrong type, or an integer outside the port's range
[[nodiscard]] bool from_python(atp_kind kind, PyObject* object, atp_value& out, std::string& scratch);

/// Materialises a config node and everything under it as ordinary Python objects: an object becomes a
/// dict, an array a list, a scalar its own type, the null form None.
///
/// Walked once, when the instance is created, rather than exposed as handles: the author of a script
/// gets a dict and never learns that a tree of numbered nodes was involved. A dict keeps its insertion
/// order, so the order the host handed the entries over survives — which is as much order as there is,
/// since a document is read through a JSON object that sorts its keys.
///
/// A host older than the config accessors yields None — the same answer a module whose node named no
/// config gets. Refusing instead would take every Python module down on such a host, including one
/// that never looks at its config, which is the opposite of what struct_size is for.
/// @param api the host's callback table, asked through atp_api_has_config whether it carries them
/// @param ctx this instance's context
/// @param node handle of the node to materialise
/// @return a new reference, or nullptr with a Python error set
[[nodiscard]] PyObject* config_to_python(const atp_api& api, atp_ctx* ctx, std::uint32_t node);

/// The bytes of the file a config came from, as a str; empty when it came from no file, and empty on a
/// host that predates the accessor for the same reason config_to_python answers None there.
///
/// Decoded as UTF-8 **strictly**, so a file in another encoding fails the creation of the module with a
/// UnicodeDecodeError naming the position instead of handing the script mojibake that would be found
/// weeks later. The host promises UTF-8 for everything crossing this boundary; a file that breaks the
/// promise is the author's mistake, and it has to be reported as one.
///
/// **The callback itself is read only after atp_api_has_config_text says it is there**, which is why
/// this pair does not share one helper taking the callback as an argument: on a host that predates
/// those fields the table is shorter than this plugin's declaration of it, so loading the member is
/// already a read past its end, whatever the check inside the helper would have answered.
/// @return a new reference, or nullptr with a Python error set
[[nodiscard]] PyObject* config_text_to_python(const atp_api& api, atp_ctx* ctx);

/// Path of that file, as a str; empty when the config came from no file.
/// @return a new reference, or nullptr with a Python error set
[[nodiscard]] PyObject* config_origin_to_python(const atp_api& api, atp_ctx* ctx);

}  // namespace atp::bridge

#endif
