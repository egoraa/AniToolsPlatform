#ifndef ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULES_HPP
#define ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULES_HPP

#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include <atp/module.hpp>
#include <atp/module_registry.hpp>

// Общий для монолита и плагина код: модули и функция их регистрации.
// Разница конфигураций — только в том, кто эту функцию вызывает:
// main напрямую или atp_register_modules из загруженной библиотеки.
// Заодно это витрина пропертей: каждая настройка здесь видна в выводе.
namespace demo {

// Форма строки печати — перечисление на уровне типа: таблица имён ниже
// делает его обычной текстовой пропертью с набором вариантов, и в конфиг
// оно едет именем ("csv"), а не числом.
enum class print_format { plain, bracketed, csv };

}  // namespace demo

// Специализация точки кастомизации — вне namespace demo, как у любого трейта.
template <>
struct atp::io::enum_names<demo::print_format> {
    static constexpr std::array entries{
        atp::io::enum_entry{demo::print_format::plain, "plain"},
        atp::io::enum_entry{demo::print_format::bracketed, "bracketed"},
        atp::io::enum_entry{demo::print_format::csv, "csv"},
    };
};

namespace demo {

struct counter_outputs : atp::io::outputs {
    atp::io::output<int>& count = make<atp::io::output<int>>("count");
};
struct counter_props : atp::io::properties {
    // Значение первого пасса.
    atp::io::property<int>& start_at = make<atp::io::property<int>>("start_at", 0);
    // Перечисление на уровне экземпляра: тип обычный (int), но допустимы
    // только перечисленные значения — в конфиге это по-прежнему число,
    // а инспектор нарисует выпадающий список.
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1, atp::io::allowed(1, 2, 5, 10));
};
using counter_ports = atp::io::ports<atp::io::inputs, counter_outputs, counter_props>;

class counter_module : public atp::module<counter_ports, "counter", atp::ver<"1.0">> {
   public:
    // Начало последовательности фиксируется на старте: правка start_at на
    // ходу не должна дёргать уже идущий счёт назад.
    void start() override {
        next_ = properties().start_at.get();
    }

    atp::work_status iterate(std::stop_token) override {
        outputs().count(next_);
        // Шаг читается каждый пасс: правка на лету (studio, -p) меняет
        // последовательность немедленно — pull-модель этого и не запрещает.
        next_ += properties().step.get();
        return atp::work_status::busy;  // счётчик работает на каждом вызове
    }

   private:
    int next_ = 0;
};

struct printer_inputs : atp::io::inputs {
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};
struct printer_props : atp::io::properties {
    // Подпись перед каждым значением; пустая — печатается имя модуля.
    atp::io::property<std::string>& tag = make<atp::io::property<std::string>>("tag");
    // Форма строки: перечисление из таблицы имён типа.
    atp::io::property<print_format>& format = make<atp::io::property<print_format>>("format", print_format::plain);
    // Булева настройка: добавляет к строке порядковый номер значения.
    atp::io::property<bool>& verbose = make<atp::io::property<bool>>("verbose", false);
    // transient: живёт только в памяти запущенного пайплайна, в конфиг при
    // сохранении из studio не попадает — пометка для текущего сеанса.
    atp::io::property<std::string>& note = make<atp::io::property<std::string>>("note", "", atp::io::transient);
};
using printer_ports = atp::io::ports<printer_inputs, atp::io::outputs, printer_props>;

// Приёмник — демонстрация пути настройки от конфига до вывода: все четыре
// проперти видны в печати, а смена формы на ходу ещё и объявляется отдельной
// строкой через правило watcher.
class printer_module : public atp::module<printer_ports, "printer", atp::ver<"1.0">> {
   public:
    // Правила наблюдения объявляются в initialize, обработчики бегут на
    // потоке модуля из poll() — то же место, где их регистрируют для входов.
    void initialize(atp::module_context&) override {
        watcher_.watch(properties().format, [](const print_format& f) {
            // Кодек перечисления переиспользуется как «значение → имя».
            std::cout << "printer format: " << atp::io::property_codec<print_format>::to_string(f) << std::endl;
        });
    }

    // endl, не '\n': печать демо должна быть видна сразу — и в консоли,
    // и при перенаправлении в файл (буфер не доживает до убийства процесса).
    void start() override {
        const std::string tag = properties().tag.get();
        if (!tag.empty()) {
            std::cout << "printer tag: " << tag << std::endl;
        }
        const std::string note = properties().note.get();
        if (!note.empty()) {
            std::cout << "printer note: " << note << " (session only)" << std::endl;
        }
    }

    atp::work_status iterate(std::stop_token) override {
        // Сначала настройки, потом данные: значения этого пасса печатаются
        // уже новой формой. poll() возвращает свой work_status — busy у него
        // означает «сработало правило», и его нельзя терять.
        atp::work_status status = watcher_.poll();
        while (std::optional<int> v = inputs().value.try_pop()) {
            std::cout << render(*v) << std::endl;
            status = atp::work_status::busy;
        }
        return status;
    }

   private:
    // Строка собирается из трёх пропертей сразу — они читаются на каждом
    // значении, поэтому правка любой из них видна со следующей же строки.
    [[nodiscard]] std::string render(int value) {
        const std::string tag = properties().tag.get();
        const std::string prefix = tag.empty() ? std::string("printer") : tag;
        ++received_;

        std::string line;
        switch (properties().format.get()) {
            case print_format::plain:
                line = prefix + ": " + std::to_string(value);
                break;
            case print_format::bracketed:
                line = prefix + ": [" + std::to_string(value) + "]";
                break;
            case print_format::csv:
                line = prefix + "," + std::to_string(value);
                break;
        }
        if (properties().verbose.get()) {
            line += " (#" + std::to_string(received_) + ")";
        }
        return line;
    }

    atp::io::watcher watcher_;
    int received_ = 0;
};

inline void register_counter_modules(atp::module_registrar& registrar) {
    // имя берётся из самого модуля (module_name) — точка регистрации
    // больше не дублирует строку
    registrar.add<counter_module>();
    registrar.add<printer_module>();
}

}  // namespace demo

#endif  // ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULES_HPP
