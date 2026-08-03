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
    if (const int* i = std::any_cast<int>(&value)) {
        json["value"] = *i;
    } else if (const unsigned* u = std::any_cast<unsigned>(&value)) {
        json["value"] = *u;
    } else if (const std::int64_t* i64 = std::any_cast<std::int64_t>(&value)) {
        json["value"] = *i64;
    } else if (const double* d = std::any_cast<double>(&value)) {
        json["value"] = *d;
    } else if (const float* f = std::any_cast<float>(&value)) {
        json["value"] = *f;
    } else if (const bool* b = std::any_cast<bool>(&value)) {
        json["value"] = *b;
    } else if (const std::string* s = std::any_cast<std::string>(&value)) {
        json["value"] = *s;
    }
    return json;
}

}  // namespace atp::mcp

#endif
