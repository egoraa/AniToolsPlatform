#include <atp/plugin.hpp>

// A plugin with a mismatched ABI: the handshake has to reject it before any C++ call.
ATP_PLUGIN_EXPORT unsigned atp_abi_version() {
    return atp::plugin_abi + 1;
}

ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar&) {}
