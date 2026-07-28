#ifndef ATP_STUDIO_UI_CREATE_GROUP_HPP
#define ATP_STUDIO_UI_CREATE_GROUP_HPP

#include "app_state.hpp"

#include <exception>
#include <optional>
#include <string>

#include <QString>

#include <atp/studio/add_group.hpp>

namespace atp::studio::ui {

/// The "new group" gesture, shared by the four places that offer it: the canvas context menu, the
/// Edit menu, the breadcrumb button and a drop from the palette. It is a free function rather than
/// a method of the scene because the palette cannot reach the scene, while the state and the
/// callbacks are what all four widgets already hold.
/// @param state application state; the group goes into the group on screen
/// @param callbacks where the refresh and the error message go
/// @param position canvas position of the new node; nullopt leaves it to the auto layout
inline void create_group(app_state& state, ui_callbacks& callbacks, std::optional<node_position> position) {
    if (state.run.running()) {
        return;  // the document structure is read-only while running
    }
    try {
        // Selecting the new group opens the inspector on its name field, which is where it gets
        // renamed — that is why the gesture asks for no name of its own.
        state.selected_child = studio::add_group(state.doc, state.current_group, position);
        callbacks.document_changed();
    } catch (const std::exception& e) {
        callbacks.error(QString::fromStdString(std::string("add group: ") + e.what()));
    }
}

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_CREATE_GROUP_HPP
