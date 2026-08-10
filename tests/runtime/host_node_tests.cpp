// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <atp/host_node.hpp>
#include <atp/module.hpp>
#include <atp/pipeline.hpp>

namespace {

class talker final : public atp::module<atp::io::ports<>, "talker"> {
   public:
    void initialize(atp::module_context& context) override {
        host = &context.host;
        context.host.info("initialised");
    }

    atp::module_host* host = nullptr;
};

}  // namespace

TEST(HostNode, EachChildGetsItsOwnHost) {
    atp::pipeline pipe;
    auto& first = pipe.root().make<talker>("first");
    auto& second = pipe.root().make<talker>("second");
    pipe.root().initialize(pipe.context());

    ASSERT_NE(first.host, nullptr);
    ASSERT_NE(second.host, nullptr);
    EXPECT_NE(first.host, second.host);
}

TEST(HostNode, CollectedLinesCarryTheModulePath) {
    atp::pipeline pipe;
    atp::group& stage = pipe.root().add_group("stage");
    stage.make<talker>("counter");
    pipe.root().initialize(pipe.context());

    const std::vector<atp::log_line> lines = pipe.collect_logs();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].path, "stage.counter");
    EXPECT_EQ(lines[0].text, "initialised");
    EXPECT_EQ(lines[0].level, atp::log_level::info);
    EXPECT_FALSE(lines[0].truncated);
}

TEST(HostNode, DrainingTwiceYieldsNothingTheSecondTime) {
    atp::pipeline pipe;
    pipe.root().make<talker>("one");
    pipe.root().initialize(pipe.context());

    EXPECT_EQ(pipe.collect_logs().size(), 1u);
    EXPECT_TRUE(pipe.collect_logs().empty());
}

TEST(HostNode, LoggingWithoutAnAttachedNotifierDoesNotWake) {
    atp::host_node node;
    node.wake();
    node.info("still alive");

    std::vector<std::string> texts;
    node.ring().drain([&texts](atp::log_level, std::string_view text, bool, std::chrono::system_clock::time_point) {
        texts.emplace_back(text);
    });
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_EQ(texts[0], "still alive");
}
