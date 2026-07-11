#ifndef ANITOOLSPLATFORM_MODULE_BASE_HPP
#define ANITOOLSPLATFORM_MODULE_BASE_HPP

#include <stop_token>
#include <string_view>

#include <atp/module_context.hpp>
#include <atp/version.hpp>

namespace atp {

// Type-erased база модуля — в одном ряду с io_base/input_base/output_base.
class module_base {
   public:
    virtual ~module_base() = default;

    // Контекст — в фазах настройки: в initialize модуль публикует свои
    // интерфейсы (context.services.provide), в start ищет соседей (at/find) —
    // двухфазность делает порядок инициализации модулей неважным; в stop
    // симметрично снимает публикации. stop() обязан быть корректен и после
    // initialize без start: при ошибке каскада запуска исполнитель
    // откатывается, вызывая stop всем прошедшим initialize. iterate —
    // горячий путь, службы фазы настройки ему не передаются.
    virtual void initialize(module_context& context) = 0;
    virtual void start(module_context& context) = 0;
    virtual void iterate(std::stop_token stop_token) = 0;
    virtual void stop(module_context& context) = 0;

    // Версия модуля для рантайм-сравнения через type-erased ссылку.
    // Наследник, не объявивший версию, отвечает default_version; сама
    // версия объявляется один раз — NTTP-параметром module (module.hpp),
    // здесь только точка доступа. Имя get_version, а не STL-шное
    // version() — имя занято типом atp::version (тот же приём, что
    // std::vector::get_allocator при занятом allocator).
    [[nodiscard]] virtual version get_version() const noexcept {
        return default_version;
    }

    // Имя модуля через type-erased ссылку — симметрично get_version.
    // Модуль, не объявивший имени, — «аноним»: пустой string_view
    // (аналог default_version у версии). Имя объявляется один раз —
    // NTTP-параметром module (module.hpp), здесь только точка доступа.
    [[nodiscard]] virtual std::string_view get_name() const noexcept {
        return {};
    }
};

}  // namespace atp

#endif  // ANITOOLSPLATFORM_MODULE_BASE_HPP
