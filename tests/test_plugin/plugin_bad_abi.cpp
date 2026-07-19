#include <atp/plugin.hpp>

// Плагин с несовпадающим ABI: рукопожатие должно отклонить его до
// какого-либо C++-вызова.
ATP_PLUGIN_EXPORT unsigned atp_abi_version() {
    return atp::plugin_abi + 1;
}

ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar&) {}
