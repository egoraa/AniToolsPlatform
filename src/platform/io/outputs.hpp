#ifndef ANITOOLSPLATFORM_IO_OUTPUTS_HPP
#define ANITOOLSPLATFORM_IO_OUTPUTS_HPP

#include <string>

#include "output_base.hpp"
#include "registry.hpp"

namespace atp::io {

    // Реестр выходов; владеет ими. Наследник объявляет выходы ссылками:
    //     output<int>& result = make<output<int>>("result");
    //     output<int>& fast = make<output<int>>("fast", unsafe);
    // Вся механика (make/get/at/find/remove/list) — в detail::registry.
    class outputs : public detail::registry<output_base> {
    public:
        outputs() : registry("output") {}
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_OUTPUTS_HPP
