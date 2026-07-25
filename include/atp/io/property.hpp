#ifndef ANITOOLSPLATFORM_IO_PROPERTY_HPP
#define ANITOOLSPLATFORM_IO_PROPERTY_HPP

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include <atp/io/property_base.hpp>
#include <atp/io/property_codec.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

namespace detail {

// Набор, заданный типом: options() объявляет только enum-кодек (таблица имён,
// см. enum_names.hpp) — requires-проверка избавляет скалярные кодеки от знания
// о нём. Заголовок enum_names.hpp здесь не нужен: специализацию приносит TU
// автора модуля.
template <property_value T>
std::vector<std::string> type_options() {
    if constexpr (requires { property_codec<T>::options(); }) {
        std::vector<std::string> result;
        result.reserve(property_codec<T>::options().size());
        for (std::string_view name : property_codec<T>::options()) {
            result.emplace_back(name);
        }
        return result;
    } else {
        return {};
    }
}

// Набор, заданный экземпляром: значения приводятся к T и печатаются кодеком —
// дальше проверка вхождения сравнивает канонические строки, поэтому "007"
// найдётся среди allowed(7, 8), а 7 и 7.0 не разъедутся.
template <property_value T, typename TValue>
std::vector<std::string> render_options(const option_set<TValue>& allowed) {
    std::vector<std::string> result;
    result.reserve(allowed.values.size());
    for (const TValue& value : allowed.values) {
        result.push_back(property_codec<T>::to_string(T(value)));
    }
    return result;
}

}  // namespace detail

// Типизированная проперть: значение-настройка модуля с дефолтом. В отличие
// от входа значение есть всегда — get() не бросает никогда. Запись зеркалит
// вход (T конструируется вне замка; на потоке пишущего пользовательский код
// не исполняется), чтение — pull-only: get() — состояние, take() — событие
// «изменилось с прошлого take». Любая запись взводит флаг изменения —
// сравнения со старым значением нет намеренно (equality не требуется).
template <property_value T>
class property : public property_base {
   public:
    // База конструируется первой, поэтому checked() уже вправе звать name() и
    // options(): сообщение об отвергнутом дефолте называет проперть и варианты.
    // Дефолт по умолчанию — T{}: у enum это значение 0, и если такого варианта
    // в таблице нет, конструктор откажет — дефолт называется явно.
    // Константа persistent квалифицирована намеренно: неквалифицированный поиск
    // в области класса нашёл бы одноимённый метод базы persistent(), а не тег.
    explicit property(std::string name, T default_value = T{}, persistence p = atp::io::persistent, safety s = safe)
        : property_base(std::move(name), typeid(T), property_codec<T>::kind, detail::type_options<T>(), p, s)
        , default_(checked(std::move(default_value)))
        , value_(default_) {}

    // Перечисление на уровне экземпляра: набор объявлен здесь же, у порта.
    // Набор типа (таблица имён enum) при этом заменяется, а не дополняется —
    // так модуль сужает enum до поддерживаемого им подмножества.
    template <typename TValue>
        requires std::constructible_from<T, const TValue&>
    property(std::string name,
             T default_value,
             const option_set<TValue>& allowed,
             persistence p = atp::io::persistent,
             safety s = safe)
        : property_base(std::move(name), typeid(T), property_codec<T>::kind, detail::render_options<T>(allowed), p, s)
        , default_(checked(std::move(default_value)))
        , value_(default_) {}

    template <typename U>
        requires std::constructible_from<T, U>
    void operator()(U&& value) {
        T incoming = checked(T(std::forward<U>(value)));  // конструирование и проверка вне замка
        auto guard = lock();
        value_ = std::move(incoming);
        changed_ = true;
    }

    // Копия: ссылка наружу была бы гонкой с конкурентной записью.
    [[nodiscard]] T get() const {
        auto guard = lock();
        return value_;
    }

    // Значение, если менялось с прошлого take, со сбросом флага; иначе
    // nullopt. Пара «состояние/событие» — та же, что get/take у входа.
    [[nodiscard]] std::optional<T> take() {
        auto guard = lock();
        if (!changed_) {
            return std::nullopt;
        }
        changed_ = false;
        return value_;
    }

    [[nodiscard]] bool changed() const override {
        auto guard = lock();
        return changed_;
    }

    // Возврат к дефолту — тоже изменение: модуль должен узнать об откате.
    void reset() override {
        auto guard = lock();
        value_ = default_;
        changed_ = true;
    }

    [[nodiscard]] std::string to_string() const override {
        auto guard = lock();
        return property_codec<T>::to_string(value_);
    }

    // Два разных отказа: строка не разобралась либо разобралась в значение вне
    // набора. Проверка вхождения — та же, что у типизированной записи: обойти
    // ограничение через строковый путь нельзя.
    void from_string(std::string_view text) override {
        std::optional<T> parsed = property_codec<T>::from_string(text);  // парсинг вне замка
        if (!parsed) {
            throw std::invalid_argument("property '" + name() + "': cannot parse '" + std::string(text) + "'" +
                                        options_hint());
        }
        T incoming = checked(std::move(*parsed));
        auto guard = lock();
        value_ = std::move(incoming);
        changed_ = true;
    }

    [[nodiscard]] std::string default_string() const override {
        return property_codec<T>::to_string(default_);  // дефолт неизменяем — замок не нужен
    }

   private:
    // Список вариантов — единственная подсказка, по которой автор конфига
    // поймёт, что писать; собирается здесь на всех потребителей сразу
    // (конфиг, -p, studio) и пуст у неограниченных пропертей.
    [[nodiscard]] std::string options_hint() const {
        if (options().empty()) {
            return {};
        }
        std::string hint = " (expected one of: ";
        for (std::size_t i = 0; i < options().size(); ++i) {
            hint += (i == 0 ? "" : ", ") + options()[i];
        }
        return hint + ")";
    }

    // Единственная проверка вхождения в набор — через неё проходят дефолт,
    // типизированная запись и from_string. Сравнение по канонической строке:
    // так один и тот же код обслуживает и таблицу имён enum (значение вне
    // таблицы печатается пустой строкой и не совпадёт ни с чем), и набор
    // значений любого другого типа. У неограниченной проперти набор пуст —
    // ни сравнения, ни печати, ни аллокации.
    [[nodiscard]] T checked(T value) const {
        if (!options().empty()) {
            const std::string text = property_codec<T>::to_string(value);
            if (std::ranges::find(options(), text) == options().end()) {
                throw std::invalid_argument("property '" + name() + "': value '" + text + "' is not allowed" +
                                            options_hint());
            }
        }
        return value;
    }

    T default_;  // после конструктора не меняется
    T value_;
    bool changed_ = false;
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PROPERTY_HPP
