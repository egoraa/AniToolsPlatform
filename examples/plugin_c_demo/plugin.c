/* SPDX-License-Identifier: Apache-2.0 */
#include <atp/plugin_c.h>

#include "scaler_module.h"

ATP_C_EXPORT unsigned atp_c_abi_version(void) {
    return ATP_C_ABI;
}

ATP_C_EXPORT unsigned atp_module_count(void) {
    return 1;
}

ATP_C_EXPORT const atp_module_desc* atp_module_desc_at(unsigned index) {
    return index == 0 ? scaler_desc() : NULL;
}
