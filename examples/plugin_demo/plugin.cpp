#include <atp/plugin.hpp>

#include "counter_modules.hpp"

// Плагин: тот же register_counter_modules, обёрнутый в контракт границы.
ATP_PLUGIN_EXPORT unsigned atp_abi_version() {
    return atp::plugin_abi;
}

ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar& registrar) {
    demo::register_counter_modules(registrar);
}
