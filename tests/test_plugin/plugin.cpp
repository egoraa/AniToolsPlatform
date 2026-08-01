#include <atp/module.hpp>
#include <atp/plugin.hpp>

namespace {

class plugin_module : public atp::module<atp::io::ports<>, "plugin_module", atp::ver<"2.0">> {};

}  // namespace

ATP_PLUGIN_EXPORT unsigned atp_abi_version() {
    return atp::plugin_abi;
}

ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar& registrar) {
    registrar.add<plugin_module>();
    registrar.add<plugin_module>("plugin_alias");
}
