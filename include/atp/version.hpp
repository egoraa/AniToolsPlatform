#ifndef ANITOOLSPLATFORM_VERSION_HPP
#define ANITOOLSPLATFORM_VERSION_HPP

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace atp {

/// Module version with a variable number of components: 1.2, 1.2.3, 1.2.3.4.
///
/// A structural type, usable as an NTTP (see module.hpp), hence a fixed-capacity array instead of a
/// vector and public members without a trailing underscore. The actual length lives in `count`
/// while the tail of `parts` is zero-padded, so 1.2 == 1.2.0 — `count` affects the representation
/// only, never the comparison.
struct version {
    /// major.minor.patch.build, as in Windows versions; the capacity is raised by one constant.
    static constexpr std::size_t max_parts = 4;

    std::array<unsigned, max_parts> parts{};
    std::size_t count{};

    constexpr version() = default;

    /// Builds a version out of up to max_parts components.
    template <std::convertible_to<unsigned>... TParts>
        requires(sizeof...(TParts) <= max_parts)
    constexpr version(TParts... p) : parts{static_cast<unsigned>(p)...}, count{sizeof...(TParts)} {}

    /// Lexicographic over the zero-padded array; `count` takes no part.
    friend constexpr std::strong_ordering operator<=>(const version& left, const version& right) {
        return left.parts <=> right.parts;
    }
    friend constexpr bool operator==(const version& left, const version& right) {
        return left.parts == right.parts;
    }

    /// Textual form with the actual number of components: "1.2.3". A version without components
    /// prints as "0" — an empty string would read as a formatting bug in the logs.
    [[nodiscard]] std::string to_string() const {
        if (count == 0) {
            return "0";
        }
        std::string text = std::to_string(parts[0]);
        for (std::size_t i = 1; i < count; ++i) {
            text += '.';
            text += std::to_string(parts[i]);
        }
        return text;
    }
};

/// Version of a module that declared none (see module_base::get_version).
inline constexpr version default_version{0, 0, 1};

namespace detail {

template <std::size_t N>
struct fixed_string {
    std::array<char, N> chars{};

    constexpr fixed_string(const char (&text)[N]) {
        std::ranges::copy(text, chars.begin());
    }

    [[nodiscard]] constexpr std::string_view view() const {
        return {chars.data(), N - 1};
    }
};

constexpr version parse_version(std::string_view text) {
    version result;
    unsigned part = 0;
    bool has_digit = false;
    for (const char c : text) {
        if (c >= '0' && c <= '9') {
            const auto digit = static_cast<unsigned>(c - '0');
            if (part > (std::numeric_limits<unsigned>::max() - digit) / 10) {
                throw std::invalid_argument("version part overflows unsigned");
            }
            part = part * 10 + digit;
            has_digit = true;
        } else if (c == '.') {
            if (!has_digit) {
                throw std::invalid_argument("empty version part");
            }
            if (result.count == version::max_parts) {
                throw std::invalid_argument("too many version parts");
            }
            result.parts[result.count++] = part;
            part = 0;
            has_digit = false;
        } else {
            throw std::invalid_argument("invalid character in version");
        }
    }
    if (!has_digit) {
        throw std::invalid_argument("empty version part");
    }
    if (result.count == version::max_parts) {
        throw std::invalid_argument("too many version parts");
    }
    result.parts[result.count++] = part;
    return result;
}

}  // namespace detail

/// Sugar for declaring a version by string: atp::ver<"1.2.3"> is the same value as
/// atp::version{1, 2, 3}, with the format checked at compile time.
template <detail::fixed_string Text>
inline constexpr version ver = detail::parse_version(Text.view());

/// Runtime counterpart of ver<"...">: same grammar, but nullopt instead of an exception, leaving it
/// to the caller (the config validator) to decide how to report the error.
[[nodiscard]] inline std::optional<version> try_parse_version(std::string_view text) noexcept {
    try {
        return detail::parse_version(text);
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace atp

#endif
