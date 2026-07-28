#ifndef ANITOOLSPLATFORM_IO_PROPERTY_CODEC_HPP
#define ANITOOLSPLATFORM_IO_PROPERTY_CODEC_HPP

#include <charconv>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace atp::io {

/// Kind of a property value — a hint for the serialisers living above the string form, so that the
/// runtime writes a number into the config rather than "number". It is the JSON type and nothing
/// else: "one of a set" is an orthogonal trait (see property_base::options()) that constrains
/// numbers, strings and enums alike without changing how they are written.
enum class property_kind { number, boolean, text };

/// Trait converting a property value to a string and back. There is no primary definition, so a
/// type without a specialisation is a compile error rather than a silent degradation. from_string
/// returns nullopt for an unparsable string — the exception carrying context (the property name)
/// is thrown by property_base, which the codec knows nothing about.
template <typename T>
struct property_codec;

namespace detail {

// from_chars/to_chars: locale-independent and allocation-free. A partial parse ("12x") is
// rejected — the string has to be a number in full.
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
    char buffer[64];  // roomy enough for any arithmetic type, so ec cannot report an overflow
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

}  // namespace detail

/// Codec of the integral types; bool is excluded — it has its own textual form below.
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

/// Codec of the floating-point types; to_chars yields the shortest string that round-trips exactly.
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

/// Codec of bool: the "true"/"false" tokens.
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

/// Codec of std::string: the identity conversion.
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

/// Contract "the type is usable as a property<T>": the full kind/to_string/from_string triple.
template <typename T>
concept property_value = requires(const T& value, std::string_view text) {
    { property_codec<T>::kind } -> std::convertible_to<property_kind>;
    { property_codec<T>::to_string(value) } -> std::same_as<std::string>;
    { property_codec<T>::from_string(text) } -> std::same_as<std::optional<T>>;
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_PROPERTY_CODEC_HPP
