// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_VALUE_JSON_HPP
#define ATP_RUNTIME_CONFIG_VALUE_JSON_HPP

#include <cstdint>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include <atp/config_value.hpp>

namespace atp::runtime {

/// Converts a JSON node into a config_value, keeping whole numbers apart from fractional ones.
///
/// Lives here rather than beside config_value because create() is part of the plugin ABI and its
/// signature may not name nlohmann::json — that would pull the library into every plugin, which is
/// the very thing atp_runtime not being exported prevents. The boundary with a module is therefore
/// the platform's own closed type, and JSON stops here.
///
/// Key order is whatever the node hands over: a document is read as nlohmann::json, whose object is a
/// std::map, so keys are already sorted by the time this runs. Nothing is reordered here, which is
/// what makes a traversal of the result reproducible.
///
/// @throws bad_config for an unsigned value above int64_t's maximum — the one JSON number the closed
///         set of forms cannot hold, and silently wrapping it would hand the module a different
///         number than the config spells
[[nodiscard]] inline config_value to_config_value(const nlohmann::json& node) {
    if (node.is_null()) {
        return config_value{};
    }
    if (node.is_boolean()) {
        return config_value(node.get<bool>());
    }
    if (node.is_number_unsigned()) {
        const std::uint64_t raw = node.get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw bad_config("config: " + std::to_string(raw) + " does not fit an integer");
        }
        return config_value(static_cast<std::int64_t>(raw));
    }
    if (node.is_number_integer()) {
        return config_value(node.get<std::int64_t>());
    }
    if (node.is_number_float()) {
        return config_value(node.get<double>());
    }
    if (node.is_string()) {
        return config_value(node.get<std::string>());
    }
    if (node.is_array()) {
        config_value::array_type items;
        items.reserve(node.size());
        for (const nlohmann::json& item : node) {
            items.push_back(to_config_value(item));
        }
        return config_value(std::move(items));
    }
    config_value::object_type entries;
    entries.reserve(node.size());
    for (const auto& [key, value] : node.items()) {
        entries.emplace_back(key, to_config_value(value));
    }
    return config_value(std::move(entries));
}

}  // namespace atp::runtime

#endif
