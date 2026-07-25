#ifndef ANITOOLSPLATFORM_MODULE_FACTORY_HPP
#define ANITOOLSPLATFORM_MODULE_FACTORY_HPP

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <atp/module_base.hpp>
#include <atp/module_factory_base.hpp>
#include <atp/version.hpp>

namespace atp {

// Контракт «модуль объявляет версию»: статическая константа module_version,
// конвертируемая в version. Именованный концепт вместо requires по месту —
// читаемый однострочный if constexpr (clang-format безусловно разворачивает
// compound requirement на несколько строк).
template <typename T>
concept has_module_version = requires {
    { T::module_version } -> std::convertible_to<version>;
};

// Типизированная фабрика. Версию отдаёт статически из M::module_version —
// без создания экземпляра; модуль, написанный мимо шаблона module<> и не
// имеющий константы, получает default_version (та же семантика, что у
// module_base::get_version по умолчанию).
// Конфиг связывается при регистрации: фабрика хранит аргументы конструктора,
// каждый create() строит экземпляр от них — все экземпляры фабрики
// одинаковы, разные конфиги оформляются разными регистрациями (алиасами).
// Настройки экземпляра идут не сюда, а в проперти модуля: их выставляют уже
// на созданном объекте, поэтому у create нет параметров.
template <std::derived_from<module_base> M, typename... TArgs>
    requires std::constructible_from<M, const TArgs&...>
class module_factory final : public module_factory_base {
   public:
    explicit module_factory(std::string name, TArgs... args) : name_(std::move(name)), args_(std::move(args)...) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] version get_version() const noexcept override {
        if constexpr (has_module_version<M>) {
            return M::module_version;
        } else {
            return default_version;
        }
    }

    [[nodiscard]] module_ptr create() const override {
        return std::apply([](const TArgs&... args) { return module_ptr(new M(args...), {}); }, args_);
    }

   private:
    std::string name_;
    std::tuple<TArgs...> args_;  // конфиг фабрики; create копирует его в экземпляр
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_MODULE_FACTORY_HPP