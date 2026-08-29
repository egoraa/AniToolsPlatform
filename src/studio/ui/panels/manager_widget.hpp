// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_MANAGER_WIDGET_HPP
#define ATP_STUDIO_UI_MANAGER_WIDGET_HPP

#include "kit/icons.hpp"
#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

#include <QIcon>
#include <QListWidget>
#include <QPoint>
#include <QTreeWidget>
#include <QWidget>

namespace atp::studio::ui {

/// Plugins panel: the module search directories and the plugins found in them, with their load
/// errors.
///
/// One list and not two. A module folder is a plugin folder — that is what carrying its own bridge
/// means — so what each bridge is told to scan is **derived** from these directories
/// (`derive_script_dirs`, once per language) rather than kept beside them, where the same folder would
/// appear twice and
/// the two lists would drift apart the moment either was edited.
///
/// The directory list carries the same three actions in a context menu as in the button bar under
/// it, because a path is what a person right-clicks. Dropping acts on the row under the cursor,
/// which the click makes current first so the button and the menu can never disagree about which
/// one they meant, and is disabled where there is no row — on the empty space of an empty list
/// adding is the only thing left to do.
///
/// The context menu of the plugin list follows what a row actually has a path to. A plugin row has
/// its file, and offers to copy it; a module row has one only when the plugin said where the module
/// is declared, which in practice means a script module and the file it was read from — those rows
/// also offer to open it. A module compiled into its plugin is declared in no file a person could
/// open, so its row has no menu at all rather than a menu that would have to lie about what it
/// points at.
///
/// The rescan button re-reads the plugins already loaded before it looks for new files. A person
/// pressing it has just edited a script or rebuilt a library and means the modules, not the directory
/// listing — and a plain scan is defined to leave a loaded plugin alone, which for a script bridge
/// means no edit could ever arrive. While the pipeline runs the re-reading is skipped and said so in
/// the log: it would unregister factories the live tree is holding, whereas loading a file that is
/// new to the session only adds.
class manager_widget final : public QWidget {
   public:
    manager_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the directory and plugin lists from the manager.
    void refresh();

   private:
    void sync_settings();

    void scan();

    void add_dir();

    void drop_dir(int row);

    void show_dirs_menu(const QPoint& pos, const QPoint& global);

    void show_plugin_menu(const QPoint& pos, const QPoint& global);

    static constexpr int path_role = Qt::UserRole;

    app_state& state_;
    ui_callbacks& callbacks_;
    QListWidget* dirs_ = nullptr;
    QTreeWidget* plugins_ = nullptr;

    QIcon directory_icon_ = icons::directory();
    QIcon plugin_icon_ = icons::plugin();
    icons::module_icons module_icons_;
};

}  // namespace atp::studio::ui

#endif
