// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_INPUTS_HPP
#define ANITOOLSPLATFORM_IO_INPUTS_HPP

#include <concepts>
#include <string>
#include <utility>

#include <atp/io/input.hpp>
#include <atp/io/input_base.hpp>
#include <atp/io/io_registry.hpp>

namespace atp::io {

/// Owning registry of inputs. An heir declares them as reference members:
///
///     input<int>& number = make<int>("number");
///     input<int>& fast = make<int>("fast", unsafe);
///     queued_input<int>& events = make<queued_input<int>>("events", drop_oldest(64));
///
/// All the machinery (get/at/find/remove/list) lives in detail::io_registry.
class inputs : public detail::io_registry<input_base> {
   public:
    inputs() : io_registry("input") {}

    /// Declares an input. T is the payload type — make<int>("count") is an input<int> — unless T is
    /// itself a kind of input, and then it is taken as written:
    /// make<queued_input<int>>("events", drop_oldest(64)).
    ///
    /// Both spellings matter, which is why the short one is a fork rather than a replacement: the
    /// short form is what a module author writes, the explicit one is what c_module writes when it
    /// builds a port from a runtime atp_kind and the type comes from a template parameter rather
    /// than from the source. This hides io_registry::make, so the base form is reached through a
    /// qualified call below — an unqualified one would recurse.
    /// @param name registration name
    /// @param args forwarded to the port's constructor after the name
    /// @throws std::runtime_error if the name is already taken
    template <typename T, typename... TArgs>
    auto& make(std::string name, TArgs&&... args) {
        if constexpr (std::derived_from<T, input_base>) {
            return io_registry::make<T>(std::move(name), std::forward<TArgs>(args)...);
        } else {
            return io_registry::make<input<T>>(std::move(name), std::forward<TArgs>(args)...);
        }
    }
};

}  // namespace atp::io

#endif
