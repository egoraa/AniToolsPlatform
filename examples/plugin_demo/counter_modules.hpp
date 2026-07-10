#ifndef ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULES_HPP
#define ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULES_HPP

#include <atp/module.hpp>
#include <atp/module_registry.hpp>

// Общий для монолита и плагина код: модуль и функция его регистрации.
// Разница конфигураций — только в том, кто эту функцию вызывает:
// main напрямую или atp_register_modules из загруженной библиотеки.
namespace demo {

    struct counter_outputs : atp::io::outputs {
        atp::io::output<int>& count = make<atp::io::output<int>>("count");
    };

    class counter_module
        : public atp::module<atp::io::inputs, counter_outputs, atp::ver<"1.0">> {
    public:
        void iterate(std::stop_token) override { outputs().count(++value_); }

    private:
        int value_ = 0;
    };

    inline void register_counter_modules(atp::module_registrar& registrar) {
        registrar.add<counter_module>("counter");
    }

} // namespace demo

#endif // ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULES_HPP
