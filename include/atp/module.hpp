#ifndef ANITOOLSPLATFORM_MODULE_HPP
#define ANITOOLSPLATFORM_MODULE_HPP

#include <stop_token>
#include <string_view>

#include <atp/io.hpp>
#include <atp/module_base.hpp>
#include <atp/version.hpp>

namespace atp {

    // «module» — контекстно-зависимое слово C++20: внутри namespace atp
    // класс с таким именем легален и конфликтов не создаёт.
    // Имя третьим параметром, версия четвёртой: имя нужно чаще, а умолчания
    // «через одно» в C++ не работают. Пустое имя — «аноним»: такой модуль
    // регистрируется только под явным именем (module_registry::add<M>(name)).
    template <typename TInputs, typename TOutputs,
              detail::fixed_string Name = "",
              version Version = default_version>
    class module : public module_base {
    public:
        // Имя и версия объявляются один раз, NTTP-параметрами, и доступны
        // и на компиляции (module_name/module_version), и в рантайме
        // (get_name/get_version) — хранить в объекте нечего. view указывает
        // в template parameter object — статическая длительность хранения.
        static constexpr std::string_view module_name = Name.view();
        static constexpr version module_version = Version;

        [[nodiscard]] std::string_view get_name() const noexcept override { return module_name; }
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
