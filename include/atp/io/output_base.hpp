// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_IO_OUTPUT_BASE_HPP
#define ANITOOLSPLATFORM_IO_OUTPUT_BASE_HPP

#include <cstddef>
#include <cstdint>

#include <atp/io/input_base.hpp>
#include <atp/io/io_base.hpp>

namespace atp::io {

/// Type-erased base of an output: what the outputs registry stores and what the connect-by-name
/// machinery works with.
///
/// Type compatibility is checked at runtime inside do_connect, an incompatible input is rejected
/// with an exception. output<T> deliberately hides these overloads behind typed ones, so that a
/// concrete output catches type errors at compile time while the runtime path stays available
/// through this base.
class output_base : public io_base {
   public:
    using io_base::io_base;

    /// Connects an input.
    /// @throws std::runtime_error if the input does not accept this output's type
    void connect(input_base& in) {
        do_connect(in);
    }

    /// Breaks the connection to @p in, identified by address.
    /// @return false if the input was not connected
    virtual bool disconnect(const input_base& in) = 0;

    /// Breaks all connections of this output.
    virtual void disconnect_all() = 0;

    /// Number of currently connected inputs.
    [[nodiscard]] virtual std::size_t connections() const = 0;

    /// Write generation, for tooling: a change between polls means the link was active. Unsafe
    /// instances are not observable and report 0.
    ///
    /// This is all an output can report about what travelled it: it keeps no copy of the value, so
    /// that a write pays for the readers it has rather than for the ones it might have.
    [[nodiscard]] virtual std::uint64_t write_count() const = 0;

   private:
    virtual void do_connect(input_base& in) = 0;
};

}  // namespace atp::io

#endif
