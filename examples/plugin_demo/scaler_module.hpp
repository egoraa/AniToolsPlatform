// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_EXAMPLES_PLUGIN_DEMO_SCALER_MODULE_HPP
#define ATP_EXAMPLES_PLUGIN_DEMO_SCALER_MODULE_HPP

#include <optional>

#include <atp/module.hpp>

#include "demo_types.hpp"

/// @file
/// The middle link of the demo chain: it takes the integer count and turns it into a real number
/// and into a user-defined structure. One module is enough to show that a type is not carried along
/// a pipeline — every connection has its own, and the ends have to agree.
namespace demo {

struct scaler_inputs : atp::io::inputs {
    /// "Last value wins": a scaler cares about the freshest count, not about every one ever sent —
    /// the deliberate contrast with the printer, which queues its events.
    atp::io::input<int>& count = make<atp::io::input<int>>("count");
};
struct scaler_outputs : atp::io::outputs {
    atp::io::output<double>& value = make<atp::io::output<double>>("value");
    atp::io::output<demo::sample>& sample = make<atp::io::output<demo::sample>>("sample");
};
struct scaler_props : atp::io::properties {
    /// Multiplier applied to the count; read every pass, so an edit on the fly is audible at once.
    atp::io::property<double>& gain = make<atp::io::property<double>>("gain", 0.5);
};
using scaler_ports = atp::io::ports<scaler_inputs, scaler_outputs, scaler_props>;

/// Scales the count and publishes the result twice: as a bare number and as a numbered sample.
class scaler_module : public atp::module<scaler_ports, "scaler", atp::ver<"1.0">> {
   public:
    atp::work_status iterate(std::stop_token) override {
        const std::optional<int> count = inputs().count.take();
        if (!count) {
            return atp::work_status::idle;
        }
        const double scaled = *count * properties().gain.get();
        outputs().value(scaled);
        outputs().sample(demo::sample{.index = ++produced_, .value = scaled});
        return atp::work_status::busy;
    }

   private:
    int produced_ = 0;
};

}  // namespace demo

#endif
