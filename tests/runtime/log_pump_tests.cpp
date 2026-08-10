// SPDX-License-Identifier: Apache-2.0
#include <cctype>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <atp/log_pump.hpp>
#include <atp/module.hpp>
#include <atp/pipeline.hpp>

namespace {

using namespace std::chrono_literals;

class chatty final : public atp::module<atp::io::ports<>, "chatty"> {
   public:
    void initialize(atp::module_context& context) override {
        context.host.warning("hello");
    }
};

}  // namespace

TEST(LogPump, CarriesLinesToTheSink) {
    atp::pipeline pipe;
    pipe.root().make<chatty>("talker");
    pipe.root().initialize(pipe.context());

    std::mutex mutex;
    std::vector<atp::log_line> seen;
    {
        atp::log_pump pump(
            pipe,
            [&](const atp::log_line& line) {
                const std::scoped_lock lock(mutex);
                seen.push_back(line);
            },
            5ms);
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline) {
            const std::scoped_lock lock(mutex);
            if (!seen.empty()) {
                break;
            }
        }
    }

    const std::scoped_lock lock(mutex);
    ASSERT_FALSE(seen.empty());
    EXPECT_EQ(seen[0].path, "talker");
    EXPECT_EQ(seen[0].text, "hello");
    EXPECT_EQ(seen[0].level, atp::log_level::warning);
}

TEST(LogPump, DrainsWhatIsLeftWhenItStops) {
    atp::pipeline pipe;
    pipe.root().make<chatty>("talker");
    pipe.root().initialize(pipe.context());

    std::mutex mutex;
    std::vector<atp::log_line> seen;
    {
        atp::log_pump pump(
            pipe,
            [&](const atp::log_line& line) {
                const std::scoped_lock lock(mutex);
                seen.push_back(line);
            },
            10s);
    }

    const std::scoped_lock lock(mutex);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].text, "hello");
}

TEST(LogPump, FormatsAReadableLine) {
    const atp::log_line line{"stage.counter", atp::log_level::warning, "device reconnected", false,
                             std::chrono::system_clock::now()};
    EXPECT_EQ(atp::format_log_line(line),
              atp::format_log_time(line.at) + " [warning] stage.counter: device reconnected");
}

TEST(LogPump, MarksTruncationInTheFormattedLine) {
    const atp::log_line line{"a", atp::log_level::info, "text", true, std::chrono::system_clock::now()};
    EXPECT_EQ(atp::format_log_line(line), atp::format_log_time(line.at) + " [info] a: text...");
}

TEST(LogPump, FormatsTheStampAsATimeOfDayToTheMillisecond) {
    const std::string stamp = atp::format_log_time(std::chrono::system_clock::now());
    ASSERT_EQ(stamp.size(), 12u);
    EXPECT_EQ(stamp[2], ':');
    EXPECT_EQ(stamp[5], ':');
    EXPECT_EQ(stamp[8], '.');
    for (const std::size_t digit : {0u, 1u, 3u, 4u, 6u, 7u, 9u, 10u, 11u}) {
        EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(stamp[digit]))) << stamp;
    }
}

TEST(LogPump, StampsALineWithTheMomentTheModuleWroteIt) {
    atp::pipeline pipe;
    pipe.root().make<chatty>("talker");

    const auto before = std::chrono::system_clock::now();
    pipe.root().initialize(pipe.context());
    const auto after = std::chrono::system_clock::now();

    const std::vector<atp::log_line> lines = pipe.collect_logs();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_GE(lines[0].at, before);
    EXPECT_LE(lines[0].at, after);
}

TEST(LogPump, ParsesLevelNames) {
    EXPECT_EQ(atp::level_from_name("error"), atp::log_level::error);
    EXPECT_EQ(atp::level_from_name("debug"), atp::log_level::debug);
    EXPECT_FALSE(atp::level_from_name("loud").has_value());
}
