#ifndef ANITOOLSPLATFORM_IO_INPUT_BASE_HPP
#define ANITOOLSPLATFORM_IO_INPUT_BASE_HPP

#include <any>
#include <typeindex>
#include <typeinfo>

#include <atp/io/io_base.hpp>

namespace atp::io {

// Приёмник уведомления «входу что-то доставили». Вешается исполнителем на
// вход (set_notifier), чтобы будить спящий поток-потребитель, не нарушая
// pull-only чтения: это не колбэк с данными, а голый сигнал. notify()
// зовётся на потоке пишущего после приёма — обязан быть быстрым, не бросать
// и не исполнять пользовательский код.
class notifier_base {
   public:
    virtual void notify() noexcept = 0;

   protected:
    ~notifier_base() = default;  // владение всегда снаружи — у исполнителя
};

// Type-erased база входа: именно её указатели хранит реестр inputs, её же
// принимает output_base::connect и хранит output<T> в списке рассылки.
// Определяет протокол доставки: вход сам отвечает, значения каких типов он
// принимает (accepts) и как принять type-erased значение (deliver) —
// выходу не нужны касты иерархии.
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
        static const erased_type meta{typeid(T), [](const void* p) { return std::any(*static_cast<const T*>(p)); }};
        return meta;
    }

    // Согласен ли вход принимать значения типа produced. Зовётся один раз
    // при подключении, не на каждую доставку.
    [[nodiscard]] virtual bool accepts(std::type_index produced) const = 0;

    // Доставка значения: value указывает на объект типа meta.type.
    // Корректность пары (value, meta) — протокольная гарантия выхода,
    // подкреплённая проверкой accepts при подключении. NVI: приём — у
    // наследника (do_deliver), уведомление — здесь, одно на любую доставку.
    void deliver(const void* value, const erased_type& meta) {
        do_deliver(value, meta);
        if (notifier_ != nullptr) {
            notifier_->notify();
        }
    }

    // Установка/снятие — фаза настройки исполнителя: строго до запуска
    // потоков и после их join; конкурентно с доставкой — гонка. Прямая
    // запись в вход (operator()) уведомителя не трогает: сигнал — про
    // доставку от выходов.
    void set_notifier(notifier_base* notifier) noexcept {
        notifier_ = notifier;
    }
    [[nodiscard]] notifier_base* notifier() const noexcept {
        return notifier_;
    }

   private:
    virtual void do_deliver(const void* value, const erased_type& meta) = 0;

    notifier_base* notifier_ = nullptr;
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_INPUT_BASE_HPP
