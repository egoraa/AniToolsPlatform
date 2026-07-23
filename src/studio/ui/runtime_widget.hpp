#ifndef ATP_STUDIO_UI_RUNTIME_WIDGET_HPP
#define ATP_STUDIO_UI_RUNTIME_WIDGET_HPP

#include "app_state.hpp"

#include <vector>

#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

namespace atp::studio::ui {

class runtime_widget final : public QWidget {
   public:
    runtime_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent = nullptr);

    void refresh();

   private:
    void start();

    app_state& state_;
    ui_callbacks& callbacks_;
    QPushButton* run_ = nullptr;
    QPushButton* stop_ = nullptr;
    QLabel* status_ = nullptr;
    QTableWidget* table_ = nullptr;
    std::vector<pipeline_runner::thread_stats> previous_;
};

}  // namespace atp::studio::ui

#endif  // ATP_STUDIO_UI_RUNTIME_WIDGET_HPP
