#include "runtime_widget.hpp"

#include <cstddef>
#include <exception>
#include <string>

#include <QHBoxLayout>
#include <QHeaderView>
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

    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels({"thread", "passes", "busy", "busy %"});
    table_->verticalHeader()->setVisible(false);
    layout->addWidget(table_, 1);

    QObject::connect(run_, &QPushButton::clicked, this, [this] { start(); });
    QObject::connect(stop_, &QPushButton::clicked, this, [this] {
        state_.run.stop();
        previous_.clear();
        callbacks_.document_changed();  // read-only снимается всем окном
    });
}

void runtime_widget::refresh() {
    const bool running = state_.run.running();
    run_->setEnabled(!running);
    stop_->setEnabled(running);
    status_->setText(running ? "running" : "stopped");

    const auto stats = state_.run.stats();
    table_->setRowCount(static_cast<int>(stats.size()));
    for (std::size_t i = 0; i < stats.size(); ++i) {
        const int row = static_cast<int>(i);
        table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(stats[i].name)));
        table_->setItem(row, 1, new QTableWidgetItem(QString::number(stats[i].passes)));
        table_->setItem(row, 2, new QTableWidgetItem(QString::number(stats[i].busy_passes)));
        // нагрузка за окно между опросами: дельты честнее накопленной суммы
        QString busy = "-";
        if (i < previous_.size() && stats[i].name == previous_[i].name && stats[i].passes > previous_[i].passes) {
            const double delta_busy = static_cast<double>(stats[i].busy_passes - previous_[i].busy_passes);
            const double delta_passes = static_cast<double>(stats[i].passes - previous_[i].passes);
            busy = QString::number(100.0 * delta_busy / delta_passes, 'f', 0) + "%";
        }
        table_->setItem(row, 3, new QTableWidgetItem(busy));
    }
    previous_ = stats;
}

void runtime_widget::start() {
    try {
        // плагины конфига — те же пути, что у atp_app: от каталога документа
        for (const std::string& p : state_.doc.config().plugins) {
            state_.manager.load_plugin(state_.config_dir() / p);
        }
        state_.run.start(state_.doc.config());
        previous_.clear();
        callbacks_.document_changed();  // окно уходит в read-only
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("run: ") + e.what()));
    }
}

}  // namespace atp::studio::ui
