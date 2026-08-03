#ifndef ANITOOLSPLATFORM_LOG_RING_HPP
#define ANITOOLSPLATFORM_LOG_RING_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <atp/module_host.hpp>

namespace atp {

/// Bounded log buffer of one module: many producers, one consumer, no allocation and no mutex on
/// the writing side.
///
/// The ticket discipline is the bounded queue of Vyukov: every slot carries a sequence number, a
/// writer claims a ticket only once the slot it maps to is free, and a full ring makes the writer
/// drop its own line instead of waiting. Claiming without publishing is therefore impossible, which
/// is what keeps the reader from stopping forever at a hole that never fills.
///
/// The newest line is dropped rather than the oldest: overwriting would race the reader for a worse
/// result, and when something floods the log it is the beginning of the flood that says what
/// happened, not the last sixty-four repetitions of it.
///
/// One consumer is a contract, not a guess — two drains on one ring would each see a part of it.
/// A writer preempted between claiming and publishing holds the reader at that slot until it
/// resumes; for a diagnostic channel that is a fair price for never blocking the writer.
class log_ring {
   public:
    /// Lines held between two drains. The console hosts drain every 50 ms, so this is the flood a
    /// module has to produce within that window before anything is lost.
    static constexpr std::size_t capacity = 64;

    /// Bytes of one line. Past it the text is cut and the record says so.
    static constexpr std::size_t text_capacity = 256;

    log_ring() noexcept {
        for (std::size_t i = 0; i < capacity; ++i) {
            slots_[i].seq.store(i, std::memory_order_relaxed);
        }
    }

    log_ring(const log_ring&) = delete;
    log_ring& operator=(const log_ring&) = delete;

    /// Writes one line. Callable from any thread, allocation-free and wait-free apart from the
    /// contended ticket.
    /// @param level severity
    /// @param text the message; longer than text_capacity it is stored cut, with the flag set
    void write(log_level level, std::string_view text) noexcept {
        std::uint64_t pos = tail_.load(std::memory_order_relaxed);
        while (true) {
            slot& s = slots_[pos % capacity];
            const std::uint64_t seq = s.seq.load(std::memory_order_acquire);
            const auto diff = static_cast<std::int64_t>(seq) - static_cast<std::int64_t>(pos);
            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    const std::size_t size = std::min(text.size(), text_capacity);
                    if (size > 0) {
                        std::memcpy(s.text.data(), text.data(), size);
                    }
                    s.size = static_cast<std::uint16_t>(size);
                    s.truncated = text.size() > text_capacity;
                    s.level = level;
                    s.seq.store(pos + 1, std::memory_order_release);
                    return;
                }
            } else if (diff < 0) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    /// Hands every published line to the sink, oldest first, and frees the slots.
    /// @param sink invoked as sink(log_level, std::string_view, bool truncated); the view is valid
    ///        only for the duration of the call
    template <typename TSink>
    void drain(TSink&& sink) {
        while (true) {
            slot& s = slots_[head_ % capacity];
            const std::uint64_t seq = s.seq.load(std::memory_order_acquire);
            const auto diff = static_cast<std::int64_t>(seq) - static_cast<std::int64_t>(head_ + 1);
            if (diff != 0) {
                return;
            }
            sink(s.level, std::string_view(s.text.data(), s.size), s.truncated);
            s.seq.store(head_ + capacity, std::memory_order_release);
            ++head_;
        }
    }

    /// Lines lost to a full ring since the beginning; monotonic, so a reader reports the difference.
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

   private:
    struct slot {
        std::atomic<std::uint64_t> seq{0};
        log_level level = log_level::info;
        bool truncated = false;
        std::uint16_t size = 0;
        std::array<char, text_capacity> text{};
    };

    std::array<slot, capacity> slots_{};
    std::atomic<std::uint64_t> tail_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::uint64_t head_ = 0;
};

}  // namespace atp

#endif
