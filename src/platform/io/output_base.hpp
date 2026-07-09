#ifndef ANITOOLSPLATFORM_IO_OUTPUT_BASE_HPP
#define ANITOOLSPLATFORM_IO_OUTPUT_BASE_HPP

#include <cstddef>

#include "io_base.hpp"
#include "input_base.hpp"

namespace atp::io {

    // Тег «доставить кэш при подключении» — в стиле safety/unsafe:
    //     out.connect(in, replay);
    struct replay_t {};
    inline constexpr replay_t replay{};

    // Type-erased база выхода: именно её указатели хранит реестр outputs,
    // с ней же работает будущая машинерия соединений (подключение по именам).
    // Совместимость типов проверяется в рантайме внутри do_connect —
    // несовместимый вход отклоняется исключением. В самом output<T> эти
    // перегрузки намеренно скрыты типизированными: у конкретного выхода
    // ошибка типа ловится компилятором, рантайм-путь доступен через базу.
    class output_base : public io_base {
    public:
        using io_base::io_base;

        // Только связь: кэш новому входу не доставляется.
        void connect(input_base& in) { do_connect(in, false); }

        // Связь + немедленная доставка кэша, если он есть.
        void connect(input_base& in, replay_t) { do_connect(in, true); }

        // Разрыв по адресу входа; false — вход не был подключён.
        virtual bool disconnect(const input_base& in) = 0;
        virtual void disconnect_all() = 0;

        [[nodiscard]] virtual std::size_t connections() const = 0;

    private:
        // Единственная точка подключения: наследник проверяет совместимость
        // входа и добавляет его в список рассылки.
        virtual void do_connect(input_base& in, bool deliver_cached) = 0;
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_OUTPUT_BASE_HPP
