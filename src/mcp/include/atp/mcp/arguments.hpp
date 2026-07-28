#ifndef ATP_MCP_ARGUMENTS_HPP
#define ATP_MCP_ARGUMENTS_HPP

#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/runtime/config_model.hpp>

namespace atp::mcp {

// Reading an argument is validation of the model's input, which the specification classes as a tool
// execution error rather than a protocol one. Throwing runtime::config_error puts these failures on
// exactly the same path as the studio's own, so the model gets a readable message either way.

/// Required string argument.
/// @throws runtime::config_error if it is absent or not a string
[[nodiscard]] inline std::string arg_string(const nlohmann::json& args, const char* name) {
    if (!args.contains(name) || !args.at(name).is_string()) {
        throw runtime::config_error(std::string("argument '") + name + "' must be a string");
    }
    return args.at(name).get<std::string>();
}

/// Optional string argument.
[[nodiscard]] inline std::string arg_string_or(const nlohmann::json& args, const char* name, std::string fallback) {
    if (!args.contains(name) || args.at(name).is_null()) {
        return fallback;
    }
    return arg_string(args, name);
}

/// Optional boolean argument.
/// @throws runtime::config_error if it is present but not a boolean
[[nodiscard]] inline bool arg_bool_or(const nlohmann::json& args, const char* name, bool fallback) {
    if (!args.contains(name) || args.at(name).is_null()) {
        return fallback;
    }
    if (!args.at(name).is_boolean()) {
        throw runtime::config_error(std::string("argument '") + name + "' must be a boolean");
    }
    return args.at(name).get<bool>();
}

/// Required non-negative integer argument. The signedness of the stored number is deliberately not
/// part of the check: nlohmann keeps a value built from a C++ int as signed and a value parsed from
/// text as unsigned, and an argument must not depend on which side produced it.
/// @throws runtime::config_error if it is absent, not an integer, or negative
[[nodiscard]] inline std::size_t arg_index(const nlohmann::json& args, const char* name) {
    if (!args.contains(name) || !args.at(name).is_number_integer()) {
        throw runtime::config_error(std::string("argument '") + name + "' must be a non-negative integer");
    }
    const auto value = args.at(name).get<long long>();
    if (value < 0) {
        throw runtime::config_error(std::string("argument '") + name + "' must be a non-negative integer");
    }
    return static_cast<std::size_t>(value);
}

/// Required scalar argument — the shape a property value is allowed to take.
/// @throws runtime::config_error if it is absent or not a number, string or boolean
[[nodiscard]] inline nlohmann::json arg_scalar(const nlohmann::json& args, const char* name) {
    if (!args.contains(name)) {
        throw runtime::config_error(std::string("argument '") + name + "' is required");
    }
    const nlohmann::json& value = args.at(name);
    if (!value.is_number() && !value.is_string() && !value.is_boolean()) {
        throw runtime::config_error(std::string("argument '") + name +
                                    "' must be a scalar (number, string or boolean)");
    }
    return value;
}

/// One field of a tool's argument schema.
struct schema_field {
    const char* name;
    const char* type;  // a JSON Schema type: "string", "number", "integer", "boolean"
    const char* description;
    bool required = true;
};

/// Builds the argument schema of a tool. additionalProperties is always false, so a misspelled
/// argument is refused by the client rather than silently ignored here.
[[nodiscard]] inline nlohmann::json object_schema(std::initializer_list<schema_field> fields) {
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json required = nlohmann::json::array();
    for (const schema_field& f : fields) {
        properties[f.name] = {{"type", f.type}, {"description", f.description}};
        if (f.required) {
            required.push_back(f.name);
        }
    }
    nlohmann::json schema{{"type", "object"}, {"properties", std::move(properties)}, {"additionalProperties", false}};
    if (!required.empty()) {
        schema["required"] = std::move(required);
    }
    return schema;
}

/// The schema of a tool that takes nothing.
[[nodiscard]] inline nlohmann::json no_arguments_schema() {
    return nlohmann::json{{"type", "object"}, {"additionalProperties", false}};
}

}  // namespace atp::mcp

#endif  // ATP_MCP_ARGUMENTS_HPP
