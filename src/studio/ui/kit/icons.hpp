// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_ICONS_HPP
#define ATP_STUDIO_UI_ICONS_HPP

#include <map>
#include <string>
#include <string_view>

#include <QIcon>
#include <QString>

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

/// A triangle pointing the way the data flows — starting the pipeline. Outlined rather than filled,
/// like the rest of the family: a solid triangle beside line art reads as a different set of icons.
[[nodiscard]] QIcon run();

/// The mark a module compiled into a plugin wears on the canvas.
///
/// These three answer with a path into the resources rather than with a ready QIcon, because of the
/// colour: on a node the artwork is tinted with a colour of the canvas scheme, while QIcon's engine
/// paints in the one the widget palette gives text — which is judged against a panel and not against
/// a node body.
[[nodiscard]] QString binary_module_artwork();

/// The mark a module written in a script wears.
///
/// A language the family has no artwork for — and a script whose language nothing here recognises —
/// gets the generic sheet: languages() is the one place a language is added, and a mark saying
/// "script" is a better answer than an empty node or than one claiming the module is binary. Hence
/// the file is looked up rather than mapped from a list written here.
/// @param language_id script_language::id, empty when the file's extension names no known language
/// @return path of the file inside the resources
[[nodiscard]] QString script_artwork(std::string_view language_id);

/// The mark a subgroup wears on the canvas — the folder the project tree already puts beside a
/// group, so that both views answer the same question in the same shape.
[[nodiscard]] QString group_artwork();

/// The marks of the module kinds, kept for as long as the panel drawing them.
///
/// An icon of this family parses its artwork when it is built, so asking for one per row would parse
/// the same handful of files once per module on every rebuild of a panel — and the palette, the
/// project tree and the plugins dock all rebuild whole. The set is fixed and tiny, so each is kept
/// after its first use. A member of the widget rather than a static: a QIcon outliving
/// QApplication is a teardown nobody needs.
class module_icons {
   public:
    /// The mark for a module declared in this file: the language's own when the extension names one,
    /// the generic sheet when it names none, and the box of ports when there is no file at all —
    /// which is what an ordinary plugin reports. The same rule the canvas draws its nodes by, so the
    /// panels and the canvas never disagree about what a module is.
    /// @param source module_info::source, empty for a module the plugin named no file for
    [[nodiscard]] QIcon of_source(std::string_view source);

   private:
    std::map<std::string, QIcon, std::less<>> kept_;
};

/// A square — stopping the pipeline. The pair with run() is the one shape convention a transport
/// control may borrow from outside this family, because it is the one every user already knows.
[[nodiscard]] QIcon stop();

/// An arrow entering an enclosure — attaching to a host that is already running. The enclosure is
/// the other process and the arrow is this one going in, which is the shape every "sign in" and
/// "connect" control is drawn as; the mirrored form would read as detaching, so nothing else in the
/// family may use it.
[[nodiscard]] QIcon attach();

/// A line running off the edge and turning back under itself — wrapping a log line too long for the
/// dock instead of scrolling to reach its end.
///
/// This one and the three below are the log strip's, and they are icons where every other strip in
/// the panels wears a text glyph. The reason is that most of those actions have no letter shape that
/// says what they do — "wrap" and "follow the newest line" are both drawn, never written — and a
/// strip where one button is a glyph and its neighbours are artwork reads as two families side by
/// side. So the strip is drawn from this family entire, the clear included.
[[nodiscard]] QIcon soft_wrap();

/// An arrow coming down onto a bar — the end of the log, and staying there as it grows.
[[nodiscard]] QIcon scroll_to_end();

/// A bin — throwing the whole log away. The one action of the strip that takes something out of the
/// view rather than changing how the view is drawn, and the shape says so.
[[nodiscard]] QIcon clear_all();

/// A funnel — opening a view of one source: what the log shows once it is narrowed to a single
/// writer. Of the strip's four this is the only one that adds something rather than changing what
/// is already there, and the shape is the one everyone already reads as "narrowed to a subset".
[[nodiscard]] QIcon open_view();

/// A cross — closing a tab of the log. It is here, in the family, rather than left to the style,
/// because the style's own indicator is a red cross: the colour the log marks an error in, spent on
/// a way out of a tab. Drawn from this family it is line art in the palette's text colour at the
/// family's own strength, so it reads as quieter than the tab's label rather than louder.
[[nodiscard]] QIcon close_tab();

}  // namespace atp::studio::ui::icons

#endif
