#ifndef ANITOOLSPLATFORM_IO_OUTPUTS_HPP
#define ANITOOLSPLATFORM_IO_OUTPUTS_HPP

#include <string>

#include <atp/io/io_registry.hpp>
#include <atp/io/output_base.hpp>

namespace atp::io {

/// Owning registry of outputs. An heir declares them as reference members:
///
///     output<int>& result = make<output<int>>("result");
///     output<int>& fast = make<output<int>>("fast", unsafe);
///
/// All the machinery (make/get/at/find/remove/list) lives in detail::io_registry.
class outputs : public detail::io_registry<output_base> {
   public:
    outputs() : io_registry("output") {}
};

}  // namespace atp::io

#endif
