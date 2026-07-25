#ifndef ANITOOLSPLATFORM_IO_PROPERTY_BASE_HPP
#define ANITOOLSPLATFORM_IO_PROPERTY_BASE_HPP

#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include <atp/io/io_base.hpp>
#include <atp/io/property_codec.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

// Сохраняемость — свойство экземпляра проперти (в стиле safety):
// persistent-значения studio пишет в конфиг при сохранении, transient
// живут только в памяти работающего пайплайна.
struct persistence {
    bool keep;
};

inline constexpr persistence persistent{true};
inline constexpr persistence transient{false};

// Набор допустимых значений — «перечисление на уровне экземпляра»: тип
// проперти остаётся обычным (int, std::string, да и enum), а список вариантов
// объявляется там же, где сам порт. Хранит значения, а не строки: в строки их
// переводит кодек проперти, чтобы сравнение шло по канонической форме.
template <typename TValue>
struct option_set {
    std::vector<TValue> values;
};

// Вокабуляр объявления:
//     make<property<int>>("channels", 2, allowed(1, 2, 6));
//     make<property<std::string>>("codec", "h264", allowed("h264", "h265"));
// Пустой набор отвергается на этапе компиляции: «перечисление ни из чего»
// молча означало бы отсутствие ограничения — ровно противоположный смысл.
template <typename... TValues>
    requires(sizeof...(TValues) > 0)
[[nodiscard]] auto allowed(TValues&&... values) {
    using value_type = std::common_type_t<std::decay_t<TValues>...>;
    return option_set<value_type>{{static_cast<value_type>(std::forward<TValues>(values))...}};
}

// Type-erased база проперти — в одном ряду с input_base/output_base:
// имя, typeid(T) и синхронизация — из io_base. Здесь — строковый доступ
// (builder, CLI и studio правят значение, не зная T), вид значения для
// сериализаторов и признак сохраняемости. Проперти не подключаются к
// выходам — это отдельный вид сущности со своим реестром (properties).
class property_base : public io_base {
   public:
    property_base(std::string name,
                  std::type_index type,
                  property_kind kind,
                  std::vector<std::string> options,
                  persistence p,
                  safety s)
        : io_base(std::move(name), type, s), kind_(kind), options_(std::move(options)), persistent_(p.keep) {}

    [[nodiscard]] property_kind kind() const noexcept {
        return kind_;
    }

    // Допустимые значения в канонической строковой форме, в порядке
    // объявления. Пусто — ограничений нет; непусто — проперть-перечисление:
    // инспектор рисует выпадающий список, а запись вне набора отвергается.
    // Откуда набор взялся (таблица имён enum-типа или option_set экземпляра),
    // потребителям знать незачем — здесь эти два пути уже сошлись.
    // Хранится копией: набор экземпляра живёт в самой проперти, а не в
    // статике кодека, и после конструктора не меняется.
    [[nodiscard]] const std::vector<std::string>& options() const noexcept {
        return options_;
    }

    [[nodiscard]] bool persistent() const noexcept {
        return persistent_;
    }

    // Строковый доступ к значению. from_string на непарсящейся строке
    // бросает std::invalid_argument с именем проперти и самой строкой,
    // не трогая ни значение, ни флаг изменения.
    [[nodiscard]] virtual std::string to_string() const = 0;
    virtual void from_string(std::string_view text) = 0;

    // Дефолт строкой: studio сравнивает с ним текущее значение при
    // сохранении — равное дефолту в конфиг не пишется (без шума в диффах).
    [[nodiscard]] virtual std::string default_string() const = 0;

    // Неразрушающий пик «менялось ли с последнего take»; само изъятие —
    // типизированный take() наследника: без T оно бессмысленно.
    [[nodiscard]] virtual bool changed() const = 0;

   private:
    property_kind kind_;
    std::vector<std::string> options_;  // пусто у неограниченных пропертей — без аллокации
    bool persistent_;
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PROPERTY_BASE_HPP
