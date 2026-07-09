#ifndef ANITOOLSPLATFORM_IO_OUTPUTS_HPP
#define ANITOOLSPLATFORM_IO_OUTPUTS_HPP

#include <string>

#include "output_base.hpp"
#include "registry.hpp"

namespace atp::io {

    // Реестр выходов; владеет ими. Наследник объявляет выходы ссылками:
    //     output<int>& result = make<output<int>>("result");
    //     output<int>& fast = make<output<int>>("fast", unsafe);
    // Вся механика (make/get/remove/list) — в detail::registry.
    class outputs : public detail::registry<output_base> {
    public:
        outputs() : registry("output") {}

        // Нетипизированный доступ — для перечисления, сброса и машинерии
        // соединений (connect по именам живёт на output_base).
        [[nodiscard]] output_base& get_output(const std::string& name) { return find(name); }
        [[nodiscard]] const output_base& get_output(const std::string& name) const { return find(name); }
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_OUTPUTS_HPP
