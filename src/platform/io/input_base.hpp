#ifndef ANITOOLSPLATFORM_IO_INPUT_BASE_HPP
#define ANITOOLSPLATFORM_IO_INPUT_BASE_HPP

#include <any>
#include <typeindex>
#include <typeinfo>

#include "io_base.hpp"

namespace atp::io {

    // Type-erased база входа: именно её указатели хранит реестр inputs, её же
    // принимает output_base::connect и хранит output<T> в списке рассылки.
    // Определяет протокол доставки: вход сам отвечает, значения каких типов он
    // принимает (accepts) и как принять type-erased значение (deliver) —
    // выходу не нужны касты иерархии (dynamic_cast запрещён стандартами уровня
    // AUTOSAR, правило A5-2-1).
    class input_base : public io_base {
    public:
        using io_base::io_base;

        // Метаданные производимого типа со стороны выхода: тег + упаковщик в
        // std::any. box нужен только универсальным входам (input<std::any>);
        // типизированный вход кастует значение напрямую.
        struct erased_type {
            std::type_index type;
            std::any (*box)(const void*);
        };

        // Одна инстанция метаданных на тип в пределах модуля (статик в
        // inline-функции). В каждой DLL — своя копия: сравнивать адреса
        // erased_type нельзя, использовать только содержимое.
        template <typename T>
        [[nodiscard]] static const erased_type& erased_of() {
            static const erased_type meta{
                typeid(T), [](const void* p) { return std::any(*static_cast<const T*>(p)); }};
            return meta;
        }

        // Согласен ли вход принимать значения типа produced. Зовётся один раз
        // при подключении, не на каждую доставку.
        [[nodiscard]] virtual bool accepts(std::type_index produced) const = 0;

        // Доставка значения: value указывает на объект типа meta.type.
        // Корректность пары (value, meta) — протокольная гарантия выхода,
        // подкреплённая проверкой accepts при подключении.
        virtual void deliver(const void* value, const erased_type& meta) = 0;
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_INPUT_BASE_HPP
