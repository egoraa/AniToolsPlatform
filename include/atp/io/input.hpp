// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_INPUT_HPP
#define ANITOOLSPLATFORM_IO_INPUT_HPP

#include <algorithm>
#include <any>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>

#include <atp/io/input_base.hpp>
#include <atp/io/threading.hpp>
#include <atp/type_compare.hpp>

namespace atp::io {

/// Base input and, at the same time, the "last value wins" input.
///
/// Other input kinds derive from it (see queued_input); the single extension point is the
/// protected virtual store(), where operator() puts the accepted value. Reading is pull-only:
/// get() copies the state, take() removes an event; reacting to new values is done by polling
/// from the consumer thread (see watcher). Delivery runs no user code — the writer's thread only
/// executes store() under the lock.
template <typename T>
class input : public input_base {
   public:
    /// @param name input name, unique within its registry
    /// @param s whether this instance serialises access
    explicit input(std::string name, safety s = safe) : input_base(std::move(name), typeid(T), s) {}

    /// Writes a value into the input. Accepts lvalues, rvalues and anything T is constructible
    /// from.
    ///
    /// A trivially copyable value goes straight into storage under the lock: its move is the same
    /// memcpy as its copy, so materialising it first would pay for two of them and put a full
    /// payload on the writer's stack. A type with a real move keeps the copy outside the lock, where
    /// an allocation does not lengthen the critical section, and its stack cost is the shell rather
    /// than the contents. A converting write materialises above heap_copy_threshold on the heap.
    template <typename TArg>
        requires std::constructible_from<T, TArg>
    void operator()(TArg&& value) {
        if constexpr (!std::same_as<std::remove_cvref_t<TArg>, T>) {
            if constexpr (sizeof(T) <= heap_copy_threshold) {
                T converted(std::forward<TArg>(value));
                auto guard = lock();
                accept(std::move(converted));
            } else {
                auto converted = std::make_unique<T>(std::forward<TArg>(value));
                auto guard = lock();
                accept(std::move(*converted));
            }
        } else if constexpr (!std::is_lvalue_reference_v<TArg>) {
            auto guard = lock();
            accept(std::forward<TArg>(value));
        } else if constexpr (std::is_trivially_copyable_v<T>) {
            auto guard = lock();
            accept(value);
        } else {
            T incoming(value);
            auto guard = lock();
            accept(std::move(incoming));
        }
    }

    /// What this input received and what it lost, as an instant snapshot.
    [[nodiscard]] input_stats stats() const override {
        if (!thread_safe()) {
            return {};
        }
        auto guard = lock();
        return {received_, discarded_, pending_count(), peak_pending_, capacity_count()};
    }

    /// Whether the input holds nothing to read. Heirs reinterpret it for their own storage.
    [[nodiscard]] virtual bool empty() const {
        auto guard = lock();
        return !value_.has_value();
    }

    /// Copy of the stored value; a reference would be a race, since another thread may overwrite
    /// it at any moment.
    /// @throws std::runtime_error if the input has no value
    [[nodiscard]] T get() const {
        auto guard = lock();
        if (!value_) {
            throw std::runtime_error("input '" + name() + "' has no value");
        }
        return *value_;
    }

    /// Removes and returns the pending value, nullopt if there was none. The counterpart of
    /// get(): a "state" input is read many times with get(), an "event" input handles every value
    /// exactly once with take(). Virtual, so taking through a base reference works for every input
    /// kind (queued_input hands out the queue head).
    [[nodiscard]] virtual std::optional<T> take() {
        auto guard = lock();
        std::optional<T> out = std::move(value_);
        value_.reset();
        return out;
    }

    void reset() override {
        auto guard = lock();
        value_.reset();
        reset_counters();
    }

    [[nodiscard]] bool accepts(std::type_index produced) const override {
        if constexpr (std::same_as<T, std::any>) {
            return true;
        } else {
            return same_type(produced, type());
        }
    }

   protected:
    /// Extension point deciding where an accepted value goes; "last value wins" by default.
    /// Called under the lock.
    ///
    /// It is a pair, and an input kind overriding one half must override the other: the inherited
    /// half would write into this class's storage instead of its own, and nothing would say so.
    /// Which half a write takes depends on whether the writer owns the value, not on the input.
    virtual void store(T&& value) {
        note_overwrite();
        value_.emplace(std::move(value));
        note_pending(1u);
    }

    /// Copying half of the extension point, for a value the writer does not hand over (see above).
    virtual void store(const T& value) {
        note_overwrite();
        value_.emplace(value);
        note_pending(1u);
    }

    /// How many values are readable right now. An heir with storage of its own answers for it; the
    /// count is what stats() reports as pending, so a kind that keeps a queue says how deep it is.
    [[nodiscard]] virtual std::size_t pending_count() const {
        return value_.has_value() ? 1u : 0u;
    }

    /// How many unread values this input can hold. One, for "last value wins" — the second value
    /// arriving before the first is read displaces it, which is exactly a capacity of one.
    [[nodiscard]] virtual std::size_t capacity_count() const {
        return 1u;
    }

    /// Records the readable count a store just produced, keeping the run's maximum. An heir
    /// overriding store calls it with its own count: the alternative, asking the virtual
    /// pending_count() after every write, puts a second indirect call on the write path to learn
    /// a number the heir already had in a local.
    void note_pending(std::size_t pending) {
        peak_pending_ = std::max(peak_pending_, pending);
    }

    /// Returns the counters to their just-constructed state. Called with the lock already held, so
    /// that an heir clearing its own storage does it in the same critical section rather than in a
    /// second one, where a reader could see the storage empty and the counters still full.
    void reset_counters() {
        received_ = 0;
        discarded_ = 0;
        peak_pending_ = 0;
    }

    /// Values accepted but never made readable. An heir that discards on its own terms — a full
    /// queue refusing one — maintains it; the base counts the displaced value of an overwrite.
    std::uint64_t discarded_ = 0;

   private:
    /// The one funnel every accepted value passes through, whatever route operator() took to get
    /// here. Counting arrivals here rather than in store() is deliberate: store() is the documented
    /// extension point, an heir overriding it is not required to chain to the base, and an input
    /// kind that legitimately keeps no storage would then report having received nothing at all.
    template <typename TValue>
    void accept(TValue&& value) {
        ++received_;
        store(std::forward<TValue>(value));
    }

    void note_overwrite() {
        if (value_) {
            ++discarded_;
        }
    }

    void do_deliver(const void* value, const erased_type& meta) override {
        if constexpr (std::same_as<T, std::any>) {
            if (meta.type == typeid(std::any)) {
                (*this)(*static_cast<const std::any*>(value));
            } else {
                (*this)(meta.box(value));
            }
        } else {
            (*this)(*static_cast<const T*>(value));
        }
    }

    void do_deliver_move(void* value, const erased_type& meta) override {
        if constexpr (std::same_as<T, std::any>) {
            if (meta.type == typeid(std::any)) {
                (*this)(std::move(*static_cast<std::any*>(value)));
            } else {
                (*this)(meta.box(value));
            }
        } else {
            (*this)(std::move(*static_cast<T*>(value)));
        }
    }

    std::optional<T> value_;
    std::uint64_t received_ = 0;
    std::size_t peak_pending_ = 0;
};

}  // namespace atp::io

#endif
