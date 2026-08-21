// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_TEMPLATE_PLUGIN_DOUBLER_MODULE_HPP
#define ATP_TEMPLATE_PLUGIN_DOUBLER_MODULE_HPP

#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <string>

#include <atp/config/fields.hpp>
#include <atp/module.hpp>
#include <atp/module/module_config.hpp>

/// @file
/// The smallest module worth writing: one input, one output, one property. It is a starting point
/// for a plugin of your own and, in CI, the far side of the only test the project has of the plugin
/// boundary itself — everything here is compiled by a build that never saw the platform's source
/// tree, against the installed SDK alone.
///
/// The two ports have deliberately different types, and not for variety. The input takes an int,
/// which proves nothing about type identity — every library agrees about int. The output carries a
/// std::string, which is where a plugin boundary actually breaks: the type has to be recognised as
/// the same across two separately built libraries (name-based, see atp/support/type_compare.hpp — hidden
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
    atp::io::output<std::string>& report = make<std::string>("report");
};
struct doubler_props : atp::io::properties {
    atp::io::property<int>& factor = make("factor", 2);
};
using doubler_ports = atp::ports<doubler_inputs, doubler_outputs, doubler_props>;

/// One band of the table: everything at or below `upto` is called `name`.
struct band_config : atp::config::fields {
    using fields::fields;
    std::int64_t& upto = field("upto", std::int64_t{0});
    std::string& name = field("name", "?");
};

/// The module's config, **declared** rather than parsed.
///
/// A declared config is read for you: the fields below are filled from the document before the
/// constructor body runs, every problem with it is reported in one list naming the file and the path,
/// and the host knows the shape without building a module — which is how an editor draws a form for it
/// instead of a box of JSON.
///
/// `list<band_config>` is the case this exists for. A list of pairs is not a value a property could
/// hold, and reading it by hand means a loop, an index, and a fallback on every key; here it is one
/// declaration and the elements arrive as objects.
///
/// Declaring is optional and not the only way. A module whose config is a format the platform does not
/// parse reads `config.text()` itself, and one that wants a single key deep in a tree can still call
/// `config.find("audio.rate")`. Declaring is what buys the checking and the form.
struct doubler_config : atp::config::fields {
    using fields::fields;
    std::deque<band_config>& bands = list<band_config>("bands");
    std::string& otherwise = field("otherwise", "large");
};

/// Multiplies every number it receives and describes the result in words.
///
/// The line it prints is what makes an end-to-end run observable from the outside: there is no other
/// way to see that a value made it out of one library and into another.
class doubler_module : public atp::module<doubler_ports, "doubler", atp::ver<"1.0">> {
   public:
    /// Naming the config type is what puts this module on the declared channel: the host reads the
    /// schema off it without building a module, and checks a document against it before building one.
    /// A module that names none is created exactly as it was before the channel existed.
    using config_type = doubler_config;

    /// Declaring this constructor is the whole of joining the config channel.
    ///
    /// The bands are the reason a config exists next to properties: a list of pairs is not a value a
    /// property could hold, it is read once before the first connection, and nobody edits it while the
    /// pipeline runs. The multiplier stays a property for the opposite reasons.
    ///
    /// Nothing here checks the config, and that is deliberate: by the time this runs it has already
    /// been checked against the declaration above, with every problem reported at once. An empty
    /// config is not a problem either — every field but a required one has a default, and this module
    /// declares none as required.
    explicit doubler_module(const atp::module_config& config) : config_(config) {}

    atp::work_status iterate(std::stop_token) override {
        atp::work_status status = atp::work_status::idle;
        while (const std::optional<int> in = inputs().value.try_pop()) {
            const int out = *in * properties().factor.get();
            std::cout << "doubler: " << *in << " -> " << out << '\n' << std::flush;
            outputs().report(std::to_string(*in) + " x " + std::to_string(properties().factor.get()) + " = " +
                             std::to_string(out) + " (" + band_of(out) + ")");
            status = atp::work_status::busy;
        }
        return status;
    }

   private:
    [[nodiscard]] const std::string& band_of(int value) const {
        for (const band_config& band : config_.bands) {
            if (value <= band.upto) {
                return band.name;
            }
        }
        return config_.otherwise;
    }

    doubler_config config_;
};

}  // namespace atp_template

#endif
