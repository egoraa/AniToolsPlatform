// SPDX-License-Identifier: Apache-2.0
#include <vector>

#include <atp/plugin_c.h>

#include "descriptors.hpp"

extern "C" ATP_C_EXPORT unsigned atp_c_abi_version(void) {
    return ATP_C_ABI;
}

extern "C" ATP_C_EXPORT unsigned atp_module_count(void) {
    return static_cast<unsigned>(atp::lua_bridge::discover().size());
}

extern "C" ATP_C_EXPORT const atp_module_desc* atp_module_desc_at(unsigned index) {
    const std::vector<atp_module_desc>& all = atp::lua_bridge::last_batch();
    return index < all.size() ? &all[index] : nullptr;
}
