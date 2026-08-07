/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ATP_C_DEMO_SCALER_MODULE_H
#define ATP_C_DEMO_SCALER_MODULE_H

#include <atp/plugin_c.h>

/// @file
/// The smallest module worth writing, in C: one input, one output, two properties.
///
/// It is the near side of the boundary atp/plugin_c.h describes, and the fixture of the tests that
/// cover it. Nothing here includes a C++ header, links the SDK or names a std type — this file
/// compiles as C99 — which is the entire claim of that path and the only way to check it is to have a
/// plugin that really is not C++.
///
/// The two ports have deliberately different kinds. The input carries an int, which proves little:
/// every language agrees about a 32-bit integer. The output carries text, and that is where the
/// interesting thing happens — the bytes are written into a buffer on this side of the boundary and
/// end up as a std::string in a port of the host, allocated and freed entirely there, with no
/// allocator crossing in either direction.
///
/// The properties cover both halves of the setting vocabulary: `factor` is an ordinary number read
/// on every pass, `mode` is an enumeration whose set of values is declared here and enforced by the
/// host, so that a config or studio writing anything else is refused before this code sees it.

/// Indices of this module's ports, which is how the ABI addresses them: the position in the arrays of
/// the descriptor. Naming them is not decoration — a wrong number reads a different port of the right
/// kind, and that is the one mistake here that produces plausible values instead of an error.
enum scaler_input_index { scaler_in_value = 0 };
enum scaler_output_index { scaler_out_report = 0 };
enum scaler_property_index { scaler_prop_factor = 0, scaler_prop_mode = 1 };

/// State of one instance: what a C++ module would keep in its members.
///
/// The api table and the context are stored here because every call back into the host needs both,
/// and create is the only place they are handed over.
typedef struct scaler_state {
    const atp_api* api;
    atp_ctx* ctx;
    long long total;
} scaler_state;

/// Creates the state. Returns NULL if it cannot be allocated, which the host reports as a refusal.
void* scaler_create(const atp_api* api, atp_ctx* ctx, void* user_data);

/// Frees the state. Cannot fail, and is called even if start was never reached.
void scaler_destroy(void* self);

/// Logs the settings the module is about to run with. Nothing here can fail; it exists to show where
/// a module that does have peers to look up would look them up.
atp_status scaler_start(void* self);

/// Drains the input queue, scales every value and describes the result.
///
/// Reports ATP_WORK_ERROR when the multiplication would overflow, rather than wrapping around: a
/// wrong number travelling on is worse than a stopped pipeline, and it is the one natural failure
/// this module has — which makes it the demonstration of how a C module raises one.
atp_work scaler_iterate(void* self);

/// The module's declaration, with static storage: everything the host keeps pointers to has to
/// outlive the registration, and the simplest way to satisfy that is never to allocate it.
const atp_module_desc* scaler_desc(void);

#endif
