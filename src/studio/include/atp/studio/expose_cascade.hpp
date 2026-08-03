#ifndef ATP_STUDIO_EXPOSE_CASCADE_HPP
#define ATP_STUDIO_EXPOSE_CASCADE_HPP

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <atp/runtime/config_model.hpp>
#include <atp/studio/node_lookup.hpp>
#include <atp/studio/node_ref.hpp>

/// @file
/// The rule that a group's exported alias is visible from outside only, and everything that follows
/// from it. When an alias disappears or is renamed, the references to it do not live in the group
/// that owned it — they live in the parent, and possibly in the parent's parent, because a re-export
/// there published an alias of its own. That walk is the reason these are not one-liners, and the
/// reason they are here rather than inside project: they are surgery on the model, they take no
/// undo snapshot, and the aggregate calls them between its own bookkeeping.
namespace atp::studio {

/// Checks that a "<child>.<port>" path names something the group actually has. A subgroup's
/// ports are its exported aliases, which the model knows in full, so a path into one is
/// verified here and a reference to a port that is not there can never be recorded. A module's
/// port list lives in the registry, which the project has no access to; for those only the
/// child is checked and the port is left to the studio's type check and, failing that, to the
/// runtime — refusing an edit out of ignorance would be worse.
/// @param input true when the path must name an input, false for an output
/// @throws runtime::config_error if the path is malformed, the child is missing, or a subgroup
///         exports no such port
inline void require_port(runtime::group_node& g,
                         const std::string& group_path,
                         const std::string& port_path,
                         bool input) {
    const std::string child = detail::port_path_child(port_path);
    const runtime::child_node* c = detail::find_child(g, child);
    if (c == nullptr) {
        throw runtime::config_error("no child '" + child + "' in group '" + group_path + "' for '" + port_path + "'");
    }
    if (!c->group) {
        return;
    }
    const std::string port = port_path.substr(child.size() + 1);
    const auto& exported = input ? c->group->expose_inputs : c->group->expose_outputs;
    if (std::ranges::none_of(exported, [&](const auto& e) { return e.first == port; })) {
        throw runtime::config_error("group '" + child + "' exports no " + (input ? "input" : "output") + " '" + port +
                                    "' (path '" + port_path + "' in group '" + group_path + "')");
    }
}

/// Exported aliases of one group, split by direction — what something above may still point at.
struct lost_aliases {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;

    [[nodiscard]] bool empty() const {
        return inputs.empty() && outputs.empty();
    }
    [[nodiscard]] std::size_t size() const {
        return inputs.size() + outputs.size();
    }
};

/// What a cascade removed above the group it started from.
struct cascade_count {
    std::size_t exposes = 0;
    std::size_t connections = 0;
};

/// Drops every export of @p g whose target starts with @p prefix, reporting which aliases went.
[[nodiscard]] inline lost_aliases take_exports_of(runtime::group_node& g, const std::string& prefix) {
    lost_aliases lost;
    std::erase_if(g.expose_inputs, [&](const auto& e) {
        if (!e.second.starts_with(prefix)) {
            return false;
        }
        lost.inputs.push_back(e.first);
        return true;
    });
    std::erase_if(g.expose_outputs, [&](const auto& e) {
        if (!e.second.starts_with(prefix)) {
            return false;
        }
        lost.outputs.push_back(e.first);
        return true;
    });
    return lost;
}

/// Propagates the disappearance of a group's exported aliases upward. A group's alias is
/// visible from outside only, so everything that referenced it sits in the parent: a re-export
/// there loses its target and goes, taking the alias the parent published with it — which is
/// why the walk repeats one level higher — and a connection naming it is dropped as well. The
/// root ends the walk, having no parent that could reference it.
/// @param group_path group whose aliases vanished; "" is the root and nothing is done
/// @param lost aliases that vanished from it
/// @return how many exports and connections the walk removed above that group
inline cascade_count cascade_lost_aliases(runtime::group_node& root, const std::string& group_path, lost_aliases lost) {
    cascade_count removed;
    std::string path = group_path;
    while (!path.empty() && !lost.empty()) {
        const node_ref self = node_ref::parse(path);
        runtime::group_node* parent = detail::find_group(root, self.group);
        if (parent == nullptr) {
            return removed;
        }
        auto names = [&](const std::vector<std::string>& aliases, const std::string& port_path) {
            return std::ranges::any_of(aliases,
                                       [&](const std::string& alias) { return port_path == self.name + "." + alias; });
        };
        const std::size_t connections_before = parent->connections.size();
        std::erase_if(parent->connections, [&](const runtime::connection_node& c) {
            return names(lost.outputs, c.from) || names(lost.inputs, c.to);
        });
        removed.connections += connections_before - parent->connections.size();

        lost_aliases next;
        std::erase_if(parent->expose_inputs, [&](const auto& e) {
            if (!names(lost.inputs, e.second)) {
                return false;
            }
            next.inputs.push_back(e.first);
            return true;
        });
        std::erase_if(parent->expose_outputs, [&](const auto& e) {
            if (!names(lost.outputs, e.second)) {
                return false;
            }
            next.outputs.push_back(e.first);
            return true;
        });
        removed.exposes += next.size();

        path = self.group;
        lost = std::move(next);
    }
    return removed;
}

/// Points the parent's references at an alias's new name: its re-export and the connections
/// naming it. One level is enough — the alias the parent publishes keeps its own name, so
/// nothing changes for anyone above.
inline void rewrite_alias_above(runtime::group_node& root,
                                const std::string& group_path,
                                const std::string& old_alias,
                                const std::string& new_alias,
                                bool input) {
    if (group_path.empty()) {
        return;
    }
    const node_ref self = node_ref::parse(group_path);
    runtime::group_node* parent = detail::find_group(root, self.group);
    if (parent == nullptr) {
        return;
    }
    const std::string was = self.name + "." + old_alias;
    const std::string now = self.name + "." + new_alias;
    for (auto& [alias, path] : input ? parent->expose_inputs : parent->expose_outputs) {
        if (path == was) {
            path = now;
        }
    }
    for (runtime::connection_node& c : parent->connections) {
        std::string& side = input ? c.to : c.from;
        if (side == was) {
            side = now;
        }
    }
}

inline void rewrite_full_path(std::string& path, const std::string& old_full, const std::string& new_full) {
    if (path == old_full) {
        path = new_full;
    } else if (path.starts_with(old_full + ".")) {
        path = new_full + path.substr(old_full.size());
    }
}

}  // namespace atp::studio

#endif
