// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_EXAMPLES_PLUGIN_DEMO_SCALER_MODULE_HPP
#define ATP_EXAMPLES_PLUGIN_DEMO_SCALER_MODULE_HPP

#include <iostream>
#include <memory>
#include <optional>
#include <string>

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
    atp::io::input<int>& count = make<int>("count");
};
struct scaler_outputs : atp::io::outputs {
    atp::io::output<double>& value = make<double>("value");
    atp::io::output<demo::sample>& sample = make<demo::sample>("sample");
};
struct scaler_props : atp::io::properties {
    /// Multiplier applied to the count; read every pass, so an edit on the fly is audible at once.
    atp::io::property<double>& gain = make("gain", 0.5);
};
using scaler_ports = atp::ports<scaler_inputs, scaler_outputs, scaler_props>;

/// Scales the count and publishes the result twice: as a bare number and as a numbered sample.
class scaler_module : public atp::module<scaler_ports, "scaler", atp::ver<"1.0">> {
   public:
    /// The base itself, declaring no field: this module wants the bytes of its file and nothing else,
    /// and that is exactly what "a config with no declarations" means.
    using config_type = atp::module_config;

    /// Takes the module config to show the half of that channel the printer does not: a config the host
    /// did **not** parse, handed over as the bytes of the file it came from.
    ///
    /// `config/rig.ini` of the sample pipeline is not JSON and is not meant to become JSON — the host
    /// reads `.json` and nothing else, so a module that speaks another format parses it itself, and the
    /// platform learns no formats it does not need. This one only reports what it was handed, which is
    /// what proves the path end to end; a real module would parse the text here.
    ///
    /// is_opaque() rather than "text is not empty": a `.json` file holding literally `null` also leaves
    /// an empty tree beside a non-empty text, and this is exactly the question that tells the two apart.
    explicit scaler_module(std::unique_ptr<atp::module_config> config)
        : from_file_(config->is_opaque() ? config->origin() + " (" + std::to_string(config->text().size()) + " bytes)"
                                         : std::string()) {}

    void start() override {
        if (!from_file_.empty()) {
            std::cout << "scaler config: " << from_file_ << std::endl;
        }
    }

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
    std::string from_file_;
    int produced_ = 0;
};

}  // namespace demo

#endif
