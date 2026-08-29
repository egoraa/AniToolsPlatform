// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <optional>

#include <gtest/gtest.h>

#include <QStandardItemModel>
#include <QString>

#include "panels/log_filter.hpp"
#include "panels/log_model.hpp"
#include "ui/qt_app.hpp"

namespace {

using atp::studio::ui::log_filter;
using atp::studio::ui::log_model;
using atp::studio::ui::log_origin;
using atp::studio::ui::log_query;

void fill(log_model& model) {
    model.append({std::chrono::system_clock::now(), atp::log_level::info, log_origin::module,
                  QStringLiteral("stage.counter"), QStringLiteral("one"), false});
    model.append({std::chrono::system_clock::now(), atp::log_level::info, log_origin::module,
                  QStringLiteral("stage.mixer"), QStringLiteral("two"), false});
    model.append({std::chrono::system_clock::now(), atp::log_level::info, log_origin::system, QString(),
                  QStringLiteral("three"), false});
}

TEST(UiLogFilter, AnEmptyQueryTakesEverything) {
    (void)atp_ui_tests::ensure_app();
    log_model model;
    fill(model);
    log_filter filter;
    filter.setSourceModel(&model);

    EXPECT_EQ(filter.rowCount(), 3);
}

TEST(UiLogFilter, AnOriginNarrowsToOneKindOfWriter) {
    (void)atp_ui_tests::ensure_app();
    log_model model;
    fill(model);
    log_filter filter;
    filter.setSourceModel(&model);

    filter.set_query({log_origin::module, QString()});

    EXPECT_EQ(filter.rowCount(), 2);

    filter.set_query({log_origin::system, QString()});

    ASSERT_EQ(filter.rowCount(), 1);
    EXPECT_TRUE(filter.index(0, 0).data(Qt::DisplayRole).toString().endsWith(QStringLiteral("three")));
}

TEST(UiLogFilter, APathNarrowsToOneInstance) {
    (void)atp_ui_tests::ensure_app();
    log_model model;
    fill(model);
    log_filter filter;
    filter.setSourceModel(&model);

    filter.set_query({std::nullopt, QStringLiteral("stage.mixer")});

    ASSERT_EQ(filter.rowCount(), 1);
    EXPECT_TRUE(filter.index(0, 0).data(Qt::DisplayRole).toString().endsWith(QStringLiteral("two")));
}

TEST(UiLogFilter, ALineArrivingLaterIsJudgedByTheSameQuery) {
    (void)atp_ui_tests::ensure_app();
    log_model model;
    log_filter filter;
    filter.setSourceModel(&model);
    filter.set_query({std::nullopt, QStringLiteral("stage.mixer")});

    fill(model);

    EXPECT_EQ(filter.rowCount(), 1);
}

TEST(UiLogFilter, AModelThatNamesNoOriginIsNotTakenForTheStudio) {
    (void)atp_ui_tests::ensure_app();
    QStandardItemModel plain(2, 1);
    plain.setData(plain.index(0, 0), QStringLiteral("stage.counter"), log_model::path_role);
    plain.setData(plain.index(1, 0), QStringLiteral("stage.mixer"), log_model::path_role);
    log_filter filter;
    filter.setSourceModel(&plain);

    filter.set_query({log_origin::system, QString()});

    EXPECT_EQ(filter.rowCount(), 0);
}

TEST(UiLogFilter, TwoQueriesAskingTheSameThingAreTheSameQuery) {
    EXPECT_EQ(log_query({log_origin::module, QString()}), log_query({log_origin::module, QString()}));
    EXPECT_NE(log_query({log_origin::module, QString()}), log_query({log_origin::system, QString()}));
    EXPECT_NE(log_query({std::nullopt, QStringLiteral("a")}), log_query({std::nullopt, QStringLiteral("b")}));
}

}  // namespace
