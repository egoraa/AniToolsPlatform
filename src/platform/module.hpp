#ifndef ANITOOLSPLATFORM_MODULE_HPP
#define ANITOOLSPLATFORM_MODULE_HPP

#include <stop_token>

#include "io.hpp"
#include "version.hpp"

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
        // версия объявляется один раз — NTTP-параметром module (см. ниже),
        // здесь только точка доступа. Имя get_version, а не STL-шное
        // version() — имя занято типом atp::version (тот же приём, что
        // std::vector::get_allocator при занятом allocator).
        [[nodiscard]] virtual version get_version() const noexcept { return default_version; }
    };

    // «module» — контекстно-зависимое слово C++20: внутри namespace atp
    // класс с таким именем легален и конфликтов не создаёт.
    template <typename TInputs, typename TOutputs, version Version = default_version>
    class module : public module_base {
    public:
        // Версия объявляется один раз, NTTP-параметром, и доступна и на
        // компиляции (module_version), и в рантайме (get_version) — хранить
        // в объекте нечего.
        static constexpr version module_version = Version;

        [[nodiscard]] version get_version() const noexcept override { return Version; }

        void initialize() override {}
        void start() override {}
        void iterate(std::stop_token) override {}
        void stop() override {}

        [[nodiscard]] TInputs& inputs() { return inputs_; }
        [[nodiscard]] const TInputs& inputs() const { return inputs_; }
        [[nodiscard]] TOutputs& outputs() { return outputs_; }
        [[nodiscard]] const TOutputs& outputs() const { return outputs_; }

    private:
        TInputs inputs_;
        TOutputs outputs_;
    };

} // namespace atp

#endif // ANITOOLSPLATFORM_MODULE_HPP
