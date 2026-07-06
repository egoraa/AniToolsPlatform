#ifndef ANITOOLSPLATFORM_IO_QUEUED_INPUT_HPP
#define ANITOOLSPLATFORM_IO_QUEUED_INPUT_HPP

#include <concepts>
#include <cstddef>
#include <deque>
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

    // Вход-очередь: значения не перезаписывают друг друга, а копятся (FIFO,
    // без ограничения). Параметризован политикой блокировки (std::mutex /
    // null_mutex) — см. алиасы queued_input / unsafe_queued_input.
    // Потребление: pop()/try_pop() по одному, drain() — вся очередь одним замком.
    // empty()/size() — мгновенный снимок: может устареть к следующей строке.
    template <typename Mutex, typename... Args>
    class basic_queued_input : public input_base {
    public:
        explicit basic_queued_input(std::string name)
            : input_base(std::move(name), typeid(std::tuple<Args...>)) {}

        // Шаблонные параметры вызова — настоящий perfect forwarding,
        // принимает и lvalue, и rvalue.
        template <typename... CallArgs>
            requires std::constructible_from<std::tuple<Args...>, CallArgs...>
        void operator()(CallArgs&&... call_args) {
            // Конструирование до захвата замка: критическая секция — только push.
            std::tuple<Args...> item(std::forward<CallArgs>(call_args)...);
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(item));
        }

        [[nodiscard]] bool empty() const {
            std::lock_guard lock(mutex_);
            return queue_.empty();
        }

        [[nodiscard]] std::size_t size() const {
            std::lock_guard lock(mutex_);
            return queue_.size();
        }

        [[nodiscard]] std::tuple<Args...> pop() {
            std::lock_guard lock(mutex_);
            if (queue_.empty()) {
                throw std::runtime_error("input '" + name() + "' queue is empty");
            }
            std::tuple<Args...> front = std::move(queue_.front());
            queue_.pop_front();
            return front;
        }

        [[nodiscard]] std::optional<std::tuple<Args...>> try_pop() {
            std::lock_guard lock(mutex_);
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
                std::lock_guard lock(mutex_);
                out.swap(queue_);
            }
            return out;
        }

        void reset() override {
            std::lock_guard lock(mutex_);
            queue_.clear();
        }

    private:
        mutable Mutex mutex_;
        std::deque<std::tuple<Args...>> queue_;
    };

    template <typename... Args>
    using queued_input = basic_queued_input<std::mutex, Args...>;

    template <typename... Args>
    using unsafe_queued_input = basic_queued_input<null_mutex, Args...>;

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_QUEUED_INPUT_HPP
