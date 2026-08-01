#ifndef ANITOOLSPLATFORM_IO_OUTPUT_BASE_HPP
#define ANITOOLSPLATFORM_IO_OUTPUT_BASE_HPP

#include <any>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <atp/io/input_base.hpp>
#include <atp/io/io_base.hpp>

namespace atp::io {

/// Tag requesting delivery of the cached value on connect: `out.connect(in, replay)`.
struct replay_t {};
inline constexpr replay_t replay{};

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

    /// Connects an input without replaying the cached value.
    /// @throws std::runtime_error if the input does not accept this output's type
    void connect(input_base& in) {
        do_connect(in, false);
    }

    /// Connects an input and immediately delivers the cached value, if there is one.
    /// @throws std::runtime_error if the input does not accept this output's type
    void connect(input_base& in, replay_t) {
        do_connect(in, true);
    }

    /// Breaks the connection to @p in, identified by address.
    /// @return false if the input was not connected
    virtual bool disconnect(const input_base& in) = 0;

    /// Breaks all connections of this output.
    virtual void disconnect_all() = 0;

    /// Number of currently connected inputs.
    [[nodiscard]] virtual std::size_t connections() const = 0;

    /// Type-erased snapshot of the cached value, for tooling. Reading happens under the output's
    /// lock, so only safe instances are observable; an unsafe one reports nullopt.
    [[nodiscard]] virtual std::optional<std::any> peek() const = 0;

    /// Write generation, for tooling: a change between polls means the link was active. Unsafe
    /// instances are not observable and report 0.
    [[nodiscard]] virtual std::uint64_t write_count() const = 0;

   private:
    virtual void do_connect(input_base& in, bool deliver_cached) = 0;
};

}  // namespace atp::io

#endif
