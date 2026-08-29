// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <optional>

#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QScrollBar>
#include <QString>
#include <QStringList>

#include "panels/log_filter.hpp"
#include "panels/log_model.hpp"
#include "panels/log_view.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::log_filter;
using atp::studio::ui::log_model;
using atp::studio::ui::log_origin;
using atp::studio::ui::log_view;

/// A view over a model of its own, the arrangement log_panel builds for every tab.
struct wired {
    log_model model;
    log_filter filter;
    log_view view;

    wired() {
        filter.setSourceModel(&model);
        view.setModel(&filter);
    }

    void say(const QString& text) {
        model.append(
            {std::chrono::system_clock::now(), atp::log_level::info, log_origin::system, QString(), text, false});
    }

    void select(int row) {
        view.selectionModel()->select(filter.index(row, 0), QItemSelectionModel::Select);
    }
};

/// A log with more lines than its viewport can hold, so that the vertical scrollbar has somewhere to
/// go — which is what every question about following the tail is really asking about.
void scrollable(wired& w) {
    w.view.resize(200, 60);
    w.view.show();
    for (int i = 0; i < 200; ++i) {
        w.say(QStringLiteral("line %1").arg(i));
    }
    QApplication::processEvents();
}

TEST(UiLogView, TakesMoreThanOneLineAtATime) {
    (void)atp_ui_tests::ensure_app();
    wired w;
    w.say(QStringLiteral("first"));
    w.say(QStringLiteral("second"));
    w.say(QStringLiteral("third"));

    EXPECT_EQ(w.view.selectionMode(), QAbstractItemView::ExtendedSelection);

    w.select(0);
    w.select(2);
    EXPECT_EQ(w.view.selectionModel()->selectedIndexes().size(), 2);
}

TEST(UiLogView, CopiesTheSelectionInTheOrderItIsShown) {
    (void)atp_ui_tests::ensure_app();
    wired w;
    w.say(QStringLiteral("first"));
    w.say(QStringLiteral("second"));
    w.say(QStringLiteral("third"));

    w.select(2);
    w.select(0);

    const QStringList lines = w.view.selected_text().split(QLatin1Char('\n'));
    ASSERT_EQ(lines.size(), 2);
    EXPECT_TRUE(lines[0].endsWith(QStringLiteral("first")));
    EXPECT_TRUE(lines[1].endsWith(QStringLiteral("third")));
}

TEST(UiLogView, SelectAllTakesEveryLine) {
    (void)atp_ui_tests::ensure_app();
    wired w;
    w.say(QStringLiteral("first"));
    w.say(QStringLiteral("second"));

    w.view.selectAll();

    EXPECT_EQ(w.view.selected_text().split(QLatin1Char('\n')).size(), 2);
}

TEST(UiLogView, CtrlCPutsTheSelectionOnTheClipboard) {
    (void)atp_ui_tests::ensure_app();
    wired w;
    w.say(QStringLiteral("second"));
    w.select(0);
    QApplication::clipboard()->setText(QStringLiteral("something else"));

    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(&w.view, &copy);

    EXPECT_TRUE(QApplication::clipboard()->text().endsWith(QStringLiteral("second")));
}

TEST(UiLogView, CopyingNothingLeavesTheClipboardAlone) {
    (void)atp_ui_tests::ensure_app();
    wired w;
    w.say(QStringLiteral("first"));
    QApplication::clipboard()->setText(QStringLiteral("kept"));

    w.view.copy_selection();

    EXPECT_EQ(QApplication::clipboard()->text(), QStringLiteral("kept"));
}

TEST(UiLogView, TheNewestLineIsTheLastOneTheWayAConsoleReads) {
    (void)atp_ui_tests::ensure_app();
    wired w;

    w.say(QStringLiteral("older"));
    w.say(QStringLiteral("newer"));

    ASSERT_EQ(w.filter.rowCount(), 2);
    EXPECT_TRUE(w.filter.index(0, 0).data(Qt::DisplayRole).toString().endsWith(QStringLiteral("older")));
    EXPECT_TRUE(w.filter.index(1, 0).data(Qt::DisplayRole).toString().endsWith(QStringLiteral("newer")));
}

TEST(UiLogView, ALineTooLongForTheDockIsNeitherWrappedNorElidedByDefault) {
    (void)atp_ui_tests::ensure_app();
    log_view view;

    EXPECT_FALSE(view.soft_wrap());
    EXPECT_FALSE(view.wordWrap());
    EXPECT_EQ(view.textElideMode(), Qt::ElideNone);
    EXPECT_EQ(view.horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
}

TEST(UiLogView, SoftWrapWrapsTheLineAndTakesTheSidewaysScrollingAway) {
    (void)atp_ui_tests::ensure_app();
    log_view view;

    view.set_soft_wrap(true);

    EXPECT_TRUE(view.soft_wrap());
    EXPECT_TRUE(view.wordWrap());
    EXPECT_EQ(view.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);

    view.set_soft_wrap(false);

    EXPECT_FALSE(view.wordWrap());
    EXPECT_EQ(view.horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
}

TEST(UiLogView, FollowingTheTailKeepsTheViewAtTheEndAsLinesArrive) {
    (void)atp_ui_tests::ensure_app();
    wired w;
    scrollable(w);

    ASSERT_GT(w.view.verticalScrollBar()->maximum(), 0);
    EXPECT_TRUE(w.view.follow_tail());
    EXPECT_EQ(w.view.verticalScrollBar()->value(), w.view.verticalScrollBar()->maximum());
}

TEST(UiLogView, ScrollingAwayFromTheEndStopsFollowingAndSaysSo) {
    (void)atp_ui_tests::ensure_app();
    wired w;
    scrollable(w);
    std::optional<bool> told;
    w.view.on_follow_tail_changed([&told](bool on) { told = on; });
    ASSERT_GT(w.view.verticalScrollBar()->maximum(), 0);

    w.view.verticalScrollBar()->setValue(0);

    EXPECT_FALSE(w.view.follow_tail());
    ASSERT_TRUE(told.has_value());
    EXPECT_FALSE(*told);
}

TEST(UiLogView, ScrollingBackToTheEndFollowsAgain) {
    (void)atp_ui_tests::ensure_app();
    wired w;
    scrollable(w);
    w.view.verticalScrollBar()->setValue(0);
    ASSERT_FALSE(w.view.follow_tail());

    w.view.verticalScrollBar()->setValue(w.view.verticalScrollBar()->maximum());

    EXPECT_TRUE(w.view.follow_tail());
}

TEST(UiLogView, ANewLineDoesNotMoveTheViewOfSomebodyReadingFurtherUp) {
    (void)atp_ui_tests::ensure_app();
    wired w;
    scrollable(w);
    w.view.verticalScrollBar()->setValue(0);
    ASSERT_FALSE(w.view.follow_tail());

    w.say(QStringLiteral("and one more"));
    QApplication::processEvents();

    EXPECT_EQ(w.view.verticalScrollBar()->value(), 0);
}

TEST(UiLogView, AskingToFollowAgainTakesTheViewToTheEndAtOnce) {
    (void)atp_ui_tests::ensure_app();
    wired w;
    scrollable(w);
    w.view.verticalScrollBar()->setValue(0);
    ASSERT_FALSE(w.view.follow_tail());

    w.view.set_follow_tail(true);

    EXPECT_TRUE(w.view.follow_tail());
    EXPECT_EQ(w.view.verticalScrollBar()->value(), w.view.verticalScrollBar()->maximum());
}

TEST(UiLogView, DroppingTheOldestLinesDoesNotUnstickTheViewFromTheTail) {
    (void)atp_ui_tests::ensure_app();
    wired w;
    scrollable(w);
    ASSERT_TRUE(w.view.follow_tail());

    for (int i = 0; i < log_model::max_lines + 50; ++i) {
        w.say(QStringLiteral("flood %1").arg(i));
    }
    QApplication::processEvents();

    EXPECT_TRUE(w.view.follow_tail());
    EXPECT_EQ(w.view.verticalScrollBar()->value(), w.view.verticalScrollBar()->maximum());
}

}  // namespace
