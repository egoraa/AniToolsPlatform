#ifndef ANITOOLSPLATFORM_IO_QUEUED_INPUT_HPP
#define ANITOOLSPLATFORM_IO_QUEUED_INPUT_HPP

#include <cstddef>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <atp/io/input.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

    // Вход-очередь: значения не перезаписывают друг друга, а копятся (FIFO,
    // без ограничения). Наследует приём значения, when()-колбэк и синхронизацию
    // от input<T>; переопределяет только store() — вместо «последнего значения»
    // кладёт в очередь. Колбэк, если задан через when(), срабатывает на каждый
    // push, значение при этом остаётся в очереди для pop().
    // Потребление: pop()/try_pop() по одному, drain() — вся очередь одним замком.
    // empty()/size() — мгновенный снимок: может устареть к следующей строке.
    template <typename T>
    class queued_input : public input<T> {
    public:
        explicit queued_input(std::string name, safety s = safe)
            : input<T>(std::move(name), s) {}

        [[nodiscard]] bool empty() const override {
            auto guard = this->lock();
            return queue_.empty();
        }

        [[nodiscard]] std::size_t size() const {
            auto guard = this->lock();
            return queue_.size();
        }

        [[nodiscard]] T pop() {
            auto guard = this->lock();
            if (queue_.empty()) {
                throw std::runtime_error("input '" + this->name() + "' queue is empty");
            }
            T front = std::move(queue_.front());
            queue_.pop_front();
            return front;
        }

        [[nodiscard]] std::optional<T> try_pop() {
            auto guard = this->lock();
            if (queue_.empty()) {
                return std::nullopt;
            }
            std::optional<T> front{std::move(queue_.front())};
            queue_.pop_front();
            return front;
        }

        // Забирает всю очередь одним захватом замка — самый дешёвый способ
        // пакетной обработки.
        [[nodiscard]] std::deque<T> drain() {
            std::deque<T> out;
            {
                auto guard = this->lock();
                out.swap(queue_);
            }
            return out;
        }

        void reset() override {
            auto guard = this->lock();
            queue_.clear();
        }

    protected:
        // Вместо «последнего значения» — добавляем в хвост очереди.
        // Вызывается базой под замком.
        void store(T&& value) override { queue_.push_back(std::move(value)); }

    private:
        std::deque<T> queue_;
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_QUEUED_INPUT_HPP
