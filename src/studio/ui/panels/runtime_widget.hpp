// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_STUDIO_UI_RUNTIME_WIDGET_HPP
#define ATP_STUDIO_UI_RUNTIME_WIDGET_HPP

#include "kit/thread_table.hpp"
#include "model/app_state.hpp"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

namespace atp::studio::ui {

/// Runtime panel: the Run/Stop controls, the thread table where the declared threads are edited and
/// their pass statistics are watched, the per-module cost table and the per-port table.
///
/// The three tables answer different questions and none replaces another: the thread one says a
/// thread is saturated, the module one says which module saturated it, the port one says whether
/// the pipeline is losing data on the way between them — which the first two cannot show at all,
/// since a pipeline dropping every second value looks perfectly busy.
///
/// Measuring modules is opt-in because it is not free — see set_metrics_enabled — so the checkbox
/// is a real control, not a display preference, and it only means anything while a pipeline runs.
/// The port table has no such switch: its counters are kept unconditionally, and giving an operator
/// a way to switch off the only sign of data loss would be a poor kind of choice.
///
/// The body scrolls, as the Inspector's does and for the same reason. Three stacked tables ask for
/// more height than the bottom row is given: the default layout hands that row a fraction of the
/// window, while a dock is never resized below the minimum its content declares, so a panel
/// declaring the sum of three tables takes the missing height from the canvas rather than from the
/// layout — the canvas is left with less room than the docks it is being edited between. Scrolling
/// returns that decision to the layout: the row keeps its fraction, and what does not fit is
/// reached with the scrollbar instead of every table being squeezed down to a header and a row.
class runtime_widget final : public QWidget {
   public:
    runtime_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Refreshes the controls, the thread table, the module table and the port table from the
    /// project and session, and stamps the moment it did.
    ///
    /// The stamp matters because the timer behind this panel only ticks while something runs: once
    /// a pipeline stops, the tables go on showing their last numbers with nothing to say how old
    /// they are. A frozen stamp names the moment they stopped being true.
    void refresh();

    /// Loads the plugins the project names and starts the pipeline, reporting a failure through the
    /// callbacks rather than throwing. Public because the toolbar drives the very same action as
    /// this panel's own button: two copies of the sequence would be two places to forget the plugin
    /// loading in.
    void start_run();

    /// Stops the pipeline and tells the window to refresh. Idempotent, as the runner's own stop is.
    void stop_run();

   private:
    void refresh_modules();
    void refresh_ports();

    app_state& state_;
    ui_callbacks& callbacks_;
    QPushButton* run_ = nullptr;
    QPushButton* stop_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* updated_ = nullptr;
    thread_table* threads_ = nullptr;
    QCheckBox* measure_ = nullptr;
    QTableWidget* modules_ = nullptr;
    QTableWidget* ports_ = nullptr;
};

}  // namespace atp::studio::ui

#endif
