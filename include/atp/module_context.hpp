// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_MODULE_CONTEXT_HPP
#define ANITOOLSPLATFORM_MODULE_CONTEXT_HPP

#include <atp/module_host.hpp>
#include <atp/service_directory.hpp>

namespace atp {

/// Context the platform hands to a module in initialize: an aggregate of references to platform
/// services. New services are added as fields, leaving the lifecycle signatures alone — but the
/// context layout is visible to plugins, so adding a field bumps plugin_abi.
///
/// Every module gets a context of its own: services is shared by the whole pipeline, host is not —
/// it is the platform's side of this module in particular. That is how a log line knows who wrote
/// it although the writer never says, and how wake() knows which thread to wake.
struct module_context {
    service_directory& services;
    module_host& host;
};

}  // namespace atp

#endif
