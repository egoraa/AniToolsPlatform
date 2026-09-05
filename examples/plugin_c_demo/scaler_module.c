/* SPDX-License-Identifier: Apache-2.0 */
#include "scaler_module.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void scaler_copy_text(char* out, size_t cap, const atp_value* value) {
    const size_t room = cap - 1;
    const size_t take = value->as.bytes.size < room ? value->as.bytes.size : room;
    memcpy(out, value->as.bytes.data, take);
    out[take] = '\0';
}

static void scaler_read_config(scaler_state* state) {
    uint32_t root;
    uint32_t bands;
    uint32_t count;
    uint32_t i;
    atp_value value;

    memcpy(state->otherwise, "large", 6);
    if (!atp_api_has_config(state->api)) {
        return;
    }

    root = state->api->config_root(state->ctx);
    if (state->api->config_value_of(state->ctx, state->api->config_find(state->ctx, root, "otherwise", 9), &value) &&
        value.kind == ATP_KIND_TEXT) {
        scaler_copy_text(state->otherwise, sizeof(state->otherwise), &value);
    }

    bands = state->api->config_find(state->ctx, root, "bands", 5);
    if (state->api->config_kind(state->ctx, bands) != ATP_CONFIG_ARRAY) {
        return;
    }
    count = state->api->config_size(state->ctx, bands);
    for (i = 0; i < count && state->band_count < scaler_max_bands; ++i) {
        const uint32_t band = state->api->config_child_at(state->ctx, bands, i);
        scaler_band* out = &state->bands[state->band_count];

        if (!state->api->config_value_of(state->ctx, state->api->config_find(state->ctx, band, "upto", 4), &value) ||
            value.kind != ATP_KIND_I64) {
            continue;
        }
        out->upto = (long long)value.as.i64;
        if (!state->api->config_value_of(state->ctx, state->api->config_find(state->ctx, band, "name", 4), &value) ||
            value.kind != ATP_KIND_TEXT) {
            continue;
        }
        scaler_copy_text(out->name, sizeof(out->name), &value);
        ++state->band_count;
    }
}

static const char* scaler_band_of(const scaler_state* state, long long value) {
    unsigned i;
    for (i = 0; i < state->band_count; ++i) {
        if (value <= state->bands[i].upto) {
            return state->bands[i].name;
        }
    }
    return state->otherwise;
}

void* scaler_create(const atp_api* api, atp_ctx* ctx, void* user_data) {
    scaler_state* state = (scaler_state*)calloc(1, sizeof(scaler_state));
    (void)user_data;
    if (state == NULL) {
        return NULL;
    }
    state->api = api;
    state->ctx = ctx;
    scaler_read_config(state);
    return state;
}

void scaler_destroy(void* self) {
    free(self);
}

static void scaler_log(const scaler_state* state, atp_log_level level, const char* text) {
    state->api->log(state->ctx, level, text, strlen(text));
}

static int32_t scaler_factor(const scaler_state* state) {
    atp_value value;
    if (!state->api->get_property(state->ctx, scaler_prop_factor, &value)) {
        return 1;
    }
    return value.as.i32;
}

static int scaler_verbose(const scaler_state* state) {
    atp_value value;
    if (!state->api->get_property(state->ctx, scaler_prop_mode, &value)) {
        return 0;
    }
    return value.as.bytes.size == 7 && memcmp(value.as.bytes.data, "verbose", 7) == 0;
}

atp_status scaler_start(void* self) {
    scaler_state* state = (scaler_state*)self;
    char line[64];
    snprintf(line, sizeof(line), "c_scaler: scaling by %d", (int)scaler_factor(state));
    scaler_log(state, ATP_LOG_INFO, line);
    return ATP_OK;
}

static void scaler_note_mode(scaler_state* state) {
    atp_value mode;
    if (!state->api->take_property(state->ctx, scaler_prop_mode, &mode)) {
        return;
    }
    char line[64];
    snprintf(line, sizeof(line), "c_scaler: mode is now %.*s", (int)mode.as.bytes.size, mode.as.bytes.data);
    scaler_log(state, ATP_LOG_INFO, line);
}

atp_work scaler_iterate(void* self) {
    scaler_state* state = (scaler_state*)self;
    atp_work status = ATP_WORK_IDLE;
    atp_value in;

    scaler_note_mode(state);
    while (state->api->take_input(state->ctx, scaler_in_value, &in)) {
        const int32_t factor = scaler_factor(state);
        char text[96];
        atp_value out;
        int written;

        if (factor != 0 && (in.as.i32 > INT32_MAX / factor || in.as.i32 < INT32_MIN / factor)) {
            char reason[96];
            snprintf(reason, sizeof(reason), "%d x %d overflows a 32-bit integer", (int)in.as.i32, (int)factor);
            state->api->set_error(state->ctx, reason, strlen(reason));
            return ATP_WORK_ERROR;
        }

        state->total += (long long)in.as.i32 * factor;
        if (scaler_verbose(state)) {
            written =
                snprintf(text, sizeof(text), "%d x %d = %d [%s] (total %lld)", (int)in.as.i32, (int)factor,
                         (int)(in.as.i32 * factor), scaler_band_of(state, (long long)in.as.i32 * factor), state->total);
        } else {
            written = snprintf(text, sizeof(text), "%d [%s]", (int)(in.as.i32 * factor),
                               scaler_band_of(state, (long long)in.as.i32 * factor));
        }

        out.kind = ATP_KIND_TEXT;
        out.as.bytes.data = text;
        out.as.bytes.size = (size_t)written;
        if (!state->api->write_output(state->ctx, scaler_out_report, &out)) {
            const char* reason = "the report output refused the value";
            state->api->set_error(state->ctx, reason, strlen(reason));
            return ATP_WORK_ERROR;
        }
        status = ATP_WORK_BUSY;

        if (state->api->stop_requested(state->ctx)) {
            break;
        }
    }
    return status;
}

static const atp_input_desc scaler_inputs[] = {
    {"value", ATP_KIND_I32, ATP_QUEUE, 64, ATP_DROP_OLDEST},
};

static const atp_output_desc scaler_outputs[] = {
    {"report", ATP_KIND_TEXT},
};

static const char* const scaler_modes[] = {"plain", "verbose"};

static const atp_property_desc scaler_properties[] = {
    {"factor", ATP_KIND_I32, "2", NULL, 0, 1},
    {"mode", ATP_KIND_TEXT, "plain", scaler_modes, 2, 1},
};

static const atp_module_desc scaler_module = {
    sizeof(atp_module_desc),
    "c_scaler",
    {1, 0, 0, 0},
    2,
    scaler_inputs,
    1,
    scaler_outputs,
    1,
    scaler_properties,
    2,
    NULL,
    scaler_create,
    scaler_destroy,
    NULL,
    scaler_start,
    scaler_iterate,
    NULL,
    NULL,
    NULL,
    0,
};

const atp_module_desc* scaler_desc(void) {
    return &scaler_module;
}
