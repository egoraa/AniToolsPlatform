#ifndef ANITOOLSPLATFORM_IO_OUTPUT_HPP
#define ANITOOLSPLATFORM_IO_OUTPUT_HPP

#include <algorithm>
#include <any>
#include <concepts>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include <atp/io/input.hpp>
#include <atp/io/output_base.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

    // Выход: push-рассылка подключённым входам + кэш последнего значения.
    // Запись out(value) доставляет значение каждому подключённому входу через
    // протокол input_base::deliver и запоминает его в кэше — для инспекции
    // через get() и доставки поздним подписчикам при connect(in, replay).
    // Совместимость входа проверяет сам вход своим accepts() — один раз при
    // подключении; кастов иерархии нет вовсе (dynamic_cast запрещён
    // стандартами уровня AUTOSAR). Универсальный input<std::any> принимает
    // значение любого выхода — упаковка в std::any происходит при доставке.
    // Потокобезопасность — как у входа: выбирается тегом safety в точке
    // создания. Рассылка идёт вне замка выхода: вход берёт свой мьютекс сам,
    // поэтому when()-колбэк входа может реентерабельно писать обратно в этот
    // же выход, вложенных замков нет. Порядок доставки при конкурентных
    // записях не гарантируется. Время жизни подключённых входов — на
    // вызывающем: соединение разрывается disconnect() до уничтожения входа.
    template <typename T>
    class output : public output_base {
    public:
        explicit output(std::string name, safety s = safe)
            : output_base(std::move(name), typeid(T), s) {}

        // Запись с perfect forwarding — зеркало input::operator():
        // T конструируется вне замка; под замком — снапшот списка рассылки
        // и копия в кэш; сама доставка — уже без замка, каждый вход копирует
        // значение внутри своего deliver().
        template <typename U>
            requires std::constructible_from<T, U>
        void operator()(U&& value) {
            T incoming(std::forward<U>(value));
            std::vector<input_base*> targets;
            {
                auto guard = lock();
                targets = targets_;
                value_ = incoming;
            }
            for (input_base* in : targets) {
                in->deliver(&incoming, input_base::erased_of<T>());
            }
        }

        // Типизированное подключение: несовпадение типов — ошибка компиляции.
        // Базовые type-erased перегрузки скрыты намеренно (см. output_base).
        void connect(input<T>& in) { attach(in, false); }
        void connect(input<T>& in, replay_t) { attach(in, true); }

        // Универсальный вход подключается к любому выходу статически;
        // requires исключает конфликт с парой выше при T == std::any.
        void connect(input<std::any>& in)
            requires (!std::same_as<T, std::any>)
        {
            attach(in, false);
        }
        void connect(input<std::any>& in, replay_t)
            requires (!std::same_as<T, std::any>)
        {
            attach(in, true);
        }

        bool disconnect(const input_base& in) override {
            auto guard = lock();
            auto it = std::find(targets_.begin(), targets_.end(), &in);
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
        void attach(input_base& in, bool deliver_cached) {
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
                in.deliver(&*snapshot, input_base::erased_of<T>());
            }
        }

        // Рантайм-проверка совместимости — на стороне входа: accepts()
        // зовётся один раз при подключении, доставка идёт без проверок.
        // Контраст с точным typeid в реестре сохраняется: реестру нужен
        // конкретный вид входа, а выходу подходит всё, что согласилось
        // принимать T (наследники input<T>, универсальный input<std::any>).
        void do_connect(input_base& in, bool deliver_cached) override {
            if (!in.accepts(typeid(T))) {
                throw std::runtime_error("input '" + in.name() + "' is not compatible with output '"
                                         + name() + "'");
            }
            attach(in, deliver_cached);
        }

        std::vector<input_base*> targets_;
        std::optional<T> value_;
    };

} // namespace atp::io

#endif // ANITOOLSPLATFORM_IO_OUTPUT_HPP
