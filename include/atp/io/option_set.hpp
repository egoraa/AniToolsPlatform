// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_OPTION_SET_HPP
#define ANITOOLSPLATFORM_IO_OPTION_SET_HPP

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <atp/io/property_codec.hpp>

namespace atp::io {

/// Set of allowed values — the instance-level flavour of an enumeration: the value type stays
/// ordinary (int, std::string, an enum too) while the list of options is declared next to the
/// declaration itself. Stores values rather than strings; the codec turns them into strings, so that
/// comparison always runs on the canonical form.
///
/// Its own header rather than a line in property_base.hpp, because a config field declares a value
/// set with the same vocabulary and must not drag io_base and a mutex along to say so.
template <typename TValue>
struct option_set {
    std::vector<TValue> values;
};

/// Declaration vocabulary for a value set:
///
///     make<property<int>>("channels", 2, allowed(1, 2, 6));
///     make<property<std::string>>("codec", "h264", allowed("h264", "h265"));
///     field("layout", channel_layout::stereo, allowed(channel_layout::mono, channel_layout::stereo));
///
/// An empty set is rejected at compile time: an "enumeration of nothing" would silently mean no
/// constraint at all — exactly the opposite of the intent.
template <typename... TValues>
    requires(sizeof...(TValues) > 0)
[[nodiscard]] auto allowed(TValues&&... values) {
    using value_type = std::common_type_t<std::decay_t<TValues>...>;
    return option_set<value_type>{{static_cast<value_type>(std::forward<TValues>(values))...}};
}

namespace detail {

/// Options a type carries on its own — an enum's name table, in declaration order. Empty for a type
/// with no table, which is every type that is not an enumeration.
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

/// The canonical strings of a listed set, which is what every later comparison runs on.
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

}  // namespace atp::io

#endif
