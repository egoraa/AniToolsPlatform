#ifndef ATP_MCP_CATALOG_TOOLS_HPP
#define ATP_MCP_CATALOG_TOOLS_HPP

#include <filesystem>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/mcp/arguments.hpp>
#include <atp/mcp/module_json.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/module_factory_base.hpp>

namespace atp::mcp {

namespace detail {

/// Renders one known plugin file, loaded or failed alike.
[[nodiscard]] inline nlohmann::json plugin_to_json(const studio::plugin_info& p) {
    nlohmann::json modules = nlohmann::json::array();
    for (const studio::module_info& m : p.modules) {
        modules.push_back(m.name);
    }
    nlohmann::json json{{"path", p.path.string()}, {"loaded", p.loaded}, {"modules", std::move(modules)}};
    if (!p.error.empty()) {
        json["error"] = p.error;
    }
    return json;
}

/// The plugin list in the shape both list_plugins and rescan_plugins return.
[[nodiscard]] inline nlohmann::json plugins_payload(workspace& ws) {
    nlohmann::json plugins = nlohmann::json::array();
    for (const studio::plugin_info& p : ws.modules().plugins()) {
        plugins.push_back(plugin_to_json(p));
    }
    return nlohmann::json{{"plugins", std::move(plugins)}};
}

}  // namespace detail

/// Registers the tools that describe what the host can instantiate.
/// @param tools registry the tools are added to
/// @param ws workspace they operate on; it must outlive the registry
inline void register_catalog_tools(tool_registry& tools, workspace& ws) {
    tools.add({"list_modules",
               "Lists every registered module with its ports and the JSON Schema of each property. "
               "Read this before adding modules to a document.",
               no_arguments_schema(), [&ws](const nlohmann::json&) {
                   nlohmann::json modules = nlohmann::json::array();
                   // list() covers monolithic registrations too, which plugins() would miss.
                   for (const module_factory_base* factory : ws.modules().registry().list()) {
                       modules.push_back(to_json(studio::module_manager::describe(*factory)));
                   }
                   return nlohmann::json{{"modules", std::move(modules)}};
               }});

    tools.add({"list_plugins", "Lists the plugin files the host knows about, loaded and failed alike.",
               no_arguments_schema(), [&ws](const nlohmann::json&) { return detail::plugins_payload(ws); }});

    tools.add({"add_plugin_search_dir", "Adds a directory that rescan_plugins will look in.",
               object_schema({{"path", "string", "Directory, relative to the workspace root"}}),
               [&ws](const nlohmann::json& args) {
                   const std::filesystem::path dir = ws.resolve_plugin(arg_string(args, "path"));
                   ws.modules().add_search_dir(dir);
                   return nlohmann::json{{"path", dir.string()}};
               }});

    tools.add({"rescan_plugins", "Scans the search directories, loading new plugins and retrying failed ones.",
               no_arguments_schema(), [&ws](const nlohmann::json&) {
                   ws.modules().rescan();
                   return detail::plugins_payload(ws);
               }});

    tools.add({"load_plugin", "Loads one plugin file. A refusal is reported in the result, not thrown.",
               object_schema({{"path", "string", "Plugin file, relative to the workspace root"}}),
               [&ws](const nlohmann::json& args) {
                   const std::filesystem::path file = ws.resolve_plugin(arg_string(args, "path"));
                   ws.modules().load_plugin(file);
                   const std::filesystem::path canonical = std::filesystem::weakly_canonical(file);
                   for (const studio::plugin_info& p : ws.modules().plugins()) {
                       if (p.path == canonical) {
                           return detail::plugin_to_json(p);
                       }
                   }
                   throw runtime::config_error("plugin '" + file.string() + "' was not recorded");
               }});
}

}  // namespace atp::mcp

#endif  // ATP_MCP_CATALOG_TOOLS_HPP
