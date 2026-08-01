#ifndef ATP_EXAMPLES_PLUGIN_DEMO_PRINTER_MODULE_HPP
#define ATP_EXAMPLES_PLUGIN_DEMO_PRINTER_MODULE_HPP

#include <any>
#include <array>
#include <iostream>
#include <optional>
#include <string>

#include <atp/module.hpp>

#include "demo_types.hpp"

/// @file
/// The sink of the demo chain. It collects every type the example produces, so one module shows all
/// of the port kinds at once: an event queue, a state input and the universal input that accepts
/// any output at all.
namespace demo {

/// Shape of a printed line — a type-level enumeration: the name table below turns it into an
/// ordinary text property with a set of options, so it reaches the config as a name ("csv") rather
/// than a number.
enum class print_format { plain, bracketed, csv };

}  // namespace demo

template <>
struct atp::io::enum_names<demo::print_format> {
    static constexpr std::array entries{
        atp::io::enum_entry{demo::print_format::plain, "plain"},
        atp::io::enum_entry{demo::print_format::bracketed, "bracketed"},
        atp::io::enum_entry{demo::print_format::csv, "csv"},
    };
};

namespace demo {

struct printer_inputs : atp::io::inputs {
    /// Events: every number that was sent gets printed, none is lost between passes.
    atp::io::queued_input<int>& value = make<atp::io::queued_input<int>>("value");
    /// State: only the freshest label matters, and a watcher rule announces it when it changes.
    atp::io::input<std::string>& label = make<atp::io::input<std::string>>("label");
    atp::io::queued_input<double>& measure = make<atp::io::queued_input<double>>("measure");
    atp::io::queued_input<demo::sample>& sample = make<atp::io::queued_input<demo::sample>>("sample");
    /// The universal input: it accepts an output of any type, and is the only case where the
    /// compatibility check lets a mismatch through. The price is that the type is known at run time
    /// only — see describe() below.
    atp::io::queued_input<std::any>& probe = make<atp::io::queued_input<std::any>>("probe");
};
struct printer_props : atp::io::properties {
    /// Label printed before every value; empty means the module name is used.
    atp::io::property<std::string>& tag = make<atp::io::property<std::string>>("tag");
    /// Line shape: an enumeration coming from the type's name table.
    atp::io::property<print_format>& format = make<atp::io::property<print_format>>("format", print_format::plain);
    /// Adds the ordinal of the printed line.
    atp::io::property<bool>& verbose = make<atp::io::property<bool>>("verbose", false);
    /// Transient: lives only in the memory of a running pipeline and never reaches the config when
    /// studio saves — a note for the current session.
    atp::io::property<std::string>& note = make<atp::io::property<std::string>>("note", "", atp::io::transient);
};
using printer_ports = atp::io::ports<printer_inputs, atp::io::outputs, printer_props>;

/// Sink demonstrating two paths at once: a setting from the config to the output — all four
/// properties show up in the printing — and a value of any type from a connection to a line.
class printer_module : public atp::module<printer_ports, "printer", atp::ver<"1.0">> {
   public:
    void initialize(atp::module_context&) override {
        watcher_.watch(properties().format, [](const print_format& f) {
            std::cout << "printer format: " << atp::io::property_codec<print_format>::to_string(f) << std::endl;
        });
        watcher_.watch(inputs().label, [](const std::string& l) { std::cout << "printer label: " << l << std::endl; });
    }

    void start() override {
        const std::string tag = properties().tag.get();
        if (!tag.empty()) {
            std::cout << "printer tag: " << tag << std::endl;
        }
        const std::string note = properties().note.get();
        if (!note.empty()) {
            std::cout << "printer note: " << note << " (session only)" << std::endl;
        }
    }

    atp::work_status iterate(std::stop_token) override {
        atp::work_status status = watcher_.poll();
        while (const std::optional<int> v = inputs().value.try_pop()) {
            print("value", std::to_string(*v));
            status = atp::work_status::busy;
        }
        while (const std::optional<double> v = inputs().measure.try_pop()) {
            print("measure", to_string(*v));
            status = atp::work_status::busy;
        }
        while (const std::optional<demo::sample> v = inputs().sample.try_pop()) {
            print("sample", to_string(*v));
            status = atp::work_status::busy;
        }
        while (const std::optional<std::any> v = inputs().probe.try_pop()) {
            print("probe", describe(*v));
            status = atp::work_status::busy;
        }
        return status;
    }

   private:
    void print(const std::string& port, const std::string& body) {
        const std::string tag = properties().tag.get();
        const std::string prefix = (tag.empty() ? std::string("printer") : tag) + "." + port;
        ++printed_;

        std::string line;
        switch (properties().format.get()) {
            case print_format::plain:
                line = prefix + ": " + body;
                break;
            case print_format::bracketed:
                line = prefix + ": [" + body + "]";
                break;
            case print_format::csv:
                line = prefix + "," + body;
                break;
        }
        if (properties().verbose.get()) {
            line += " (#" + std::to_string(printed_) + ")";
        }
        std::cout << line << std::endl;
    }

    [[nodiscard]] static std::string describe(const std::any& value) {
        if (const int* v = std::any_cast<int>(&value)) {
            return std::to_string(*v);
        }
        if (const double* v = std::any_cast<double>(&value)) {
            return to_string(*v);
        }
        if (const std::string* v = std::any_cast<std::string>(&value)) {
            return *v;
        }
        if (const demo::sample* v = std::any_cast<demo::sample>(&value)) {
            return to_string(*v);
        }
        return std::string("<") + value.type().name() + ">";
    }

    atp::io::watcher watcher_;
    int printed_ = 0;
};

}  // namespace demo

#endif
