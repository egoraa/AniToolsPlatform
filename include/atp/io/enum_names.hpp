#ifndef ANITOOLSPLATFORM_IO_ENUM_NAMES_HPP
#define ANITOOLSPLATFORM_IO_ENUM_NAMES_HPP

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include <atp/io/property_codec.hpp>

namespace atp::io {

// Строка таблицы имён: значение enum и его текстовая форма. Имя — и токен
// конфига, и метка в выпадающем списке studio: разделять их незачем, пока
// у нас нет локализации (прецедент — "on_demand" у режимов потоков).
template <typename E>
struct enum_entry {
    E value;
    std::string_view name;
};

// Точка кастомизации: модуль специализирует её для своего enum, объявляя
//     static constexpr std::array entries{enum_entry{E::a, "a"}, ...};
// Primary-определения нет — enum без таблицы получает ошибку компиляции,
// а не молчаливую деградацию до числового или текстового кодека.
template <typename E>
struct enum_names;

template <typename E>
concept named_enum = std::is_enum_v<E> && requires { enum_names<E>::entries; };

namespace detail {

// Имена отдельным массивом: options() отдаёт span, а entries хранит пары.
// Переменная-шаблон своя в каждой DLL — как у erased_of<T>(), поэтому
// сравнивается только содержимое string_view, никогда не адреса.
template <named_enum E>
inline constexpr auto enum_option_names = [] {
    constexpr std::size_t count = std::tuple_size_v<std::remove_cvref_t<decltype(enum_names<E>::entries)>>;
    std::array<std::string_view, count> names{};
    for (std::size_t i = 0; i < count; ++i) {
        names[i] = enum_names<E>::entries[i].name;
    }
    return names;
}();

}  // namespace detail

// Кодек любого enum с таблицей имён. Поиск линейный: таблицы измеряются
// единицами строк, а порядок объявления — единственный внятный порядок для
// списка вариантов (сортировка или хеш-таблица его бы разрушили).
template <named_enum E>
struct property_codec<E> {
    // Имя варианта — обычная строка: в конфиге enum неотличим от текстовой
    // проперти, «одно из» выражает не kind, а непустой options().
    static constexpr property_kind kind = property_kind::text;

    static std::string to_string(const E& value) {
        for (const enum_entry<E>& e : enum_names<E>::entries) {
            if (e.value == value) {
                return std::string(e.name);
            }
        }
        return {};  // недостижимо: property<E> не пускает внутрь значение вне таблицы
    }

    static std::optional<E> from_string(std::string_view text) {
        for (const enum_entry<E>& e : enum_names<E>::entries) {
            if (e.name == text) {
                return e.value;
            }
        }
        return std::nullopt;
    }

    // Допустимые значения в порядке объявления — «перечисление на уровне
    // типа»: property<E> перенесёт их в свой набор, дальше они неотличимы от
    // перечисленных через allowed(). Span смотрит в статику (у плагина — в его
    // DLL, пришпиленную module_deleter'ом).
    static constexpr std::span<const std::string_view> options() {
        return detail::enum_option_names<E>;
    }
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_ENUM_NAMES_HPP
