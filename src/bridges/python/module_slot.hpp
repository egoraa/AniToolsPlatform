// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_PYTHON_MODULE_SLOT_HPP
#define ANITOOLSPLATFORM_BRIDGES_PYTHON_MODULE_SLOT_HPP

#include <deque>
#include <string>
#include <vector>

#include <atp/plugin_c.h>

namespace atp::bridge {

/// Everything one described module owns, and what the descriptor's user_data points at.
///
/// A pointer to the slot rather than an index, because the two numbers a module needs are not the
/// same one: the descriptor is addressed by its position in this library's storage, while the class
/// behind it is addressed by its position in the package's registry. The slot carries the second and
/// is found by the first.
///
/// Every member is storage the descriptor points into, so nothing here may be moved or freed while a
/// registration lives. A deque and not a vector for the strings: a vector reallocates, and each
/// c_str() already handed to a descriptor would dangle the moment it did.
struct module_slot {
    std::string name;
    /// Script the class was read from, which is the file its author edits.
    std::string source;
    /// Index of the class in the package's registry, passed back to atp._create.
    long long python_index = 0;
    std::deque<std::string> texts;
    std::deque<std::vector<const char*>> option_pointers;
    std::vector<atp_input_desc> inputs;
    std::vector<atp_output_desc> outputs;
    std::vector<atp_property_desc> properties;
    /// Declared kinds by port index, which is what a conversion is chosen by at run time.
    std::vector<atp_kind> input_kinds;
    std::vector<atp_kind> output_kinds;
    std::vector<atp_kind> property_kinds;
};

}  // namespace atp::bridge

#endif
