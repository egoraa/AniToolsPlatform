#ifndef ANITOOLSPLATFORM_MODULE_BASE_HPP
#define ANITOOLSPLATFORM_MODULE_BASE_HPP

#include <memory>
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

// Делетер модуля с пином: shared_ptr удерживает библиотеку плагина от
// выгрузки, пока жив модуль, — его vtable и код деструктора лежат в ней.
// У монолитных модулей пин пуст (обычный delete). Пин — член делетера,
// а не контрол-блок shared_ptr: владение модулем остаётся уникальным.
struct module_deleter {
    std::shared_ptr<void> pin{};

    void operator()(module_base* m) const noexcept {
        delete m;  // пин ещё жив (член делетера) — код деструктора доступен
    }
};

using module_ptr = std::unique_ptr<module_base, module_deleter>;

}  // namespace atp

#endif  // ANITOOLSPLATFORM_MODULE_BASE_HPP
