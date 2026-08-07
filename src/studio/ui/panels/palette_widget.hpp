// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_PALETTE_WIDGET_HPP
#define ATP_STUDIO_UI_PALETTE_WIDGET_HPP

#include "kit/icons.hpp"
#include "kit/ui_style.hpp"
#include "model/app_state.hpp"

#include <optional>

#include <QIcon>
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

    [[nodiscard]] QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;

   private:
    static constexpr int group_role = Qt::UserRole + 3;

    void add_from_item(QTreeWidgetItem* item);

    void add_module_from_item(QTreeWidgetItem* item, std::optional<node_position> position);

    app_state& state_;
    ui_callbacks& callbacks_;

    QIcon group_icon_ = icons::group();
    QIcon plugin_icon_ = icons::plugin();
    QIcon module_icon_ = icons::module();
};

}  // namespace atp::studio::ui

#endif
