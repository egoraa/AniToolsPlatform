// SPDX-License-Identifier: Apache-2.0
#include <chrono>
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
    const atp::log_line line{"stage.counter", atp::log_level::warning, "device reconnected", false};
    EXPECT_EQ(atp::format_log_line(line), "[warning] stage.counter: device reconnected");
}

TEST(LogPump, MarksTruncationInTheFormattedLine) {
    const atp::log_line line{"a", atp::log_level::info, "text", true};
    EXPECT_EQ(atp::format_log_line(line), "[info] a: text...");
}

TEST(LogPump, ParsesLevelNames) {
    EXPECT_EQ(atp::level_from_name("error"), atp::log_level::error);
    EXPECT_EQ(atp::level_from_name("debug"), atp::log_level::debug);
    EXPECT_FALSE(atp::level_from_name("loud").has_value());
}
