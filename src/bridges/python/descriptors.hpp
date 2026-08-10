// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_PYTHON_DESCRIPTORS_HPP
#define ANITOOLSPLATFORM_BRIDGES_PYTHON_DESCRIPTORS_HPP

#include <vector>

#include <atp/plugin_c.h>

namespace atp::bridge {

/// Runs discovery and returns the descriptors of what it found this time.
///
/// One batch per call, because the host asks once per load and then addresses the answer by
/// position: a second load must not see the first one's modules shifted under it. Everything the
/// descriptors point at — names, option strings, port arrays — is owned by storage that is never
/// freed, since the host keeps the pointers for as long as the registration lives and the library is
/// pinned anyway. The cost is that repeated loads accumulate storage, which is bounded by how many
/// times a process reloads the bridge.
///
/// The descriptors themselves are part of that never-freed storage, and each call adds a batch
/// rather than emptying the previous one. A module the host built keeps a pointer to its descriptor
/// for its whole life and dereferences it once more when it is destroyed — so a reload that reused
/// the array would leave every module of the run before it pointing into freed memory, and a
/// pipeline that is merely stopped still owns its modules.
[[nodiscard]] const std::vector<atp_module_desc>& discover();

/// The batch the last discover() produced, without running discovery again.
///
/// The host asks for the count once and then for each descriptor in turn; rediscovering inside that
/// walk would renumber the very list it is walking.
[[nodiscard]] const std::vector<atp_module_desc>& last_batch();

}  // namespace atp::bridge

#endif
