#include <iostream>
#include <stop_token>

#include "counter_modules.hpp"

// Монолит: модуль слинкован в исполняемый файл, main сам зовёт регистрацию.
int main() {
    atp::module_registry registry;
    atp::module_registrar registrar{registry};
    demo::register_counter_modules(registrar);

    std::cout << "monolith modules:\n";
    for (const auto* factory : registry.list()) {
        std::cout << "  '" << factory->name() << "' v" << factory->get_version().to_string() << '\n';
    }

    auto counter = registry.create("counter");
    counter->initialize();
    counter->start();
    counter->iterate(std::stop_token{});
    counter->stop();
    std::cout << "counter iterated once\n";
    return 0;
}
