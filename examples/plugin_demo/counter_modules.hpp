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

class counter_module : public atp::module<atp::io::inputs, counter_outputs, "counter", atp::ver<"1.0">> {
   public:
    atp::work_status iterate(std::stop_token) override {
        outputs().count(++value_);
        return atp::work_status::busy;  // счётчик работает на каждом вызове
    }

   private:
    int value_ = 0;
};

inline void register_counter_modules(atp::module_registrar& registrar) {
    // имя берётся из самого модуля (module_name) — точка регистрации
    // больше не дублирует строку
    registrar.add<counter_module>();
}

}  // namespace demo

#endif  // ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULES_HPP
