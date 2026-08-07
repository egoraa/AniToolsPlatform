// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_MCP_EXECUTION_TOOLS_HPP
#define ATP_MCP_EXECUTION_TOOLS_HPP

#include <string>

#include <nlohmann/json.hpp>

#include <atp/mcp/arguments.hpp>
#include <atp/mcp/control_tools.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/studio/property_sync.hpp>

namespace atp::mcp {

/// Registers the tools that run the document and observe the running pipeline.
/// @param tools registry the tools are added to
/// @param ws workspace they operate on; it must outlive the registry
inline void register_execution_tools(tool_registry& tools, workspace& ws) {
    tools.add({"run",
               "Builds the pipeline from the current document and starts it. Returns immediately: "
               "poll get_status and read_connections to see what the pipeline is actually doing.",
               no_arguments_schema(), [&ws](const nlohmann::json&) {
                   for (const std::string& plugin : ws.project().config().plugins) {
                       ws.modules().load_plugin(ws.resolve_plugin((ws.project_dir() / plugin).string()));
                   }
                   ws.run_session().start(ws.project().config());
                   return nlohmann::json{{"running", ws.run_session().running()}};
               }});

    tools.add(
        {"stop", "Stops the run; a no-op if nothing is running.", no_arguments_schema(), [&ws](const nlohmann::json&) {
             ws.run_session().stop();
             return nlohmann::json{{"running", ws.run_session().running()}};
         }});

    register_control_tools(tools, ws.run_session());

    tools.add({"sync_persistent_properties",
               "Pulls the persistent property values of the live modules into the document, dropping "
               "the ones equal to their default. Run it before saving a pipeline that has been tuned "
               "while running.",
               no_arguments_schema(), [&ws](const nlohmann::json&) {
                   const group* root = ws.run_session().live_root();
                   if (root == nullptr) {
                       throw runtime::config_error("nothing is running, so there are no live values to pull");
                   }
                   studio::sync_persistent_properties(ws.project(), ws.project().config(), *root);
                   return nlohmann::json{{"synced", true}};
               }});
}

}  // namespace atp::mcp

#endif
