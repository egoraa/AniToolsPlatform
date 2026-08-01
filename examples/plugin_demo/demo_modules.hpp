#ifndef ATP_EXAMPLES_PLUGIN_DEMO_DEMO_MODULES_HPP
#define ATP_EXAMPLES_PLUGIN_DEMO_DEMO_MODULES_HPP

#include <atp/module_registry.hpp>

#include "counter_module.hpp"
#include "printer_module.hpp"
#include "scaler_module.hpp"

/// @file
/// Code shared by the monolithic and the plugin demo: the modules and the function registering
/// them. The two configurations differ only in who calls that function — main directly, or
/// atp_register_modules from the loaded library.
///
/// The chain is counter → scaler → printer, and it is deliberately heterogeneous: int, std::string,
/// double, the user-defined demo::sample and the universal std::any all travel over its
/// connections. In the studio each type gets its own pin colour, an output feeds both a typed and a
/// universal input, and a pair like scaler.value (double) → printer.value (int) is refused before
/// it ever reaches the document.
namespace demo {

/// Registers the demo modules; called by main in the monolithic build and by atp_register_modules
/// in the plugin one.
inline void register_demo_modules(atp::module_registrar& registrar) {
    registrar.add<counter_module>();
    registrar.add<scaler_module>();
    registrar.add<printer_module>();
}

}  // namespace demo

#endif
