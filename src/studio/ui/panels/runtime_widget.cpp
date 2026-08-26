// SPDX-License-Identifier: Apache-2.0
#include "panels/runtime_widget.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <vector>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QString>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <atp/runtime/log_pump.hpp>

namespace atp::studio::ui {

namespace {

constexpr int module_column = 0;
constexpr int calls_column = 1;
constexpr int busy_column = 2;
constexpr int total_column = 3;
constexpr int max_column = 4;

constexpr int port_column = 0;
constexpr int received_column = 1;
constexpr int discarded_column = 2;
constexpr int pending_column = 3;
constexpr int peak_column = 4;
constexpr int capacity_column = 5;

/// A cell holding a number. Right-aligned, because a column read down for orders of magnitude is
/// read by its last digit and a left-aligned one hides that behind a ragged edge.
/// @param text the rendered number
/// @return the cell, ready to be put in a table
[[nodiscard]] QTableWidgetItem* number_cell(const QString& text) {
    auto* cell = new QTableWidgetItem(text);
    cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return cell;
}

}  // namespace

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
    updated_ = new QLabel(controls);
    updated_->setObjectName("runtime.updated");
    updated_->setToolTip("When the tables below were last read from the running pipeline");
    style::muted(updated_);
    controls_layout->addWidget(updated_);
    layout->addWidget(controls);

    layout->addWidget(style::section_header("threads", this));
    threads_ = new thread_table(state_, callbacks_, this);
    layout->addWidget(threads_, 1);

    layout->addWidget(style::section_header("modules", this));
    measure_ = new QCheckBox("measure each module's iterate", this);
    measure_->setToolTip(
        "Times every iterate. Not free — it cost a quarter of the throughput of a two-module "
        "pipeline — so switch it on to find the slow module, then off again.");
    layout->addWidget(measure_);
    modules_ = new QTableWidget(0, 5, this);
    modules_->setObjectName("module_metrics");
    modules_->setHorizontalHeaderLabels({"module", "calls", "busy", "total ms", "max us"});
    modules_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    modules_->horizontalHeader()->setSectionResizeMode(module_column, QHeaderView::Stretch);
    modules_->verticalHeader()->setVisible(false);
    style::set_placeholder(modules_, "Run the pipeline and switch measuring on to collect timings.");
    layout->addWidget(modules_, 1);

    layout->addWidget(style::section_header("ports", this));
    ports_ = new QTableWidget(0, 6, this);
    ports_->setObjectName("input_metrics");
    ports_->setHorizontalHeaderLabels({"port", "received", "discarded", "pending", "peak", "capacity"});
    ports_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ports_->horizontalHeader()->setSectionResizeMode(port_column, QHeaderView::Stretch);
    ports_->verticalHeader()->setVisible(false);
    style::set_placeholder(ports_, "Run the pipeline to see what the inputs received.");
    layout->addWidget(ports_, 1);

    QObject::connect(measure_, &QCheckBox::toggled, this, [this](bool on) {
        (void)state_.view->set_metrics_enabled(on);
        refresh_modules();
    });

    QObject::connect(run_, &QPushButton::clicked, this, [this] { start_run(); });
    QObject::connect(stop_, &QPushButton::clicked, this, [this] { stop_run(); });
}

void runtime_widget::refresh() {
    const bool running = state_.view->running();
    const bool attached = state_.attached();
    run_->setEnabled(!running && !attached);
    stop_->setEnabled(running && !attached);
    status_->setText(attached ? QString::fromStdString("attached to " + state_.endpoint())
                              : QString(running ? "running" : "stopped"));
    threads_->refresh();

    measure_->setEnabled(running);
    {
        const QSignalBlocker block(measure_);
        measure_->setChecked(state_.view->metrics_enabled());
    }
    refresh_modules();
    refresh_ports();
    updated_->setText(
        QString::fromStdString("updated " + atp::runtime::format_log_time(std::chrono::system_clock::now())));
}

void runtime_widget::refresh_ports() {
    std::vector<runtime::group::port_stats> ports = state_.view->input_metrics();
    std::ranges::sort(ports, [](const runtime::group::port_stats& a, const runtime::group::port_stats& b) {
        return a.stats.discarded > b.stats.discarded;
    });
    ports_->setRowCount(static_cast<int>(ports.size()));
    for (std::size_t index = 0; index < ports.size(); ++index) {
        const runtime::group::port_stats& p = ports[index];
        const int row = static_cast<int>(index);
        ports_->setItem(row, port_column, new QTableWidgetItem(QString::fromStdString(p.path)));
        ports_->setItem(row, received_column, number_cell(QString::number(p.stats.received)));
        ports_->setItem(row, discarded_column, number_cell(QString::number(p.stats.discarded)));
        ports_->setItem(row, pending_column, number_cell(QString::number(p.stats.pending)));
        ports_->setItem(row, peak_column, number_cell(QString::number(p.stats.peak_pending)));
        ports_->setItem(row, capacity_column, number_cell(QString::number(p.stats.capacity)));
    }
}

void runtime_widget::refresh_modules() {
    std::vector<runtime::group::module_stats> stats = state_.view->module_metrics();
    std::ranges::sort(stats, [](const runtime::group::module_stats& a, const runtime::group::module_stats& b) {
        return a.total > b.total;
    });
    modules_->setRowCount(static_cast<int>(stats.size()));
    for (std::size_t index = 0; index < stats.size(); ++index) {
        const runtime::group::module_stats& s = stats[index];
        const int row = static_cast<int>(index);
        modules_->setItem(row, module_column, new QTableWidgetItem(QString::fromStdString(s.path)));
        modules_->setItem(row, calls_column, number_cell(QString::number(s.calls)));
        modules_->setItem(row, busy_column, number_cell(QString::number(s.busy_calls)));
        modules_->setItem(
            row, total_column,
            number_cell(QString::number(std::chrono::duration<double, std::milli>(s.total).count(), 'f', 3)));
        modules_->setItem(
            row, max_column,
            number_cell(QString::number(std::chrono::duration<double, std::micro>(s.max).count(), 'f', 1)));
    }
}

void runtime_widget::stop_run() {
    state_.run.stop();
    callbacks_.project_changed();
}

void runtime_widget::start_run() {
    try {
        for (const std::string& p : state_.doc.config().plugins) {
            state_.manager.load_plugin(state_.config_dir() / p);
        }
        state_.run.start(state_.doc.config(), state_.saved_dir());
        callbacks_.project_changed();
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("run: ") + e.what()));
        callbacks_.project_changed();
    }
}

}  // namespace atp::studio::ui
