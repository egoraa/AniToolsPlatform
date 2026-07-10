#ifndef ANITOOLSPLATFORM_MODULE_HPP
#define ANITOOLSPLATFORM_MODULE_HPP

#include <stop_token>

#include <atp/io.hpp>
#include <atp/module_base.hpp>
#include <atp/version.hpp>

namespace atp {

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
