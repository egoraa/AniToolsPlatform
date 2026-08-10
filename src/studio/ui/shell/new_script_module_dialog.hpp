// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_NEW_SCRIPT_MODULE_DIALOG_HPP
#define ATP_STUDIO_UI_NEW_SCRIPT_MODULE_DIALOG_HPP

#include <string_view>

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QString>

#include <atp/module_registry.hpp>
#include <atp/studio/languages.hpp>

namespace atp::studio::ui {

/// Asks what module to create, in which language, and where. Like the attach dialog it remembers
/// nothing of its own: the language and the directory to offer come from the settings and go back
/// there through the caller.
///
/// The OK button carries the whole validation — a name that could not become a module, a name already
/// registered, a file already on disk — with the reason on a label beside it. A dialog that accepted
/// and then failed would have written half a gesture, and the half it wrote is a file.
///
/// The language is a field here rather than two menu items, because everything else about the gesture
/// is identical between languages: the same name, the same folder, and a note that has to change with
/// the choice anyway. Two menu items would have been two copies of this dialog.
///
/// It opens twice as wide as the layout asks for. Both things a person reads here are absolute paths —
/// the directory field and the note naming the file to be written — and the natural hint sizes to the
/// labels, which leaves a path scrolled out of sight in a field too narrow to check it in.
class new_script_module_dialog final : public QDialog {
   public:
    /// @param directory directory to offer first
    /// @param registry registry the name must not already be in
    /// @param initial_language id of the language to select; anything unknown falls back to the first
    /// @param parent owner widget
    new_script_module_dialog(const QString& directory,
                             const module_registry& registry,
                             std::string_view initial_language,
                             QWidget* parent = nullptr);

    /// Module name the person entered.
    [[nodiscard]] QString module_name() const;

    /// Directory the person chose.
    [[nodiscard]] QString directory() const;

    /// Language the person chose.
    [[nodiscard]] const script_language& language() const;

   private:
    void revalidate();

    const module_registry& registry_;
    QComboBox* language_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* directory_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* ok_ = nullptr;
};

}  // namespace atp::studio::ui

#endif
