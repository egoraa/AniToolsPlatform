#ifndef ATP_STUDIO_UI_PALETTE_WIDGET_HPP
#define ATP_STUDIO_UI_PALETTE_WIDGET_HPP

#include "app_state.hpp"
#include "ui_style.hpp"

#include <optional>

#include <QTreeWidget>

#include <atp/studio/add_module.hpp>

namespace atp::studio::ui {

/// Palette of the loaded modules, grouped by plugin. Modules are added by a double click or by
/// dragging onto the canvas.
class palette_widget final : public QTreeWidget {
   public:
    palette_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the tree from the loaded plugins.
    void refresh();

   protected:
    [[nodiscard]] QStringList mimeTypes() const override;

    // Returning nullptr means no drag: a plugin header (no parent), a broken module (disabled) and
    // a running pipeline are not draggable.
    [[nodiscard]] QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;

   private:
    // Marks the palette's only entry that is not a module; the modules keep UserRole..UserRole+2
    // for the factory, the version and the plugin path.
    static constexpr int group_role = Qt::UserRole + 3;

    void add_from_item(QTreeWidgetItem* item);

    // Shared path for the double click (position = nullopt → auto layout) and the drop onto the
    // canvas (position = the drop point).
    void add_module_from_item(QTreeWidgetItem* item, std::optional<node_position> position);

    app_state& state_;
    ui_callbacks& callbacks_;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_PALETTE_WIDGET_HPP
