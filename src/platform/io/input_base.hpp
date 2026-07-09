#ifndef ANITOOLSPLATFORM_IO_INPUT_BASE_HPP
#define ANITOOLSPLATFORM_IO_INPUT_BASE_HPP

#include "io_base.hpp"

namespace atp::io {

    // Type-erased база входа: именно её указатели хранит реестр inputs,
    // её же принимает output_base::connect. Вся механика — в io_base;
    // пустой наследник существует намеренно, как типовое различие входов
    // и выходов.
    class input_base : public io_base {
    public:
        using io_base::io_base;
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_INPUT_BASE_HPP
