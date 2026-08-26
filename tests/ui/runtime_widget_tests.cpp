// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <QCheckBox>
#include <QLabel>
#include <QString>
#include <QTableWidget>

#include <chrono>
#include <string>
#include <vector>

#include "model/app_state.hpp"
#include "panels/runtime_widget.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::app_state;
using atp::studio::ui::runtime_widget;
using atp::studio::ui::ui_callbacks;

QCheckBox* measure_box(runtime_widget& w) {
    return w.findChild<QCheckBox*>();
}

QTableWidget* modules_table(runtime_widget& w) {
    return w.findChild<QTableWidget*>(QStringLiteral("module_metrics"));
}

QTableWidget* ports_table(runtime_widget& w) {
    return w.findChild<QTableWidget*>(QStringLiteral("input_metrics"));
}

QLabel* placeholder_of(QTableWidget* table) {
    return table->viewport()->findChild<QLabel*>(QStringLiteral("view.placeholder"));
}

TEST(UiRuntimeWidget, AnEmptyMetricsTableSaysWhatItIsWaitingFor) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    runtime_widget widget(state, callbacks);

    widget.refresh();

    for (QTableWidget* table : {modules_table(widget), ports_table(widget)}) {
        ASSERT_NE(table, nullptr);
        EXPECT_EQ(table->rowCount(), 0);
        QLabel* note = placeholder_of(table);
        ASSERT_NE(note, nullptr);
        EXPECT_FALSE(note->isHidden());
        EXPECT_FALSE(note->text().isEmpty());
    }
}

TEST(UiRuntimeWidget, TheWaitingNoteGoesAwayAsSoonAsThereIsARow) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    runtime_widget widget(state, callbacks);
    widget.refresh();

    QTableWidget* table = modules_table(widget);
    ASSERT_NE(table, nullptr);
    QLabel* note = placeholder_of(table);
    ASSERT_NE(note, nullptr);
    ASSERT_FALSE(note->isHidden());

    table->setRowCount(1);
    EXPECT_TRUE(note->isHidden());

    table->setRowCount(0);
    EXPECT_FALSE(note->isHidden());
}

/// A view that reports whatever a test puts in it, so the metric tables can be filled without
/// standing a pipeline up. Their contents had no coverage at all before this.
class stub_view final : public atp::studio::runtime_view_base {
   public:
    std::vector<atp::runtime::group::module_stats> modules;
    std::vector<atp::runtime::group::port_stats> ports;

    [[nodiscard]] bool running() const override {
        return true;
    }
    [[nodiscard]] std::string error_text() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::runtime::pipeline_runner::thread_stats> stats() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::runtime::connection_sample> sample_connections() const override {
        return {};
    }
    [[nodiscard]] std::vector<atp::runtime::group::module_stats> module_metrics() const override {
        return modules;
    }
    [[nodiscard]] std::vector<atp::runtime::group::port_stats> input_metrics() const override {
        return ports;
    }
    [[nodiscard]] bool metrics_enabled() const override {
        return true;
    }
    bool set_metrics_enabled(bool) override {
        return true;
    }
    [[nodiscard]] std::vector<atp::studio::live_property> live_properties(const std::string&) const override {
        return {};
    }
    void set_property(const atp::runtime::property_override&) override {}
};

TEST(UiRuntimeWidget, EveryNumberIsAlignedToTheEdgeItIsReadFrom) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    stub_view view;
    view.modules.push_back({"root.a", 12, 5, std::chrono::nanoseconds(1000), std::chrono::nanoseconds(900)});
    atp::runtime::group::port_stats port;
    port.path = "root.a.in";
    view.ports.push_back(port);
    state.view = &view;

    ui_callbacks callbacks;
    runtime_widget widget(state, callbacks);
    widget.refresh();

    QTableWidget* modules = modules_table(widget);
    QTableWidget* ports_view = ports_table(widget);
    ASSERT_EQ(modules->rowCount(), 1);
    ASSERT_EQ(ports_view->rowCount(), 1);

    for (QTableWidget* table : {modules, ports_view}) {
        EXPECT_NE(table->item(0, 0)->textAlignment() & Qt::AlignRight, Qt::AlignRight)
            << "the first column is a name and reads from the left";
        for (int column = 1; column < table->columnCount(); ++column) {
            ASSERT_NE(table->item(0, column), nullptr) << "column " << column;
            EXPECT_EQ(table->item(0, column)->textAlignment(), Qt::AlignRight | Qt::AlignVCenter)
                << "column " << column;
        }
    }
}

TEST(UiRuntimeWidget, OffersTheModuleTableAndTheMeasureSwitch) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    runtime_widget widget(state, callbacks);

    QTableWidget* modules = modules_table(widget);
    ASSERT_NE(modules, nullptr);
    EXPECT_EQ(modules->columnCount(), 5);
    EXPECT_EQ(modules->horizontalHeaderItem(0)->text(), QStringLiteral("module"));
    EXPECT_NE(measure_box(widget), nullptr);
}

TEST(UiRuntimeWidget, OffersThePortTableAlongsideTheModuleOne) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    runtime_widget widget(state, callbacks);

    QTableWidget* ports = ports_table(widget);
    ASSERT_NE(ports, nullptr);
    EXPECT_EQ(ports->columnCount(), 6);
    EXPECT_EQ(ports->horizontalHeaderItem(0)->text(), QStringLiteral("port"));
    EXPECT_EQ(ports->horizontalHeaderItem(2)->text(), QStringLiteral("discarded"));
}

TEST(UiRuntimeWidget, PortTableStaysEmptyWhileNothingRuns) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    runtime_widget widget(state, callbacks);

    widget.refresh();

    EXPECT_EQ(ports_table(widget)->rowCount(), 0);
}

TEST(UiRuntimeWidget, RefusesToMeasureWhileNothingRuns) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    runtime_widget widget(state, callbacks);

    widget.refresh();

    EXPECT_FALSE(state.run.running());
    EXPECT_FALSE(measure_box(widget)->isEnabled());
    EXPECT_FALSE(measure_box(widget)->isChecked());
    EXPECT_EQ(modules_table(widget)->rowCount(), 0);
}

TEST(UiRuntimeWidget, StampsTheMomentTheTablesWereRead) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    runtime_widget widget(state, callbacks);

    auto* updated = widget.findChild<QLabel*>(QStringLiteral("runtime.updated"));
    ASSERT_NE(updated, nullptr);
    EXPECT_TRUE(updated->text().isEmpty());

    widget.refresh();

    const QString text = updated->text();
    ASSERT_TRUE(text.startsWith(QStringLiteral("updated ")));
    const QString stamp = text.mid(8);
    EXPECT_EQ(stamp.size(), 12);
    EXPECT_EQ(stamp[2], QLatin1Char(':'));
    EXPECT_EQ(stamp[8], QLatin1Char('.'));
}

TEST(UiRuntimeWidget, TogglingTheSwitchWithNothingRunningIsHarmless) {
    (void)atp_ui_tests::ensure_app();
    app_state state;
    ui_callbacks callbacks;
    runtime_widget widget(state, callbacks);

    measure_box(widget)->setChecked(true);

    EXPECT_FALSE(state.run.metrics_enabled());
    EXPECT_EQ(modules_table(widget)->rowCount(), 0);
}

}  // namespace
