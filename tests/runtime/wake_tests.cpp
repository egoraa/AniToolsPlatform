// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stop_token>
#include <thread>

#include <gtest/gtest.h>

#include <atp/module.hpp>
#include <atp/runtime/pipeline.hpp>
#include <atp/runtime/pipeline_runner.hpp>

namespace {

using namespace std::chrono_literals;

class sleeper final : public atp::module<atp::ports<>, "sleeper"> {
   public:
    void initialize(atp::module_context& context) override {
        host = &context.host;
    }

    atp::work_status iterate(std::stop_token) override {
        passes.fetch_add(1, std::memory_order_relaxed);
        return atp::work_status::idle;
    }

    atp::module_host* host = nullptr;
    std::atomic<std::uint64_t> passes{0};
};

}  // namespace

TEST(Wake, WakesTheOnDemandThreadWellBeforeTheBackoffCap) {
    atp::runtime::pipeline pipe;
    auto& module = pipe.root().make<sleeper>("sleeper");
    atp::runtime::pipeline_runner runner;
    runner.add_thread("main", {.mode = atp::runtime::thread_mode::on_demand});
    runner.start(pipe);

    std::this_thread::sleep_for(50ms);
    const std::uint64_t before = module.passes.load(std::memory_order_relaxed);

    const auto sent = std::chrono::steady_clock::now();
    module.host->wake();
    while (module.passes.load(std::memory_order_relaxed) == before && std::chrono::steady_clock::now() - sent < 1s) {
        std::this_thread::yield();
    }
    const auto latency = std::chrono::steady_clock::now() - sent;

    EXPECT_GT(module.passes.load(std::memory_order_relaxed), before);
    EXPECT_LT(latency, atp::runtime::pipeline_runner::idle_sleep_cap);
    runner.stop();
}

TEST(Wake, IsANoOpBeforeStartAndAfterStop) {
    atp::runtime::pipeline pipe;
    auto& module = pipe.root().make<sleeper>("sleeper");
    pipe.root().initialize(pipe.context());
    module.host->wake();

    atp::runtime::pipeline_runner runner;
    runner.add_thread("main", {.mode = atp::runtime::thread_mode::on_demand});
    runner.start(pipe);
    runner.stop();
    module.host->wake();

    SUCCEED();
}

TEST(Wake, IsANoOpOnAThrottledThread) {
    atp::runtime::pipeline pipe;
    auto& module = pipe.root().make<sleeper>("sleeper");
    atp::runtime::pipeline_runner runner;
    runner.add_thread("main", {.mode = atp::runtime::thread_mode::throttled, .period = 200ms});
    runner.start(pipe);

    std::this_thread::sleep_for(50ms);
    const std::uint64_t before = module.passes.load(std::memory_order_relaxed);
    module.host->wake();
    std::this_thread::sleep_for(50ms);

    EXPECT_EQ(module.passes.load(std::memory_order_relaxed), before);
    runner.stop();
}
