// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_OUTPUTS_HPP
#define ANITOOLSPLATFORM_IO_OUTPUTS_HPP

#include <concepts>
#include <string>
#include <utility>

#include <atp/io/io_registry.hpp>
#include <atp/io/output.hpp>
#include <atp/io/output_base.hpp>

namespace atp::io {

/// Owning registry of outputs. An heir declares them as reference members:
///
///     output<int>& result = make<int>("result");
///     output<int>& fast = make<int>("fast", unsafe);
///
/// All the machinery (get/at/find/remove/list) lives in detail::io_registry.
class outputs : public detail::io_registry<output_base> {
   public:
    outputs() : io_registry("output") {}

    /// Declares an output, the mirror of inputs::make: T is the payload type unless it is itself a
    /// kind of output, and the explicit spelling stays for the code that builds a port from a
    /// runtime kind.
    /// @param name registration name
    /// @param args forwarded to the port's constructor after the name
    /// @throws std::runtime_error if the name is already taken
    template <typename T, typename... TArgs>
    auto& make(std::string name, TArgs&&... args) {
        if constexpr (std::derived_from<T, output_base>) {
            return io_registry::make<T>(std::move(name), std::forward<TArgs>(args)...);
        } else {
            return io_registry::make<output<T>>(std::move(name), std::forward<TArgs>(args)...);
        }
    }
};

}  // namespace atp::io

#endif
