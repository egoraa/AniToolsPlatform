#include <atp/plugin.hpp>

#include "demo_modules.hpp"

ATP_PLUGIN_EXPORT unsigned atp_abi_version() {
    return atp::plugin_abi;
}

ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar& registrar) {
    demo::register_demo_modules(registrar);
}
