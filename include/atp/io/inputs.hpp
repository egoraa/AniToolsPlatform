#ifndef ANITOOLSPLATFORM_IO_INPUTS_HPP
#define ANITOOLSPLATFORM_IO_INPUTS_HPP

#include <string>

#include <atp/io/input_base.hpp>
#include <atp/io/registry.hpp>

namespace atp::io {

    // Реестр входов; владеет ими. Наследник объявляет входы ссылками:
    //     input<int>& number = make<input<int>>("number");
    //     input<int>& fast = make<input<int>>("fast", unsafe);
    //     queued_input<int>& events = make<queued_input<int>>("events");
    // Вся механика (make/get/at/find/remove/list) — в detail::registry.
    class inputs : public detail::registry<input_base> {
    public:
        inputs() : registry("input") {}
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_INPUTS_HPP
