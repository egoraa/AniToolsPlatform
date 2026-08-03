#ifndef ATP_STUDIO_UI_CLIPBOARD_ACTIONS_HPP
#define ATP_STUDIO_UI_CLIPBOARD_ACTIONS_HPP

#include "model/app_state.hpp"

#include <exception>
#include <optional>
#include <string>
#include <vector>

#include <QString>

#include <atp/studio/clipboard.hpp>
#include <atp/studio/node_position.hpp>

namespace atp::studio::ui {

/// Fills the clipboard from a selection, reporting a failure through the callbacks. The shared step
/// of copy and cut; on its own it says nothing on success, since the two gestures word it
/// differently.
/// @param verb what the user asked for, so a failure is reported as the gesture they made
/// @return true if the clipboard now holds the selection
inline bool take_nodes(app_state& state,
                       ui_callbacks& callbacks,
                       const std::string& group,
                       const std::vector<std::string>& names,
                       const char* verb) {
    if (state.view->running() || names.empty()) {
        return false;
    }
    try {
        state.clip = state.doc.copy_children(group, names);
    } catch (const std::exception& e) {
        callbacks.error(QString::fromStdString(std::string(verb) + ": " + e.what()));
        return false;
    }
    return true;
}

/// Copies the named children of a group into the clipboard, leaving the project alone.
/// @return true if the clipboard was filled
inline bool copy_nodes(app_state& state,
                       ui_callbacks& callbacks,
                       const std::string& group,
                       const std::vector<std::string>& names) {
    if (!take_nodes(state, callbacks, group, names, "copy")) {
        return false;
    }
    callbacks.error(QString("copied %1 node(s)").arg(state.clip.nodes.size()));
    return true;
}

/// Copies the named children into the clipboard and removes them, as one undo step.
/// @return true if the project changed, which is when the caller owes a refresh
inline bool cut_nodes(app_state& state,
                      ui_callbacks& callbacks,
                      const std::string& group,
                      const std::vector<std::string>& names) {
    if (!take_nodes(state, callbacks, group, names, "cut")) {
        return false;
    }
    try {
        state.doc.remove_children(group, names);
    } catch (const std::exception& e) {
        callbacks.error(QString::fromStdString(std::string("cut: ") + e.what()));
        return false;
    }
    callbacks.error(QString("cut %1 node(s)").arg(names.size()));
    state.selected_child.clear();
    return true;
}

/// Pastes the clipboard into a group.
/// @param at position for the top-left of the pasted block; nullopt keeps the stored positions
/// @return the names the pasted nodes got, empty if nothing was pasted
inline std::vector<std::string> paste_nodes(app_state& state,
                                            ui_callbacks& callbacks,
                                            const std::string& group,
                                            std::optional<node_position> at) {
    if (state.view->running() || state.clip.empty()) {
        return {};
    }
    std::vector<std::string> made;
    try {
        made = state.doc.paste(group, state.clip, at);
    } catch (const std::exception& e) {
        callbacks.error(QString::fromStdString(std::string("paste: ") + e.what()));
        return {};
    }
    callbacks.error(QString("pasted %1 node(s)").arg(made.size()));
    return made;
}

}  // namespace atp::studio::ui

#endif
