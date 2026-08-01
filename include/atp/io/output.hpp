#ifndef ANITOOLSPLATFORM_IO_OUTPUT_HPP
#define ANITOOLSPLATFORM_IO_OUTPUT_HPP

#include <algorithm>
#include <any>
#include <concepts>
#include <cstddef>
#include <cstdint>
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
    template <typename U>
        requires std::constructible_from<T, U>
    void operator()(U&& value) {
        T incoming(std::forward<U>(value));
        std::vector<input_base*> targets;
        {
            auto guard = lock();
            targets = targets_;
            value_ = incoming;
            ++writes_;
        }
        for (input_base* in : targets) {
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
        auto it = std::find(targets_.begin(), targets_.end(), &in);
        if (it == targets_.end()) {
            return false;
        }
        targets_.erase(it);
        return true;
    }

    void disconnect_all() override {
        auto guard = lock();
        targets_.clear();
    }

    [[nodiscard]] std::size_t connections() const override {
        auto guard = lock();
        return targets_.size();
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
    void attach(input_base& in, bool deliver_cached) {
        std::optional<T> snapshot;
        {
            auto guard = lock();
            if (std::find(targets_.begin(), targets_.end(), &in) != targets_.end()) {
                throw std::runtime_error("input '" + in.name() + "' is already connected to output '" + name() + "'");
            }
            targets_.push_back(&in);
            if (deliver_cached && value_) {
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

    std::vector<input_base*> targets_;
    std::optional<T> value_;
    std::uint64_t writes_ = 0;
};

}  // namespace atp::io

#endif
