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

/// Row of a name table: an enum value and its textual form. The name serves both as the config
/// token and as the studio drop-down label — there is no reason to separate them while there is no
/// localisation.
template <typename E>
struct enum_entry {
    E value;
    std::string_view name;
};

/// Customisation point: a module specialises it for its own enum, declaring
/// `static constexpr std::array entries{enum_entry{E::a, "a"}, ...};`. There is no primary
/// definition, so an enum without a table is a compile error rather than a silent fallback to the
/// numeric or textual codec.
template <typename E>
struct enum_names;

/// An enum equipped with an enum_names table.
template <typename E>
concept named_enum = std::is_enum_v<E> && requires { enum_names<E>::entries; };

namespace detail {

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

/// Codec of any enum with a name table. Lookup is linear: tables are a handful of rows long, and
/// declaration order is the only sensible order for a list of options — sorting or hashing would
/// destroy it.
template <named_enum E>
struct property_codec<E> {
    /// An option name is an ordinary string: in the config an enum is indistinguishable from a
    /// textual property, since "one of a set" is expressed by options(), not by the kind.
    static constexpr property_kind kind = property_kind::text;

    static std::string to_string(const E& value) {
        for (const enum_entry<E>& e : enum_names<E>::entries) {
            if (e.value == value) {
                return std::string(e.name);
            }
        }
        return {};
    }

    static std::optional<E> from_string(std::string_view text) {
        for (const enum_entry<E>& e : enum_names<E>::entries) {
            if (e.name == text) {
                return e.value;
            }
        }
        return std::nullopt;
    }

    /// Allowed values in declaration order — the type-level flavour of an enumeration: property<E>
    /// copies them into its own set, where they become indistinguishable from the ones listed with
    /// allowed(). The span points into static storage (for a plugin, inside its DLL, pinned by
    /// module_deleter).
    static constexpr std::span<const std::string_view> options() {
        return detail::enum_option_names<E>;
    }
};

}  // namespace atp::io

#endif
