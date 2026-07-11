#ifndef ANITOOLSPLATFORM_MODULE_FACTORY_BASE_HPP
#define ANITOOLSPLATFORM_MODULE_FACTORY_BASE_HPP

#include <memory>
#include <string_view>

#include <atp/module_base.hpp>
#include <atp/version.hpp>

namespace atp {

    // Type-erased фабрика модуля — в одном ряду с module_base. Владелец —
    // module_registry; имя задаётся в точке регистрации (возможны алиасы
    // одного типа под разными именами), поэтому хранится в фабрике, а не
    // выводится из типа модуля.
    class module_factory_base {
    public:
        virtual ~module_factory_base() = default;

        module_factory_base(const module_factory_base&) = delete;
        module_factory_base& operator=(const module_factory_base&) = delete;

        [[nodiscard]] virtual std::string_view name() const noexcept = 0;
        [[nodiscard]] virtual version get_version() const noexcept = 0;

        // unique_ptr<module_base> безопасен и через границу плагина:
        // виртуальный деструктор module_base ведёт в deleting destructor
        // той библиотеки, где модуль создан, — память освобождает она же.
        [[nodiscard]] virtual std::unique_ptr<module_base> create() const = 0;

    protected:
        module_factory_base() = default;
    };

} // namespace atp

#endif // ANITOOLSPLATFORM_MODULE_FACTORY_BASE_HPP