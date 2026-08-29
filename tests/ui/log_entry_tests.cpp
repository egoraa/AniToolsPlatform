// SPDX-License-Identifier: Apache-2.0
#include <chrono>

#include <gtest/gtest.h>

#include <QString>

#include "panels/log_entry.hpp"

namespace {

using atp::studio::ui::log_entry;
using atp::studio::ui::log_origin;
using atp::studio::ui::render_log_line;

log_entry module_line(const QString& text) {
    return {std::chrono::system_clock::now(),
            atp::log_level::warning,
            log_origin::module,
            QStringLiteral("stage.counter"),
            text,
            false};
}

TEST(UiLogEntry, NamesTheInstanceThatWroteTheLine) {
    EXPECT_TRUE(render_log_line(module_line(QStringLiteral("reconnected")))
                    .contains(QStringLiteral(" [warning] stage.counter: reconnected")));
}

TEST(UiLogEntry, SaysSystemWhenTheStudioWroteIt) {
    const log_entry entry{std::chrono::system_clock::now(),
                          atp::log_level::info,
                          log_origin::system,
                          QString(),
                          QStringLiteral("saved"),
                          false};

    EXPECT_TRUE(render_log_line(entry).contains(QStringLiteral(" [info] system: saved")));
}

TEST(UiLogEntry, ATruncatedMessageEndsInAnEllipsis) {
    log_entry entry = module_line(QStringLiteral("cut here"));
    entry.truncated = true;

    EXPECT_TRUE(render_log_line(entry).endsWith(QStringLiteral("cut here...")));
}

TEST(UiLogEntry, StampsTheLineWithTheTimeOfDayToTheMillisecond) {
    const QString stamp = render_log_line(module_line(QStringLiteral("x"))).left(12);

    ASSERT_EQ(stamp.size(), 12);
    EXPECT_EQ(stamp[2], QLatin1Char(':'));
    EXPECT_EQ(stamp[5], QLatin1Char(':'));
    EXPECT_EQ(stamp[8], QLatin1Char('.'));
}

}  // namespace
