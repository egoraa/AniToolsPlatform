#include <iostream>
#include <stop_token>

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
    atp::module_context context{services};

    auto counter = registry.create("counter");
    counter->initialize(context);
    counter->start();
    counter->iterate(std::stop_token{});
    counter->stop();
    std::cout << "counter iterated once\n";
    return 0;
}
