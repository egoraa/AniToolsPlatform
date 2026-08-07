// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULE_HPP
#define ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULE_HPP

#include <string>

#include <atp/module.hpp>

/// @file
/// The producer of the demo chain. It doubles as a showcase of properties: every setting here is
/// visible in the output.
namespace demo {

struct counter_outputs : atp::io::outputs {
    atp::io::output<int>& count = make<atp::io::output<int>>("count");
    /// The same step in words. A second type on one module, so a single node already sends out two
    /// differently coloured edges — and the string can go both into a typed input and into the
    /// universal one.
    atp::io::output<std::string>& label = make<atp::io::output<std::string>>("label");
};
struct counter_props : atp::io::properties {
    /// Value of the first pass.
    atp::io::property<int>& start_at = make<atp::io::property<int>>("start_at", 0);
    /// Instance-level enumeration: the type stays ordinary (int) but only the listed values are
    /// allowed — in the config this is still a number, and the inspector draws a drop-down.
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1, atp::io::allowed(1, 2, 5, 10));
};
using counter_ports = atp::io::ports<atp::io::inputs, counter_outputs, counter_props>;

/// Counts upwards from start_at, emitting one value per pass.
class counter_module : public atp::module<counter_ports, "counter", atp::ver<"1.0">> {
   public:
    /// Says one line, which is what makes the host's log channel visible end to end in the demo.
    void initialize(atp::module_context& context) override {
        context.host.info("counter ready");
    }

    void start() override {
        next_ = properties().start_at.get();
    }

    atp::work_status iterate(std::stop_token) override {
        outputs().count(next_);
        outputs().label("tick " + std::to_string(next_));
        next_ += properties().step.get();
        return atp::work_status::busy;
    }

   private:
    int next_ = 0;
};

}  // namespace demo

#endif
