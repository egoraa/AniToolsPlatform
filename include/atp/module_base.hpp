#ifndef ANITOOLSPLATFORM_MODULE_BASE_HPP
#define ANITOOLSPLATFORM_MODULE_BASE_HPP

#include <stop_token>

#include <atp/version.hpp>

namespace atp {

    // Type-erased база модуля — в одном ряду с io_base/input_base/output_base.
    class module_base {
    public:
        virtual ~module_base() = default;

        virtual void initialize() = 0;
        virtual void start() = 0;
        virtual void iterate(std::stop_token stop_token) = 0;
        virtual void stop() = 0;

        // Версия модуля для рантайм-сравнения через type-erased ссылку.
        // Наследник, не объявивший версию, отвечает default_version; сама
        // версия объявляется один раз — NTTP-параметром module (module.hpp),
        // здесь только точка доступа. Имя get_version, а не STL-шное
        // version() — имя занято типом atp::version (тот же приём, что
        // std::vector::get_allocator при занятом allocator).
        [[nodiscard]] virtual version get_version() const noexcept { return default_version; }
    };

} // namespace atp

#endif // ANITOOLSPLATFORM_MODULE_BASE_HPP
