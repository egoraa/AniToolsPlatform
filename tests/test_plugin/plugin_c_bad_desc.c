/* SPDX-License-Identifier: Apache-2.0 */
#include <atp/plugin_c.h>

#include <stdlib.h>

static void* nothing_create(const atp_api* api, atp_ctx* ctx, void* user_data) {
    (void)api;
    (void)ctx;
    (void)user_data;
    return calloc(1, 1);
}

static void nothing_destroy(void* self) {
    free(self);
}

static atp_work nothing_iterate(void* self) {
    (void)self;
    return ATP_WORK_IDLE;
}

static const atp_module_desc sound_module = {
    sizeof(atp_module_desc),
    "c_sound",
    {1, 0, 0, 0},
    1,
    NULL,
    0,
    NULL,
    0,
    NULL,
    0,
    NULL,
    nothing_create,
    nothing_destroy,
    NULL,
    NULL,
    nothing_iterate,
    NULL,
    NULL,
    NULL,
    0,
};

static const atp_module_desc broken_module = {
    sizeof(atp_module_desc), "c_broken",      {1, 0, 0, 0}, 1,    NULL, 0,    NULL, 0,    NULL, 0, NULL,
    nothing_create,          nothing_destroy, NULL,         NULL, NULL, NULL, NULL, NULL, 0,
};

ATP_C_EXPORT unsigned atp_c_abi_version(void) {
    return ATP_C_ABI;
}

ATP_C_EXPORT unsigned atp_module_count(void) {
    return 2;
}

ATP_C_EXPORT const atp_module_desc* atp_module_desc_at(unsigned index) {
    if (index == 0) {
        return &sound_module;
    }
    if (index == 1) {
        return &broken_module;
    }
    return NULL;
}
