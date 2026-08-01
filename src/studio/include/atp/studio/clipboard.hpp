#ifndef ATP_STUDIO_CLIPBOARD_HPP
#define ATP_STUDIO_CLIPBOARD_HPP

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <atp/runtime/config_model.hpp>
#include <atp/studio/node_position.hpp>

namespace atp::studio {

/// One node in the clipboard: a detached copy of a child together with what described it from the
/// outside. The keys in `subtree_positions` and `assignments` are paths relative to the node itself
/// — an empty key is the node, "inner.leaf" a descendant — so a snapshot does not remember where it
/// was taken and can be pasted anywhere.
struct clip_node {
    /// Owning copy of the child, subgroup and all.
    runtime::child_node node;

    /// Canvas position of the node; absent if it never had one.
    std::optional<node_position> position;

    /// Canvas positions of everything under it.
    std::map<std::string, node_position> subtree_positions;

    /// Thread assignments of the node and its subtree, as (relative path, thread name).
    std::vector<std::pair<std::string, std::string>> assignments;
};

/// Detached copies of a selection: what a copy or a cut produced and what a paste consumes. Holding
/// values rather than paths into the project is what makes cut possible at all — the source is gone
/// by the time the paste happens — and it also survives undo, renaming and opening another project.
/// Move-only, since a child_node owns its subgroup through a unique_ptr.
struct clipboard {
    /// Copied children, in the order they sat in the source group.
    std::vector<clip_node> nodes;

    /// Connections that ran between the copied children, still in the source names; a paste
    /// rewrites the prefixes. A connection with one end outside the selection is not here.
    std::vector<runtime::connection_node> connections;

    /// Whether there is anything to paste.
    [[nodiscard]] bool empty() const {
        return nodes.empty();
    }
};

/// Settings lifted off one module, to be handed to another. Only the values the project states
/// explicitly are taken — a property left at its default is not a setting the user chose, and
/// carrying it would write that default into every module it is pasted onto.
struct property_clip {
    /// Factory the values came from, so the report can name it. Applying across factories is
    /// allowed: what matches is matched by name.
    std::string factory;

    /// The values, in the order the source module listed them.
    std::vector<std::pair<std::string, nlohmann::json>> values;

    /// Whether there is anything to apply.
    [[nodiscard]] bool empty() const {
        return values.empty();
    }
};

}  // namespace atp::studio

#endif
