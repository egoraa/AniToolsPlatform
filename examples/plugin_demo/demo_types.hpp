// SPDX-License-Identifier: Apache-2.0
#ifndef ATP_EXAMPLES_PLUGIN_DEMO_DEMO_TYPES_HPP
#define ATP_EXAMPLES_PLUGIN_DEMO_DEMO_TYPES_HPP

#include <sstream>
#include <string>

/// @file
/// Types travelling over the demo ports beyond the built-in ones. A port carries any copyable type:
/// the studio colours a pin by the typeid of that type, while the runtime monitor, which can print
/// only the common ones, shows the type name instead of a value — no lie about what it cannot read.
namespace demo {

/// A measurement with its ordinal — a user-defined type on a port.
struct sample {
    int index = 0;
    double value = 0.0;
};

/// Shortest readable form of a real number: std::to_string would pad "52.5" with zeros.
inline std::string to_string(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

/// Text form of a sample. A free function rather than a member, so the type stays a plain aggregate
/// that ports copy around.
inline std::string to_string(const sample& value) {
    return "#" + std::to_string(value.index) + " = " + to_string(value.value);
}

}  // namespace demo

#endif
