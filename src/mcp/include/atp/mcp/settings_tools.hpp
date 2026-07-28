#ifndef ATP_MCP_SETTINGS_TOOLS_HPP
#define ATP_MCP_SETTINGS_TOOLS_HPP

#include <chrono>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/mcp/arguments.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/pipeline_runner.hpp>

namespace atp::mcp {

namespace detail {

/// Turns the "mode" argument into the runner's enum.
/// @throws runtime::config_error on an unknown name
[[nodiscard]] inline thread_mode parse_thread_mode(const std::string& text) {
    if (text == "on_demand") {
        return thread_mode::on_demand;
    }
    if (text == "throttled") {
        return thread_mode::throttled;
    }
    if (text == "spinning") {
        return thread_mode::spinning;
    }
    throw runtime::config_error("unknown thread mode '" + text + "', expected on_demand, throttled or spinning");
}

/// The schema of add_thread. object_schema cannot express an enum, and the mode is exactly the
/// argument the model should not have to guess, so it is spelled out here.
[[nodiscard]] inline nlohmann::json add_thread_schema() {
    nlohmann::json schema = object_schema({{"name", "string", "Thread name"},
                                           {"mode", "string", "Pacing mode"},
                                           {"period_ms", "integer", "Iteration period, throttled only", false}});
    schema["properties"]["mode"]["enum"] = nlohmann::json::array({"on_demand", "throttled", "spinning"});
    return schema;
}

}  // namespace detail

/// Registers the tools that edit per-instance settings and the runner's thread layout.
/// @param tools registry the tools are added to
/// @param ws workspace they operate on; it must outlive the registry
inline void register_settings_tools(tool_registry& tools, workspace& ws) {
    tools.add({"set_property",
               "Writes a property value into the document. Allowed values come from the property's "
               "schema in list_modules. To edit a running pipeline use set_live_property instead.",
               object_schema({{"group_path", "string", "Group holding the module; \"\" is the root"},
                              {"name", "string", "Module name within that group"},
                              {"property", "string", "Property name"},
                              {"value", "string", "Scalar value: number, string or boolean"}}),
               [&ws](const nlohmann::json& args) {
                   const std::string property = arg_string(args, "property");
                   ws.doc().set_property(arg_string(args, "group_path"), arg_string(args, "name"), property,
                                         arg_scalar(args, "value"));
                   return nlohmann::json{{"set", property}};
               }});

    tools.add({"clear_property", "Drops a property value, which means going back to the module's default.",
               object_schema({{"group_path", "string", "Group holding the module; \"\" is the root"},
                              {"name", "string", "Module name within that group"},
                              {"property", "string", "Property name"}}),
               [&ws](const nlohmann::json& args) {
                   ws.doc().clear_property(arg_string(args, "group_path"), arg_string(args, "name"),
                                           arg_string(args, "property"));
                   return nlohmann::json{{"cleared", true}};
               }});

    tools.add({"add_thread", "Declares a runner thread. A throttled thread requires a positive period_ms.",
               detail::add_thread_schema(), [&ws](const nlohmann::json& args) {
                   const std::string name = arg_string(args, "name");
                   const thread_mode mode = detail::parse_thread_mode(arg_string(args, "mode"));
                   const auto period =
                       std::chrono::milliseconds(args.contains("period_ms") && !args.at("period_ms").is_null()
                                                     ? static_cast<long long>(arg_index(args, "period_ms"))
                                                     : 0LL);
                   ws.doc().add_thread(name, mode, period);
                   return nlohmann::json{{"added", name}};
               }});

    tools.add({"remove_thread", "Removes a thread along with every assignment pointing at it.",
               object_schema({{"name", "string", "Thread name"}}), [&ws](const nlohmann::json& args) {
                   ws.doc().remove_thread(arg_string(args, "name"));
                   return nlohmann::json{{"removed", true}};
               }});

    tools.add({"set_assignment", "Assigns a group to a thread, replacing any previous assignment of that group.",
               object_schema({{"group_path", "string", "Group to assign; the root cannot be assigned by path"},
                              {"thread", "string", "Thread name declared with add_thread"}}),
               [&ws](const nlohmann::json& args) {
                   ws.doc().set_assignment(arg_string(args, "group_path"), arg_string(args, "thread"));
                   return nlohmann::json{{"assigned", true}};
               }});

    tools.add({"clear_assignment", "Drops a group's thread assignment, letting it run inline in its ancestor again.",
               object_schema({{"group_path", "string", "Group to unassign"}}), [&ws](const nlohmann::json& args) {
                   ws.doc().clear_assignment(arg_string(args, "group_path"));
                   return nlohmann::json{{"cleared", true}};
               }});

    tools.add({"add_config_plugin", "Adds a plugin path to the config's plugin list, so run() loads it.",
               object_schema({{"path", "string", "Plugin path, relative to the config's directory"}}),
               [&ws](const nlohmann::json& args) {
                   const std::string path = arg_string(args, "path");
                   // Validated against the plugin policy now, but stored verbatim: the config's own
                   // paths are relative to the config's directory, and rewriting them would break
                   // the file for atp_app.
                   (void)ws.resolve_plugin((ws.document_dir() / path).string());
                   ws.doc().add_plugin(path);
                   return nlohmann::json{{"added", path}};
               }});

    tools.add({"remove_config_plugin", "Removes a plugin path from the config's plugin list.",
               object_schema({{"path", "string", "Plugin path as it appears in the config"}}),
               [&ws](const nlohmann::json& args) {
                   ws.doc().remove_plugin(arg_string(args, "path"));
                   return nlohmann::json{{"removed", true}};
               }});
}

}  // namespace atp::mcp

#endif  // ATP_MCP_SETTINGS_TOOLS_HPP
