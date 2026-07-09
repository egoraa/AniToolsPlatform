#ifndef ANITOOLSPLATFORM_IO_INPUT_HPP
#define ANITOOLSPLATFORM_IO_INPUT_HPP

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>

#include "input_base.hpp"
#include "threading.hpp"

namespace atp::io {

    // Базовый вход и одновременно вход «последнее значение побеждает».
    // От него наследуются остальные виды входов (см. queued_input): единственная
    // точка расширения — защищённый virtual store(), куда operator() кладёт
    // принятое значение. Потокобезопасен по умолчанию; блокировка отключается
    // тегом unsafe в конструкторе (см. safety). С включённой блокировкой колбэк
    // вызывается вне замка со снапшотом значения именно этого вызова, поэтому
    // реентерабельные и конкурентные записи безопасны; порядок колбэков при
    // конкурентных записях не гарантируется.
    template <typename T>
    class input : public input_base {
    public:
        explicit input(std::string name, safety s = safe)
            : input_base(std::move(name), typeid(T), s) {}

        // Одиночное значение с perfect forwarding: принимает и lvalue,
        // и rvalue, а также всё, из чего T конструируется неявно.
        template <typename U>
            requires std::constructible_from<T, U>
        void operator()(U&& value) {
            // Конструируем T вне замка — критическая секция только на store().
            T incoming(std::forward<U>(value));
            callback_ptr cb;
            std::optional<T> snapshot;
            {
                auto guard = lock();
                cb = callback_;
                if (cb) {
                    snapshot = incoming;  // копия для колбэка, пока держим замок
                }
                store(std::move(incoming));  // virtual: наследник решает, куда положить
            }
            if (cb) {
                (*cb)(*snapshot);
            }
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

        void reset() override {
            auto guard = lock();
            value_.reset();
        }

        void when(std::function<void(const T&)> callback) {
            auto cb = callback
                ? std::make_shared<const std::function<void(const T&)>>(std::move(callback))
                : callback_ptr{};
            auto guard = lock();
            callback_ = std::move(cb);
        }

    protected:
        // Точка расширения: куда положить принятое значение. По умолчанию —
        // «последнее значение побеждает». Вызывается под замком.
        virtual void store(T&& value) { value_.emplace(std::move(value)); }

    private:
        // shared_ptr: operator() дёшево копирует указатель под замком и зовёт
        // колбэк уже без замка — when() из другого потока не выдернет
        // объект из-под работающего вызова.
        using callback_ptr = std::shared_ptr<const std::function<void(const T&)>>;

        std::optional<T> value_;
        callback_ptr callback_;
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_INPUT_HPP
