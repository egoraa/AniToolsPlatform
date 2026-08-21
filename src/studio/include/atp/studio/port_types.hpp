// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_PORT_TYPES_HPP
#define ATP_STUDIO_PORT_TYPES_HPP

#include <algorithm>
#include <any>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

#include <atp/studio/module_manager.hpp>
#include <atp/studio/project.hpp>
#include <atp/support/type_compare.hpp>
#include <atp/support/version.hpp>

namespace atp::studio {

/// Looks a module description up by factory name and version; nullptr if the factory is not loaded.
/// A callback rather than module_manager directly, because the GUI hands out a cached description —
/// creating a probe instance on every rebuild of the scene would be far too expensive.
using describe_fn = std::function<const module_info*(const std::string&, const std::optional<version>&)>;

/// Resolves the type of a "child.port" path inside a group: a module's port comes from the
/// description, a subgroup's port is followed recursively through its exports down to the real one.
/// @return nullopt if the factory is not loaded or the port does not exist
[[nodiscard]] inline std::optional<std::type_index> resolve_port_type(const runtime::group_node& g,
                                                                      const std::string& port_path,
                                                                      bool output,
                                                                      const describe_fn& describe) {
    const std::size_t dot = port_path.find('.');
    if (dot == std::string::npos) {
        return std::nullopt;
    }
    const std::string child = port_path.substr(0, dot);
    const std::string port = port_path.substr(dot + 1);
    for (const runtime::child_node& c : g.modules) {
        if (c.module && c.module->name == child) {
            const module_info* info = describe(c.module->factory, c.module->factory_version);
            if (info == nullptr) {
                return std::nullopt;
            }
            const auto& list = output ? info->outputs : info->inputs;
            for (const port_info& p : list) {
                if (p.name == port) {
                    return p.type;
                }
            }
            return std::nullopt;
        }
        if (c.group && c.group->name == child) {
            const auto& expose = output ? c.group->expose_outputs : c.group->expose_inputs;
            for (const auto& [alias, path] : expose) {
                if (alias == port) {
                    return resolve_port_type(*c.group, path, output, describe);
                }
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

/// Whether the type is the universal one: an io::input<std::any> takes a value of any type. Decided
/// through same_type rather than ==, because the tag routinely comes from a plugin, where the
/// erased_of<T> statics live per-DLL.
[[nodiscard]] inline bool is_universal(std::type_index type) {
    return same_type(type, typeid(std::any));
}

/// Compatibility rule, the same one the runtime applies in io::input::accepts: an exact type match
/// or the universal input<std::any>. The reverse — output<std::any> into a typed input — is
/// deliberately unsupported.
[[nodiscard]] inline bool types_compatible(std::type_index produced, std::type_index accepted) {
    return same_type(accepted, produced) || is_universal(accepted);
}

/// Connects two ports, checking their types before writing into the project. When the type is
/// unknown on either side (the plugin is not loaded) the check is skipped: forbidding a connection
/// out of ignorance is worse than letting the runtime refuse it at startup.
/// @throws runtime::config_error if the group is missing, the types are incompatible, or
///         project::connect rejects the connection
inline void connect_ports(project& proj,
                          const std::string& group_path,
                          const std::string& from,
                          const std::string& to,
                          const describe_fn& describe) {
    const runtime::group_node* g = proj.group_at(group_path);
    if (g == nullptr) {
        throw runtime::config_error("no group '" + group_path + "'");
    }
    const auto produced = resolve_port_type(*g, from, true, describe);
    const auto accepted = resolve_port_type(*g, to, false, describe);
    if (produced && accepted && !types_compatible(*produced, *accepted)) {
        throw runtime::config_error("incompatible types: output '" + from + "' is " + produced->name() + ", input '" +
                                    to + "' accepts " + accepted->name());
    }
    proj.connect(group_path, from, to);
}

/// Ports of the group's modules that can be exported in the given direction, as "child.port"
/// paths in child order. A module child contributes its declared ports, a subgroup child its own
/// aliases — from outside that is the only way its ports are visible. A child whose factory is not
/// loaded contributes nothing: its port list is unknown, and guessing one is worse than an empty
/// drop-down.
/// @param g group whose modules are looked at
/// @param inputs true for input ports, false for output ports
/// @param describe module description lookup
/// @return the candidate "child.port" paths
[[nodiscard]] inline std::vector<std::string> expose_candidates(const runtime::group_node& g,
                                                                bool inputs,
                                                                const describe_fn& describe) {
    std::vector<std::string> result;
    for (const runtime::child_node& c : g.modules) {
        if (c.module) {
            const runtime::module_node& declared = *c.module;
            const module_info* info = describe(declared.factory, declared.factory_version);
            if (info == nullptr) {
                continue;
            }
            for (const port_info& p : inputs ? info->inputs : info->outputs) {
                result.push_back(declared.name + "." + p.name);
            }
        } else if (c.group) {
            for (const auto& [alias, path] : inputs ? c.group->expose_inputs : c.group->expose_outputs) {
                result.push_back(c.group->name + "." + alias);
            }
        }
    }
    return result;
}

/// Exports a child port out of a group under an automatic alias. The base alias is the port name,
/// with a numeric suffix (_2, _3, …) on a collision in that direction. A port that is already
/// exported in this direction is not duplicated.
/// @return the name the port is visible under from outside
/// @throws runtime::config_error if the group is missing or the path is malformed
[[nodiscard]] inline std::string expose_port(project& proj,
                                             const std::string& group_path,
                                             const std::string& port_path,
                                             bool is_output) {
    const runtime::group_node* g = proj.group_at(group_path);
    if (g == nullptr) {
        throw runtime::config_error("no group '" + group_path + "'");
    }
    const auto& map = is_output ? g->expose_outputs : g->expose_inputs;
    for (const auto& [alias, path] : map) {
        if (path == port_path) {
            return alias;
        }
    }
    const std::size_t dot = port_path.find('.');
    if (dot == std::string::npos || dot + 1 == port_path.size()) {
        throw runtime::config_error("expected '<child>.<port>', got '" + port_path + "'");
    }
    const std::string base = port_path.substr(dot + 1);
    std::string alias = base;
    for (std::size_t n = 2; std::ranges::any_of(map, [&](const auto& e) { return e.first == alias; }); ++n) {
        alias = base + "_" + std::to_string(n);
    }
    if (is_output) {
        proj.set_expose_output(group_path, alias, port_path);
    } else {
        proj.set_expose_input(group_path, alias, port_path);
    }
    return alias;
}

}  // namespace atp::studio

#endif
