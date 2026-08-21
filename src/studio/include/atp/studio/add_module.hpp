// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_ADD_MODULE_HPP
#define ATP_STUDIO_ADD_MODULE_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include <atp/studio/project.hpp>
#include <atp/support/version.hpp>

namespace atp::studio {

/// Request to add a module to a group. A structure rather than an argument list, because adding has
/// two callers (a double click and a drop onto the canvas) that differ in exactly one field.
struct add_module_request {
    std::string group_path;
    std::string factory;
    std::optional<version> factory_version;
    std::filesystem::path plugin;
    std::filesystem::path config_dir;
    std::optional<node_position> position;
};

/// Outcome of adding a module.
struct add_module_result {
    std::string name;
    std::string warning;
};

/// Adds a module together with everything that comes with it: the DLL entry in the config's plugin
/// list (without which the module would not start) and, when given, the node position.
/// @throws runtime::config_error under the same conditions as project::add_module
inline add_module_result add_module(project& proj, const add_module_request& request) {
    add_module_result result;
    result.name = detail::unique_child_name(proj.group_at(request.group_path), request.factory);
    proj.add_module(request.group_path, request.factory, result.name, request.factory_version);

    std::error_code ec;
    const std::filesystem::path relative = request.config_dir.empty()
                                               ? std::filesystem::path{}
                                               : std::filesystem::relative(request.plugin, request.config_dir, ec);
    std::string entry;
    if (!ec && !relative.empty()) {
        entry = relative.generic_string();
    } else {
        entry = request.plugin.generic_string();
        result.warning = "plugin path is absolute: " + entry;
    }
    proj.add_plugin(entry);

    if (request.position) {
        proj.set_position(node_ref{request.group_path, result.name}.full(), *request.position);
    }
    return result;
}

}  // namespace atp::studio

#endif
