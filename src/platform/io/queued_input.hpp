#ifndef ANITOOLSPLATFORM_IO_QUEUED_INPUT_HPP
#define ANITOOLSPLATFORM_IO_QUEUED_INPUT_HPP

#include <concepts>
#include <cstddef>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeinfo>
#include <utility>

#include "input_base.hpp"
#include "threading.hpp"

namespace atp::io {

    // Вход-очередь: значения не перезаписывают друг друга, а копятся (FIFO,
    // без ограничения). Потокобезопасен по умолчанию; блокировка отключается
    // тегом unsafe в конструкторе (см. safety).
    // Потребление: pop()/try_pop() по одному, drain() — вся очередь одним замком.
    // empty()/size() — мгновенный снимок: может устареть к следующей строке.
    template <typename... Args>
    class queued_input : public input_base {
    public:
        explicit queued_input(std::string name, safety s = safe)
            : input_base(std::move(name), typeid(std::tuple<Args...>), s) {}

        // Шаблонные параметры вызова — настоящий perfect forwarding,
        // принимает и lvalue, и rvalue.
        template <typename... CallArgs>
            requires std::constructible_from<std::tuple<Args...>, CallArgs...>
        void operator()(CallArgs&&... call_args) {
            // Конструирование до захвата замка: критическая секция — только push.
            std::tuple<Args...> item(std::forward<CallArgs>(call_args)...);
            auto guard = lock();
            queue_.push_back(std::move(item));
        }

        [[nodiscard]] bool empty() const {
            auto guard = lock();
            return queue_.empty();
        }

        [[nodiscard]] std::size_t size() const {
            auto guard = lock();
            return queue_.size();
        }

        [[nodiscard]] std::tuple<Args...> pop() {
            auto guard = lock();
            if (queue_.empty()) {
                throw std::runtime_error("input '" + name() + "' queue is empty");
            }
            std::tuple<Args...> front = std::move(queue_.front());
            queue_.pop_front();
            return front;
        }

        [[nodiscard]] std::optional<std::tuple<Args...>> try_pop() {
            auto guard = lock();
            if (queue_.empty()) {
                return std::nullopt;
            }
            std::optional<std::tuple<Args...>> front{std::move(queue_.front())};
            queue_.pop_front();
            return front;
        }

        // Забирает всю очередь одним захватом замка — самый дешёвый способ
        // пакетной обработки.
        [[nodiscard]] std::deque<std::tuple<Args...>> drain() {
            std::deque<std::tuple<Args...>> out;
            {
                auto guard = lock();
                out.swap(queue_);
            }
            return out;
        }

        void reset() override {
            auto guard = lock();
            queue_.clear();
        }

    private:
        std::deque<std::tuple<Args...>> queue_;
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_QUEUED_INPUT_HPP
