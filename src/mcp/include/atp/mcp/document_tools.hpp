// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_MCP_DOCUMENT_TOOLS_HPP
#define ATP_MCP_DOCUMENT_TOOLS_HPP

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/mcp/arguments.hpp>
#include <atp/mcp/tool_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/module_factory_base.hpp>
#include <atp/studio/layout.hpp>
#include <atp/studio/port_types.hpp>
#include <atp/version.hpp>

namespace atp::mcp {

namespace detail {

/// Builds the lookup port_types needs. A probe instance per call would be wasteful when a single
/// connect resolves two ports, so the descriptions are memoised for the lifetime of the callback.
[[nodiscard]] inline studio::describe_fn make_describe(workspace& ws) {
    auto cache = std::make_shared<std::map<std::pair<std::string, std::string>, studio::module_info>>();
    return [&ws, cache](const std::string& factory, const std::optional<version>& ver) -> const studio::module_info* {
        const std::pair<std::string, std::string> key{factory, ver ? ver->to_string() : std::string{}};
        const auto it = cache->find(key);
        if (it != cache->end()) {
            return &it->second;
        }
        const module_factory_base* f =
            ver ? ws.modules().registry().find(factory, *ver) : ws.modules().registry().find(factory);
        if (f == nullptr) {
            return nullptr;
        }
        return &cache->emplace(key, studio::module_manager::describe(*f)).first->second;
    };
}

/// Reads the optional "version" argument of add_module.
/// @throws runtime::config_error if the text is present but unparsable
[[nodiscard]] inline std::optional<version> arg_version(const nlohmann::json& args) {
    const std::string text = arg_string_or(args, "version", "");
    if (text.empty()) {
        return std::nullopt;
    }
    const std::optional<version> parsed = try_parse_version(text);
    if (!parsed) {
        throw runtime::config_error("version '" + text + "' is not a dotted number like '1.2.3'");
    }
    return parsed;
}

}  // namespace detail

/// Registers the tools that edit the document's structure and move it to and from disk.
/// @param tools registry the tools are added to
/// @param ws workspace they operate on; it must outlive the registry
inline void register_document_tools(tool_registry& tools, workspace& ws) {
    tools.add({"new_document", "Discards the current document and starts an empty one.", no_arguments_schema(),
               [&ws](const nlohmann::json&) {
                   ws.reset_document();
                   return nlohmann::json{{"reset", true}};
               }});

    tools.add({"open_document", "Opens a config together with its *.layout.json sidecar.",
               object_schema({{"path", "string", "Config file, relative to the workspace root"}}),
               [&ws](const nlohmann::json& args) {
                   const std::filesystem::path file = ws.resolve(arg_string(args, "path"));
                   ws.open_document(file);
                   return nlohmann::json{{"opened", file.string()}, {"had_includes", ws.project().had_includes()}};
               }});

    tools.add({"save_document", "Writes the config and, next to it, the *.layout.json sidecar holding node positions.",
               object_schema({{"path", "string", "Config file, relative to the workspace root"}}),
               [&ws](const nlohmann::json& args) {
                   const std::filesystem::path file = ws.resolve(arg_string(args, "path"));
                   ws.save_document(file);
                   std::filesystem::path layout = file;
                   layout.replace_extension(".layout.json");
                   return nlohmann::json{{"config", file.string()}, {"layout", layout.string()}};
               }});

    tools.add({"get_document",
               "Returns the whole config as JSON. Cheaper for you to read once than to reassemble "
               "the state from individual queries.",
               no_arguments_schema(), [&ws](const nlohmann::json&) {
                   return nlohmann::json{{"document", runtime::encode(ws.project().config())}};
               }});

    tools.add({"add_module", "Adds a module to a group.",
               object_schema({{"group_path", "string", "Group to add to; \"\" is the root"},
                              {"factory", "string", "Factory name from list_modules"},
                              {"name", "string", "Child name; defaults to the factory name", false},
                              {"version", "string", "Factory version; defaults to the latest", false}}),
               [&ws](const nlohmann::json& args) {
                   const std::string factory = arg_string(args, "factory");
                   const std::string name = arg_string_or(args, "name", "");
                   ws.project().add_module(arg_string(args, "group_path"), factory, name, detail::arg_version(args));
                   return nlohmann::json{{"added", name.empty() ? factory : name}};
               }});

    tools.add({"add_group", "Adds an empty subgroup to a group.",
               object_schema(
                   {{"group_path", "string", "Parent group; \"\" is the root"}, {"name", "string", "Subgroup name"}}),
               [&ws](const nlohmann::json& args) {
                   const std::string name = arg_string(args, "name");
                   ws.project().add_group(arg_string(args, "group_path"), name);
                   return nlohmann::json{{"added", name}};
               }});

    tools.add({"remove_child",
               "Removes a module or subgroup together with everything that referenced it: "
               "connections, exported ports, thread assignments and canvas positions.",
               object_schema({{"group_path", "string", "Group holding the child; \"\" is the root"},
                              {"name", "string", "Child name"}}),
               [&ws](const nlohmann::json& args) {
                   ws.project().remove_child(arg_string(args, "group_path"), arg_string(args, "name"));
                   return nlohmann::json{{"removed", true}};
               }});

    tools.add({"rename_child", "Renames a child, rewriting every path that referenced it.",
               object_schema({{"group_path", "string", "Group holding the child; \"\" is the root"},
                              {"old_name", "string", "Current name"},
                              {"new_name", "string", "New name"}}),
               [&ws](const nlohmann::json& args) {
                   ws.project().rename_child(arg_string(args, "group_path"), arg_string(args, "old_name"),
                                             arg_string(args, "new_name"));
                   return nlohmann::json{{"renamed", true}};
               }});

    tools.add({"move_child",
               "Moves a module or subgroup into another group. Connections and exported ports of "
               "the source group that referenced it are dropped; thread assignments and canvas "
               "positions of the moved subtree follow it. The name gets a numeric suffix if it is "
               "taken in the target group.",
               object_schema({{"from_group", "string", "Group currently holding the child; \"\" is the root"},
                              {"name", "string", "Child name"},
                              {"to_group", "string", "Group to move it into; \"\" is the root"}}),
               [&ws](const nlohmann::json& args) {
                   const studio::move_result result = ws.project().move_child(
                       arg_string(args, "from_group"), arg_string(args, "name"), arg_string(args, "to_group"));
                   return nlohmann::json{{"moved", result.new_name},
                                         {"dropped_connections", result.dropped_connections},
                                         {"dropped_exposes", result.dropped_exposes}};
               }});

    tools.add({"connect",
               "Connects two 'child.port' paths inside a group. Port types are checked before the "
               "edit is recorded.",
               object_schema({{"group_path", "string", "Group holding both children; \"\" is the root"},
                              {"from", "string", "Source port, as 'child.port'"},
                              {"to", "string", "Destination port, as 'child.port'"}}),
               [&ws](const nlohmann::json& args) {
                   studio::connect_ports(ws.project(), arg_string(args, "group_path"), arg_string(args, "from"),
                                         arg_string(args, "to"), detail::make_describe(ws));
                   return nlohmann::json{{"connected", true}};
               }});

    tools.add({"disconnect", "Removes a connection by its index in the group's connection list.",
               object_schema({{"group_path", "string", "Group holding the connection; \"\" is the root"},
                              {"index", "integer", "Index in the group's connection list"}}),
               [&ws](const nlohmann::json& args) {
                   ws.project().disconnect(arg_string(args, "group_path"), arg_index(args, "index"));
                   return nlohmann::json{{"disconnected", true}};
               }});

    tools.add({"expose_port",
               "Exports a child port out of a group under an automatic alias, which is what makes it "
               "reachable from the parent.",
               object_schema({{"group_path", "string", "Group to export out of"},
                              {"port_path", "string", "Port to export, as 'child.port'"},
                              {"output", "boolean", "True for an output port, false for an input"}}),
               [&ws](const nlohmann::json& args) {
                   const std::string alias =
                       studio::expose_port(ws.project(), arg_string(args, "group_path"), arg_string(args, "port_path"),
                                           arg_bool_or(args, "output", true));
                   return nlohmann::json{{"alias", alias}};
               }});

    tools.add(
        {"remove_expose_input",
         "Removes an exported input alias from a group, and with it every re-export and connection "
         "above that named it. Use rename_expose_input to change an alias without losing them.",
         object_schema({{"group_path", "string", "Group holding the alias"}, {"alias", "string", "Alias to remove"}}),
         [&ws](const nlohmann::json& args) {
             ws.project().remove_expose_input(arg_string(args, "group_path"), arg_string(args, "alias"));
             return nlohmann::json{{"removed", true}};
         }});

    tools.add(
        {"remove_expose_output",
         "Removes an exported output alias from a group, and with it every re-export and connection "
         "above that named it. Use rename_expose_output to change an alias without losing them.",
         object_schema({{"group_path", "string", "Group holding the alias"}, {"alias", "string", "Alias to remove"}}),
         [&ws](const nlohmann::json& args) {
             ws.project().remove_expose_output(arg_string(args, "group_path"), arg_string(args, "alias"));
             return nlohmann::json{{"removed", true}};
         }});

    tools.add({"rename_expose_input",
               "Renames an exported input alias, pointing the parent's reference at the new name. "
               "Unlike a remove-then-expose pair this keeps the re-export above and the connections "
               "naming it.",
               object_schema({{"group_path", "string", "Group holding the alias"},
                              {"alias", "string", "Current alias"},
                              {"new_alias", "string", "New alias"}}),
               [&ws](const nlohmann::json& args) {
                   ws.project().rename_expose_input(arg_string(args, "group_path"), arg_string(args, "alias"),
                                                    arg_string(args, "new_alias"));
                   return nlohmann::json{{"renamed", true}};
               }});

    tools.add({"rename_expose_output",
               "Renames an exported output alias, pointing the parent's reference at the new name. "
               "Unlike a remove-then-expose pair this keeps the re-export above and the connections "
               "naming it.",
               object_schema({{"group_path", "string", "Group holding the alias"},
                              {"alias", "string", "Current alias"},
                              {"new_alias", "string", "New alias"}}),
               [&ws](const nlohmann::json& args) {
                   ws.project().rename_expose_output(arg_string(args, "group_path"), arg_string(args, "alias"),
                                                     arg_string(args, "new_alias"));
                   return nlohmann::json{{"renamed", true}};
               }});

    tools.add({"auto_layout", "Recomputes canvas positions for one level of a group.",
               object_schema({{"group_path", "string", "Group to lay out; \"\" is the root"}}),
               [&ws](const nlohmann::json& args) {
                   const std::string group_path = arg_string(args, "group_path");
                   const runtime::group_node* g = ws.project().group_at(group_path);
                   if (g == nullptr) {
                       throw runtime::config_error("no group at path '" + group_path + "'");
                   }
                   std::size_t placed = 0;
                   for (const auto& [child, position] : studio::auto_layout(*g)) {
                       ws.project().set_position(studio::node_ref{group_path, child}.full(), position);
                       ++placed;
                   }
                   return nlohmann::json{{"placed", placed}};
               }});

    tools.add({"undo", "Reverts the last editing operation.", no_arguments_schema(),
               [&ws](const nlohmann::json&) { return nlohmann::json{{"undone", ws.project().undo()}}; }});

    tools.add({"redo", "Reapplies the last undone operation.", no_arguments_schema(),
               [&ws](const nlohmann::json&) { return nlohmann::json{{"redone", ws.project().redo()}}; }});
}

}  // namespace atp::mcp

#endif
