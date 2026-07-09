#ifndef ANITOOLSPLATFORM_IO_INPUTS_HPP
#define ANITOOLSPLATFORM_IO_INPUTS_HPP

#include <string>

#include "input_base.hpp"
#include "registry.hpp"

namespace atp::io {

    // Реестр входов; владеет ими. Наследник объявляет входы ссылками:
    //     input<int>& number = make<input<int>>("number");
    //     input<int>& fast = make<input<int>>("fast", unsafe);
    //     queued_input<int>& events = make<queued_input<int>>("events");
    // Вся механика (make/get/remove/list) — в detail::registry.
    class inputs : public detail::registry<input_base> {
    public:
        inputs() : registry("input") {}

        // Нетипизированный доступ — для перечисления, сброса и машинерии
        // соединений.
        [[nodiscard]] input_base& get_input(const std::string& name) { return find(name); }
        [[nodiscard]] const input_base& get_input(const std::string& name) const { return find(name); }
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_INPUTS_HPP
