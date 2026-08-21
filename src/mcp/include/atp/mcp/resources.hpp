// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_MCP_RESOURCES_HPP
#define ATP_MCP_RESOURCES_HPP

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <atp/hosting/module_factory_base.hpp>
#include <atp/mcp/module_json.hpp>
#include <atp/mcp/resource_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/runtime/json_codec.hpp>

namespace atp::mcp {

/// Registers the read-only views of the workspace. They carry the same content as the corresponding
/// tools, but a client may prefer to attach a resource once instead of calling a tool per turn.
/// @param resources registry the resources are added to
/// @param ws workspace they read; it must outlive the registry
inline void register_resources(resource_registry& resources, workspace& ws) {
    resources.add({"atp://document", "Current pipeline config", "application/json",
                   [&ws] { return runtime::json_dump(runtime::encode(ws.project().config()), 2); }});

    resources.add({"atp://modules", "Catalog of registered modules", "application/json", [&ws] {
                       nlohmann::json modules = nlohmann::json::array();
                       for (const module_factory_base* factory : ws.modules().registry().list()) {
                           modules.push_back(to_json(studio::module_manager::describe(*factory)));
                       }
                       return modules.dump(2);
                   }});

    resources.add({"atp://docs/architecture", "Platform architecture digest", "text/markdown", [&ws] {
                       const std::filesystem::path file = ws.root() / "docs" / "architecture.md";
                       std::ifstream in(file);
                       if (!in) {
                           return std::string("docs/architecture.md is not available in this workspace");
                       }
                       std::stringstream body;
                       body << in.rdbuf();
                       return body.str();
                   }});
}

}  // namespace atp::mcp

#endif
