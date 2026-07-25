#ifndef ANITOOLSPLATFORM_IO_WATCHER_HPP
#define ANITOOLSPLATFORM_IO_WATCHER_HPP

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <atp/io/input.hpp>
#include <atp/io/property.hpp>
#include <atp/io/queued_input.hpp>
#include <atp/io/threading.hpp>

namespace atp::io {

// Опросный наблюдатель: правила «вход → обработчик», проверяемые вызовом
// poll() с потока модуля. Замена when(): та же эргономика «колбэк на
// значение», но обработчик исполняется там, где живёт модуль, — гонок
// с iterate нет by construction. НЕ потокобезопасен: watch() — фаза
// настройки (initialize), poll() — поток модуля; опрашивать следует
// собственные входы модуля-владельца.
class watcher {
   public:
    // input<T>: появилось значение → изъять (take) и вызвать обработчик.
    // type_identity_t выключает вывод T из обработчика: T задаёт вход,
    // а лямбда просто конвертируется в std::function.
    template <typename T>
    void watch(input<T>& in, std::type_identity_t<std::function<void(const T&)>> handler) {
        rules_.push_back(std::make_unique<value_rule<T>>(in, std::move(handler)));
    }

    // queued_input<T>: обработчик на каждый элемент; изъятие — drain()
    // одним замком на пасс (дешевле поэлементного take базовой перегрузки).
    template <typename T>
    void watch(queued_input<T>& in, std::type_identity_t<std::function<void(const T&)>> handler) {
        rules_.push_back(std::make_unique<queue_rule<T>>(in, std::move(handler)));
    }

    // property<T>: значение изменилось (см. property::take) → обработчик.
    // Реакция на настройки декларируется рядом с правилами для входов.
    template <typename T>
    void watch(property<T>& prop, std::type_identity_t<std::function<void(const T&)>> handler) {
        rules_.push_back(std::make_unique<property_rule<T>>(prop, std::move(handler)));
    }

    // Пасс по правилам в порядке регистрации; busy — хоть одно сработало.
    // Возврат отдаётся прямо из iterate: return watcher_.poll();
    [[nodiscard]] work_status poll() {
        work_status pass = work_status::idle;
        for (const auto& rule : rules_) {
            if (rule->poll() == work_status::busy) {
                pass = work_status::busy;
            }
        }
        return pass;
    }

   private:
    // Правила полиморфны: будущие виды («значение изменилось», предикаты,
    // батч-обработчик очереди целиком) — новые перегрузки watch без ломки.
    struct rule_base {
        virtual ~rule_base() = default;
        virtual work_status poll() = 0;
    };

    template <typename T>
    struct value_rule : rule_base {
        value_rule(input<T>& in, std::function<void(const T&)> handler) : in(&in), handler(std::move(handler)) {}
        work_status poll() override {
            std::optional<T> value = in->take();
            if (!value) {
                return work_status::idle;
            }
            handler(*value);
            return work_status::busy;
        }
        input<T>* in;
        std::function<void(const T&)> handler;
    };

    template <typename T>
    struct queue_rule : rule_base {
        queue_rule(queued_input<T>& in, std::function<void(const T&)> handler) : in(&in), handler(std::move(handler)) {}
        work_status poll() override {
            auto batch = in->drain();
            if (batch.empty()) {
                return work_status::idle;
            }
            for (const T& value : batch) {
                handler(value);
            }
            return work_status::busy;
        }
        queued_input<T>* in;
        std::function<void(const T&)> handler;
    };

    template <typename T>
    struct property_rule : rule_base {
        property_rule(property<T>& prop, std::function<void(const T&)> handler)
            : prop(&prop), handler(std::move(handler)) {}
        work_status poll() override {
            std::optional<T> value = prop->take();
            if (!value) {
                return work_status::idle;
            }
            handler(*value);
            return work_status::busy;
        }
        property<T>* prop;
        std::function<void(const T&)> handler;
    };

    std::vector<std::unique_ptr<rule_base>> rules_;
};

}  // namespace atp::io

#endif  // ANITOOLSPLATFORM_IO_WATCHER_HPP
