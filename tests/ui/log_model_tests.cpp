// SPDX-License-Identifier: Apache-2.0
#include <chrono>

#include <gtest/gtest.h>

#include <QBrush>
#include <QColor>
#include <QString>
#include <QStringList>

#include "panels/log_model.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::log_entry;
using atp::studio::ui::log_model;
using atp::studio::ui::log_origin;

log_entry from_module(const QString& path, const QString& text) {
    return {std::chrono::system_clock::now(), atp::log_level::info, log_origin::module, path, text, false};
}

log_entry from_system(const QString& text, atp::log_level level = atp::log_level::info) {
    return {std::chrono::system_clock::now(), level, log_origin::system, QString(), text, false};
}

QString line_at(const log_model& model, int row) {
    return model.index(row, 0).data(Qt::DisplayRole).toString();
}

TEST(UiLogModel, KeepsLinesInTheOrderTheyArrived) {
    (void)atp_ui_tests::ensure_app();
    log_model model;

    model.append(from_system(QStringLiteral("older")));
    model.append(from_system(QStringLiteral("newer")));

    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_TRUE(line_at(model, 0).endsWith(QStringLiteral("older")));
    EXPECT_TRUE(line_at(model, 1).endsWith(QStringLiteral("newer")));
}

TEST(UiLogModel, AnswersTheOriginAndThePathAsRoles) {
    (void)atp_ui_tests::ensure_app();
    log_model model;

    model.append(from_module(QStringLiteral("stage.counter"), QStringLiteral("x")));
    model.append(from_system(QStringLiteral("y")));

    EXPECT_EQ(model.index(0, 0).data(log_model::origin_role).toInt(), static_cast<int>(log_origin::module));
    EXPECT_EQ(model.index(0, 0).data(log_model::path_role).toString(), QStringLiteral("stage.counter"));
    EXPECT_EQ(model.index(1, 0).data(log_model::origin_role).toInt(), static_cast<int>(log_origin::system));
    EXPECT_TRUE(model.index(1, 0).data(log_model::path_role).toString().isEmpty());
}

TEST(UiLogModel, ColoursAnErrorAndLeavesACalmLineAlone) {
    (void)atp_ui_tests::ensure_app();
    log_model model;

    model.append(from_system(QStringLiteral("all is well")));
    model.append(from_system(QStringLiteral("it broke"), atp::log_level::error));

    EXPECT_FALSE(model.index(0, 0).data(Qt::ForegroundRole).isValid());
    EXPECT_EQ(model.index(1, 0).data(Qt::ForegroundRole).value<QBrush>().color(), QColor(220, 80, 80));
}

TEST(UiLogModel, DropsTheOldestPastTheLimit) {
    (void)atp_ui_tests::ensure_app();
    log_model model;
    for (int i = 0; i < log_model::max_lines + 5; ++i) {
        model.append(from_system(QStringLiteral("line %1").arg(i)));
    }

    EXPECT_EQ(model.rowCount(), log_model::max_lines);
    EXPECT_TRUE(line_at(model, 0).endsWith(QStringLiteral("line 5")));
    EXPECT_TRUE(line_at(model, model.rowCount() - 1).endsWith(QStringLiteral("line %1").arg(log_model::max_lines + 4)));
}

TEST(UiLogModel, RemembersEveryPathInTheOrderItFirstAppeared) {
    (void)atp_ui_tests::ensure_app();
    log_model model;

    model.append(from_module(QStringLiteral("b"), QStringLiteral("x")));
    model.append(from_module(QStringLiteral("a"), QStringLiteral("x")));
    model.append(from_module(QStringLiteral("b"), QStringLiteral("x")));
    model.append(from_system(QStringLiteral("x")));

    EXPECT_EQ(model.paths(), QStringList({QStringLiteral("b"), QStringLiteral("a")}));
}

TEST(UiLogModel, EvictionDoesNotForgetAPath) {
    (void)atp_ui_tests::ensure_app();
    log_model model;
    model.append(from_module(QStringLiteral("early"), QStringLiteral("x")));
    for (int i = 0; i < log_model::max_lines; ++i) {
        model.append(from_system(QStringLiteral("line %1").arg(i)));
    }

    EXPECT_TRUE(model.paths().contains(QStringLiteral("early")));
}

TEST(UiLogModel, ClearingTakesTheLinesAndThePathsAway) {
    (void)atp_ui_tests::ensure_app();
    log_model model;
    model.append(from_module(QStringLiteral("stage.counter"), QStringLiteral("x")));

    model.clear();

    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_TRUE(model.paths().isEmpty());
}

}  // namespace
