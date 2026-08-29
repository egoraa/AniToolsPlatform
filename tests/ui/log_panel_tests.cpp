// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <optional>

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QApplication>
#include <QString>
#include <QTabBar>
#include <QToolButton>

#include "panels/log_panel.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::log_entry;
using atp::studio::ui::log_origin;
using atp::studio::ui::log_panel;

log_entry from_module(const QString& path, const QString& text) {
    return {std::chrono::system_clock::now(), atp::log_level::info, log_origin::module, path, text, false};
}

log_entry from_system(const QString& text) {
    return {std::chrono::system_clock::now(), atp::log_level::info, log_origin::system, QString(), text, false};
}

int shown(const log_panel& panel) {
    return panel.current_view()->model()->rowCount();
}

QTabBar* tabs_of(const log_panel& panel) {
    return panel.findChild<QTabBar*>(QStringLiteral("log.tabs"));
}

TEST(UiLogPanel, StartsAsOneViewOfEverything) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(false, true);
    panel.append(from_module(QStringLiteral("stage.counter"), QStringLiteral("x")));
    panel.append(from_system(QStringLiteral("y")));

    EXPECT_EQ(panel.view_count(), 1);
    ASSERT_NE(panel.current_view(), nullptr);
    EXPECT_EQ(shown(panel), 2);
}

TEST(UiLogPanel, TheTabBarStaysOutOfSightWhileThereIsOnlyOneView) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(false, true);
    QTabBar* tabs = tabs_of(panel);
    ASSERT_NE(tabs, nullptr);

    EXPECT_TRUE(tabs->isHidden());

    panel.open_view({std::nullopt, QStringLiteral("stage.mixer")}, QStringLiteral("stage.mixer"));

    EXPECT_FALSE(tabs->isHidden());

    Q_EMIT tabs->tabCloseRequested(1);

    EXPECT_TRUE(tabs->isHidden());
}

TEST(UiLogPanel, TheCloseIndicatorSitsAgainstTheEdgeOfItsTab) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(false, true);
    panel.open_view({std::nullopt, QStringLiteral("stage.counter")}, QStringLiteral("stage.counter"));
    panel.resize(600, 300);
    panel.show();
    QApplication::processEvents();
    QTabBar* tabs = tabs_of(panel);
    ASSERT_NE(tabs, nullptr);
    auto* close = qobject_cast<QAbstractButton*>(tabs->tabButton(1, QTabBar::RightSide));
    ASSERT_NE(close, nullptr);

    const int gap = tabs->tabRect(1).right() - close->geometry().right();

    EXPECT_GE(gap, 0);
    EXPECT_LE(gap, 4) << "the close indicator drifted back into the middle of the tab";
}

TEST(UiLogPanel, ATabClosesFromItsOwnIndicator) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(false, true);
    panel.open_view({std::nullopt, QStringLiteral("stage.counter")}, QStringLiteral("stage.counter"));
    panel.open_view({std::nullopt, QStringLiteral("stage.mixer")}, QStringLiteral("stage.mixer"));
    QTabBar* tabs = tabs_of(panel);
    ASSERT_NE(tabs, nullptr);
    auto* close = qobject_cast<QAbstractButton*>(tabs->tabButton(1, QTabBar::RightSide));
    ASSERT_NE(close, nullptr);

    close->click();

    EXPECT_EQ(panel.view_count(), 2);
    EXPECT_EQ(tabs->tabText(1), QStringLiteral("stage.mixer"));
}

TEST(UiLogPanel, OpeningAViewOfOneInstanceShowsOnlyItsLines) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(false, true);
    panel.append(from_module(QStringLiteral("stage.counter"), QStringLiteral("x")));
    panel.append(from_module(QStringLiteral("stage.mixer"), QStringLiteral("y")));

    panel.open_view({std::nullopt, QStringLiteral("stage.mixer")}, QStringLiteral("stage.mixer"));

    EXPECT_EQ(panel.view_count(), 2);
    ASSERT_NE(panel.current_view(), nullptr);
    EXPECT_EQ(shown(panel), 1);
}

TEST(UiLogPanel, AskingTwiceForTheSameViewRaisesItInsteadOfOpeningASecond) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(false, true);
    panel.open_view({std::nullopt, QStringLiteral("stage.mixer")}, QStringLiteral("stage.mixer"));
    panel.open_view({std::nullopt, QStringLiteral("stage.counter")}, QStringLiteral("stage.counter"));
    ASSERT_EQ(panel.view_count(), 3);

    panel.open_view({std::nullopt, QStringLiteral("stage.mixer")}, QStringLiteral("stage.mixer"));

    EXPECT_EQ(panel.view_count(), 3);
    QTabBar* tabs = tabs_of(panel);
    ASSERT_NE(tabs, nullptr);
    EXPECT_EQ(tabs->tabText(tabs->currentIndex()), QStringLiteral("stage.mixer"));
}

TEST(UiLogPanel, TheViewOfEverythingCannotBeClosed) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(false, true);
    panel.open_view({std::nullopt, QStringLiteral("stage.mixer")}, QStringLiteral("stage.mixer"));
    QTabBar* tabs = tabs_of(panel);
    ASSERT_NE(tabs, nullptr);

    EXPECT_EQ(tabs->tabButton(0, QTabBar::RightSide), nullptr);
    EXPECT_NE(tabs->tabButton(1, QTabBar::RightSide), nullptr);
}

TEST(UiLogPanel, ClosingAViewLeavesTheLogAlone) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(false, true);
    panel.append(from_module(QStringLiteral("stage.mixer"), QStringLiteral("x")));
    panel.open_view({std::nullopt, QStringLiteral("stage.mixer")}, QStringLiteral("stage.mixer"));
    QTabBar* tabs = tabs_of(panel);
    ASSERT_NE(tabs, nullptr);

    Q_EMIT tabs->tabCloseRequested(1);

    EXPECT_EQ(panel.view_count(), 1);
    EXPECT_EQ(shown(panel), 1);
}

TEST(UiLogPanel, ClearingEmptiesEveryViewAtOnce) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(false, true);
    panel.append(from_module(QStringLiteral("stage.mixer"), QStringLiteral("x")));
    panel.open_view({std::nullopt, QStringLiteral("stage.mixer")}, QStringLiteral("stage.mixer"));
    ASSERT_EQ(shown(panel), 1);

    panel.clear();

    QTabBar* tabs = tabs_of(panel);
    ASSERT_NE(tabs, nullptr);
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        EXPECT_EQ(shown(panel), 0);
    }
}

TEST(UiLogPanel, WrappingIsTheDocksAnswerAndReachesAViewOpenedLater) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(true, true);

    panel.open_view({std::nullopt, QStringLiteral("stage.mixer")}, QStringLiteral("stage.mixer"));

    EXPECT_TRUE(panel.current_view()->soft_wrap());

    panel.set_soft_wrap(false);

    EXPECT_FALSE(panel.current_view()->soft_wrap());
}

TEST(UiLogPanel, WrappingMovesTheButtonThatStandsForIt) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(true, true);
    auto* wrap = panel.findChild<QToolButton*>(QStringLiteral("log.soft_wrap"));
    ASSERT_NE(wrap, nullptr);
    bool told = false;
    panel.on_soft_wrap_changed([&told](bool) { told = true; });

    panel.set_soft_wrap(false);

    EXPECT_FALSE(wrap->isChecked());
    EXPECT_FALSE(told);
}

TEST(UiLogPanel, FollowingTheTailIsEachViewsOwnAnswer) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(false, true);
    panel.open_view({std::nullopt, QStringLiteral("stage.mixer")}, QStringLiteral("stage.mixer"));
    QTabBar* tabs = tabs_of(panel);
    auto* follow = panel.findChild<QToolButton*>(QStringLiteral("log.follow_tail"));
    ASSERT_NE(tabs, nullptr);
    ASSERT_NE(follow, nullptr);

    panel.current_view()->set_follow_tail(false);
    tabs->setCurrentIndex(0);

    EXPECT_TRUE(panel.current_view()->follow_tail());
    EXPECT_TRUE(follow->isChecked());

    tabs->setCurrentIndex(1);

    EXPECT_FALSE(panel.current_view()->follow_tail());
    EXPECT_FALSE(follow->isChecked());
}

TEST(UiLogPanel, TheStripCarriesFourActionsAndEveryOneOfThemWearsAnIcon) {
    (void)atp_ui_tests::ensure_app();
    log_panel panel(false, true);

    for (const QString& name : {QStringLiteral("log.soft_wrap"), QStringLiteral("log.follow_tail"),
                                QStringLiteral("log.clear"), QStringLiteral("log.open_view")}) {
        auto* button = panel.findChild<QToolButton*>(name);
        ASSERT_NE(button, nullptr) << name.toStdString();
        EXPECT_FALSE(button->icon().isNull()) << name.toStdString();
    }
}

}  // namespace
