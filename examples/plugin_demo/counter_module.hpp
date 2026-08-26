// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULE_HPP
#define ATP_EXAMPLES_PLUGIN_DEMO_COUNTER_MODULE_HPP

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>

#include <atp/module.hpp>

/// @file
/// The producer of the demo chain. It doubles as a showcase of properties: every setting here is
/// visible in the output.
namespace demo {

struct counter_outputs : atp::io::outputs {
    atp::io::output<int>& count = make<int>("count");
    /// The same step in words. A second type on one module, so a single node already sends out two
    /// differently coloured edges — and the string can go both into a typed input and into the
    /// universal one.
    atp::io::output<std::string>& label = make<std::string>("label");
};
struct counter_props : atp::io::properties {
    /// Value of the first pass.
    atp::io::property<int>& start_at = make("start_at", 0);
    /// Instance-level enumeration: the type stays ordinary (int) but only the listed values are
    /// allowed — in the config this is still a number, and the inspector draws a drop-down.
    atp::io::property<int>& step = make("step", 1, atp::io::allowed(1, 2, 5, 10));
};
using counter_ports = atp::ports<atp::io::inputs, counter_outputs, counter_props>;

/// How a milestone's text is rendered — the config counterpart of printer's format property.
///
/// Both are enumerations declared the same way, by a name table on the type, and the pair is what the
/// two channels look like side by side: format is turned while the pipeline runs and is therefore a
/// property, while a milestone's emphasis is part of the row somebody wrote into the document and is
/// never touched again.
enum class emphasis { plain, shout, quiet };

}  // namespace demo

template <>
struct atp::io::enum_names<demo::emphasis> {
    static constexpr std::array entries{
        atp::io::enum_entry{demo::emphasis::plain, "plain"},
        atp::io::enum_entry{demo::emphasis::shout, "shout"},
        atp::io::enum_entry{demo::emphasis::quiet, "quiet"},
    };
};

namespace demo {

/// One row of the milestone table: at this count, say this instead of the ordinary label, in this
/// shape.
///
/// `how` is an enumeration rather than a string because the module knows exactly three renderings: as
/// a string a fourth name would reach this code and quietly render nothing, while an enum field is
/// refused by the loader with all three names spelled out in the message. In the document it is still
/// a name — an enumeration is a string with a set here, not a kind of its own.
struct milestone : atp::module_config {
    using module_config::module_config;
    std::int64_t& at = field("at", std::int64_t{0});
    std::string& say = field("say", "milestone");
    emphasis& how = field("how", emphasis::plain);
};

/// What counter declares as its config — and why any of it is a config rather than a property.
///
/// `milestones` is a table, and a property holds one scalar: there is no spelling of "a list of pairs"
/// among them. It is also read once, before the first connection, and nobody turns it while watching
/// the output — which is the other half of the rule. `start_at` and `step` stayed properties for
/// exactly the opposite reasons.
///
/// `prefix` could have been a property and is deliberately not: it is decided when the pipeline is
/// written and it keeps this example down to one config, which the inspector then shows as a tree with
/// a scalar and a table side by side.
struct counter_config : atp::module_config {
    using module_config::module_config;
    std::string& prefix = field("prefix", "tick");
    std::deque<milestone>& milestones = list<milestone>("milestones");
};

/// Counts upwards from start_at, emitting one value per pass.
class counter_module : public atp::module<counter_ports, "counter", atp::ver<"1.0">> {
   public:
    using config_type = counter_config;

    /// Nothing is parsed here: the fields are filled before this body runs and the document has already
    /// been checked against the declaration, with every problem reported at once.
    explicit counter_module(std::unique_ptr<counter_config> config) : config_(std::move(config)) {}

    /// Says one line, which is what makes the host's log channel visible end to end in the demo.
    void initialize(atp::module_context& context) override {
        context.host.info("counter ready");
    }

    void start() override {
        next_ = properties().start_at.get();
    }

    atp::work_status iterate(std::stop_token) override {
        outputs().count(next_);
        outputs().label(label_for(next_));
        next_ += properties().step.get();
        return atp::work_status::busy;
    }

   private:
    [[nodiscard]] std::string label_for(int value) const {
        for (const milestone& m : config_->milestones) {
            if (m.at == value) {
                return rendered(m);
            }
        }
        return config_->prefix + " " + std::to_string(value);
    }

    [[nodiscard]] static std::string rendered(const milestone& m) {
        switch (m.how) {
            case emphasis::shout: {
                std::string loud = m.say;
                std::ranges::transform(loud, loud.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                return loud + "!";
            }
            case emphasis::quiet:
                return "(" + m.say + ")";
            case emphasis::plain:
                break;
        }
        return m.say;
    }

    std::unique_ptr<counter_config> config_;
    int next_ = 0;
};

}  // namespace demo

#endif
