// SPDX-License-Identifier: Apache-2.0
#include <memory>

#include <gtest/gtest.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QListView>
#include <QModelIndex>
#include <QScrollBar>
#include <QString>
#include <QToolButton>

#include "model/app_state.hpp"
#include "shell/main_window.hpp"
#include "ui/qt_app.hpp"

namespace {

QListView* lines(const atp::studio::ui::main_window* window) {
    return window->findChild<QListView*>(QStringLiteral("log.lines"));
}

int line_count(const QListView* view) {
    return view->model() != nullptr ? view->model()->rowCount() : 0;
}

QModelIndex newest(const QListView* view) {
    const int rows = line_count(view);
    return rows > 0 ? view->model()->index(rows - 1, 0) : QModelIndex();
}

QString newest_text(const QListView* view) {
    return newest(view).data(Qt::DisplayRole).toString();
}

TEST(UiLogDock, AFormattedLineReachesTheLogDock) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = lines(window.get());
    ASSERT_NE(dock, nullptr);

    window->report(QStringLiteral("device reconnected"));

    ASSERT_GT(line_count(dock), 0);
    EXPECT_TRUE(newest_text(dock).endsWith(QStringLiteral(" device reconnected")));
    window.reset();
}

TEST(UiLogDock, NamesTheLevelAndTheSourceLikeAModuleLineDoes) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = lines(window.get());
    ASSERT_NE(dock, nullptr);

    window->report(QStringLiteral("all is well"));
    const QString calm = newest_text(dock);
    window->report(QStringLiteral("it broke"), atp::log_level::error);
    const QString loud = newest_text(dock);

    EXPECT_TRUE(calm.contains(QStringLiteral(" [info] system: all is well")));
    EXPECT_TRUE(loud.contains(QStringLiteral(" [error] system: it broke")));
    window.reset();
}

TEST(UiLogDock, StampsTheStudiosOwnLineWithTheTimeOfDay) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = lines(window.get());
    ASSERT_NE(dock, nullptr);

    window->report(QStringLiteral("saved"));

    ASSERT_GT(line_count(dock), 0);
    const QString stamp = newest_text(dock).left(12);
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
    auto* dock = lines(window.get());
    ASSERT_NE(dock, nullptr);

    window->report(QStringLiteral("all is well"));
    const QVariant calm = newest(dock).data(Qt::ForegroundRole);
    window->report(QStringLiteral("it broke"), atp::log_level::error);
    const QVariant loud = newest(dock).data(Qt::ForegroundRole);

    EXPECT_FALSE(calm.isValid());
    EXPECT_EQ(loud.value<QBrush>().color(), QColor(220, 80, 80));
    window.reset();
}

TEST(UiLogDock, DrainingAnIdleSessionAddsNothing) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = lines(window.get());
    ASSERT_NE(dock, nullptr);
    const int before = line_count(dock);

    window->drain_logs();

    EXPECT_EQ(line_count(dock), before);
    window.reset();
}

TEST(UiLogDock, TheStripCarriesTheFourActionsOnTheLogAndNoneUnderIt) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);

    auto* wrap = window->findChild<QToolButton*>(QStringLiteral("log.soft_wrap"));
    auto* follow = window->findChild<QToolButton*>(QStringLiteral("log.follow_tail"));
    auto* open_view = window->findChild<QToolButton*>(QStringLiteral("log.open_view"));
    auto* clear = window->findChild<QToolButton*>(QStringLiteral("log.clear"));
    ASSERT_NE(wrap, nullptr);
    ASSERT_NE(follow, nullptr);
    ASSERT_NE(open_view, nullptr);
    ASSERT_NE(clear, nullptr);

    EXPECT_TRUE(wrap->isCheckable());
    EXPECT_TRUE(follow->isCheckable());
    EXPECT_FALSE(open_view->isCheckable());
    EXPECT_FALSE(clear->isCheckable());
    EXPECT_FALSE(wrap->icon().isNull());
    EXPECT_FALSE(follow->icon().isNull());
    EXPECT_FALSE(open_view->icon().isNull());
    EXPECT_FALSE(clear->icon().isNull());
    window.reset();
}

TEST(UiLogDock, TheStripStartsWhereTheProfileLeftIt) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    state.settings.log_soft_wrap = true;
    state.settings.log_follow_tail = false;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);

    auto* wrap = window->findChild<QToolButton*>(QStringLiteral("log.soft_wrap"));
    auto* follow = window->findChild<QToolButton*>(QStringLiteral("log.follow_tail"));
    auto* dock = lines(window.get());
    ASSERT_NE(wrap, nullptr);
    ASSERT_NE(follow, nullptr);
    ASSERT_NE(dock, nullptr);

    EXPECT_TRUE(wrap->isChecked());
    EXPECT_FALSE(follow->isChecked());
    EXPECT_TRUE(dock->wordWrap());
    window.reset();
}

TEST(UiLogDock, ClearingTakesEveryLineAway) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    auto* dock = lines(window.get());
    auto* clear = window->findChild<QToolButton*>(QStringLiteral("log.clear"));
    ASSERT_NE(dock, nullptr);
    ASSERT_NE(clear, nullptr);
    window->report(QStringLiteral("something"));
    ASSERT_GT(line_count(dock), 0);

    clear->click();

    EXPECT_EQ(line_count(dock), 0);
    window.reset();
}

TEST(UiLogDock, ScrollingUpTheLogUnchecksTheFollowButton) {
    (void)atp_ui_tests::ensure_app();
    atp::studio::ui::app_state state;
    auto window = std::make_unique<atp::studio::ui::main_window>(state);
    window->resize(1600, 900);
    window->show();
    auto* dock = lines(window.get());
    auto* follow = window->findChild<QToolButton*>(QStringLiteral("log.follow_tail"));
    ASSERT_NE(dock, nullptr);
    ASSERT_NE(follow, nullptr);
    for (int i = 0; i < 400; ++i) {
        window->report(QStringLiteral("line %1").arg(i));
    }
    QApplication::processEvents();
    ASSERT_GT(dock->verticalScrollBar()->maximum(), 0);
    ASSERT_TRUE(follow->isChecked());

    dock->verticalScrollBar()->setValue(0);

    EXPECT_FALSE(follow->isChecked());

    dock->verticalScrollBar()->setValue(dock->verticalScrollBar()->maximum());

    EXPECT_TRUE(follow->isChecked());
    window.reset();
}

}  // namespace
