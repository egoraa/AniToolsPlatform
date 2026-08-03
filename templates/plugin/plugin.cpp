#include <atp/plugin.hpp>

#include "doubler_module.hpp"

ATP_PLUGIN_EXPORT unsigned atp_abi_version() {
    return atp::plugin_abi;
}

ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar& registrar) {
    registrar.add<atp_template::doubler_module>();
}
