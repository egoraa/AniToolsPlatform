#ifndef ATP_STUDIO_UI_RUNTIME_WIDGET_HPP
#define ATP_STUDIO_UI_RUNTIME_WIDGET_HPP

#include "kit/thread_table.hpp"
#include "model/app_state.hpp"

#include <QLabel>
#include <QPushButton>
#include <QWidget>

namespace atp::studio::ui {

/// Runtime panel: the Run/Stop controls and the thread table, where the declared threads are edited
/// and their pass statistics are watched.
class runtime_widget final : public QWidget {
   public:
    runtime_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    /// Refreshes the controls and the thread table from the project and the session.
    void refresh();

   private:
    void start();

    app_state& state_;
    ui_callbacks& callbacks_;
    QPushButton* run_ = nullptr;
    QPushButton* stop_ = nullptr;
    QLabel* status_ = nullptr;
    thread_table* threads_ = nullptr;
};

}  // namespace atp::studio::ui

#endif
