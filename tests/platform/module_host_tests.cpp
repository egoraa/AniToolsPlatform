// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <atp/module_host.hpp>

namespace {

class recording_host final : public atp::module_host {
   public:
    void log(atp::log_level level, std::string_view text) noexcept override {
        lines.emplace_back(level, std::string(text));
    }

    void wake() noexcept override {
        wakes++;
    }

    std::vector<std::pair<atp::log_level, std::string>> lines;
    int wakes = 0;
};

}  // namespace

TEST(ModuleHost, ShorthandsCarryTheLevel) {
    recording_host host;
    host.error("e");
    host.warning("w");
    host.info("i");
    host.debug("d");

    ASSERT_EQ(host.lines.size(), 4u);
    EXPECT_EQ(host.lines[0].first, atp::log_level::error);
    EXPECT_EQ(host.lines[1].first, atp::log_level::warning);
    EXPECT_EQ(host.lines[2].first, atp::log_level::info);
    EXPECT_EQ(host.lines[3].first, atp::log_level::debug);
    EXPECT_EQ(host.lines[3].second, "d");
}

TEST(ModuleHost, WakeReachesTheImplementation) {
    recording_host host;
    host.wake();
    host.wake();
    EXPECT_EQ(host.wakes, 2);
}
