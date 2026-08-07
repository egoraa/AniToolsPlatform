// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_BLOB_HPP
#define ANITOOLSPLATFORM_IO_BLOB_HPP

#include <cstddef>
#include <vector>

namespace atp::io {

/// Payload of a port carrying bytes whose meaning the platform does not know.
///
/// A port may already be declared with any type, so this name adds no capability — it exists so that
/// two modules meaning "some serialised payload" spell it the same way and their ports connect. The
/// need is the plugin_c.h path, where a foreign module's escape hatch from the closed set of payload
/// types is bytes, and a C++ peer has to be able to name the very same type: ATP_KIND_BLOB is this
/// alias and nothing else.
using blob = std::vector<std::byte>;

}  // namespace atp::io

#endif
