#include "panels/runtime_widget.hpp"

#include <exception>
#include <string>

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace atp::studio::ui {

runtime_widget::runtime_widget(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QWidget(this);
    auto* controls_layout = new QHBoxLayout(controls);
    controls_layout->setContentsMargins(0, 0, 0, 0);
    run_ = new QPushButton("Run", controls);
    stop_ = new QPushButton("Stop", controls);
    status_ = new QLabel("stopped", controls);
    controls_layout->addWidget(run_);
    controls_layout->addWidget(stop_);
    controls_layout->addWidget(status_);
    controls_layout->addStretch(1);
    layout->addWidget(controls);

    layout->addWidget(style::section_header("threads", this));
    threads_ = new thread_table(state_, callbacks_, this);
    layout->addWidget(threads_, 1);

    QObject::connect(run_, &QPushButton::clicked, this, [this] { start(); });
    QObject::connect(stop_, &QPushButton::clicked, this, [this] {
        state_.run.stop();
        callbacks_.project_changed();
    });
}

void runtime_widget::refresh() {
    const bool running = state_.run.running();
    run_->setEnabled(!running);
    stop_->setEnabled(running);
    status_->setText(running ? "running" : "stopped");
    threads_->refresh();
}

void runtime_widget::start() {
    try {
        for (const std::string& p : state_.doc.config().plugins) {
            state_.manager.load_plugin(state_.config_dir() / p);
        }
        state_.run.start(state_.doc.config());
        callbacks_.project_changed();
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("run: ") + e.what()));
    }
}

}  // namespace atp::studio::ui
