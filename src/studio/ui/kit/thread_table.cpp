// SPDX-License-Identifier: Apache-2.0
#include "kit/thread_table.hpp"

#include <chrono>
#include <cstddef>
#include <exception>

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <atp/studio/thread_resolve.hpp>

namespace atp::studio::ui {

namespace {

constexpr int name_column = 0;
constexpr int mode_column = 1;
constexpr int period_column = 2;
constexpr int groups_column = 3;
constexpr int passes_column = 4;
constexpr int busy_column = 5;

const char* mode_name(runtime::thread_mode mode) {
    switch (mode) {
        case runtime::thread_mode::on_demand:
            return "on_demand";
        case runtime::thread_mode::throttled:
            return "throttled";
        case runtime::thread_mode::spinning:
            return "spinning";
    }
    return "on_demand";
}

runtime::thread_mode mode_at(int index) {
    if (index == 1) {
        return runtime::thread_mode::throttled;
    }
    if (index == 2) {
        return runtime::thread_mode::spinning;
    }
    return runtime::thread_mode::on_demand;
}

}  // namespace

thread_table::thread_table(app_state& state, ui_callbacks& callbacks, QWidget* parent)
    : QWidget(parent), state_(state), callbacks_(callbacks) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    table_ = new QTableWidget(0, 6, this);
    table_->setHorizontalHeaderLabels({"thread", "mode", "period", "groups", "passes", "busy %"});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(groups_column, QHeaderView::Stretch);
    style::embed_view(table_);
    layout->addWidget(table_, 1);

    const style::button_bar bar = style::make_button_bar(this);
    add_ = style::tool_button(style::glyph::add, "declare a thread", bar.box);
    remove_ = style::tool_button(style::glyph::drop, "drop the thread and the assignments on it", bar.box);
    bar.row->addWidget(add_);
    bar.row->addWidget(remove_);
    bar.row->addStretch(1);
    layout->addWidget(bar.box);

    QObject::connect(add_, &QToolButton::clicked, this, [this] { add_thread(); });
    QObject::connect(remove_, &QToolButton::clicked, this, [this] { remove_selected(); });
}

void thread_table::refresh() {
    const std::string key = signature();
    if (key != signature_) {
        signature_ = key;
        QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
        return;
    }
    update_stats();
    const bool running = state_.view->running();
    add_->setEnabled(!running);
    remove_->setEnabled(!running);
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* mode = qobject_cast<QComboBox*>(table_->cellWidget(row, mode_column));
        QWidget* period = table_->cellWidget(row, period_column);
        if (mode == nullptr || period == nullptr) {
            continue;
        }
        mode->setEnabled(!running);
        period->setEnabled(!running && mode_at(mode->currentIndex()) == runtime::thread_mode::throttled);
    }
}

std::string thread_table::signature() const {
    std::string key;
    for (const runtime::thread_node& t : state_.doc.config().threads) {
        key += t.name;
        key += mode_name(t.mode);
        key += std::to_string(t.period.count());
        key += '\0';
    }
    for (const auto& [path, thread] : state_.doc.config().assignments) {
        key += path + '>' + thread + '\0';
    }
    return key;
}

void thread_table::rebuild() {
    filling_ = true;
    table_->clearContents();
    const auto& threads = state_.doc.config().threads;
    table_->setRowCount(static_cast<int>(threads.size()));
    for (std::size_t i = 0; i < threads.size(); ++i) {
        const int row = static_cast<int>(i);
        const runtime::thread_node& t = threads[i];

        auto* name = new QTableWidgetItem(QString::fromStdString(t.name));
        name->setFlags(name->flags().setFlag(Qt::ItemIsEditable, false));
        table_->setItem(row, name_column, name);

        auto* mode = new QComboBox(table_);
        mode->addItems({"on_demand", "throttled", "spinning"});
        mode->setCurrentText(mode_name(t.mode));
        table_->setCellWidget(row, mode_column, mode);

        auto* period = new QSpinBox(table_);
        period->setRange(1, 60000);
        period->setSuffix(" ms");
        period->setValue(t.period.count() > 0 ? static_cast<int>(t.period.count()) : 10);
        period->setEnabled(t.mode == runtime::thread_mode::throttled);
        table_->setCellWidget(row, period_column, period);

        const std::string name_copy = t.name;
        const auto commit = [this, name_copy, mode, period] {
            if (filling_) {
                return;
            }
            const runtime::thread_mode m = mode_at(mode->currentIndex());
            period->setEnabled(m == runtime::thread_mode::throttled);
            try {
                state_.doc.set_thread(name_copy, m,
                                      m == runtime::thread_mode::throttled ? std::chrono::milliseconds(period->value())
                                                                           : std::chrono::milliseconds{});
            } catch (const std::exception& e) {
                callbacks_.error(QString::fromStdString(std::string("thread: ") + e.what()));
                return;
            }
            callbacks_.project_changed();
        };
        QObject::connect(mode, &QComboBox::currentIndexChanged, this, [commit](int) { commit(); });
        QObject::connect(period, &QSpinBox::valueChanged, this, [commit](int) { commit(); });

        QString groups;
        for (const std::string& path : groups_on_thread(state_.doc.config(), t.name)) {
            groups += (groups.isEmpty() ? "" : ", ") + QString::fromStdString(path);
        }
        auto* groups_item = new QTableWidgetItem(groups);
        groups_item->setFlags(groups_item->flags().setFlag(Qt::ItemIsEditable, false));
        table_->setItem(row, groups_column, groups_item);

        table_->setItem(row, passes_column, new QTableWidgetItem("-"));
        table_->setItem(row, busy_column, new QTableWidgetItem("-"));
    }
    filling_ = false;
}

void thread_table::update_stats() {
    const auto stats = state_.view->stats();
    if (stats.empty()) {
        previous_.clear();
        for (int row = 0; row < table_->rowCount(); ++row) {
            table_->item(row, passes_column)->setText("-");
            table_->item(row, busy_column)->setText("-");
        }
        return;
    }
    for (std::size_t i = 0; i < stats.size(); ++i) {
        for (int row = 0; row < table_->rowCount(); ++row) {
            if (table_->item(row, name_column)->text().toStdString() != stats[i].name) {
                continue;
            }
            table_->item(row, passes_column)->setText(QString::number(stats[i].passes));
            QString busy = "-";
            if (i < previous_.size() && stats[i].name == previous_[i].name && stats[i].passes > previous_[i].passes) {
                const double delta_busy = static_cast<double>(stats[i].busy_passes - previous_[i].busy_passes);
                const double delta_passes = static_cast<double>(stats[i].passes - previous_[i].passes);
                busy = QString::number(100.0 * delta_busy / delta_passes, 'f', 0) + "%";
            }
            table_->item(row, busy_column)->setText(busy);
        }
    }
    previous_ = stats;
}

void thread_table::add_thread() {
    bool accepted = false;
    const QString name =
        QInputDialog::getText(this, "New thread", "Thread name:", QLineEdit::Normal, QString(), &accepted);
    if (!accepted || name.isEmpty()) {
        return;
    }
    try {
        state_.doc.add_thread(name.toStdString(), runtime::thread_mode::on_demand);
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("thread: ") + e.what()));
        return;
    }
    callbacks_.project_changed();
}

void thread_table::remove_selected() {
    const int row = table_->currentRow();
    if (row < 0 || table_->item(row, name_column) == nullptr) {
        return;
    }
    try {
        state_.doc.remove_thread(table_->item(row, name_column)->text().toStdString());
    } catch (const std::exception& e) {
        callbacks_.error(QString::fromStdString(std::string("thread: ") + e.what()));
        return;
    }
    callbacks_.project_changed();
}

}  // namespace atp::studio::ui
