#ifndef ATP_STUDIO_LAYOUT_HPP
#define ATP_STUDIO_LAYOUT_HPP

#include <cstddef>
#include <string>
#include <unordered_map>

#include <atp/runtime/config_model.hpp>
#include <atp/studio/document.hpp>

namespace atp::studio {

/// Grid step of the auto layout, sized for a typical node.
inline constexpr float layout_column_width = 260.0f;
inline constexpr float layout_row_height = 140.0f;

/// Lays out one level of a group, which is what the canvas shows at a time: a child's layer is the
/// longest path along the connections from the sources, the column is the layer and the row is the
/// ordinal within it.
/// @return positions keyed by child name
[[nodiscard]] inline std::unordered_map<std::string, node_position> auto_layout(const runtime::group_node& g) {
    std::unordered_map<std::string, std::size_t> layer;
    for (const runtime::child_node& c : g.modules) {
        layer[c.module ? c.module->name : c.group->name] = 0;
    }
    // The pass count is capped by the size of the graph, so a cycle in the connections cannot spin
    // the layout forever, and a draft view needs no honest topological sort.
    for (std::size_t pass = 0; pass < g.modules.size(); ++pass) {
        bool changed = false;
        for (const runtime::connection_node& c : g.connections) {
            const std::string from = c.from.substr(0, c.from.find('.'));
            const std::string to = c.to.substr(0, c.to.find('.'));
            auto f = layer.find(from);
            auto t = layer.find(to);
            if (f != layer.end() && t != layer.end() && t->second < f->second + 1 &&
                f->second + 1 < g.modules.size() + 1) {
                t->second = f->second + 1;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    std::unordered_map<std::size_t, std::size_t> row;
    std::unordered_map<std::string, node_position> out;
    for (const runtime::child_node& c : g.modules) {
        const std::string& name = c.module ? c.module->name : c.group->name;
        const std::size_t l = layer[name];
        out[name] = {static_cast<float>(l) * layout_column_width, static_cast<float>(row[l]++) * layout_row_height};
    }
    return out;
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_LAYOUT_HPP
