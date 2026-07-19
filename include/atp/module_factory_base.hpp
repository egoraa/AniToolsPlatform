#ifndef ANITOOLSPLATFORM_MODULE_FACTORY_BASE_HPP
#define ANITOOLSPLATFORM_MODULE_FACTORY_BASE_HPP

#include <memory>
#include <string>
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

    // module_ptr безопасен и через границу плагина: деструктор модуля —
    // код той библиотеки, где он создан, а пин в делетере не даёт ей
    // выгрузиться, пока модуль жив.
    // Основной путь создания: config — сырой текст параметров экземпляра
    // (узел params конфига пайплайна), пустая строка — параметров нет.
    // Интерпретацию строки определяет фабрика (см. module_config).
    [[nodiscard]] virtual module_ptr create(const std::string& config) const = 0;

    // Сахар для вызывающих без параметров (реестр, тесты, group).
    [[nodiscard]] module_ptr create() const {
        return create(std::string{});
    }

   protected:
    module_factory_base() = default;
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_MODULE_FACTORY_BASE_HPP