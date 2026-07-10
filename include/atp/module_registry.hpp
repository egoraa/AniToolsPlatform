#ifndef ANITOOLSPLATFORM_MODULE_REGISTRY_HPP
#define ANITOOLSPLATFORM_MODULE_REGISTRY_HPP

#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <atp/module_factory.hpp>

namespace atp {

    // Реестр фабрик модулей; владеет ими. API — сознательное зеркало
    // io-реестров (at/find/remove/list, те же контракты ошибок), но без
    // переиспользования detail::registry: тот привязан к io_base и сигнатуре
    // конструктора (name, safety).
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

        // Общий путь — для нестандартных фабрик.
        module_factory& add(std::unique_ptr<module_factory> factory) {
            if (!factory) {
                throw std::invalid_argument("null module factory");
            }
            module_factory& ref = *factory;
            // try_emplace: при дубликате аргументы не переносятся — factory
            // остаётся владельцем, ref валидна для текста ошибки.
            auto [it, inserted] =
                registry_.try_emplace(std::string(ref.name()), std::move(factory));
            if (!inserted) {
                throw std::runtime_error("duplicate module name '" +
                                         std::string(ref.name()) + "'");
            }
            return ref;
        }

        [[nodiscard]] std::unique_ptr<module_base> create(const std::string& name) const {
            return at(name).create();
        }

        // Пара в духе std::map: at() бросает, find() возвращает nullptr.
        [[nodiscard]] module_factory& at(const std::string& name) const {
            module_factory* factory = find(name);
            if (!factory) {
                throw std::runtime_error("no module named '" + name + "'");
            }
            return *factory;
        }

        [[nodiscard]] module_factory* find(const std::string& name) const {
            auto it = registry_.find(name);
            return it == registry_.end() ? nullptr : it->second.get();
        }

        bool remove(const std::string& name) { return registry_.erase(name) > 0; }

        [[nodiscard]] std::vector<const module_factory*> list() const {
            std::vector<const module_factory*> result;
            result.reserve(registry_.size());
            for (const auto& [name, factory] : registry_) {
                result.push_back(factory.get());
            }
            return result;
        }

    private:
        std::unordered_map<std::string, std::unique_ptr<module_factory>> registry_;
    };

    // Тонкая обёртка «реестр + список имён, зарегистрированных через неё».
    // Функции регистрации модулей принимают её, а не реестр напрямую, —
    // так module_loader знает, какие фабрики принесла его библиотека,
    // и может снять их при выгрузке. Класс конкретный (не виртуальный):
    // header-only платформа инстанцируется в каждом участнике заново.
    class module_registrar {
    public:
        explicit module_registrar(module_registry& registry) : registry_(&registry) {}

        template <std::derived_from<module_base> M>
            requires std::default_initializable<M>
        module_factory& add(std::string name) {
            return add(std::make_unique<typed_module_factory<M>>(std::move(name)));
        }

        module_factory& add(std::unique_ptr<module_factory> factory) {
            // имя запоминается только после успешной регистрации
            module_factory& ref = registry_->add(std::move(factory));
            registered_.emplace_back(ref.name());
            return ref;
        }

        [[nodiscard]] const std::vector<std::string>& registered() const {
            return registered_;
        }

    private:
        module_registry* registry_;
        std::vector<std::string> registered_;
    };

} // namespace atp

#endif // ANITOOLSPLATFORM_MODULE_REGISTRY_HPP
