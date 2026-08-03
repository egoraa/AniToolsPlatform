#ifndef ANITOOLSPLATFORM_IO_OUTPUT_HPP
#define ANITOOLSPLATFORM_IO_OUTPUT_HPP

#include <algorithm>
#include <any>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include <atp/io/input.hpp>
#include <atp/io/output_base.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

/// Output: push delivery to the connected inputs plus a cache of the last written value.
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

    /// Writes a value: caches it and delivers it to every connected input.
    template <typename TArg>
        requires std::constructible_from<T, TArg>
    void operator()(TArg&& value) {
        T incoming(std::forward<TArg>(value));
        subscriber_list targets;
        {
            auto guard = lock();
            targets = targets_;
            value_ = incoming;
            ++writes_;
        }
        for (input_base* in : *targets) {
            in->deliver(&incoming, input_base::erased_of<T>());
        }
    }

    /// Connects a typed input; a type mismatch is a compile error. The type-erased base overloads
    /// are hidden deliberately (see output_base).
    /// @throws std::runtime_error if the input is already connected
    void connect(input<T>& in) {
        attach(in, false);
    }

    /// Connects a typed input and immediately delivers the cached value, if there is one.
    /// @throws std::runtime_error if the input is already connected
    void connect(input<T>& in, replay_t) {
        attach(in, true);
    }

    /// A universal input connects to any output statically; the requires clause keeps this pair
    /// from clashing with the typed one when T is std::any.
    /// @throws std::runtime_error if the input is already connected
    void connect(input<std::any>& in)
        requires(!std::same_as<T, std::any>)
    {
        attach(in, false);
    }

    /// Connects a universal input and immediately delivers the cached value, if there is one.
    /// @throws std::runtime_error if the input is already connected
    void connect(input<std::any>& in, replay_t)
        requires(!std::same_as<T, std::any>)
    {
        attach(in, true);
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

    /// Whether nothing has been written yet.
    [[nodiscard]] bool empty() const {
        auto guard = lock();
        return !value_.has_value();
    }

    /// Copy of the cached value; a reference would be a race, since another thread may overwrite
    /// it at any moment.
    /// @throws std::runtime_error if nothing has been written yet
    [[nodiscard]] T get() const {
        auto guard = lock();
        if (!value_) {
            throw std::runtime_error("output '" + name() + "' has no value");
        }
        return *value_;
    }

    [[nodiscard]] std::optional<std::any> peek() const override {
        if (!thread_safe()) {
            return std::nullopt;
        }
        auto guard = lock();
        if (!value_) {
            return std::nullopt;
        }
        return std::any(*value_);
    }

    [[nodiscard]] std::uint64_t write_count() const override {
        if (!thread_safe()) {
            return 0;
        }
        auto guard = lock();
        return writes_;
    }

    /// Clears the cache only; connections survive — use disconnect_all() to break them.
    void reset() override {
        auto guard = lock();
        value_.reset();
    }

   private:
    /// Registers the input and, for a replay connect, takes a copy of the cached value to deliver
    /// once the lock is gone — delivery never runs under the output's own lock.
    ///
    /// Unwrapping the optional and letting `snapshot` be assigned the value rather than the whole
    /// optional is deliberate, though it reads as a round trip: assigning the optional directly
    /// changes how GCC 13 inlines the replay path into a universal input and trips its known
    /// -Warray-bounds false positive inside <any>, which -Werror then turns into a failed build.
    void attach(input_base& in, bool deliver_cached) {
        std::optional<T> snapshot;
        {
            auto guard = lock();
            if (std::find(targets_->begin(), targets_->end(), &in) != targets_->end()) {
                throw std::runtime_error("input '" + in.name() + "' is already connected to output '" + name() + "'");
            }
            auto next = std::make_shared<std::vector<input_base*>>(*targets_);
            next->push_back(&in);
            targets_ = std::move(next);
            if (deliver_cached && value_) {
                // NOLINTNEXTLINE(bugprone-optional-value-conversion)
                snapshot = *value_;
            }
        }
        if (snapshot) {
            in.deliver(&*snapshot, input_base::erased_of<T>());
        }
    }

    void do_connect(input_base& in, bool deliver_cached) override {
        if (!in.accepts(typeid(T))) {
            throw std::runtime_error("input '" + in.name() + "' is not compatible with output '" + name() + "'");
        }
        attach(in, deliver_cached);
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
    std::optional<T> value_;
    std::uint64_t writes_ = 0;
};

}  // namespace atp::io

#endif
