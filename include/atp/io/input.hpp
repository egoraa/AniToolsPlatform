#ifndef ANITOOLSPLATFORM_IO_INPUT_HPP
#define ANITOOLSPLATFORM_IO_INPUT_HPP

#include <any>
#include <concepts>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <utility>

#include <atp/io/input_base.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

// Базовый вход и одновременно вход «последнее значение побеждает».
// От него наследуются остальные виды входов (см. queued_input): единственная
// точка расширения — защищённый virtual store(), куда operator() кладёт
// принятое значение. Приём type-erased значений от выходов — через протокол
// accepts/deliver (см. input_base). Потокобезопасен по умолчанию; блокировка
// отключается тегом unsafe в конструкторе (см. safety). Чтение — pull-only:
// get() — копия состояния, take() — изъятие события; реакция на новые
// значения — опросом с потока потребителя (см. watcher). Доставка не
// исполняет пользовательский код — на потоке пишущего только store под замком.
template <typename T>
class input : public input_base {
   public:
    explicit input(std::string name, safety s = safe) : input_base(std::move(name), typeid(T), s) {}

    // Одиночное значение с perfect forwarding: принимает и lvalue,
    // и rvalue, а также всё, из чего T конструируется неявно.
    template <typename U>
        requires std::constructible_from<T, U>
    void operator()(U&& value) {
        // Конструируем T вне замка — критическая секция только на store().
        T incoming(std::forward<U>(value));
        auto guard = lock();
        store(std::move(incoming));  // virtual: наследник решает, куда положить
    }

    // Универсальный запрос «пусто ли»: у base — нет ли значения, у
    // наследников (см. queued_input) — своя трактовка через override.
    [[nodiscard]] virtual bool empty() const {
        auto guard = lock();
        return !value_.has_value();
    }

    // Возвращает копию: ссылка наружу была бы гонкой — другой поток
    // может перезаписать значение в любой момент.
    [[nodiscard]] T get() const {
        auto guard = lock();
        if (!value_) {
            throw std::runtime_error("input '" + name() + "' has no value");
        }
        return *value_;
    }

    // Изъятие значения: optional пуст, если писем не было. Пара к get():
    // get() — вход-«состояние» (читается многократно), take() — вход-«событие»
    // (каждое значение обрабатывается ровно раз). virtual: наследник изымает
    // из своего хранилища (queued_input отдаёт голову очереди) — в отличие от
    // невиртуального get(), take через ссылку на базу работает у всех входов.
    [[nodiscard]] virtual std::optional<T> take() {
        auto guard = lock();
        std::optional<T> out = std::move(value_);
        value_.reset();
        return out;
    }

    void reset() override {
        auto guard = lock();
        value_.reset();
    }

    // Протокол доставки (см. input_base). Типизированный вход принимает
    // ровно свой T; input<std::any> универсален — принимает всё и
    // упаковывает значение через meta.box. Обе ветки решаются на этапе
    // компиляции, полной специализации для std::any не требуется;
    // queued_input наследует протокол как есть.
    [[nodiscard]] bool accepts(std::type_index produced) const override {
        if constexpr (std::same_as<T, std::any>) {
            return true;
        } else {
            return produced == type();
        }
    }

    void deliver(const void* value, const erased_type& meta) override {
        if constexpr (std::same_as<T, std::any>) {
            // any→any — без двойной упаковки
            if (meta.type == typeid(std::any)) {
                (*this)(*static_cast<const std::any*>(value));
            } else {
                (*this)(meta.box(value));
            }
        } else {
            // meta.type == typeid(T) — протокольная гарантия выхода
            (*this)(*static_cast<const T*>(value));
        }
    }

   protected:
    // Точка расширения: куда положить принятое значение. По умолчанию —
    // «последнее значение побеждает». Вызывается под замком.
    virtual void store(T&& value) {
        value_.emplace(std::move(value));
    }

   private:
    std::optional<T> value_;
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_INPUT_HPP
