// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <QCheckBox>
#include <QTableWidget>

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
