// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_QUEUED_INPUT_HPP
#define ANITOOLSPLATFORM_IO_QUEUED_INPUT_HPP

#include <cstddef>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <atp/io/input.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

/// What a full queue does with a value that has nowhere to go.
enum class overflow_policy {
    drop_oldest,
    drop_incoming,
};

/// How many values a queue holds and what happens at that number.
///
/// There is deliberately no way to say "no limit". An unbounded queue between threads of different
/// pacing is not a configuration but the platform's default way to die: a producer faster than its
/// consumer grows it until the process runs out of memory, with no error, no warning and no metric
/// along the way. Blocking the writer was considered as the fourth policy and refused — the wait
/// would sit on the writer's thread inside deliver(), which deadlocks two modules that feed each
/// other and needs waking on stop; so was failing the pipeline, which can only be done by throwing
/// from store() and would break delivery halfway down a subscriber list, taking out the module that
/// happens to be writing rather than the one that overflowed.
struct queue_limit {
    std::size_t capacity;
    overflow_policy policy;
};

/// Capacity whose overflow trims the queue head: the live-data choice, where the consumer should
/// always get the freshest values and an old one is worth less than a new one.
[[nodiscard]] constexpr queue_limit drop_oldest(std::size_t n) noexcept {
    return {n, overflow_policy::drop_oldest};
}

/// Capacity whose overflow refuses the arriving value and leaves the queue as it was: the choice
/// where an unbroken run of values matters more than having its latest end.
[[nodiscard]] constexpr queue_limit drop_incoming(std::size_t n) noexcept {
    return {n, overflow_policy::drop_incoming};
}

/// Queueing input: values accumulate in FIFO order instead of overwriting each other, up to a
/// declared capacity. Inherits reception and synchronisation from input<T> and overrides store().
///
/// The capacity has a default rather than being mandatory, so that it is not a rewrite of every
/// existing declaration. The price is that a forgotten limit loses data silently instead of growing
/// silently, and stats().discarded is what makes that visible: an operator sees the number rise,
/// which is a diagnosis rather than the missing one it replaces.
template <typename T>
class queued_input : public input<T> {
   public:
    /// @param name input name, unique within its registry
    /// @param limit queue capacity and what overflow does to it
    /// @param s whether this instance serialises access
    /// @throws std::invalid_argument if the capacity is zero, which is an input accepting nothing
    explicit queued_input(std::string name, queue_limit limit = drop_oldest(32), safety s = safe)
        : input<T>(std::move(name), s), limit_(limit) {
        if (limit_.capacity == 0) {
            throw std::invalid_argument("input '" + this->name() + "' cannot have a zero capacity");
        }
    }

    /// How many values the queue holds before its policy applies.
    [[nodiscard]] std::size_t capacity() const noexcept {
        return limit_.capacity;
    }

    /// What overflow does to this queue. Deliberately absent from input_stats: an operator needs to
    /// know that values are being lost, and the rule by which they are lost is a property of the
    /// module's design, readable in its source.
    [[nodiscard]] overflow_policy policy() const noexcept {
        return limit_.policy;
    }

    /// Whether the queue is empty. An instant snapshot: it may be stale by the next line.
    [[nodiscard]] bool empty() const override {
        auto guard = this->lock();
        return queue_.empty();
    }

    /// Number of queued values. An instant snapshot: it may be stale by the next line.
    [[nodiscard]] std::size_t size() const {
        auto guard = this->lock();
        return queue_.size();
    }

    /// Removes and returns the queue head.
    /// @throws std::runtime_error if the queue is empty
    [[nodiscard]] T pop() {
        auto guard = this->lock();
        if (queue_.empty()) {
            throw std::runtime_error("input '" + this->name() + "' queue is empty");
        }
        T front = std::move(queue_.front());
        queue_.pop_front();
        return front;
    }

    /// Removes and returns the queue head, nullopt if the queue is empty.
    [[nodiscard]] std::optional<T> try_pop() {
        auto guard = this->lock();
        if (queue_.empty()) {
            return std::nullopt;
        }
        std::optional<T> front{std::move(queue_.front())};
        queue_.pop_front();
        return front;
    }

    /// Taking the next value means taking the FIFO head (see input::take).
    [[nodiscard]] std::optional<T> take() override {
        return try_pop();
    }

    /// Hands out the whole queue under a single lock acquisition — the cheapest way to process a
    /// batch.
    [[nodiscard]] std::deque<T> drain() {
        std::deque<T> out;
        {
            auto guard = this->lock();
            out.swap(queue_);
        }
        return out;
    }

    void reset() override {
        auto guard = this->lock();
        queue_.clear();
        this->reset_counters();
    }

   protected:
    /// Appends to the queue tail instead of replacing the last value, applying the capacity first.
    /// Called by the base under the lock, which is what makes trimming the head cheap: the writer
    /// already holds the very lock a reader would have to take.
    ///
    /// A value the writer handed over through deliver_move is simply destroyed when the incoming
    /// one loses. The move was wasted, not wrong — overflow is not worth complicating the delivery
    /// protocol for.
    void store(T&& value) override {
        if (make_room()) {
            queue_.push_back(std::move(value));
        }
        this->note_pending(queue_.size());
    }

    /// Copying half of the same extension point (see input::store): both halves are overridden
    /// together, or a write the writer does not own would land in the base's storage.
    void store(const T& value) override {
        if (make_room()) {
            queue_.push_back(value);
        }
        this->note_pending(queue_.size());
    }

    [[nodiscard]] std::size_t pending_count() const override {
        return queue_.size();
    }

    [[nodiscard]] std::size_t capacity_count() const override {
        return limit_.capacity;
    }

   private:
    [[nodiscard]] bool make_room() {
        if (queue_.size() < limit_.capacity) {
            return true;
        }
        ++this->discarded_;
        if (limit_.policy == overflow_policy::drop_incoming) {
            return false;
        }
        queue_.pop_front();
        return true;
    }

    queue_limit limit_;
    std::deque<T> queue_;
};

}  // namespace atp::io

#endif
