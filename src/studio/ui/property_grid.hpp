#ifndef ATP_STUDIO_UI_PROPERTY_GRID_HPP
#define ATP_STUDIO_UI_PROPERTY_GRID_HPP

#include "app_state.hpp"
#include "ui_style.hpp"

#include <memory>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QWidget>

namespace atp::studio::ui {

/// Editor of one property value. The value set is looked at first: a non-empty one means a
/// drop-down regardless of the kind, since an arbitrary value cannot be typed in, only an option
/// picked. Without a set the kind decides: boolean gets a check box, everything else a line edit.
struct property_editor {
    QWidget* widget = nullptr;
    QCheckBox* check = nullptr;  // set for a boolean without a value set
    QComboBox* combo = nullptr;  // set for any property with a value set
    QLineEdit* line = nullptr;   // set for number/text without a value set
    io::property_kind kind = io::property_kind::text;

    /// Current value in the canonical string form property_base::from_string expects.
    [[nodiscard]] std::string text() const;

    /// Writes a value in without emitting the editing signals — used by sync and by the rollback
    /// after a rejected edit.
    /// @param value the value in its canonical string form
    void set_text(const std::string& value);
};

/// One row of the grid: the name, the editor and the reset button, which only appears while the
/// value differs from the default.
struct property_row {
    property_info info;
    QLabel* label = nullptr;
    std::unique_ptr<property_editor> editor;
    QToolButton* reset = nullptr;
};

/// Property rows of one module. A value is applied the moment the editor is done with it — there is
/// no Apply button — and a rejected value rolls back with the reason shown on the editor itself
/// rather than in the error log across the window.
class property_grid final : public QWidget {
   public:
    property_grid(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the rows from the module's description. A module without properties (or without a
    /// loaded factory) leaves the grid empty, and the caller may hide it.
    /// @param group_path path of the group holding the module
    /// @param child name of the module inside that group
    /// @param m the module node the values are read from
    void rebuild(const std::string& group_path, const std::string& child, const runtime::module_node& m);

    /// True when the last rebuild produced no rows.
    [[nodiscard]] bool empty() const {
        return rows_.empty();
    }

    /// Pushes the current values into the existing editors, leaving the focused one alone: what the
    /// user is typing must survive a refresh caused by their own edit.
    void sync();

   private:
    // Value to show: the live module's while running, the document's otherwise, and the
    // description's default when the document has none.
    [[nodiscard]] std::string current_value(const property_row& row) const;

    void apply(property_row& row);

    void reset(property_row& row);

    // Bold name and a visible reset button while the value differs from the default.
    void mark(property_row& row, const std::string& value);

    app_state& state_;
    ui_callbacks& callbacks_;
    QFormLayout* form_ = nullptr;
    std::string group_path_;
    std::string child_;
    // The connect lambdas hold pointers to the rows, so the vector is cleared only together with
    // the widgets and their connections.
    std::vector<std::unique_ptr<property_row>> rows_;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_PROPERTY_GRID_HPP
