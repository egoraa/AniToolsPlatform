#ifndef ANITOOLSPLATFORM_MODULE_REGISTRY_HPP
#define ANITOOLSPLATFORM_MODULE_REGISTRY_HPP

#include <concepts>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <atp/module_factory.hpp>

namespace atp {

    // Реестр фабрик модулей; владеет ими. Одно имя может держать несколько
    // версий: ключ фабрики — пара (имя, версия), версия берётся из самой
    // фабрики. Запрос без версии означает последнюю (наибольшую) версию.
    // API — сознательное зеркало io-реестров (at/find/remove/list, те же
    // контракты ошибок), но без переиспользования detail::registry: тот
    // привязан к io_base и сигнатуре конструктора (name, safety).
    // НЕ потокобезопасен — регистрация относится к фазе настройки.
    class module_registry {
    public:
        module_registry() = default;

        module_registry(const module_registry&) = delete;
        module_registry& operator=(const module_registry&) = delete;

        // Сахар: типовая фабрика по умолчанию. Имя задаётся здесь, в точке
        // регистрации, — один тип можно зарегистрировать под алиасами.
        template <std::derived_from<module_base> M>
            requires std::default_initializable<M>
        module_factory& add(std::string name) {
            return add(std::make_unique<typed_module_factory<M>>(std::move(name)));
        }

        // Общий путь — для нестандартных фабрик. Дубликат — совпадение
        // и имени, и версии; одно имя с разными версиями — норма.
        module_factory& add(std::unique_ptr<module_factory> factory) {
            if (!factory) {
                throw std::invalid_argument("null module factory");
            }
            module_factory& ref = *factory;
            // Пустая внутренняя map при новом имени не «повисает»: следом
            // try_emplace обязательно вставляет в неё первую версию.
            auto& versions = registry_[std::string(ref.name())];
            // try_emplace: при дубликате аргументы не переносятся — factory
            // остаётся владельцем, ref валидна для текста ошибки.
            auto [it, inserted] =
                versions.try_emplace(ref.get_version(), std::move(factory));
            if (!inserted) {
                throw std::runtime_error("duplicate module '" +
                                         std::string(ref.name()) + "' version '" +
                                         ref.get_version().to_string() + "'");
            }
            return ref;
        }

        // Без версии — последняя (наибольшая) зарегистрированная.
        [[nodiscard]] std::unique_ptr<module_base> create(const std::string& name) const {
            return at(name).create();
        }

        // С версией — точное совпадение (1.2 == 1.2.0: дополнение нулями).
        [[nodiscard]] std::unique_ptr<module_base> create(const std::string& name,
                                                          const version& v) const {
            return at(name, v).create();
        }

        // Пара в духе std::map: at() бросает, find() возвращает nullptr.
        [[nodiscard]] module_factory& at(const std::string& name) const {
            module_factory* factory = find(name);
            if (!factory) {
                throw std::runtime_error("no module named '" + name + "'");
            }
            return *factory;
        }

        // Различает два случая: имя не найдено / имя есть, версии нет.
        [[nodiscard]] module_factory& at(const std::string& name, const version& v) const {
            auto it = registry_.find(name);
            if (it == registry_.end()) {
                throw std::runtime_error("no module named '" + name + "'");
            }
            auto found = it->second.find(v);
            if (found == it->second.end()) {
                throw std::runtime_error("module '" + name + "' has no version '" +
                                         v.to_string() + "'");
            }
            return *found->second;
        }

        [[nodiscard]] module_factory* find(const std::string& name) const {
            auto it = registry_.find(name);
            if (it == registry_.end()) {
                return nullptr;
            }
            // инвариант «внутренняя map не пуста» делает rbegin() безопасным
            return it->second.rbegin()->second.get();
        }

        [[nodiscard]] module_factory* find(const std::string& name, const version& v) const {
            auto it = registry_.find(name);
            if (it == registry_.end()) {
                return nullptr;
            }
            auto found = it->second.find(v);
            return found == it->second.end() ? nullptr : found->second.get();
        }

        // Версии имени по возрастанию; неизвестное имя — пустой вектор
        // (в духе find, не at: перечисление — не ошибка).
        [[nodiscard]] std::vector<version> versions(const std::string& name) const {
            std::vector<version> result;
            auto it = registry_.find(name);
            if (it == registry_.end()) {
                return result;
            }
            result.reserve(it->second.size());
            for (const auto& [ver, factory] : it->second) {
                result.push_back(ver);
            }
            return result;
        }

        // Снять все версии имени.
        bool remove(const std::string& name) { return registry_.erase(name) > 0; }

        // Снять одну версию. Опустевшая запись имени стирается целиком —
        // поддержка инварианта «внутренняя map не пуста» (см. registry_).
        bool remove(const std::string& name, const version& v) {
            auto it = registry_.find(name);
            if (it == registry_.end()) {
                return false;
            }
            if (it->second.erase(v) == 0) {
                return false;
            }
            if (it->second.empty()) {
                registry_.erase(it);
            }
            return true;
        }

        [[nodiscard]] std::vector<const module_factory*> list() const {
            std::vector<const module_factory*> result;
            for (const auto& [name, versions] : registry_) {
                for (const auto& [ver, factory] : versions) {
                    result.push_back(factory.get());
                }
            }
            return result;
        }

    private:
        // Имя → версии по возрастанию (operator<=> у version). Инвариант:
        // внутренняя map никогда не пуста — при удалении последней версии
        // стирается вся запись имени, find(name) не находит имя-пустышку.
        std::unordered_map<std::string, std::map<version, std::unique_ptr<module_factory>>>
            registry_;
    };

    // Тонкая обёртка «реестр + пары (имя, версия), зарегистрированные через
    // неё». Функции регистрации модулей принимают её, а не реестр напрямую, —
    // так module_loader знает, какие фабрики принесла его библиотека,
    // и может снять их при выгрузке, не задевая чужие версии тех же имён.
    // Класс конкретный (не виртуальный): header-only платформа
    // инстанцируется в каждом участнике заново.
    class module_registrar {
    public:
        explicit module_registrar(module_registry& registry) : registry_(&registry) {}

        template <std::derived_from<module_base> M>
            requires std::default_initializable<M>
        module_factory& add(std::string name) {
            return add(std::make_unique<typed_module_factory<M>>(std::move(name)));
        }

        module_factory& add(std::unique_ptr<module_factory> factory) {
            // пара (имя, версия) запоминается только после успешной регистрации
            module_factory& ref = registry_->add(std::move(factory));
            registered_.emplace_back(std::string(ref.name()), ref.get_version());
            return ref;
        }

        [[nodiscard]] const std::vector<std::pair<std::string, version>>& registered() const {
            return registered_;
        }

    private:
        module_registry* registry_;
        std::vector<std::pair<std::string, version>> registered_;
    };

} // namespace atp

#endif // ANITOOLSPLATFORM_MODULE_REGISTRY_HPP
