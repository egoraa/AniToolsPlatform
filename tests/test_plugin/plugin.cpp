#include <atp/module.hpp>
#include <atp/plugin.hpp>

// Test plugin for module_loader: a correct contract, the module's own name from the NTTP plus an
// alias of the same type, module version 2.0.
namespace {

class plugin_module : public atp::module<atp::io::ports<>, "plugin_module", atp::ver<"2.0">> {};

}  // namespace

ATP_PLUGIN_EXPORT unsigned atp_abi_version() {
    return atp::plugin_abi;
}

ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar& registrar) {
    registrar.add<plugin_module>();                // its own name, "plugin_module"
    registrar.add<plugin_module>("plugin_alias");  // an alias on top of the own name
}
