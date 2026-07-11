#include <iostream>
#include <stop_token>

#include <atp/module_loader.hpp>

// Хост с плагином: модуль приезжает из динамической библиотеки, путь к ней
// подставляет CMake (ATP_DEMO_PLUGIN).
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
    counter->start(context);
    counter->iterate(std::stop_token{});
    counter->stop(context);
    counter.reset();  // модуль умирает до загрузчика — контракт времени жизни
    std::cout << "counter iterated once\n";
    return 0;
}
