#ifndef ANITOOLSPLATFORM_SERVICE_DIRECTORY_HPP
#define ANITOOLSPLATFORM_SERVICE_DIRECTORY_HPP

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

namespace atp {

// Невладеющий справочник сервисов: «кто (имя поставщика) какие интерфейсы
// предоставляет». Модуль публикует свои интерфейсы в initialize(), соседи
// находят их в start() — двухфазность publish/lookup делает порядок
// инициализации модулей неважным. Ключ — пара (имя, тип интерфейса):
// один поставщик может публиковать несколько интерфейсов, один интерфейс —
// публиковаться под разными именами.
//
// Типобезопасность без dynamic_cast: указатель хранится
// как void*, но достаётся только под тем же статическим типом TService,
// под которым положили, — совпадение проверяется по type_index, поэтому
// обратный static_cast корректен по построению. Та же дисциплина, что у
// input_base::accepts()/deliver(): никаких кастов по иерархии.
//
// Время жизни — контракт на вызывающем, как у io-соединений: публикации
// снимаются (remove) до разрушения сервиса — обычное место для этого stop().
// НЕ потокобезопасен — фаза настройки, как io-реестры.
class service_directory {
   public:
    service_directory() = default;

    service_directory(const service_directory&) = delete;
    service_directory& operator=(const service_directory&) = delete;

    // Публикация интерфейса под именем поставщика. TService указывается
    // явно (provide<camera_control>(...)): вывод из аргумента подставил бы
    // конкретный класс модуля вместо интерфейса, и потребитель не нашёл бы
    // запись. const-тип отвергается: typeid стирает const, и публикация
    // provide<const T> молча столкнулась бы с provide<T>.
    template <typename TService>
        requires(!std::is_const_v<TService>)
    void provide(const std::string& name, TService& service) {
        if (name.empty()) {
            throw std::invalid_argument("empty service provider name");
        }
        // Пустая внутренняя map при новом имени не «повисает»: следом
        // try_emplace обязательно вставляет первую запись (как в module_registry).
        auto& services = entries_[name];
        auto [it, inserted] = services.try_emplace(std::type_index(typeid(TService)), std::addressof(service));
        if (!inserted) {
            throw std::runtime_error("duplicate service '" + name + "' interface '" + typeid(TService).name() + "'");
        }
    }

    // Пара в духе std::map: at() бросает, find() возвращает nullptr.
    // at различает два случая: имени нет / имя есть, интерфейса нет.
    template <typename TService>
    [[nodiscard]] TService& at(const std::string& name) const {
        auto it = entries_.find(name);
        if (it == entries_.end()) {
            throw std::runtime_error("no service provider named '" + name + "'");
        }
        auto found = it->second.find(std::type_index(typeid(TService)));
        if (found == it->second.end()) {
            throw std::runtime_error("provider '" + name + "' has no interface '" + typeid(TService).name() + "'");
        }
        return *static_cast<TService*>(found->second);
    }

    template <typename TService>
    [[nodiscard]] TService* find(const std::string& name) const {
        auto it = entries_.find(name);
        if (it == entries_.end()) {
            return nullptr;
        }
        auto found = it->second.find(std::type_index(typeid(TService)));
        return found == it->second.end() ? nullptr : static_cast<TService*>(found->second);
    }

    // Снять все публикации имени — обычный путь в stop().
    bool remove(const std::string& name) {
        return entries_.erase(name) > 0;
    }

    // Снять одну публикацию. Опустевшая запись имени стирается целиком —
    // поддержка инварианта «внутренняя map не пуста» (см. entries_).
    template <typename TService>
    bool remove(const std::string& name) {
        auto it = entries_.find(name);
        if (it == entries_.end()) {
            return false;
        }
        if (it->second.erase(std::type_index(typeid(TService))) == 0) {
            return false;
        }
        if (it->second.empty()) {
            entries_.erase(it);
        }
        return true;
    }

   private:
    // Имя поставщика → его интерфейсы. Инвариант: внутренняя map никогда
    // не пуста — при снятии последнего интерфейса стирается вся запись
    // имени, find(name) не находит имя-пустышку (как в module_registry).
    std::unordered_map<std::string, std::map<std::type_index, void*>> entries_;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_SERVICE_DIRECTORY_HPP
