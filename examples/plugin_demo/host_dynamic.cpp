#include <iostream>
#include <stop_token>

#include <atp/module_loader.hpp>

int main() {
    atp::module_registry registry;
    atp::module_loader plugin{ATP_DEMO_PLUGIN, registry};

    std::cout << "plugin " << plugin.path().string() << " modules:\n";
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
    counter.reset();
    std::cout << "counter iterated once\n";
    return 0;
}
