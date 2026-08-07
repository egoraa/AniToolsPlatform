// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QKeyEvent>
#include <QString>

#include "panels/log_widget.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::log_widget;

log_widget& filled(log_widget& log) {
    log.addItem(QStringLiteral("first"));
    log.addItem(QStringLiteral("second"));
    log.addItem(QStringLiteral("third"));
    return log;
}

TEST(UiLogWidget, TakesMoreThanOneLineAtATime) {
    (void)atp_ui_tests::ensure_app();
    log_widget log;
    filled(log);

    EXPECT_EQ(log.selectionMode(), QAbstractItemView::ExtendedSelection);

    log.item(0)->setSelected(true);
    log.item(2)->setSelected(true);
    EXPECT_EQ(log.selectedItems().size(), 2);
}

TEST(UiLogWidget, CopiesTheSelectionInTheOrderItIsShown) {
    (void)atp_ui_tests::ensure_app();
    log_widget log;
    filled(log);

    log.item(2)->setSelected(true);
    log.item(0)->setSelected(true);

    EXPECT_EQ(log.selected_text(), QStringLiteral("first\nthird"));
}

TEST(UiLogWidget, SelectAllTakesEveryLine) {
    (void)atp_ui_tests::ensure_app();
    log_widget log;
    filled(log);

    log.selectAll();

    EXPECT_EQ(log.selected_text(), QStringLiteral("first\nsecond\nthird"));
}

TEST(UiLogWidget, CtrlCPutsTheSelectionOnTheClipboard) {
    (void)atp_ui_tests::ensure_app();
    log_widget log;
    filled(log);
    log.item(1)->setSelected(true);
    QApplication::clipboard()->setText(QStringLiteral("something else"));

    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(&log, &copy);

    EXPECT_EQ(QApplication::clipboard()->text(), QStringLiteral("second"));
}

TEST(UiLogWidget, CopyingNothingLeavesTheClipboardAlone) {
    (void)atp_ui_tests::ensure_app();
    log_widget log;
    filled(log);
    QApplication::clipboard()->setText(QStringLiteral("kept"));

    log.copy_selection();

    EXPECT_EQ(QApplication::clipboard()->text(), QStringLiteral("kept"));
}

}  // namespace
