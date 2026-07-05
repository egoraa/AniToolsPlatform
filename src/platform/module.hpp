#ifndef ANITOOLSPLATFORM_MODULE_HPP
#define ANITOOLSPLATFORM_MODULE_HPP

#include <stop_token>

#include "io.hpp"

namespace atp {

    class IModule {
    public:
        virtual ~IModule() = default;

        virtual void initialize() = 0;
        virtual void start() = 0;
        virtual void iterate(std::stop_token stop_token) = 0;
        virtual void stop() = 0;
    };

    template <typename TInputs, typename TOutputs>
    class Module : public IModule {
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
