// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_NODE_REF_HPP
#define ATP_STUDIO_NODE_REF_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace atp::studio {

/// Address of a node in the project tree: the group holding it and its name inside that group. It
/// replaces the pair of strings the editor used to carry around, and with it the string arithmetic
/// every holder of that pair had to repeat — joining, splitting and asking whether one node sits
/// inside another.
///
/// The two halves are what an editor actually knows: a view shows one group and selects one child of
/// it. A node is therefore addressed the same way from the canvas, the project tree and a drag
/// payload, and the dot-separated form only appears where the project itself is spoken to.
///
/// A node has two equal representations — `{"a", ""}` is the group `a` addressed as itself, `{"", "a"}`
/// is that same group addressed as a child of the root. Both are meaningful editor states (inside the
/// group versus selected in its parent), and `full()` maps them onto the one path the project knows.
struct node_ref {
    /// Dot-separated path of the holding group; empty is the project root.
    std::string group;

    /// Name inside that group; empty addresses the group itself.
    std::string name;

    /// Splits a full path into its holding group and its own name. An empty path is the root.
    [[nodiscard]] static node_ref parse(std::string_view full) {
        const std::size_t dot = full.rfind('.');
        if (dot == std::string_view::npos) {
            return {std::string(), std::string(full)};
        }
        return {std::string(full.substr(0, dot)), std::string(full.substr(dot + 1))};
    }

    /// The path the project addresses this node by: "group.child", or just the name at the root.
    [[nodiscard]] std::string full() const {
        if (name.empty()) {
            return group;
        }
        return group.empty() ? name : group + "." + name;
    }

    /// Whether this addresses the project root, which has no name of its own.
    [[nodiscard]] bool is_root() const {
        return group.empty() && name.empty();
    }

    /// Whether @p group_path is this node itself or a group nested inside it. This is what makes a
    /// move refusable before it is attempted: a group cannot be moved into its own subtree, since
    /// that would cut the subtree out of the tree together with its destination.
    [[nodiscard]] bool contains(std::string_view group_path) const {
        const std::string self = full();
        if (self.empty()) {
            return true;
        }
        if (group_path == self) {
            return true;
        }
        return group_path.size() > self.size() && group_path.starts_with(self) && group_path[self.size()] == '.';
    }

    friend bool operator==(const node_ref&, const node_ref&) = default;
};

}  // namespace atp::studio

#endif
