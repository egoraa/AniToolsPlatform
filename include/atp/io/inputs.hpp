#ifndef ANITOOLSPLATFORM_IO_INPUTS_HPP
#define ANITOOLSPLATFORM_IO_INPUTS_HPP

#include <string>

#include <atp/io/input_base.hpp>
#include <atp/io/io_registry.hpp>

namespace atp::io {

/// Owning registry of inputs. An heir declares them as reference members:
///
///     input<int>& number = make<input<int>>("number");
///     input<int>& fast = make<input<int>>("fast", unsafe);
///     queued_input<int>& events = make<queued_input<int>>("events");
///
/// All the machinery (make/get/at/find/remove/list) lives in detail::io_registry.
class inputs : public detail::io_registry<input_base> {
   public:
    inputs() : io_registry("input") {}
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_INPUTS_HPP
