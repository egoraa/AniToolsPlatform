// SPDX-License-Identifier: Apache-2.0
#include <memory>

#include <gtest/gtest.h>

#include <QBrush>
#include <QColor>
#include <QListWidget>
#include <QListWidgetItem>
#include <QString>

#include <atp/runtime/log_pump.hpp>

#include "model/app_state.hpp"
#include "shell/main_window.hpp"
#include "ui/qt_app.hpp"

namespace {

TEST(UiLogDock, AFormattedLineReachesTheLogDock) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = window->findChild<QListWidget*>(QStringLiteral("log.lines"));
    ASSERT_NE(dock, nullptr);

    window->report(QStringLiteral("device reconnected"));

    ASSERT_GT(dock->count(), 0);
    EXPECT_TRUE(dock->item(0)->text().endsWith(QStringLiteral(" device reconnected")));
    window.reset();
}

TEST(UiLogDock, NamesTheLevelInBracketsLikeAModuleLineDoes) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = window->findChild<QListWidget*>(QStringLiteral("log.lines"));
    ASSERT_NE(dock, nullptr);

    window->report(QStringLiteral("all is well"));
    const QString calm = dock->item(0)->text();
    window->report(QStringLiteral("it broke"), atp::log_level::error);
    const QString loud = dock->item(0)->text();

    EXPECT_TRUE(calm.contains(QStringLiteral(" [info] all is well")));
    EXPECT_TRUE(loud.contains(QStringLiteral(" [error] it broke")));
    window.reset();
}

TEST(UiLogDock, StampsTheStudiosOwnLineWithTheTimeOfDay) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = window->findChild<QListWidget*>(QStringLiteral("log.lines"));
    ASSERT_NE(dock, nullptr);

    window->report(QStringLiteral("saved"));

    ASSERT_GT(dock->count(), 0);
    const QString stamp = dock->item(0)->text().left(12);
    EXPECT_EQ(stamp.size(), 12);
    EXPECT_EQ(stamp[2], QLatin1Char(':'));
    EXPECT_EQ(stamp[5], QLatin1Char(':'));
    EXPECT_EQ(stamp[8], QLatin1Char('.'));
    window.reset();
}

TEST(UiLogDock, ColoursAnErrorAndLeavesACalmLineAlone) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = window->findChild<QListWidget*>(QStringLiteral("log.lines"));
    ASSERT_NE(dock, nullptr);

    window->report(QStringLiteral("all is well"));
    const QBrush calm = dock->item(0)->foreground();
    window->report(QStringLiteral("it broke"), atp::log_level::error);
    const QBrush loud = dock->item(0)->foreground();

    EXPECT_NE(loud.color(), calm.color());
    EXPECT_EQ(loud.color(), QColor(220, 80, 80));
    window.reset();
}

TEST(UiLogDock, DrainingAnIdleSessionAddsNothing) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = window->findChild<QListWidget*>(QStringLiteral("log.lines"));
    ASSERT_NE(dock, nullptr);
    const int before = dock->count();

    window->drain_logs();

    EXPECT_EQ(dock->count(), before);
    window.reset();
}

}  // namespace
