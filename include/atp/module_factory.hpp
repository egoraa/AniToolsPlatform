#ifndef ANITOOLSPLATFORM_MODULE_FACTORY_HPP
#define ANITOOLSPLATFORM_MODULE_FACTORY_HPP

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <atp/module_base.hpp>
#include <atp/version.hpp>

namespace atp {

    // Type-erased фабрика модуля — в одном ряду с module_base. Владелец —
    // module_registry; имя задаётся в точке регистрации (возможны алиасы
    // одного типа под разными именами), поэтому хранится в фабрике, а не
    // выводится из типа модуля.
    class module_factory {
    public:
        virtual ~module_factory() = default;

        module_factory(const module_factory&) = delete;
        module_factory& operator=(const module_factory&) = delete;

        [[nodiscard]] virtual std::string_view name() const noexcept = 0;
        [[nodiscard]] virtual version get_version() const noexcept = 0;

        // unique_ptr<module_base> безопасен и через границу плагина:
        // виртуальный деструктор module_base ведёт в deleting destructor
        // той библиотеки, где модуль создан, — память освобождает она же.
        [[nodiscard]] virtual std::unique_ptr<module_base> create() const = 0;

    protected:
        module_factory() = default;
    };

    // Типизированная фабрика. Версию отдаёт статически из M::module_version —
    // без создания экземпляра; модуль, написанный мимо шаблона module<> и не
    // имеющий константы, получает default_version (та же семантика, что у
    // module_base::get_version по умолчанию).
    template <std::derived_from<module_base> M>
        requires std::default_initializable<M>
    class typed_module_factory final : public module_factory {
    public:
        explicit typed_module_factory(std::string name) : name_(std::move(name)) {}

        [[nodiscard]] std::string_view name() const noexcept override { return name_; }

        [[nodiscard]] version get_version() const noexcept override {
            if constexpr (requires { { M::module_version } -> std::convertible_to<version>; }) {
                return M::module_version;
            } else {
                return default_version;
            }
        }

        [[nodiscard]] std::unique_ptr<module_base> create() const override {
            return std::make_unique<M>();
        }

    private:
        std::string name_;
    };

} // namespace atp

#endif // ANITOOLSPLATFORM_MODULE_FACTORY_HPP
