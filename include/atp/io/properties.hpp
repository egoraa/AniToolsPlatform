// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_PROPERTIES_HPP
#define ANITOOLSPLATFORM_IO_PROPERTIES_HPP

#include <concepts>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include <atp/io/io_registry.hpp>
#include <atp/io/property.hpp>
#include <atp/io/property_base.hpp>

namespace atp::io {

/// Owning registry of properties, the mirror of inputs/outputs: same machinery, same declaration
/// pattern, with the default value and the tags forwarded to the constructor:
///
///     property<int>& limit = make("limit", 10);
///     property<std::string>& file = make<std::string>("file", "", transient);
///     property<int>& channels = make("channels", 2, allowed(1, 2, 6));
class properties : public detail::io_registry<property_base> {
   public:
    properties() : io_registry("property") {}

    /// Declares a property, deducing its type from the default value: make("gain", 0.5) is a
    /// property<double>.
    ///
    /// The explicit form stays and is not redundant — make<std::string>("file", "", transient),
    /// because deducing from "" would give const char*, which is no property type. T may also be a
    /// property type spelled in full, which is what c_module writes when it builds one from a
    /// runtime kind.
    ///
    /// One template with a void sentinel rather than two overloads, because two would be ambiguous
    /// for make<int>("limit", 10): the explicit form would take int as T, the deducing one as the
    /// value type, and both are viable. if constexpr also keeps property<void> from ever being named
    /// in a discarded branch. The value stays in the pack rather than being a parameter of its own,
    /// because the explicit form is allowed to omit it — make<property<std::string>>("tag") declares
    /// one that defaults to a default-constructed value.
    /// @param name registration name
    /// @param args default value first, then tags such as `transient` or `allowed(...)`
    /// @throws std::runtime_error if the name is already taken
    template <typename T = void, typename... TArgs>
    auto& make(std::string name, TArgs&&... args) {
        if constexpr (std::is_void_v<T>) {
            static_assert(sizeof...(TArgs) > 0,
                          "a property declared without an explicit type needs a default value to deduce it from");
            using deduced = std::decay_t<std::tuple_element_t<0, std::tuple<TArgs..., int>>>;
            return io_registry::make<property<deduced>>(std::move(name), std::forward<TArgs>(args)...);
        } else if constexpr (std::derived_from<T, property_base>) {
            return io_registry::make<T>(std::move(name), std::forward<TArgs>(args)...);
        } else {
            return io_registry::make<property<T>>(std::move(name), std::forward<TArgs>(args)...);
        }
    }
};

}  // namespace atp::io

#endif
