// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_TEMPLATE_PLUGIN_DOUBLER_MODULE_HPP
#define ATP_TEMPLATE_PLUGIN_DOUBLER_MODULE_HPP

#include <iostream>
#include <optional>
#include <string>

#include <atp/module.hpp>

/// @file
/// The smallest module worth writing: one input, one output, one property. It is a starting point
/// for a plugin of your own and, in CI, the far side of the only test the project has of the plugin
/// boundary itself — everything here is compiled by a build that never saw the platform's source
/// tree, against the installed SDK alone.
///
/// The two ports have deliberately different types, and not for variety. The input takes an int,
/// which proves nothing about type identity — every library agrees about int. The output carries a
/// std::string, which is where a plugin boundary actually breaks: the type has to be recognised as
/// the same across two separately built libraries (name-based, see atp/type_compare.hpp — hidden
/// visibility rules out comparing addresses), and the buffer allocated here is released by the
/// module on the other side, which is the contract that a mismatched C++ runtime violates and the
/// ABI handshake cannot detect.
namespace atp_template {

struct doubler_inputs : atp::io::inputs {
    /// A queue rather than a state input: a value arriving while this module is between passes must
    /// not be dropped, or what reaches the far side would depend on scheduling.
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
};
struct doubler_outputs : atp::io::outputs {
    atp::io::output<std::string>& report = make<atp::io::output<std::string>>("report");
};
struct doubler_props : atp::io::properties {
    atp::io::property<int>& factor = make<atp::io::property<int>>("factor", 2);
};
using doubler_ports = atp::io::ports<doubler_inputs, doubler_outputs, doubler_props>;

/// Multiplies every number it receives and describes the result in words.
///
/// The line it prints is what makes an end-to-end run observable from the outside: there is no other
/// way to see that a value made it out of one library and into another.
class doubler_module : public atp::module<doubler_ports, "doubler", atp::ver<"1.0">> {
   public:
    atp::work_status iterate(std::stop_token) override {
        atp::work_status status = atp::work_status::idle;
        while (const std::optional<int> in = inputs().value.try_pop()) {
            const int out = *in * properties().factor.get();
            std::cout << "doubler: " << *in << " -> " << out << '\n' << std::flush;
            outputs().report(std::to_string(*in) + " x " + std::to_string(properties().factor.get()) + " = " +
                             std::to_string(out));
            status = atp::work_status::busy;
        }
        return status;
    }
};

}  // namespace atp_template

#endif
