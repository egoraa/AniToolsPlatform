#ifndef ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP
#define ATP_STUDIO_UI_INSPECTOR_WIDGET_HPP

#include "kit/expose_editor.hpp"
#include "kit/property_grid.hpp"
#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

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

    /// Rebuilds the form from the current selection and project.
    void refresh();

   private:
    [[nodiscard]] std::string form_key() const;

    void rebuild();

    void sync();

    void apply_lock();

    void clear_body();

    style::section add_section(const QString& title);

    void guard(const char* context, const std::function<void()>& operation);

    void commit_rename(const std::string& old_name, QLineEdit* edit);

    void build_module_section(const runtime::module_node& m);

    void build_group_section(const std::string& group_path, const std::string& name, bool renameable);

    app_state& state_;
    ui_callbacks& callbacks_;
    QWidget* body_ = nullptr;
    QVBoxLayout* body_layout_ = nullptr;
    std::string form_key_;
    property_grid* properties_ = nullptr;
    expose_editor* expose_inputs_ = nullptr;
    expose_editor* expose_outputs_ = nullptr;
    std::vector<QWidget*> property_rows_;
};

}  // namespace atp::studio::ui

#endif
