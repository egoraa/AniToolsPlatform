#ifndef ANITOOLSPLATFORM_IO_OUTPUT_HPP
#define ANITOOLSPLATFORM_IO_OUTPUT_HPP

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include "input.hpp"
#include "output_base.hpp"
#include "threading.hpp"

namespace atp::io {

    // Выход: push-рассылка подключённым входам + кэш последнего значения.
    // Запись out(value) доставляет значение каждому подключённому input<T>
    // (включая наследников вроде queued_input<T>) и запоминает его в кэше —
    // для инспекции через get() и доставки поздним подписчикам при
    // connect(in, replay). Потокобезопасность — как у входа: выбирается тегом
    // safety в точке создания. Рассылка идёт вне замка выхода: вход берёт
    // свой мьютекс сам, поэтому when()-колбэк входа может реентерабельно
    // писать обратно в этот же выход, вложенных замков нет. Порядок доставки
    // при конкурентных записях не гарантируется. Время жизни подключённых
    // входов — на вызывающем: соединение разрывается disconnect() до
    // уничтожения входа.
    template <typename T>
    class output : public output_base {
    public:
        explicit output(std::string name, safety s = safe)
            : output_base(std::move(name), typeid(T), s) {}

        // Запись с perfect forwarding — зеркало input::operator():
        // T конструируется вне замка; под замком — снапшот списка рассылки
        // и копия в кэш; сама доставка — уже без замка, каждый вход копирует
        // значение внутри своего operator().
        template <typename U>
            requires std::constructible_from<T, U>
        void operator()(U&& value) {
            T incoming(std::forward<U>(value));
            std::vector<input<T>*> targets;
            {
                auto guard = lock();
                targets = targets_;
                value_ = incoming;
            }
            for (input<T>* in : targets) {
                (*in)(incoming);
            }
        }

        // Типизированное подключение: несовпадение типов — ошибка компиляции.
        // Базовые type-erased перегрузки скрыты намеренно (см. output_base).
        void connect(input<T>& in) { attach(in, false); }
        void connect(input<T>& in, replay_t) { attach(in, true); }

        bool disconnect(const input_base& in) override {
            auto guard = lock();
            auto it = std::find_if(targets_.begin(), targets_.end(), [&in](const input<T>* t) {
                return static_cast<const input_base*>(t) == &in;
            });
            if (it == targets_.end()) {
                return false;
            }
            targets_.erase(it);
            return true;
        }

        void disconnect_all() override {
            auto guard = lock();
            targets_.clear();
        }

        [[nodiscard]] std::size_t connections() const override {
            auto guard = lock();
            return targets_.size();
        }

        [[nodiscard]] bool empty() const {
            auto guard = lock();
            return !value_.has_value();
        }

        // Возвращает копию кэша: ссылка наружу была бы гонкой — другой поток
        // может перезаписать значение в любой момент.
        [[nodiscard]] T get() const {
            auto guard = lock();
            if (!value_) {
                throw std::runtime_error("output '" + name() + "' has no value");
            }
            return *value_;
        }

        // Чистит только кэш: соединения остаются, для их разрыва —
        // disconnect_all().
        void reset() override {
            auto guard = lock();
            value_.reset();
        }

    private:
        // Общий путь подключения: под замком — проверка дубликата, добавление
        // и снапшот кэша для replay; доставка кэша — уже вне замка.
        void attach(input<T>& in, bool deliver_cached) {
            std::optional<T> snapshot;
            {
                auto guard = lock();
                if (std::find(targets_.begin(), targets_.end(), &in) != targets_.end()) {
                    throw std::runtime_error("input '" + in.name() + "' is already connected to output '"
                                             + name() + "'");
                }
                targets_.push_back(&in);
                if (deliver_cached && value_) {
                    snapshot = *value_;
                }
            }
            if (snapshot) {
                in(std::move(*snapshot));
            }
        }

        // Рантайм-проверка через dynamic_cast — намеренный контраст с точным
        // typeid в реестре: реестру нужен конкретный вид входа, а выходу
        // подходит любой наследник input<T> (в queued_input доставка попадёт
        // в очередь — это и требуется).
        void do_connect(input_base& in, bool deliver_cached) override {
            auto* typed = dynamic_cast<input<T>*>(&in);
            if (!typed) {
                throw std::runtime_error("input '" + in.name() + "' is not compatible with output '"
                                         + name() + "'");
            }
            attach(*typed, deliver_cached);
        }

        std::vector<input<T>*> targets_;
        std::optional<T> value_;
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_OUTPUT_HPP
