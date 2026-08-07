/* SPDX-License-Identifier: Apache-2.0 */
#include <atp/plugin_c.h>

#include <stdlib.h>
#include <string.h>

enum { in_state_i32 = 0, in_queue_i32 = 1, in_state_text = 2, in_state_blob = 3 };
enum { out_i32 = 0, out_i64 = 1, out_f64 = 2, out_bool = 3, out_text = 4, out_blob = 5 };
enum { prop_gain = 0, prop_mode = 1, prop_count = 2, prop_flag = 3, prop_fail = 4 };

typedef struct probe_state {
    const atp_api* api;
    atp_ctx* ctx;
    int initialized;
    int started;
    int stopped;
} probe_state;

static void* probe_create(const atp_api* api, atp_ctx* ctx, void* user_data) {
    probe_state* state = (probe_state*)calloc(1, sizeof(probe_state));
    (void)user_data;
    if (state == NULL) {
        return NULL;
    }
    state->api = api;
    state->ctx = ctx;
    return state;
}

static void probe_destroy(void* self) {
    free(self);
}

static atp_status probe_initialize(void* self) {
    probe_state* state = (probe_state*)self;
    const char* line = "initialized";
    state->initialized = 1;
    state->api->log(state->ctx, ATP_LOG_DEBUG, line, strlen(line));
    return ATP_OK;
}

static atp_status probe_start(void* self) {
    probe_state* state = (probe_state*)self;
    const char* line = "started";
    state->started = 1;
    state->api->log(state->ctx, ATP_LOG_INFO, line, strlen(line));
    return ATP_OK;
}

static atp_status probe_stop(void* self) {
    ((probe_state*)self)->stopped = 1;
    return ATP_OK;
}

static int probe_flag(const probe_state* state, uint32_t index) {
    atp_value value;
    if (!state->api->get_property(state->ctx, index, &value)) {
        return 0;
    }
    return value.as.boolean != 0;
}

static int32_t probe_count(const probe_state* state) {
    atp_value value;
    if (!state->api->get_property(state->ctx, prop_count, &value)) {
        return 1;
    }
    return value.as.i32;
}

static atp_work probe_iterate(void* self) {
    probe_state* state = (probe_state*)self;
    atp_work status = ATP_WORK_IDLE;
    atp_value value;
    atp_value out;

    if (probe_flag(state, prop_fail)) {
        const char* reason = "asked to fail";
        state->api->set_error(state->ctx, reason, strlen(reason));
        return ATP_WORK_ERROR;
    }
    if (state->api->stop_requested(state->ctx)) {
        return ATP_WORK_IDLE;
    }

    while (state->api->take_input(state->ctx, in_queue_i32, &value)) {
        out.kind = ATP_KIND_I32;
        out.as.i32 = value.as.i32;
        state->api->write_output(state->ctx, out_i32, &out);
        status = ATP_WORK_BUSY;
    }
    if (state->api->get_input(state->ctx, in_state_i32, &value)) {
        out.kind = ATP_KIND_I64;
        out.as.i64 = (int64_t)value.as.i32 * probe_count(state);
        state->api->write_output(state->ctx, out_i64, &out);
        status = ATP_WORK_BUSY;
    }
    if (state->api->get_input(state->ctx, in_state_text, &value)) {
        out.kind = ATP_KIND_TEXT;
        out.as.bytes = value.as.bytes;
        state->api->write_output(state->ctx, out_text, &out);
        status = ATP_WORK_BUSY;
    }
    if (state->api->get_input(state->ctx, in_state_blob, &value)) {
        out.kind = ATP_KIND_BLOB;
        out.as.bytes = value.as.bytes;
        state->api->write_output(state->ctx, out_blob, &out);
        status = ATP_WORK_BUSY;
    }
    if (state->api->take_property(state->ctx, prop_gain, &value)) {
        out.kind = ATP_KIND_F64;
        out.as.f64 = value.as.f64;
        state->api->write_output(state->ctx, out_f64, &out);
        status = ATP_WORK_BUSY;
    }
    if (state->api->take_property(state->ctx, prop_flag, &value)) {
        out.kind = ATP_KIND_BOOL;
        out.as.boolean = value.as.boolean;
        state->api->write_output(state->ctx, out_bool, &out);
        status = ATP_WORK_BUSY;
    }
    return status;
}

static const atp_input_desc probe_inputs[] = {
    {"state_i32", ATP_KIND_I32, ATP_STATE, 0, ATP_DROP_OLDEST},
    {"queue_i32", ATP_KIND_I32, ATP_QUEUE, 2, ATP_DROP_INCOMING},
    {"state_text", ATP_KIND_TEXT, ATP_STATE, 0, ATP_DROP_OLDEST},
    {"state_blob", ATP_KIND_BLOB, ATP_STATE, 0, ATP_DROP_OLDEST},
};

static const atp_output_desc probe_outputs[] = {
    {"out_i32", ATP_KIND_I32},   {"out_i64", ATP_KIND_I64},   {"out_f64", ATP_KIND_F64},
    {"out_bool", ATP_KIND_BOOL}, {"out_text", ATP_KIND_TEXT}, {"out_blob", ATP_KIND_BLOB},
};

static const char* const probe_modes[] = {"plain", "verbose"};
static const char* const probe_counts[] = {"1", "2", "3"};

static const atp_property_desc probe_properties[] = {
    {"gain", ATP_KIND_F64, "1.5", NULL, 0, 1},        {"mode", ATP_KIND_TEXT, "plain", probe_modes, 2, 1},
    {"count", ATP_KIND_I32, "3", probe_counts, 3, 1}, {"flag", ATP_KIND_BOOL, "true", NULL, 0, 0},
    {"fail", ATP_KIND_BOOL, "false", NULL, 0, 0},
};

static const atp_module_desc probe_module = {
    sizeof(atp_module_desc),
    "c_probe",
    {2, 1, 0, 0},
    2,
    probe_inputs,
    4,
    probe_outputs,
    6,
    probe_properties,
    5,
    NULL,
    probe_create,
    probe_destroy,
    probe_initialize,
    probe_start,
    probe_iterate,
    probe_stop,
};

static const atp_module_desc bare_module = {
    sizeof(atp_module_desc),
    "c_bare",
    {1, 0, 0, 0},
    1,
    NULL,
    0,
    NULL,
    0,
    NULL,
    0,
    NULL,
    probe_create,
    probe_destroy,
    NULL,
    NULL,
    probe_iterate,
    NULL,
};

typedef struct grown_desc {
    atp_module_desc base;
    unsigned long long field_from_a_later_abi;
} grown_desc;

static const grown_desc grown_module = {
    {
        sizeof(grown_desc),
        "c_grown",
        {3, 0, 0, 0},
        1,
        NULL,
        0,
        NULL,
        0,
        NULL,
        0,
        NULL,
        probe_create,
        probe_destroy,
        NULL,
        NULL,
        probe_iterate,
        NULL,
    },
    0xfeedfaceu,
};

ATP_C_EXPORT unsigned atp_c_abi_version(void) {
    return ATP_C_ABI;
}

ATP_C_EXPORT unsigned atp_module_count(void) {
    return 3;
}

ATP_C_EXPORT const atp_module_desc* atp_module_desc_at(unsigned index) {
    if (index == 0) {
        return &probe_module;
    }
    if (index == 1) {
        return &bare_module;
    }
    if (index == 2) {
        return &grown_module.base;
    }
    return NULL;
}
