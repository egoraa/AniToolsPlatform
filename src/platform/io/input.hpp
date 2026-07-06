#ifndef ANITOOLSPLATFORM_IO_INPUT_HPP
#define ANITOOLSPLATFORM_IO_INPUT_HPP

#include <concepts>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeinfo>
#include <utility>

#include "input_base.hpp"
#include "threading.hpp"

namespace atp::io {

    // Вход «последнее значение побеждает», параметризованный политикой
    // блокировки (std::mutex / null_mutex) — см. алиасы input / unsafe_input.
    // С std::mutex потокобезопасен: колбэк вызывается вне замка со снапшотом
    // значения именно этого вызова, поэтому реентерабельные и конкурентные
    // записи безопасны; порядок колбэков при конкурентных записях не гарантируется.
    template <typename Mutex, typename... Args>
    class basic_input : public input_base {
    public:
        explicit basic_input(std::string name)
            : input_base(std::move(name), typeid(std::tuple<Args...>)) {}

        // Шаблонные параметры вызова — настоящий perfect forwarding,
        // принимает и lvalue, и rvalue.
        template <typename... CallArgs>
            requires std::constructible_from<std::tuple<Args...>, CallArgs...>
        void operator()(CallArgs&&... call_args) {
            callback_ptr cb;
            std::optional<std::tuple<Args...>> snapshot;
            {
                std::lock_guard lock(mutex_);
                value_.emplace(std::forward<CallArgs>(call_args)...);
                cb = callback_;
                if (cb) {
                    snapshot = *value_;  // значение этого вызова, пока держим замок
                }
            }
            if (cb) {
                std::apply(*cb, *snapshot);
            }
        }

        [[nodiscard]] bool has_value() const {
            std::lock_guard lock(mutex_);
            return value_.has_value();
        }

        // Возвращает копию: ссылка наружу была бы гонкой — другой поток
        // может перезаписать значение в любой момент.
        [[nodiscard]] std::tuple<Args...> get() const {
            std::lock_guard lock(mutex_);
            if (!value_) {
                throw std::runtime_error("input '" + name() + "' has no value");
            }
            return *value_;
        }

        void reset() override {
            std::lock_guard lock(mutex_);
            value_.reset();
        }

        void when(std::function<void(const Args&...)> callback) {
            auto cb = callback
                ? std::make_shared<const std::function<void(const Args&...)>>(std::move(callback))
                : callback_ptr{};
            std::lock_guard lock(mutex_);
            callback_ = std::move(cb);
        }

    private:
        // shared_ptr: operator() дёшево копирует указатель под замком и зовёт
        // колбэк уже без замка — when() из другого потока не выдернет
        // объект из-под работающего вызова.
        using callback_ptr = std::shared_ptr<const std::function<void(const Args&...)>>;

        mutable Mutex mutex_;
        std::optional<std::tuple<Args...>> value_;
        callback_ptr callback_;
    };

    template <typename... Args>
    using input = basic_input<std::mutex, Args...>;

    template <typename... Args>
    using unsafe_input = basic_input<null_mutex, Args...>;

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_INPUT_HPP
