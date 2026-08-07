// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include <atp/runtime/command_queue.hpp>

namespace {

TEST(CommandQueue, RunsTheCallOnTheOwnerThreadAndReturnsItsResult) {
    atp::runtime::command_queue queue;
    const std::thread::id owner = std::this_thread::get_id();
    std::thread::id ran_on;

    std::thread caller([&] {
        const int answer = queue.call([&] {
            ran_on = std::this_thread::get_id();
            return 42;
        });
        EXPECT_EQ(answer, 42);
    });

    while (ran_on == std::thread::id{}) {
        queue.run_pending(std::chrono::milliseconds(20));
    }
    caller.join();
    EXPECT_EQ(ran_on, owner);
}

TEST(CommandQueue, RethrowsOnTheCallingThread) {
    atp::runtime::command_queue queue;
    std::atomic<bool> ran{false};

    std::thread caller([&] {
        EXPECT_THROW((void)queue.call([&]() -> int {
            ran.store(true);
            throw std::runtime_error("boom");
        }),
                     std::runtime_error);
    });

    while (!ran.load()) {
        queue.run_pending(std::chrono::milliseconds(20));
    }
    caller.join();
}

TEST(CommandQueue, ClosingFailsTheWaiterInsteadOfLeavingItBlocked) {
    atp::runtime::command_queue queue;
    std::thread caller([&] { EXPECT_THROW((void)queue.call([] { return 1; }), std::runtime_error); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue.close();
    caller.join();

    EXPECT_THROW((void)queue.call([] { return 1; }), std::runtime_error);
}

TEST(CommandQueue, ReturnsAfterTheTimeoutWhenNothingIsQueued) {
    atp::runtime::command_queue queue;
    const auto started = std::chrono::steady_clock::now();
    queue.run_pending(std::chrono::milliseconds(30));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_GE(elapsed, std::chrono::milliseconds(20));
    EXPECT_LT(elapsed, std::chrono::milliseconds(2000));
}

}  // namespace
