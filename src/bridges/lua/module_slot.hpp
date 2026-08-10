// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_LUA_MODULE_SLOT_HPP
#define ANITOOLSPLATFORM_BRIDGES_LUA_MODULE_SLOT_HPP

#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include <atp/plugin_c.h>

namespace atp::lua_bridge {

/// Everything one described module owns, and what the descriptor's user_data points at.
///
/// Every member is storage the descriptor points into, so nothing here may be moved or freed while a
/// registration lives. A deque and not a vector for the strings: a vector reallocates, and each
/// c_str() already handed to a descriptor would dangle the moment it did.
struct module_slot {
    std::string name;
    /// Script the module was declared in, as the descriptor carries it: **UTF-8**, because that is
    /// what crosses the C boundary.
    std::string source;
    /// The same script as a path, kept beside it because an instance re-executes the file and must
    /// open it without going back through a narrow encoding.
    std::filesystem::path file;
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

}  // namespace atp::lua_bridge

#endif
