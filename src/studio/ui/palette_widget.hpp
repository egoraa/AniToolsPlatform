#ifndef ATP_STUDIO_UI_PALETTE_WIDGET_HPP
#define ATP_STUDIO_UI_PALETTE_WIDGET_HPP

#include "app_state.hpp"

#include <optional>

#include <QTreeWidget>

#include <atp/studio/add_module.hpp>

namespace atp::studio::ui {

class palette_widget final : public QTreeWidget {
   public:
    palette_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    void refresh();

   protected:
    [[nodiscard]] QStringList mimeTypes() const override;

    // nullptr — перетаскивания не будет: заголовок плагина (нет родителя),
    // сломанный модуль (isDisabled) и запущенный пайплайн не перетаскиваются.
    [[nodiscard]] QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;

   private:
    void add_from_item(QTreeWidgetItem* item);

    // Общий путь для двойного клика (position = nullopt → auto_layout) и
    // сброса на канвас (position = точка сброса).
    void add_module_from_item(QTreeWidgetItem* item, std::optional<node_position> position);

    app_state& state_;
    ui_callbacks& callbacks_;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_PALETTE_WIDGET_HPP
