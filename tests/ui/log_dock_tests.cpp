// SPDX-License-Identifier: Apache-2.0
#include <memory>

#include <gtest/gtest.h>

#include <QListWidget>
#include <QListWidgetItem>
#include <QString>

#include <atp/log_pump.hpp>

#include "model/app_state.hpp"
#include "shell/main_window.hpp"
#include "ui/qt_app.hpp"

namespace {

TEST(UiLogDock, AFormattedLineReachesTheErrorsDock) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = window->findChild<QListWidget*>(QStringLiteral("log.errors"));
    ASSERT_NE(dock, nullptr);

    const atp::log_line line{"stage.counter", atp::log_level::warning, "device reconnected", false};
    window->report(QString::fromStdString(atp::format_log_line(line)));

    ASSERT_GT(dock->count(), 0);
    EXPECT_EQ(dock->item(0)->text(), QStringLiteral("[warning] stage.counter: device reconnected"));
    window.reset();
}

TEST(UiLogDock, DrainingAnIdleSessionAddsNothing) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = window->findChild<QListWidget*>(QStringLiteral("log.errors"));
    ASSERT_NE(dock, nullptr);
    const int before = dock->count();

    window->drain_logs();

    EXPECT_EQ(dock->count(), before);
    window.reset();
}

}  // namespace
