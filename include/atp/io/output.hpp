// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_OUTPUT_HPP
#define ANITOOLSPLATFORM_IO_OUTPUT_HPP

#include <algorithm>
#include <any>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include <atp/io/input.hpp>
#include <atp/io/output_base.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

/// Output: push delivery to the connected inputs.
///
/// Keeps no copy of what it wrote. An output used to cache the last value for tooling to read, which
/// made every write pay a copy of the payload for a reader that polls a few times a second — and for
/// the headless runs that have no reader at all.
///
/// Compatibility is decided by the input itself through accepts(), once per connect; there are no
/// hierarchy casts at all. Delivery runs outside the output's lock — every input takes its own
/// mutex, so locks never nest, and only store() runs on the writer's thread. The delivery order
/// under concurrent writes is not guaranteed. Connected inputs are owned by the caller:
/// disconnect() before destroying an input.
template <typename T>
class output : public output_base {
   public:
    /// @param name output name, unique within its registry
    /// @param s whether this instance serialises access
    explicit output(std::string name, safety s = safe) : output_base(std::move(name), typeid(T), s) {}

    /// Writes a value: delivers it to every connected input.
    ///
    /// The consistent snapshot every subscriber sees is the caller's own object — it outlives the
    /// call — so a write of an exact-type value materialises nothing at all. Only a converting write
    /// needs a T of its own, and that one goes to the heap above heap_copy_threshold, which is what
    /// keeps a large payload from overflowing the writer's stack.
    template <typename TArg>
        requires std::constructible_from<T, TArg>
    void operator()(TArg&& value) {
        subscriber_list targets;
        {
            auto guard = lock();
            targets = targets_;
            ++writes_;
        }
        if constexpr (std::same_as<std::remove_cvref_t<TArg>, T>) {
            dispatch(*targets, std::forward<TArg>(value));
        } else if constexpr (sizeof(T) <= heap_copy_threshold) {
            T converted(std::forward<TArg>(value));
            dispatch(*targets, std::move(converted));
        } else {
            auto converted = std::make_unique<T>(std::forward<TArg>(value));
            dispatch(*targets, std::move(*converted));
        }
    }

    /// Connects a typed input; a type mismatch is a compile error. The type-erased base overloads
    /// are hidden deliberately (see output_base).
    /// @throws std::runtime_error if the input is already connected
    void connect(input<T>& in) {
        attach(in);
    }

    /// A universal input connects to any output statically; the requires clause keeps this pair
    /// from clashing with the typed one when T is std::any.
    /// @throws std::runtime_error if the input is already connected
    void connect(input<std::any>& in)
        requires(!std::same_as<T, std::any>)
    {
        attach(in);
    }

    bool disconnect(const input_base& in) override {
        auto guard = lock();
        if (std::find(targets_->begin(), targets_->end(), &in) == targets_->end()) {
            return false;
        }
        auto next = std::make_shared<std::vector<input_base*>>();
        next->reserve(targets_->size() - 1);
        std::ranges::copy_if(*targets_, std::back_inserter(*next), [&](const input_base* t) { return t != &in; });
        targets_ = std::move(next);
        return true;
    }

    void disconnect_all() override {
        auto guard = lock();
        targets_ = std::make_shared<const std::vector<input_base*>>();
    }

    [[nodiscard]] std::size_t connections() const override {
        auto guard = lock();
        return targets_->size();
    }

    [[nodiscard]] std::uint64_t write_count() const override {
        if (!thread_safe()) {
            return 0;
        }
        auto guard = lock();
        return writes_;
    }

    /// Zeroes the write counter; connections survive — use disconnect_all() to break them.
    void reset() override {
        auto guard = lock();
        writes_ = 0;
    }

   private:
    /// Hands the value to every subscriber. Runs outside the output's lock: an input takes its own
    /// mutex in store(), and nesting the two would be a lock order to reason about.
    ///
    /// A value the writer owns is handed over to the **last** subscriber rather than copied into it.
    /// The delivery order does not change: the move goes to whoever would have been served last
    /// anyway, and everyone before it still sees the value intact.
    template <typename TValue>
    void dispatch(const std::vector<input_base*>& targets, TValue&& value) {
        if (targets.empty()) {
            return;
        }
        const input_base::erased_type& meta = input_base::erased_of<T>();
        for (std::size_t i = 0; i + 1 < targets.size(); ++i) {
            targets[i]->deliver(&value, meta);
        }
        if constexpr (std::is_lvalue_reference_v<TValue>) {
            targets.back()->deliver(&value, meta);
        } else {
            targets.back()->deliver_move(&value, meta);
        }
    }

    void attach(input_base& in) {
        auto guard = lock();
        if (std::find(targets_->begin(), targets_->end(), &in) != targets_->end()) {
            throw std::runtime_error("input '" + in.name() + "' is already connected to output '" + name() + "'");
        }
        auto next = std::make_shared<std::vector<input_base*>>(*targets_);
        next->push_back(&in);
        targets_ = std::move(next);
    }

    void do_connect(input_base& in) override {
        if (!in.accepts(typeid(T))) {
            throw std::runtime_error("input '" + in.name() + "' is not compatible with output '" + name() + "'");
        }
        attach(in);
    }

    /// Copy-on-write subscriber list. The write path must iterate the subscribers outside the
    /// output's lock — an input takes its own mutex in store(), and nesting the two would be a lock
    /// order to reason about — so it needs the list to stay alive without holding the lock. It used
    /// to copy the vector for that, which is a heap allocation on every value written to every
    /// output, to reproduce a list that only changes during setup. Sharing an immutable vector gives
    /// the same guarantee for a refcount bump: connect and disconnect build a new vector and swap
    /// the pointer, paying an allocation where allocations are affordable.
    ///
    /// Never null, so the write path dereferences without a branch.
    using subscriber_list = std::shared_ptr<const std::vector<input_base*>>;

    subscriber_list targets_ = std::make_shared<const std::vector<input_base*>>();
    std::uint64_t writes_ = 0;
};

}  // namespace atp::io

#endif
