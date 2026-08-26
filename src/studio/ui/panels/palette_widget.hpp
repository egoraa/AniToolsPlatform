// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_PALETTE_WIDGET_HPP
#define ATP_STUDIO_UI_PALETTE_WIDGET_HPP

#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

#include <QLineEdit>
#include <QString>
#include <QTreeWidget>
#include <QWidget>

namespace atp::studio::ui {

/// The tree of the palette. Declared here only so that the panel can hold one; it is a QTreeWidget
/// heir because dragging a module onto the canvas is done by overriding mimeTypes and mimeData, and
/// that has to be the view itself.
class palette_tree;

/// Palette of the loaded modules, grouped by plugin, with a filter above it. Modules are added by a
/// double click or by dragging onto the canvas.
///
/// It shows the same modules as the Plugins dock and is **not** the same panel, which is worth
/// saying because merging the two looks tempting from a screenshot. The two answer different
/// questions: this one is "what can I put on the canvas", the other is "what loaded, and why did the
/// rest not". Only the second carries the search directories, the load status and the menus that
/// copy a plugin's path or open a script — none of which belong on a palette, and all of which would
/// have been lost in a merge. What the palette was actually missing is a way to find a module in a
/// list of a hundred, which is what the filter is.
class palette_widget final : public QWidget {
   public:
    palette_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the tree from the loaded plugins and re-applies whatever the filter says, so a
    /// rescan does not quietly widen what is on screen.
    void refresh();

    /// Hides every module whose name does not contain the text and every plugin left with none.
    ///
    /// The entry that adds an empty group is never hidden: it is not a module and there is nothing
    /// about it to match, so filtering it away would only take a tool from the person using one.
    /// An empty filter hides **nothing**, which has to be said separately: a plugin that loaded and
    /// registered no module has no matching child to count, and counting alone would make the
    /// palette claim the plugin is not there at all.
    /// @param text what to look for, case-insensitively; empty shows everything
    void set_filter(const QString& text);

    /// The tree itself, for a test to walk and for the panel's own filtering.
    [[nodiscard]] QTreeWidget& tree();

   private:
    void apply_filter();

    app_state& state_;
    ui_callbacks& callbacks_;
    QLineEdit* filter_ = nullptr;
    palette_tree* tree_ = nullptr;
};

}  // namespace atp::studio::ui

#endif
