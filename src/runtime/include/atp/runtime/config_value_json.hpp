// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_RUNTIME_CONFIG_VALUE_JSON_HPP
#define ATP_RUNTIME_CONFIG_VALUE_JSON_HPP

#include <cstdint>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include <atp/config/node.hpp>

namespace atp::runtime {

/// Converts a JSON node into a config::node, keeping whole numbers apart from fractional ones.
///
/// Lives here rather than beside config::node because create() is part of the plugin ABI and its
/// signature may not name nlohmann::json — that would pull the library into every plugin, which is
/// the very thing atp_runtime not being exported prevents. The boundary with a module is therefore
/// the platform's own closed type, and JSON stops here.
///
/// Key order is whatever the node hands over: a document is read as nlohmann::json, whose object is a
/// std::map, so keys are already sorted by the time this runs. Nothing is reordered here, which is
/// what makes a traversal of the result reproducible.
///
/// @throws config::access_error for an unsigned value above int64_t's maximum — the one JSON number the closed
///         set of forms cannot hold, and silently wrapping it would hand the module a different
///         number than the config spells
[[nodiscard]] inline atp::config::node to_config_value(const nlohmann::json& node) {
    if (node.is_null()) {
        return atp::config::node{};
    }
    if (node.is_boolean()) {
        return {node.get<bool>()};
    }
    if (node.is_number_unsigned()) {
        const std::uint64_t raw = node.get<std::uint64_t>();
        if (raw > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw atp::config::access_error("config: " + std::to_string(raw) + " does not fit an integer");
        }
        return {static_cast<std::int64_t>(raw)};
    }
    if (node.is_number_integer()) {
        return {node.get<std::int64_t>()};
    }
    if (node.is_number_float()) {
        return {node.get<double>()};
    }
    if (node.is_string()) {
        return {node.get<std::string>()};
    }
    if (node.is_array()) {
        atp::config::node::array_type items;
        items.reserve(node.size());
        for (const nlohmann::json& item : node) {
            items.push_back(to_config_value(item));
        }
        return {std::move(items)};
    }
    atp::config::node::object_type entries;
    entries.reserve(node.size());
    for (const auto& [key, value] : node.items()) {
        entries.emplace_back(key, to_config_value(value));
    }
    return {std::move(entries)};
}

/// Converts a config::node back into JSON, keeping the two number forms apart the same way.
///
/// The reverse of the above and, unlike it, not needed by the pipeline: a config travels **into** a
/// module and never back out. It serves the way out of the process instead — json_dump writes through
/// it, the MCP document tool hands the edited project over it, and the MCP catalog prints a declared
/// field's default with it. That last one used to convert four scalars by hand, under a comment saying
/// the tree had no converter in this direction; it has had one since this function was written.
/// @param value node to convert
[[nodiscard]] inline nlohmann::json to_json_value(const atp::config::node& value) {
    switch (value.kind()) {
        case atp::config::kind::null:
            return nullptr;
        case atp::config::kind::boolean:
            return value.as_bool();
        case atp::config::kind::integer:
            return value.as_int();
        case atp::config::kind::real:
            return value.as_double();
        case atp::config::kind::string:
            return value.as_string();
        case atp::config::kind::array: {
            nlohmann::json out = nlohmann::json::array();
            for (const atp::config::node& item : value.elements()) {
                out.push_back(to_json_value(item));
            }
            return out;
        }
        case atp::config::kind::object:
            break;
    }
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [key, entry] : value.entries()) {
        out[key] = to_json_value(entry);
    }
    return out;
}

}  // namespace atp::runtime

#endif
