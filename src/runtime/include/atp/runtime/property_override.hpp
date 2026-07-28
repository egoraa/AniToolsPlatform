#ifndef ATP_RUNTIME_PROPERTY_OVERRIDE_HPP
#define ATP_RUNTIME_PROPERTY_OVERRIDE_HPP

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include <atp/group.hpp>
#include <atp/runtime/config_model.hpp>

namespace atp::runtime {

/// An edit of one property addressed by its path in the group tree; the sources are the -p flag of
/// atp_app ("group.module.prop=value") and studio's on-the-fly edits.
struct property_override {
    std::string module_path;  // module path in the group tree, segments separated by '.'
    std::string name;         // property name
    std::string value;        // string value, parsed by the property itself
};

/// Parses "path.prop=value": splitting on the FIRST '=' (a value may contain one) and then, to the
/// left of it, on the LAST '.' (a property name cannot contain a dot, a path can).
/// @throws config_error on a malformed argument, quoting the original string
[[nodiscard]] inline property_override parse_property_override(std::string_view arg) {
    const std::size_t eq = arg.find('=');
    if (eq == std::string_view::npos) {
        throw config_error("property override '" + std::string(arg) + "': expected 'path.prop=value'");
    }
    const std::string_view left = arg.substr(0, eq);
    const std::size_t dot = left.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == left.size()) {
        throw config_error("property override '" + std::string(arg) + "': expected 'path.prop=value'");
    }
    return {std::string(left.substr(0, dot)), std::string(left.substr(dot + 1)), std::string(arg.substr(eq + 1))};
}

/// Descends the tree: every path segment but the last names a group, the last one names a module.
/// Shared by writing (apply_property_override) and reading (the inspector).
/// @throws config_error naming the full path whenever a group, a module or the property is missing
[[nodiscard]] inline io::property_base& find_property(group& root,
                                                      std::string_view module_path,
                                                      const std::string& name) {
    group* current = &root;
    std::size_t begin = 0;
    while (true) {
        const std::size_t dot = module_path.find('.', begin);
        if (dot == std::string_view::npos) {
            break;
        }
        const std::string segment(module_path.substr(begin, dot - begin));
        group* next = current->find_group(segment);
        if (next == nullptr) {
            throw config_error("property override: no group '" + segment + "' in path '" + std::string(module_path) +
                               "'");
        }
        current = next;
        begin = dot + 1;
    }
    const std::string module_name(module_path.substr(begin));
    module_base* m = current->find_module(module_name);
    if (m == nullptr) {
        throw config_error("property override: no module at path '" + std::string(module_path) + "'");
    }
    io::property_base* prop = m->properties().find(name);
    if (prop == nullptr) {
        throw config_error("property override: module '" + std::string(module_path) + "' has no property '" + name +
                           "'");
    }
    return *prop;
}

/// Applies an override to the live module tree.
/// @throws config_error if the path does not resolve, or the value does not parse and is therefore
///         a configuration error too, not a logic one
inline void apply_property_override(group& root, const property_override& o) {
    io::property_base& prop = find_property(root, o.module_path, o.name);
    try {
        prop.from_string(o.value);
    } catch (const std::invalid_argument& e) {
        throw config_error(std::string("property override: ") + e.what());
    }
}

}  // namespace atp::runtime

#endif  // ATP_RUNTIME_PROPERTY_OVERRIDE_HPP
