#ifndef ANITOOLSPLATFORM_IO_PROPERTY_CODEC_HPP
#define ANITOOLSPLATFORM_IO_PROPERTY_CODEC_HPP

#include <charconv>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace atp::io {

// Вид значения проперти — подсказка сериализаторам, живущим над строкой:
// рантайм по нему пишет в конфиг число, а не "число". Это JSON-тип и только:
// «одно из набора» — свойство ортогональное (см. property_base::options()),
// им ограничены бывают и числа, и строки, и enum, а тип записи от этого не
// меняется. Виджет инспектор выбирает по обоим признакам.
enum class property_kind { number, boolean, text };

// Трейт конвертации значения проперти в строку и обратно. Primary-определения
// нет: тип без специализации — ошибка компиляции, а не тихая деградация.
// from_string возвращает nullopt на непарсящейся строке — исключение с
// контекстом (имя проперти) бросает property_base, кодек контекста не знает.
template <typename T>
struct property_codec;

namespace detail {

// Числа — через from_chars/to_chars: локаленезависимо и без аллокаций.
// Частичный разбор ("12x") — отказ: строка обязана быть числом целиком.
template <typename T>
std::optional<T> parse_number(std::string_view text) {
    T value{};
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

template <typename T>
std::string print_number(T value) {
    char buffer[64];  // с запасом для любых арифметических типов
    // Отказ невозможен при таком буфере, поэтому ec не проверяется.
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

}  // namespace detail

// Целые; bool исключён — у него собственная текстовая форма ниже.
template <std::integral T>
    requires(!std::same_as<T, bool>)
struct property_codec<T> {
    static constexpr property_kind kind = property_kind::number;
    static std::string to_string(const T& value) {
        return detail::print_number(value);
    }
    static std::optional<T> from_string(std::string_view text) {
        return detail::parse_number<T>(text);
    }
};

// Плавающие: to_chars даёт кратчайшую строку с точным round-trip.
template <std::floating_point T>
struct property_codec<T> {
    static constexpr property_kind kind = property_kind::number;
    static std::string to_string(const T& value) {
        return detail::print_number(value);
    }
    static std::optional<T> from_string(std::string_view text) {
        return detail::parse_number<T>(text);
    }
};

template <>
struct property_codec<bool> {
    static constexpr property_kind kind = property_kind::boolean;
    static std::string to_string(const bool& value) {
        return value ? "true" : "false";
    }
    static std::optional<bool> from_string(std::string_view text) {
        if (text == "true") {
            return true;
        }
        if (text == "false") {
            return false;
        }
        return std::nullopt;
    }
};

template <>
struct property_codec<std::string> {
    static constexpr property_kind kind = property_kind::text;
    static std::string to_string(const std::string& value) {
        return value;
    }
    static std::optional<std::string> from_string(std::string_view text) {
        return std::string(text);
    }
};

// Контракт «тип пригоден для property<T>»: полная тройка kind/to/from.
template <typename T>
concept property_value = requires(const T& value, std::string_view text) {
    { property_codec<T>::kind } -> std::convertible_to<property_kind>;
    { property_codec<T>::to_string(value) } -> std::same_as<std::string>;
    { property_codec<T>::from_string(text) } -> std::same_as<std::optional<T>>;
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PROPERTY_CODEC_HPP
