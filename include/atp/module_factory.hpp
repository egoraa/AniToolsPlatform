#ifndef ANITOOLSPLATFORM_MODULE_FACTORY_HPP
#define ANITOOLSPLATFORM_MODULE_FACTORY_HPP

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
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
template <std::derived_from<module_base> M>
    requires std::default_initializable<M>
class module_factory final : public module_factory_base {
   public:
    explicit module_factory(std::string name) : name_(std::move(name)) {}

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
        return module_ptr(new M(), {});
    }

   private:
    std::string name_;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_MODULE_FACTORY_HPP