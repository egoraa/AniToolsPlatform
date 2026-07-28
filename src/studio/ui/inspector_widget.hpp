#ifndef ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP
#define ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP

#include "app_state.hpp"
#include "expose_editor.hpp"
#include "property_grid.hpp"
#include "ui_style.hpp"

#include <functional>
#include <string>
#include <vector>

#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace atp::studio::ui {

/// Inspector: what the canvas selection is — a module with its properties, a group with its thread
/// and its exports, and the current group itself when nothing is selected. The form is rebuilt only
/// when its identity changes — a different selection or a different module description; an edit of a
/// value merely pushes the new values into the widgets already on screen.
class inspector_widget final : public QWidget {
   public:
    inspector_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the form from the current selection and document.
    void refresh();

   private:
    // Identity of the form currently on screen: the description cache generation, the group path,
    // the selected child and the child's factory@version. While it holds, refresh() only pushes
    // values into the existing widgets — rebuilding would destroy the very editor whose editing
    // signal is running.
    [[nodiscard]] std::string form_key() const;

    void rebuild();

    // Pushes the current values into the widgets of a form that stays on screen.
    void sync();

    void apply_lock();

    void clear_body();

    // Starts a new titled block in the body.
    style::section add_section(const QString& title);

    void guard(const char* context, const std::function<void()>& operation);

    // Renames the child the field belongs to, rolling the field back and marking it when the
    // document refuses the name.
    void commit_rename(const std::string& old_name, QLineEdit* edit);

    void build_module_section(const runtime::module_node& m);

    // @param group_path path of the group being shown, empty for the current group's own root
    // @param name its name, as the section title and the name row
    // @param renameable false for the current group: renaming it belongs to its parent's view
    void build_group_section(const std::string& group_path, const std::string& name, bool renameable);

    app_state& state_;
    ui_callbacks& callbacks_;
    QWidget* body_ = nullptr;
    QVBoxLayout* body_layout_ = nullptr;
    // Key of the form on screen; an empty one means there is nothing built yet.
    std::string form_key_;
    // Nested editors of the current form, owned by the body like every other section widget.
    property_grid* properties_ = nullptr;
    expose_editor* expose_inputs_ = nullptr;
    expose_editor* expose_outputs_ = nullptr;
    // Widgets of the property block of the last built form — the only ones that stay enabled while
    // the pipeline runs.
    std::vector<QWidget*> property_rows_;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP
