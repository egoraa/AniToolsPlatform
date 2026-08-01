#ifndef ANITOOLSPLATFORM_IO_INPUT_HPP
#define ANITOOLSPLATFORM_IO_INPUT_HPP

#include <any>
#include <concepts>
#include <optional>
#include <stdexcept>
#include <string>
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
    template <typename U>
        requires std::constructible_from<T, U>
    void operator()(U&& value) {
        T incoming(std::forward<U>(value));
        auto guard = lock();
        store(std::move(incoming));
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
    virtual void store(T&& value) {
        value_.emplace(std::move(value));
    }

   private:
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

    std::optional<T> value_;
};

}  // namespace atp::io

#endif
