#ifndef ATP_STUDIO_UI_CANVAS_WIDGET_HPP
#define ATP_STUDIO_UI_CANVAS_WIDGET_HPP

#include "canvas/canvas_scene.hpp"
#include "kit/ui_style.hpp"

#include <QGraphicsView>
#include <QWidget>

namespace atp::studio::ui {

/// Central widget: the group breadcrumbs plus the canvas view.
class canvas_widget final : public QWidget {
   public:
    canvas_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the breadcrumbs and the scene from the current project.
    void refresh();

    /// Scene shown by this widget.
    [[nodiscard]] canvas_scene& scene();

   private:
    void rebuild_crumbs();

    app_state& state_;
    ui_callbacks& callbacks_;
    QWidget* crumbs_ = nullptr;
    canvas_scene* scene_ = nullptr;
    QGraphicsView* view_ = nullptr;
};

}  // namespace atp::studio::ui

#endif
