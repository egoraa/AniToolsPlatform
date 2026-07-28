#ifndef ANITOOLSPLATFORM_MODULE_CONTEXT_HPP
#define ANITOOLSPLATFORM_MODULE_CONTEXT_HPP

#include <atp/service_directory.hpp>

namespace atp {

/// Context the platform hands to a module in initialize: an aggregate of references to platform
/// services. New services are added as fields, leaving the lifecycle signatures alone — but the
/// context layout is visible to plugins, so adding a field bumps plugin_abi.
struct module_context {
    service_directory& services;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_MODULE_CONTEXT_HPP
