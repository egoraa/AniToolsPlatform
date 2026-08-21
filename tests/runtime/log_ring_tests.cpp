// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include <atp/runtime/log_ring.hpp>

namespace {

std::vector<std::string> drain_texts(atp::runtime::log_ring& ring) {
    std::vector<std::string> out;
    ring.drain([&out](atp::log_level, std::string_view text, bool, std::chrono::system_clock::time_point) {
        out.emplace_back(text);
    });
    return out;
}

}  // namespace

TEST(LogRing, DrainsInWriteOrder) {
    atp::runtime::log_ring ring;
    ring.write(atp::log_level::info, "one");
    ring.write(atp::log_level::warning, "two");

    const std::vector<std::string> texts = drain_texts(ring);
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0], "one");
    EXPECT_EQ(texts[1], "two");
    EXPECT_TRUE(drain_texts(ring).empty());
}

TEST(LogRing, CarriesTheLevel) {
    atp::runtime::log_ring ring;
    ring.write(atp::log_level::error, "boom");

    atp::log_level seen = atp::log_level::debug;
    ring.drain(
        [&seen](atp::log_level level, std::string_view, bool, std::chrono::system_clock::time_point) { seen = level; });
    EXPECT_EQ(seen, atp::log_level::error);
}

TEST(LogRing, StampsTheMomentOfTheWrite) {
    atp::runtime::log_ring ring;

    const auto before = std::chrono::system_clock::now();
    ring.write(atp::log_level::info, "now");
    const auto after = std::chrono::system_clock::now();

    std::chrono::system_clock::time_point seen{};
    ring.drain(
        [&seen](atp::log_level, std::string_view, bool, std::chrono::system_clock::time_point at) { seen = at; });
    EXPECT_GE(seen, before);
    EXPECT_LE(seen, after);
}

TEST(LogRing, TruncatesAndSaysSo) {
    atp::runtime::log_ring ring;
    const std::string long_text(atp::runtime::log_ring::text_capacity + 10, 'x');
    ring.write(atp::log_level::info, long_text);

    std::string text;
    bool truncated = false;
    ring.drain([&](atp::log_level, std::string_view t, bool cut, std::chrono::system_clock::time_point) {
        text = t;
        truncated = cut;
    });
    EXPECT_EQ(text.size(), atp::runtime::log_ring::text_capacity);
    EXPECT_TRUE(truncated);
}

TEST(LogRing, EmptyTextIsAllowed) {
    atp::runtime::log_ring ring;
    ring.write(atp::log_level::info, "");

    const std::vector<std::string> texts = drain_texts(ring);
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_TRUE(texts[0].empty());
}

TEST(LogRing, DropsTheNewestWhenFullAndCounts) {
    atp::runtime::log_ring ring;
    for (std::size_t i = 0; i < atp::runtime::log_ring::capacity + 5; ++i) {
        ring.write(atp::log_level::info, std::to_string(i));
    }
    EXPECT_EQ(ring.dropped(), 5u);

    const std::vector<std::string> texts = drain_texts(ring);
    ASSERT_EQ(texts.size(), atp::runtime::log_ring::capacity);
    EXPECT_EQ(texts.front(), "0");
    EXPECT_EQ(texts.back(), std::to_string(atp::runtime::log_ring::capacity - 1));
}

TEST(LogRing, SlotsAreReusedAfterDraining) {
    atp::runtime::log_ring ring;
    for (std::size_t round = 0; round < 4; ++round) {
        for (std::size_t i = 0; i < atp::runtime::log_ring::capacity; ++i) {
            ring.write(atp::log_level::info, "x");
        }
        EXPECT_EQ(drain_texts(ring).size(), atp::runtime::log_ring::capacity);
    }
    EXPECT_EQ(ring.dropped(), 0u);
}

TEST(LogRing, ManyProducersLoseNothingButWhatItCounts) {
    constexpr int producers = 4;
    constexpr int per_producer = 500;

    atp::runtime::log_ring ring;
    std::unordered_set<std::string> seen;
    std::atomic<bool> stop{false};

    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            ring.drain([&seen](atp::log_level, std::string_view text, bool, std::chrono::system_clock::time_point) {
                seen.emplace(text);
            });
        }
        ring.drain([&seen](atp::log_level, std::string_view text, bool, std::chrono::system_clock::time_point) {
            seen.emplace(text);
        });
    });

    {
        std::vector<std::jthread> writers;
        writers.reserve(producers);
        for (int t = 0; t < producers; ++t) {
            writers.emplace_back([&ring, t] {
                for (int i = 0; i < per_producer; ++i) {
                    ring.write(atp::log_level::info, "t" + std::to_string(t) + "-" + std::to_string(i));
                }
            });
        }
    }
    stop.store(true, std::memory_order_relaxed);
    reader.join();

    EXPECT_EQ(seen.size() + ring.dropped(), static_cast<std::size_t>(producers) * per_producer);
    for (const std::string& text : seen) {
        EXPECT_EQ(text.find('t'), 0u);
        EXPECT_NE(text.find('-'), std::string::npos);
    }
}
