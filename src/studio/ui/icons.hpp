#ifndef ATP_STUDIO_UI_ICONS_HPP
#define ATP_STUDIO_UI_ICONS_HPP

#include <QIcon>

/// The mark the application wears and the icons its menus put next to their actions.
///
/// The menu icons are drawn rather than loaded: line art on a small square grid, painted in the
/// colour the palette gives text. One set then follows a light theme, a dark theme and any screen
/// density — a set of files would need a copy per theme and a copy per density, and would still be
/// wrong on the next one. The application mark is the opposite case, fixed artwork in fixed
/// colours, and comes from the compiled-in atp.ico — the same file the executable wears on Windows.
namespace atp::studio::ui::icons {

/// The application mark, in every size the .ico carries.
[[nodiscard]] QIcon brand();

/// A blank sheet — a document that does not exist yet.
[[nodiscard]] QIcon new_document();

/// A folder — a document that does.
[[nodiscard]] QIcon open_document();

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

}  // namespace atp::studio::ui::icons

#endif  // ATP_STUDIO_UI_ICONS_HPP
