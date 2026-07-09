#ifndef ANITOOLSPLATFORM_MODULE_HPP
#define ANITOOLSPLATFORM_MODULE_HPP

#include <stop_token>

#include "io.hpp"

namespace atp {

    // Type-erased база модуля — в одном ряду с io_base/input_base/output_base.
    class module_base {
    public:
        virtual ~module_base() = default;

        virtual void initialize() = 0;
        virtual void start() = 0;
        virtual void iterate(std::stop_token stop_token) = 0;
        virtual void stop() = 0;
    };

    // «module» — контекстно-зависимое слово C++20: внутри namespace atp
    // класс с таким именем легален и конфликтов не создаёт.
    template <typename TInputs, typename TOutputs>
    class module : public module_base {
    public:
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
