#ifndef ATP_STUDIO_UI_PROPERTY_GRID_HPP
#define ATP_STUDIO_UI_PROPERTY_GRID_HPP

#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

#include <memory>
#include <string>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPoint>
#include <QString>
#include <QToolButton>
#include <QWidget>

namespace atp::studio::ui {

/// Editor of one property value. The value set is looked at first: a non-empty one means a
/// drop-down regardless of the kind, since an arbitrary value cannot be typed in, only an option
/// picked. Without a set the kind decides: boolean gets a check box, everything else a line edit.
struct property_editor {
    QWidget* widget = nullptr;
    QCheckBox* check = nullptr;
    QComboBox* combo = nullptr;
    QLineEdit* line = nullptr;
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

    /// Value of one row as it is shown, in the canonical string form another field or a config
    /// would take.
    /// @param name property name
    /// @return the value, empty when the module declares no such property
    [[nodiscard]] QString value_text(const std::string& name) const;

    /// Every row as "name = value", one per line, sorted by name. The rows themselves come in the
    /// order the io_registry hands them out, which is an unordered_map's and therefore says nothing;
    /// text meant to be pasted into a report or diffed against another module needs an order of its
    /// own.
    [[nodiscard]] QString all_text() const;

    /// Puts one row's value on the system clipboard, leaving it alone when there is no such row.
    void copy_value(const std::string& name) const;

    /// Puts every row on the system clipboard as "name = value" lines.
    void copy_all() const;

   protected:
    /// Offers the two copies. There is deliberately no Ctrl+C here: the row editors are line edits
    /// and combo boxes that copy their own text with it, and taking that away to copy the row would
    /// be a poor trade.
    void contextMenuEvent(QContextMenuEvent* event) override;

   private:
    /// Row the position falls in — its label, its editor or its reset button; nullptr between rows.
    [[nodiscard]] const property_row* row_at(const QPoint& pos) const;

    [[nodiscard]] std::string current_value(const property_row& row) const;

    void apply(property_row& row);

    void reset(property_row& row);

    void mark(property_row& row, const std::string& value);

    app_state& state_;
    ui_callbacks& callbacks_;
    QFormLayout* form_ = nullptr;
    std::string group_path_;
    std::string child_;
    std::vector<std::unique_ptr<property_row>> rows_;
};

}  // namespace atp::studio::ui

#endif
