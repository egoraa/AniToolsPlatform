// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_PROPERTY_HPP
#define ANITOOLSPLATFORM_IO_PROPERTY_HPP

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include <atp/io/option_set.hpp>
#include <atp/io/property_base.hpp>
#include <atp/io/property_codec.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

/// Typed property: a module setting with a default value. Unlike an input it always holds a value,
/// so get() never throws. Writing mirrors an input (T is constructed outside the lock, no user code
/// runs on the writer's thread), reading is pull-only: get() for state, take() for the "changed
/// since the last take" event. Every write raises the changed flag — there is deliberately no
/// comparison with the old value, so T needs no equality.
template <property_value T>
class property : public property_base {
   public:
    /// @param name property name, unique within its registry
    /// @param default_value initial value; T{} for an enum means the value 0, and if the name table
    ///        has no such option the constructor rejects it — spell the default out
    /// @param p whether the value is written to the config on save
    /// @param s whether this instance serialises access
    /// @throws std::invalid_argument if the default is outside the type-level value set
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    explicit property(std::string name, T default_value = T{}, persistence p = atp::io::persistent, safety s = safe)
        : property_base(std::move(name), typeid(T), property_codec<T>::kind, detail::type_options<T>(), p, s)
        , default_(checked(std::move(default_value)))
        , value_(default_) {}

    /// Declares the property with an instance-level value set, listed right at the port. The
    /// type-level set (an enum name table) is replaced rather than extended — this is how a module
    /// narrows an enum down to the subset it supports.
    /// @throws std::invalid_argument if the default is outside the set
    template <typename TValue>
        requires std::constructible_from<T, const TValue&>
    property(std::string name,
             T default_value,
             const option_set<TValue>& allowed,
             persistence p = atp::io::persistent,
             safety s = safe)
        : property_base(std::move(name), typeid(T), property_codec<T>::kind, detail::render_options<T>(allowed), p, s)
        , default_(checked(std::move(default_value)))
        , value_(default_) {}

    /// Writes a value and raises the changed flag.
    /// @throws std::invalid_argument if the value is outside the value set
    template <typename TArg>
        requires std::constructible_from<T, TArg>
    void operator()(TArg&& value) {
        T incoming = checked(T(std::forward<TArg>(value)));
        auto guard = lock();
        value_ = std::move(incoming);
        changed_ = true;
    }

    /// Copy of the current value; a reference would race with a concurrent write.
    [[nodiscard]] T get() const {
        auto guard = lock();
        return value_;
    }

    /// The value if it changed since the last take, clearing the flag; nullopt otherwise. The same
    /// state/event pair as get/take on an input.
    [[nodiscard]] std::optional<T> take() {
        auto guard = lock();
        if (!changed_) {
            return std::nullopt;
        }
        changed_ = false;
        return value_;
    }

    [[nodiscard]] bool changed() const override {
        auto guard = lock();
        return changed_;
    }

    /// Restores the default value. That, too, is a change — the module has to learn about the
    /// rollback.
    void reset() override {
        auto guard = lock();
        value_ = default_;
        changed_ = true;
    }

    [[nodiscard]] std::string to_string() const override {
        auto guard = lock();
        return property_codec<T>::to_string(value_);
    }

    void from_string(std::string_view text) override {
        std::optional<T> parsed = property_codec<T>::from_string(text);
        if (!parsed) {
            throw std::invalid_argument("property '" + name() + "': cannot parse '" + std::string(text) + "'" +
                                        options_hint());
        }
        T incoming = checked(std::move(*parsed));
        auto guard = lock();
        value_ = std::move(incoming);
        changed_ = true;
    }

    [[nodiscard]] std::string default_string() const override {
        return property_codec<T>::to_string(default_);
    }

   private:
    [[nodiscard]] std::string options_hint() const {
        if (options().empty()) {
            return {};
        }
        std::string hint = " (expected one of: ";
        for (std::size_t i = 0; i < options().size(); ++i) {
            hint += (i == 0 ? "" : ", ") + options()[i];
        }
        return hint + ")";
    }

    [[nodiscard]] T checked(T value) const {
        if (!options().empty()) {
            const std::string text = property_codec<T>::to_string(value);
            if (std::ranges::find(options(), text) == options().end()) {
                throw std::invalid_argument("property '" + name() + "': value '" + text + "' is not allowed" +
                                            options_hint());
            }
        }
        return value;
    }

    T default_;
    T value_;
    bool changed_ = false;
};

}  // namespace atp::io

#endif
