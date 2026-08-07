// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_NODE_LOOKUP_HPP
#define ATP_STUDIO_NODE_LOOKUP_HPP

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <atp/runtime/config_model.hpp>

/// @file
/// Navigation and naming over the config tree: everything an editing operation needs to *find* a
/// node or decide what to call it, and nothing that changes the project. These were the first half
/// of project.hpp; they are here because they answer questions about the model rather than edit it,
/// which is also why add_group, add_module and the canvas call them directly.
namespace atp::studio::detail {

/// Group at a dotted path, "" being the root; nullptr if some segment names no subgroup.
[[nodiscard]] inline const runtime::group_node* find_group(const runtime::group_node& root, const std::string& path) {
    const runtime::group_node* current = &root;
    std::size_t begin = 0;
    while (!path.empty()) {
        const std::size_t dot = path.find('.', begin);
        const std::string segment = path.substr(begin, dot == std::string::npos ? dot : dot - begin);
        const runtime::group_node* next = nullptr;
        for (const runtime::child_node& c : current->modules) {
            if (c.group && c.group->name == segment) {
                next = c.group.get();
                break;
            }
        }
        if (next == nullptr) {
            return nullptr;
        }
        current = next;
        if (dot == std::string::npos) {
            break;
        }
        begin = dot + 1;
    }
    return current;
}

/// Mutable overload; the const one holds the logic and this one casts the result back, so the two
/// cannot drift apart.
[[nodiscard]] inline runtime::group_node* find_group(runtime::group_node& root, const std::string& path) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<runtime::group_node*>(find_group(std::as_const(root), path));
}

/// Rejects a name that would break path addressing: a dot is the separator, so a name carrying one
/// would produce a path nothing can resolve back to this node.
/// @throws runtime::config_error if the name is empty or contains '.'
inline void check_name(const std::string& name, const char* what) {
    if (name.empty() || name.contains('.')) {
        throw runtime::config_error(std::string(what) + " '" + name + "' must be non-empty and contain no '.'");
    }
}

/// Child part of a "<child>.<port>" path.
/// @throws runtime::config_error if the path is not exactly two non-empty segments
[[nodiscard]] inline std::string port_path_child(const std::string& path) {
    const std::size_t dot = path.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == path.size() ||
        path.find('.', dot + 1) != std::string::npos) {
        throw runtime::config_error("expected '<child>.<port>', got '" + path + "'");
    }
    return path.substr(0, dot);
}

/// Name of a child, whichever of the two kinds it is.
[[nodiscard]] inline const std::string& child_name(const runtime::child_node& c) {
    return c.module ? c.module->name : c.group->name;
}

/// Child of @p g under @p name; nullptr if there is none.
[[nodiscard]] inline runtime::child_node* find_child(runtime::group_node& g, const std::string& name) {
    for (runtime::child_node& c : g.modules) {
        if (child_name(c) == name) {
            return &c;
        }
    }
    return nullptr;
}

/// Deep copy of a child. A subgroup is owned through a unique_ptr, so there is no implicit copy and
/// the recursion is spelled out; a module is a plain value and copies itself. The subgroup's own
/// connections and exported ports travel with it because they name only things inside the subtree,
/// which the copy reproduces whole.
/// @param c child to clone
/// @return an independent child holding no reference to the original
[[nodiscard]] inline runtime::child_node clone_child(const runtime::child_node& c) {
    runtime::child_node copy;
    if (c.module) {
        copy.module = c.module;
        return copy;
    }
    copy.group = std::make_unique<runtime::group_node>();
    copy.group->name = c.group->name;
    copy.group->expose_inputs = c.group->expose_inputs;
    copy.group->expose_outputs = c.group->expose_outputs;
    copy.group->connections = c.group->connections;
    for (const runtime::child_node& child : c.group->modules) {
        copy.group->modules.push_back(clone_child(child));
    }
    return copy;
}

/// Name for a new child of @p g: @p base, with a numeric suffix if the name is taken. A null group
/// yields the base name — reporting a missing group is the caller's operation, not the name's.
[[nodiscard]] inline std::string unique_child_name(const runtime::group_node* g, const std::string& base) {
    auto taken = [&](const std::string& name) {
        return g != nullptr &&
               std::ranges::any_of(g->modules, [&](const runtime::child_node& c) { return child_name(c) == name; });
    };
    if (!taken(base)) {
        return base;
    }
    for (int i = 2;; ++i) {
        const std::string candidate = base + "_" + std::to_string(i);
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

/// Retargets a "<child>.<port>" path when the child is renamed; leaves other paths alone.
inline void rewrite_port_prefix(std::string& port_path, const std::string& old_name, const std::string& new_name) {
    if (port_path.starts_with(old_name + ".")) {
        port_path = new_name + port_path.substr(old_name.size());
    }
}

}  // namespace atp::studio::detail

#endif
