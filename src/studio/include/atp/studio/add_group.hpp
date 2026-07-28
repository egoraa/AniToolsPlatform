#ifndef ATP_STUDIO_ADD_GROUP_HPP
#define ATP_STUDIO_ADD_GROUP_HPP

#include <optional>
#include <string>

#include <atp/studio/document.hpp>

namespace atp::studio {

/// Adds an empty subgroup under a generated name: "group", or "group_2" and on when the name is
/// taken. There is no request structure like add_module's: a group brings no plugin with it, so the
/// whole operation is a path and a position.
/// @param doc document to edit
/// @param group_path group to add to ("" is the root)
/// @param position canvas position of the new node; nullopt leaves it to the auto layout
/// @return the name the group was actually added under
/// @throws runtime::config_error if there is no group at @p group_path
inline std::string add_group(document& doc, const std::string& group_path, std::optional<node_position> position = {}) {
    const std::string name = detail::unique_child_name(doc.group_at(group_path), "group");
    doc.add_group(group_path, name);
    if (position) {
        doc.set_position(detail::full_path(group_path, name), *position);
    }
    return name;
}

}  // namespace atp::studio

#endif  // ATP_STUDIO_ADD_GROUP_HPP
