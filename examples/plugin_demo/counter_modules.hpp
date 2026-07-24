#ifndef ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULES_HPP
#define ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULES_HPP

#include <iostream>
#include <optional>
#include <utility>

#include <atp/module.hpp>
#include <atp/module_config.hpp>
#include <atp/module_registry.hpp>

// Общий для монолита и плагина код: модуль и функция его регистрации.
// Разница конфигураций — только в том, кто эту функцию вызывает:
// main напрямую или atp_register_modules из загруженной библиотеки.
namespace demo {

struct counter_outputs : atp::io::outputs {
    atp::io::output<int>& count = make<atp::io::output<int>>("count");
};
using counter_ports = atp::io::ports<atp::io::inputs, counter_outputs>;

class counter_module : public atp::module<counter_ports, "counter", atp::ver<"1.0">> {
   public:
    atp::work_status iterate(std::stop_token) override {
        outputs().count(++value_);
        return atp::work_status::busy;  // счётчик работает на каждом вызове
    }

   private:
    int value_ = 0;
};

struct printer_inputs : atp::io::inputs {
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};
using printer_ports = atp::io::ports<printer_inputs>;

// Приёмник с параметрами — демонстрация пути конфига до модуля. Пока
// печатает сырую строку params как есть: типизированный разбор появится
// вместе с обработчиками module_config.
class printer_module : public atp::module<printer_ports, "printer", atp::ver<"1.0">> {
   public:
    explicit printer_module(atp::module_config config) : config_(std::move(config)) {}

    // endl, не '\n': печать демо должна быть видна сразу — и в консоли,
    // и при перенаправлении в файл (буфер не доживает до убийства процесса).
    void start() override {
        if (!config_.raw.empty()) {
            std::cout << "printer params: " << config_.raw << std::endl;
        }
    }

    atp::work_status iterate(std::stop_token) override {
        atp::work_status status = atp::work_status::idle;
        while (std::optional<int> v = inputs().value.try_pop()) {
            std::cout << "printer: " << *v << std::endl;
            status = atp::work_status::busy;
        }
        return status;
    }

   private:
    atp::module_config config_;
};

inline void register_counter_modules(atp::module_registrar& registrar) {
    // имя берётся из самого модуля (module_name) — точка регистрации
    // больше не дублирует строку
    registrar.add<counter_module>();
    registrar.add<printer_module>();
}

}  // namespace demo

#endif  // ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULES_HPP
