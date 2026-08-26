// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_CANVAS_WIDGET_HPP
#define ATP_STUDIO_UI_CANVAS_WIDGET_HPP

#include "canvas/canvas_palette.hpp"
#include "canvas/canvas_scene.hpp"
#include "kit/ui_style.hpp"

#include <QEvent>
#include <QGraphicsView>
#include <QObject>
#include <QPoint>
#include <QWidget>

namespace atp::studio::ui {

/// Central widget: the group breadcrumbs plus the canvas view.
class canvas_widget final : public QWidget {
   public:
    canvas_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Rebuilds the breadcrumbs and the scene from the current project, in the colours of the
    /// widget's palette as it stands now. The palette is re-read here and not only in changeEvent so
    /// that every rebuild is in the current scheme whatever brought it about.
    void refresh();

    /// Scene shown by this widget.
    [[nodiscard]] canvas_scene& scene();

    /// Current scale of the view, 1.0 being one scene unit to one logical pixel.
    [[nodiscard]] double zoom() const;

    /// Steps the scale up, down, or back to one. Each step is a fixed ratio rather than a fixed
    /// amount, so zooming out feels like the reverse of zooming in; the range is clamped, because a
    /// view that can be scaled to nothing is a view a person can get lost in.
    void zoom_in();
    void zoom_out();
    void zoom_reset();

    /// Scales and centres the view so the whole group fits in it. An empty scene is left alone:
    /// fitInView on an empty rectangle divides by zero and leaves a degenerate transform behind, and
    /// there would be nothing to look at either way.
    void zoom_to_fit();

   protected:
    /// Repaints the canvas when the scheme under it changes. This is the **only** route that is
    /// sound, and the theme menu takes it too: measured on Windows, choosing a theme moves
    /// QApplication::palette() first and delivers QEvent::PaletteChange to the widget after, so a
    /// rebuild triggered from the menu handler would run while this widget's own palette still holds
    /// the scheme the user just left. Waiting for the event is what gets the right colours, and it
    /// is why there is no second call beside it.
    ///
    /// The scene is rebuilt rather than recoloured in place, which is what it does on every project
    /// change anyway — the models are small and an incremental repaint would be a second way to say
    /// the same thing.
    void changeEvent(QEvent* event) override;

    /// Turns the wheel into a zoom and the middle button into a pan. The view's own viewport gets
    /// these events, not this widget, which is why they are caught by a filter rather than by an
    /// override: the wheel would otherwise scroll and the middle button would do nothing.
    ///
    /// **Ctrl** and the wheel zoom; the wheel alone is left to the view, which scrolls with it as any
    /// other scroll area does. A wheel with no vertical component — a trackpad swiped sideways — is
    /// passed on untouched rather than eaten.
    ///
    /// The pan moves the scrollbars by hand instead of asking for ScrollHandDrag, because that mode
    /// is honoured for the **left** button alone: setting it on a middle press changes the cursor to
    /// an open hand and scrolls nothing, which looks exactly like a pan that does not work.
    /// @param watched the object the event was sent to
    /// @param event the event
    /// @return whether the event was consumed here
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void rebuild_crumbs();

    /// Applies zoom_ to the view. One place, so the clamp and the transform cannot drift apart.
    void apply_zoom();

    /// Hands the scene the colours of this widget's current palette.
    void apply_colors();

    app_state& state_;
    ui_callbacks& callbacks_;
    QWidget* crumbs_ = nullptr;
    canvas_scene* scene_ = nullptr;
    QGraphicsView* view_ = nullptr;
    double zoom_ = 1.0;
    bool panning_ = false;
    QPoint pan_from_;
};

}  // namespace atp::studio::ui

#endif
