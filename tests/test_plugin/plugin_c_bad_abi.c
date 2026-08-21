/* SPDX-License-Identifier: Apache-2.0 */
#include <atp/plugin_c.h>

#include <stdlib.h>

static void* refuse_create(const atp_api* api, atp_ctx* ctx, void* user_data) {
    (void)api;
    (void)ctx;
    (void)user_data;
    return NULL;
}

static void refuse_destroy(void* self) {
    (void)self;
}

static atp_work refuse_iterate(void* self) {
    (void)self;
    return ATP_WORK_IDLE;
}

static const atp_module_desc future_module = {
    sizeof(atp_module_desc), "c_future",     {1, 0, 0, 0}, 1,    NULL,           0,    NULL, 0, NULL, 0, NULL,
    refuse_create,           refuse_destroy, NULL,         NULL, refuse_iterate, NULL, NULL,
};

ATP_C_EXPORT unsigned atp_c_abi_version(void) {
    return ATP_C_ABI + 1;
}

ATP_C_EXPORT unsigned atp_module_count(void) {
    return 1;
}

ATP_C_EXPORT const atp_module_desc* atp_module_desc_at(unsigned index) {
    return index == 0 ? &future_module : NULL;
}
