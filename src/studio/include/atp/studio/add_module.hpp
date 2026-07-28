#ifndef ATP_STUDIO_ADD_MODULE_HPP
#define ATP_STUDIO_ADD_MODULE_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include <atp/studio/document.hpp>
#include <atp/version.hpp>

namespace atp::studio {

/// Request to add a module to a group. A structure rather than an argument list, because adding has
/// two callers (a double click and a drop onto the canvas) that differ in exactly one field.
struct add_module_request {
    std::string group_path;                  // group to add to
    std::string factory;                     // factory name
    std::optional<version> factory_version;  // nullopt means the latest
    std::filesystem::path plugin;            // DLL the factory came from
    std::filesystem::path config_dir;        // document directory; empty if it has not been saved
    std::optional<node_position> position;   // nullopt leaves the position to the auto layout
};

/// Outcome of adding a module.
struct add_module_result {
    std::string name;     // actual node name, suffixed on a collision
    std::string warning;  // non-empty when the plugin path had to stay absolute
};

/// Adds a module together with everything that comes with it: the DLL entry in the config's plugin
/// list (without which the module would not start) and, when given, the node position.
/// @throws runtime::config_error under the same conditions as document::add_module
inline add_module_result add_module(document& doc, const add_module_request& request) {
    add_module_result result;
    result.name = detail::unique_child_name(doc.group_at(request.group_path), request.factory);
    doc.add_module(request.group_path, request.factory, result.name, request.factory_version);

    // The plugin path is made relative where possible, so the document stays portable. relative()
    // can also climb up through "..", and returns empty only when the base is empty (the document
    // has not been saved) or the paths are incompatible (different roots on Windows) — which is
    // exactly when the warning is issued.
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(request.plugin, request.config_dir, ec);
    std::string entry;
    if (!ec && !relative.empty()) {
        entry = relative.generic_string();
    } else {
        entry = request.plugin.generic_string();
        result.warning = "plugin path is absolute: " + entry;
    }
    doc.add_plugin(entry);

    if (request.position) {
        doc.set_position(detail::full_path(request.group_path, result.name), *request.position);
    }
    return result;
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_ADD_MODULE_HPP
