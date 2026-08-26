// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_CONFIG_READ_HPP
#define ANITOOLSPLATFORM_CONFIG_READ_HPP

#include <cstdint>
#include <string>
#include <utility>

#include <atp/config/node.hpp>
#include <atp/config/scalar.hpp>

namespace atp::config {

/// Value of @p found, or @p fallback when there is no node or it holds another form — the two failures
/// a caller with a default in hand treats the same way.
///
/// Takes a nullable node rather than a config, so one utility serves both levels of lookup: what
/// raw_config::find(path) answers and what node::find(key) answers. Members on raw_config would
/// have meant four more of them for the same four types.
template <scalar T>
[[nodiscard]] inline T value_or(const node* found, T fallback) {
    if (found == nullptr) {
        return fallback;
    }
    if constexpr (std::same_as<T, bool>) {
        return found->try_as_bool().value_or(fallback);
    } else if constexpr (std::same_as<T, std::int64_t>) {
        return found->try_as_int().value_or(fallback);
    } else if constexpr (std::same_as<T, double>) {
        return found->try_as_double().value_or(fallback);
    } else {
        return found->try_as_string().value_or(std::move(fallback));
    }
}

[[nodiscard]] inline bool bool_or(const node* found, bool fallback) {
    return value_or<bool>(found, fallback);
}

[[nodiscard]] inline std::int64_t int_or(const node* found, std::int64_t fallback) {
    return value_or<std::int64_t>(found, fallback);
}

[[nodiscard]] inline double double_or(const node* found, double fallback) {
    return value_or<double>(found, fallback);
}

[[nodiscard]] inline std::string string_or(const node* found, std::string fallback) {
    return value_or<std::string>(found, std::move(fallback));
}

}  // namespace atp::config

#endif
