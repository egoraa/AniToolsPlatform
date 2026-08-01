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

/// Queueing input: values accumulate in FIFO order instead of overwriting each other, without a
/// size limit. Inherits reception and synchronisation from input<T> and overrides store().
template <typename T>
class queued_input : public input<T> {
   public:
    /// @param name input name, unique within its registry
    /// @param s whether this instance serialises access
    explicit queued_input(std::string name, safety s = safe) : input<T>(std::move(name), s) {}

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
    }

   protected:
    /// Appends to the queue tail instead of replacing the last value. Called by the base under the
    /// lock.
    void store(T&& value) override {
        queue_.push_back(std::move(value));
    }

   private:
    std::deque<T> queue_;
};

}  // namespace atp::io

#endif
