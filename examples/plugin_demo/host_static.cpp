// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <stop_token>

#include <atp/null_host.hpp>

#include "demo_modules.hpp"

int main() {
    atp::module_registry registry;
    atp::module_registrar registrar{registry};
    demo::register_demo_modules(registrar);

    std::cout << "monolith modules:\n";
    for (const auto* factory : registry.list()) {
        std::cout << "  '" << factory->name() << "' v" << factory->get_version().to_string() << '\n';
    }

    atp::service_directory services;
    atp::null_host host;
    atp::module_context context{services, host};

    auto counter = registry.create("counter");
    counter->initialize(context);
    counter->start();
    counter->iterate(std::stop_token{});
    counter->stop();
    std::cout << "counter iterated once\n";
    return 0;
}
