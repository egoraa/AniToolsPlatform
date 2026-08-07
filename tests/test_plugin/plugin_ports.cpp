// SPDX-License-Identifier: Apache-2.0
#include <optional>
#include <stop_token>

#include <atp/module.hpp>
#include <atp/plugin.hpp>

#include "boundary_types.hpp"

namespace {

struct echo_inputs : atp::io::inputs {
    atp::io::input<atp_tests::boundary_payload>& in = make<atp::io::input<atp_tests::boundary_payload>>("in");
    atp::io::input<int>& number = make<atp::io::input<int>>("number");
};
struct echo_outputs : atp::io::outputs {
    atp::io::output<atp_tests::boundary_payload>& out = make<atp::io::output<atp_tests::boundary_payload>>("out");
};
using echo_ports = atp::io::ports<echo_inputs, echo_outputs>;

class echo_module : public atp::module<echo_ports, "plugin_echo", atp::ver<"1.0">>, public atp_tests::boundary_service {
   public:
    void initialize(atp::module_context& context) override {
        context.services.provide<atp_tests::boundary_service>("echo", *this);
        services_ = &context.services;
    }

    void stop() override {
        if (services_ != nullptr) {
            (void)services_->remove<atp_tests::boundary_service>("echo");
            services_ = nullptr;
        }
    }

    atp::work_status iterate(std::stop_token) override {
        const std::optional<atp_tests::boundary_payload> incoming = inputs().in.take();
        if (!incoming) {
            return atp::work_status::idle;
        }
        outputs().out(*incoming);
        return atp::work_status::busy;
    }

    [[nodiscard]] int doubled(int value) const override {
        return value * 2;
    }

   private:
    atp::service_directory* services_ = nullptr;
};

}  // namespace

ATP_PLUGIN_EXPORT unsigned atp_abi_version() {
    return atp::plugin_abi;
}

ATP_PLUGIN_EXPORT void atp_register_modules(atp::module_registrar& registrar) {
    registrar.add<echo_module>();
}
