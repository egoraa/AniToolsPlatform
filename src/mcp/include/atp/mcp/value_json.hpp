#ifndef ATP_MCP_VALUE_JSON_HPP
#define ATP_MCP_VALUE_JSON_HPP

#include <any>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include <atp/mcp/type_name.hpp>

namespace atp::mcp {

/// Renders a monitored value for the agent. The type is always reported; the value is null for a
/// type this layer cannot express, which is honest — the studio's format_value refuses the same
/// cases rather than printing a lie.
[[nodiscard]] inline nlohmann::json value_to_json(const std::any& value) {
    nlohmann::json json{{"type", type_name(value.type())}, {"value", nullptr}};
    if (const int* v = std::any_cast<int>(&value)) {
        json["value"] = *v;
    } else if (const unsigned* v = std::any_cast<unsigned>(&value)) {
        json["value"] = *v;
    } else if (const std::int64_t* v = std::any_cast<std::int64_t>(&value)) {
        json["value"] = *v;
    } else if (const double* v = std::any_cast<double>(&value)) {
        json["value"] = *v;
    } else if (const float* v = std::any_cast<float>(&value)) {
        json["value"] = *v;
    } else if (const bool* v = std::any_cast<bool>(&value)) {
        json["value"] = *v;
    } else if (const std::string* v = std::any_cast<std::string>(&value)) {
        json["value"] = *v;
    }
    return json;
}

}  // namespace atp::mcp

#endif
