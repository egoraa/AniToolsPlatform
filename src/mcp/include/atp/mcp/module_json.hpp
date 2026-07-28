#ifndef ATP_MCP_MODULE_JSON_HPP
#define ATP_MCP_MODULE_JSON_HPP

#include <utility>

#include <nlohmann/json.hpp>

#include <atp/io/property_codec.hpp>
#include <atp/mcp/type_name.hpp>
#include <atp/studio/module_manager.hpp>

namespace atp::mcp {

/// Describes one property as a JSON Schema fragment. This is the whole reason the catalog is worth
/// exposing: the model learns the allowed values before it writes one, instead of from the text of a
/// rejection. The default stays a string on both sides — property_base::default_string keeps
/// everything in string form, and the config builder converts on the way in.
[[nodiscard]] inline nlohmann::json property_schema(const studio::property_info& p) {
    nlohmann::json schema;
    switch (p.kind) {
        case io::property_kind::number:
            schema["type"] = "number";
            break;
        case io::property_kind::boolean:
            schema["type"] = "boolean";
            break;
        case io::property_kind::text:
            schema["type"] = "string";
            break;
    }
    schema["default"] = p.default_value;
    if (!p.options.empty()) {
        schema["enum"] = p.options;
    }
    return schema;
}

/// Describes one declared port.
[[nodiscard]] inline nlohmann::json to_json(const studio::port_info& p) {
    return nlohmann::json{{"name", p.name}, {"type", type_name(p.type)}};
}

/// Describes one registered module. A broken module is reported rather than hidden — the model has
/// to know the factory exists but cannot be instantiated.
[[nodiscard]] inline nlohmann::json to_json(const studio::module_info& m) {
    nlohmann::json inputs = nlohmann::json::array();
    for (const studio::port_info& p : m.inputs) {
        inputs.push_back(to_json(p));
    }
    nlohmann::json outputs = nlohmann::json::array();
    for (const studio::port_info& p : m.outputs) {
        outputs.push_back(to_json(p));
    }
    nlohmann::json properties = nlohmann::json::array();
    for (const studio::property_info& p : m.properties) {
        properties.push_back({{"name", p.name}, {"persistent", p.persistent}, {"schema", property_schema(p)}});
    }
    nlohmann::json json{{"name", m.name},
                        {"version", m.ver.to_string()},
                        {"inputs", std::move(inputs)},
                        {"outputs", std::move(outputs)},
                        {"properties", std::move(properties)},
                        {"broken", m.broken}};
    if (!m.error.empty()) {
        json["error"] = m.error;
    }
    return json;
}

}  // namespace atp::mcp

#endif  // ATP_MCP_MODULE_JSON_HPP
