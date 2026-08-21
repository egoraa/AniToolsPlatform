// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_MCP_MODULE_JSON_HPP
#define ATP_MCP_MODULE_JSON_HPP

#include <utility>

#include <nlohmann/json.hpp>

#include <atp/io/property_codec.hpp>
#include <atp/mcp/type_name.hpp>
#include <atp/runtime/config_value_json.hpp>
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

/// Describes one declared config field as a JSON Schema fragment, recursing into a group's children and
/// into the element fields of an array of groups. The model needs the shape before it writes a config,
/// exactly as it needs a property's enum before it writes a value.
///
/// A required field carries no "default": it has none, and printing null for one would read as a value
/// the field may take.
[[nodiscard]] inline nlohmann::json to_json(const config::field_declaration& f) {
    nlohmann::json out{{"name", f.name}};
    switch (f.kind) {
        case config::field_kind::boolean:
            out["type"] = "boolean";
            break;
        case config::field_kind::integer:
            out["type"] = "integer";
            break;
        case config::field_kind::real:
            out["type"] = "number";
            break;
        case config::field_kind::string:
            out["type"] = "string";
            break;
        case config::field_kind::object:
            out["type"] = "object";
            break;
        case config::field_kind::array:
            out["type"] = "array";
            break;
    }
    if (f.required) {
        out["required"] = true;
    } else if (f.kind != config::field_kind::object && f.kind != config::field_kind::array) {
        out["default"] = runtime::to_json_value(f.default_value);
    }
    if (!f.children.empty()) {
        nlohmann::json children = nlohmann::json::array();
        for (const config::field_declaration& child : f.children) {
            children.push_back(to_json(child));
        }
        out[f.kind == config::field_kind::array ? "items" : "fields"] = std::move(children);
    } else if (f.kind == config::field_kind::array) {
        config::field_declaration element;
        element.kind = f.element;
        nlohmann::json items = to_json(element);
        items.erase("name");
        out["items"] = std::move(items);
    }
    return out;
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
    if (m.config_schema.has_value()) {
        nlohmann::json fields = nlohmann::json::array();
        for (const config::field_declaration& f : *m.config_schema) {
            fields.push_back(to_json(f));
        }
        json["config"] = nlohmann::json{{"fields", std::move(fields)}};
    }
    return json;
}

}  // namespace atp::mcp

#endif
