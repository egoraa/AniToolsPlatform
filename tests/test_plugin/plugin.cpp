#include <atp/module.hpp>
#include <atp/plugin.hpp>

// Тестовый плагин для module_loader: корректный контракт, два имени
// (второе — алиас того же типа), версия модуля 2.0.
namespace {

    class plugin_module
        : public atp::module<atp::io::inputs, atp::io::outputs, atp::ver<"2.0">> {};

} // namespace

ATP_PLUGIN_EXPORT unsigned atp_abi_version() {
    return atp::plugin_abi;
}

ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar& registrar) {
    registrar.add<plugin_module>("plugin_module");
    registrar.add<plugin_module>("plugin_alias");
}
