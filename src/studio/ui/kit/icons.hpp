#ifndef ATP_STUDIO_UI_ICONS_HPP
#define ATP_STUDIO_UI_ICONS_HPP

#include <QIcon>

/// The mark the application wears and the icons its menus put next to their actions.
///
/// The artwork lives in the resources, one SVG per icon, so replacing an icon means dropping in
/// another file — no C++ and no CMake change. The colour does not live there: it is read from the
/// palette at every repaint and painted over the artwork, so one file follows a light theme, a dark
/// theme and any screen density, and whatever colour the file names is ignored. The conventions a
/// replacement has to keep are in resources/icons/README.md. The application mark is the opposite
/// case, fixed artwork in fixed colours, and comes from the compiled-in atp.ico — the same file the
/// executable wears on Windows.
namespace atp::studio::ui::icons {

/// The application mark, in every size the .ico carries.
[[nodiscard]] QIcon brand();

/// A blank sheet — a project that does not exist yet.
[[nodiscard]] QIcon new_project();

/// A folder — a project that does.
[[nodiscard]] QIcon open_project();

/// A clock — what was open before.
[[nodiscard]] QIcon recent();

/// A floppy disk. Obsolete for thirty years and still the only shape everyone reads as "save".
[[nodiscard]] QIcon save();

/// The same disk with an ellipsis on its label: saving that asks for a name first.
[[nodiscard]] QIcon save_as();

/// An arrow curving back.
[[nodiscard]] QIcon undo();

/// The same arrow, mirrored.
[[nodiscard]] QIcon redo();

/// A marching-ants frame around a plus — enclosing what is selected.
[[nodiscard]] QIcon new_group();

/// A folder — a group of other nodes in the project tree.
[[nodiscard]] QIcon group();

/// A box with two inputs on the left and one output on the right: what a module is — a node with
/// declared ports. The silhouette is asymmetric on purpose, so that at 16 px a module still reads as
/// something other than a group.
[[nodiscard]] QIcon module();

/// A stack of plates seen in perspective — a plugin: the library a set of modules arrives in. Only
/// the top plate is drawn whole; the two below show the edge they turn towards the viewer, which is
/// all a stack shows anyway and all that survives at 16 px.
[[nodiscard]] QIcon plugin();

/// A folder — a directory of the file system, as opposed to a group of nodes.
[[nodiscard]] QIcon directory();

}  // namespace atp::studio::ui::icons

#endif
