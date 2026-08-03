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
/// their pass statistics are watched, and the per-module cost table.
///
/// The two tables answer different questions and neither replaces the other: the thread one says a
/// thread is saturated, the module one says which module saturated it. Measuring is opt-in because
/// it is not free — see set_metrics_enabled — so the checkbox is a real control, not a display
/// preference, and it only means anything while a pipeline runs.
class runtime_widget final : public QWidget {
   public:
    runtime_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Refreshes the controls, the thread table and the module table from the project and session.
    void refresh();

   private:
    void start();
    void refresh_modules();

    app_state& state_;
    ui_callbacks& callbacks_;
    QPushButton* run_ = nullptr;
    QPushButton* stop_ = nullptr;
    QLabel* status_ = nullptr;
    thread_table* threads_ = nullptr;
    QCheckBox* measure_ = nullptr;
    QTableWidget* modules_ = nullptr;
};

}  // namespace atp::studio::ui

#endif
