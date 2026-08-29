// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_MCP_MODULE_JSON_HPP
#define ATP_MCP_MODULE_JSON_HPP

#include <cstdint>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/io/property_codec.hpp>
#include <atp/mcp/type_name.hpp>
#include <atp/module/module_config.hpp>
#include <atp/studio/module_manager.hpp>

namespace atp::mcp {

/// Describes one property as a JSON Schema fragment. This is the whole reason the catalog is worth
/// exposing: the model learns the allowed values before it writes one, instead of from the text of a
/// rejection. The default stays a string on both sides — property_base::default_string keeps
/// everything in string form, and the config builder converts on the way in.
[[nodiscard]] inline nlohmann::json property_schema(const studio::property_info& p) {
    nlohmann::json schema;
    switch (p.kind) {
        case io::property_kind::integer:
            schema["type"] = "integer";
            break;
        case io::property_kind::real:
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

/// The declared default of one scalar field, typed as the model expects to read it back.
///
/// Built from default_string() rather than from the value the prototype happens to hold: a schema says
/// what the field falls back to, and the two answers part company the moment anybody writes the field.
[[nodiscard]] inline nlohmann::json config_default(const atp::module_config::entry& f) {
    std::string text = f.default_string();
    switch (f.kind()) {
        case atp::field_kind::boolean:
            return io::property_codec<bool>::from_string(text).value_or(false);
        case atp::field_kind::integer:
            return io::property_codec<std::int64_t>::from_string(text).value_or(0);
        case atp::field_kind::real:
            return io::property_codec<double>::from_string(text).value_or(0.0);
        case atp::field_kind::string:
        case atp::field_kind::object:
        case atp::field_kind::array:
            break;
    }
    return text;
}

/// Describes one declared config field as a JSON Schema fragment, recursing into a group's fields and
/// into the element shape of an array of groups. The model needs the shape before it writes a config,
/// exactly as it needs a property's enum before it writes a value.
///
/// Read off the config object itself rather than off a copied description: the object is what the
/// module was built against, so there is one implementation of what a field is and no second one to
/// drift from it.
///
/// A required field carries no "default": it has none, and printing null for one would read as a value
/// the field may take.
///
/// A field with a declared value set carries "enum", the same keyword and the same spelling a property
/// schema uses — an enumeration is a string with a set of names here as it is there, and the model has
/// no reason to learn two vocabularies for one idea. On an array the keyword belongs to "items": what
/// is constrained is each element, not the array.
[[nodiscard]] inline nlohmann::json to_json(const atp::module_config::entry& f) {
    nlohmann::json out{{"name", std::string(f.name())}};
    switch (f.kind()) {
        case atp::field_kind::boolean:
            out["type"] = "boolean";
            break;
        case atp::field_kind::integer:
            out["type"] = "integer";
            break;
        case atp::field_kind::real:
            out["type"] = "number";
            break;
        case atp::field_kind::string:
            out["type"] = "string";
            break;
        case atp::field_kind::object:
            out["type"] = "object";
            break;
        case atp::field_kind::array:
            out["type"] = "array";
            break;
    }
    if (f.required()) {
        out["required"] = true;
    } else if (f.kind() != atp::field_kind::object && f.kind() != atp::field_kind::array) {
        out["default"] = config_default(f);
    }
    if (f.kind() != atp::field_kind::array && !f.options().empty()) {
        out["enum"] = f.options();
    }
    if (f.kind() == atp::field_kind::object) {
        nlohmann::json fields = nlohmann::json::array();
        for (const atp::module_config::entry& child : f.group().entries()) {
            fields.push_back(to_json(child));
        }
        if (!fields.empty()) {
            out["fields"] = std::move(fields);
        }
        return out;
    }
    if (f.kind() != atp::field_kind::array) {
        return out;
    }
    if (f.element() == atp::field_kind::object) {
        nlohmann::json items = nlohmann::json::array();
        for (const atp::module_config::entry& child : f.element_shape().entries()) {
            items.push_back(to_json(child));
        }
        if (!items.empty()) {
            out["items"] = std::move(items);
            return out;
        }
    }
    switch (f.element()) {
        case atp::field_kind::boolean:
            out["items"] = nlohmann::json{{"type", "boolean"}};
            break;
        case atp::field_kind::integer:
            out["items"] = nlohmann::json{{"type", "integer"}};
            break;
        case atp::field_kind::real:
            out["items"] = nlohmann::json{{"type", "number"}};
            break;
        case atp::field_kind::string:
            out["items"] = nlohmann::json{{"type", "string"}};
            break;
        case atp::field_kind::object:
            out["items"] = nlohmann::json{{"type", "object"}};
            break;
        case atp::field_kind::array:
            out["items"] = nlohmann::json{{"type", "array"}};
            break;
    }
    if (!f.options().empty()) {
        out["items"]["enum"] = f.options();
    }
    return out;
}

/// Describes one declared port.
[[nodiscard]] inline nlohmann::json to_json(const studio::port_info& p) {
    return nlohmann::json{{"name", p.name}, {"type", type_name(p.type)}};
}

/// Describes one registered module. A broken module is reported rather than hidden — the model has
/// to know the factory exists but cannot be instantiated.
///
/// The "config" key is there exactly when the module is handed a config, and its "fields" are the ones
/// the module declared. **An empty "fields" is not the same as no key**: it says the module takes a
/// config and describes none of it, which is what a module reading its own file through text() does,
/// and what a C module and both bridges do with the document whole. Reporting those as a module that
/// accepts no config is how a "config": "file:rig.ini" that would have reached one stops being
/// written.
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
    if (m.takes_config) {
        nlohmann::json fields = nlohmann::json::array();
        if (m.config_schema) {
            for (const atp::module_config::entry& f : m.config_schema->entries()) {
                fields.push_back(to_json(f));
            }
        }
        json["config"] = nlohmann::json{{"fields", std::move(fields)}};
    }
    return json;
}

}  // namespace atp::mcp

#endif
