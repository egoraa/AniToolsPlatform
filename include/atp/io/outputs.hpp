#ifndef ANITOOLSPLATFORM_IO_OUTPUTS_HPP
#define ANITOOLSPLATFORM_IO_OUTPUTS_HPP

#include <string>

#include <atp/io/io_registry.hpp>
#include <atp/io/output_base.hpp>

namespace atp::io {

// Реестр выходов; владеет ими. Наследник объявляет выходы ссылками:
//     output<int>& result = make<output<int>>("result");
//     output<int>& fast = make<output<int>>("fast", unsafe);
// Вся механика (make/get/at/find/remove/list) — в detail::io_registry.
class outputs : public detail::io_registry<output_base> {
   public:
    outputs() : io_registry("output") {}
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_OUTPUTS_HPP
