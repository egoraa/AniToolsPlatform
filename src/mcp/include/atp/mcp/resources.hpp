// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_MCP_RESOURCES_HPP
#define ATP_MCP_RESOURCES_HPP

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/hosting/module_factory_base.hpp>
#include <atp/mcp/module_json.hpp>
#include <atp/mcp/resource_registry.hpp>
#include <atp/mcp/workspace.hpp>
#include <atp/runtime/json_codec.hpp>

namespace atp::mcp {

/// Registers the read-only views of the workspace. They carry the same content as the corresponding
/// tools, but a client may prefer to attach a resource once instead of calling a tool per turn.
///
/// The architecture digest is one resource over many files: docs/architecture.md is a hub whose
/// chapters live in docs/architecture/, and a client asking for the digest wants the whole of it,
/// not the hub's table of contents. The chapters are appended in file-name order and each carries
/// its own heading, so the concatenation stays readable and a chapter added later needs no change
/// here. A missing chapter directory is not an error: the hub alone is still the digest.
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
                       std::stringstream body;
                       const auto append = [&body](const std::filesystem::path& file) {
                           std::ifstream in(file);
                           if (!in) {
                               return false;
                           }
                           body << in.rdbuf() << '\n';
                           return true;
                       };
                       if (!append(ws.root() / "docs" / "architecture.md")) {
                           return std::string("docs/architecture.md is not available in this workspace");
                       }
                       std::vector<std::filesystem::path> chapters;
                       std::error_code ec;
                       for (const std::filesystem::directory_entry& entry :
                            std::filesystem::directory_iterator(ws.root() / "docs" / "architecture", ec)) {
                           if (entry.path().extension() == ".md") {
                               chapters.push_back(entry.path());
                           }
                       }
                       std::ranges::sort(chapters);
                       for (const std::filesystem::path& chapter : chapters) {
                           (void)append(chapter);
                       }
                       return body.str();
                   }});
}

}  // namespace atp::mcp

#endif
