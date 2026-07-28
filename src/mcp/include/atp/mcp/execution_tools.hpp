#ifndef ATP_MCP_EXECUTION_TOOLS_HPP
#define ATP_MCP_EXECUTION_TOOLS_HPP

#include <exception>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/mcp/arguments.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/value_json.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/runtime/property_override.hpp>
#include <atp/studio/property_sync.hpp>

namespace atp::mcp {

namespace detail {

/// Text of the runner's first error, or an empty string if the run is clean. The session hands the
/// error out as an exception_ptr, so rethrowing is the only way to read it.
[[nodiscard]] inline std::string error_text(const std::exception_ptr& error) {
    if (!error) {
        return {};
    }
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown error";
    }
}

}  // namespace detail

/// Registers the tools that run the document and observe the running pipeline.
/// @param tools registry the tools are added to
/// @param ws workspace they operate on; it must outlive the registry
inline void register_execution_tools(tool_registry& tools, workspace& ws) {
    tools.add({"run",
               "Builds the pipeline from the current document and starts it. Returns immediately: "
               "poll get_status and read_connections to see what the pipeline is actually doing.",
               no_arguments_schema(), [&ws](const nlohmann::json&) {
                   // Config plugins are relative to the config's directory, exactly as atp_app
                   // resolves them, and go through the same policy as an explicit load_plugin.
                   for (const std::string& plugin : ws.doc().config().plugins) {
                       ws.modules().load_plugin(ws.resolve_plugin((ws.document_dir() / plugin).string()));
                   }
                   ws.run_session().start(ws.doc().config());
                   return nlohmann::json{{"running", ws.run_session().running()}};
               }});

    tools.add(
        {"stop", "Stops the run; a no-op if nothing is running.", no_arguments_schema(), [&ws](const nlohmann::json&) {
             ws.run_session().stop();
             return nlohmann::json{{"running", ws.run_session().running()}};
         }});

    tools.add({"get_status", "Whether a pipeline is running, its first error, and the pass counters per thread.",
               no_arguments_schema(), [&ws](const nlohmann::json&) {
                   nlohmann::json threads = nlohmann::json::array();
                   for (const auto& t : ws.run_session().stats()) {
                       threads.push_back({{"name", t.name}, {"passes", t.passes}, {"busy_passes", t.busy_passes}});
                   }
                   nlohmann::json status{{"running", ws.run_session().running()}, {"threads", std::move(threads)}};
                   const std::string error = detail::error_text(ws.run_session().error());
                   if (!error.empty()) {
                       status["error"] = error;
                   }
                   return status;
               }});

    tools.add({"read_connections",
               "Samples every connection: the last value that travelled it and how many writes it "
               "has seen. This is how you tell a running pipeline from a merely valid one.",
               no_arguments_schema(), [&ws](const nlohmann::json&) {
                   nlohmann::json connections = nlohmann::json::array();
                   for (const auto& s : ws.run_session().sample_connections()) {
                       nlohmann::json entry{{"group_path", s.group_path}, {"index", s.index}, {"writes", s.writes}};
                       entry["value"] = s.value ? value_to_json(*s.value) : nlohmann::json(nullptr);
                       connections.push_back(std::move(entry));
                   }
                   return nlohmann::json{{"connections", std::move(connections)}};
               }});

    tools.add({"set_live_property",
               "Edits a property of a running module on the fly. This does not touch the document — "
               "use set_property for that.",
               object_schema({{"path", "string", "Property path, as 'group.module.property'"},
                              {"value", "string", "Scalar value: number, string or boolean"}}),
               [&ws](const nlohmann::json& args) {
                   const std::string path = arg_string(args, "path");
                   const nlohmann::json value = arg_scalar(args, "value");
                   // parse_property_override splits on the FIRST '=', and a dotted path holds none,
                   // so a value containing '=' survives the round trip.
                   const std::string text = value.is_string() ? value.get<std::string>() : value.dump();
                   ws.run_session().set_property(runtime::parse_property_override(path + "=" + text));
                   return nlohmann::json{{"set", path}};
               }});

    tools.add({"sync_persistent_properties",
               "Pulls the persistent property values of the live modules into the document, dropping "
               "the ones equal to their default. Run it before saving a pipeline that has been tuned "
               "while running.",
               no_arguments_schema(), [&ws](const nlohmann::json&) {
                   const group* root = ws.run_session().live_root();
                   if (root == nullptr) {
                       throw runtime::config_error("nothing is running, so there are no live values to pull");
                   }
                   studio::sync_persistent_properties(ws.doc(), ws.doc().config(), *root);
                   return nlohmann::json{{"synced", true}};
               }});
}

}  // namespace atp::mcp

#endif  // ATP_MCP_EXECUTION_TOOLS_HPP
