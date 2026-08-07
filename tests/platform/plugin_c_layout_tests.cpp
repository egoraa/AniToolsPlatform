// SPDX-License-Identifier: Apache-2.0
#include <cstddef>

#include <atp/plugin_c.h>

namespace {

// This file has no TEST on purpose: what it checks is checked by compiling it.
//
// A plugin that is not C++ cannot include plugin_c.h — it mirrors the declarations in its own language
// (templates/plugin_rust/src/abi.rs is the one in this repository). Such a mirror is sound only as long
// as the layout it was written against holds, and nothing else in the tree would notice a field being
// reordered or retyped here: every C++ user of the header recompiles against it and stays correct while
// every foreign mirror silently starts reading the wrong bytes. Pinning the numbers turns that into a
// build failure, next to the header it is about.
//
// Only LP64/LLP64 is pinned, which is every platform this project builds for. Elsewhere the asserts
// pass vacuously rather than pretending to know the answer.

constexpr bool lp64 = sizeof(void*) == 8;

static_assert(sizeof(atp_kind) == sizeof(int));

static_assert(!lp64 || sizeof(atp_value) == 24);
static_assert(!lp64 || offsetof(atp_value, kind) == 0);
static_assert(!lp64 || offsetof(atp_value, as) == 8);

static_assert(!lp64 || sizeof(atp_api) == 88);
static_assert(!lp64 || offsetof(atp_api, struct_size) == 0);
static_assert(!lp64 || offsetof(atp_api, get_input) == 8);
static_assert(!lp64 || offsetof(atp_api, take_input) == 16);
static_assert(!lp64 || offsetof(atp_api, write_output) == 24);
static_assert(!lp64 || offsetof(atp_api, get_property) == 32);
static_assert(!lp64 || offsetof(atp_api, take_property) == 40);
static_assert(!lp64 || offsetof(atp_api, set_property) == 48);
static_assert(!lp64 || offsetof(atp_api, stop_requested) == 56);
static_assert(!lp64 || offsetof(atp_api, log) == 64);
static_assert(!lp64 || offsetof(atp_api, wake) == 72);
static_assert(!lp64 || offsetof(atp_api, set_error) == 80);

static_assert(!lp64 || sizeof(atp_input_desc) == 24);
static_assert(!lp64 || offsetof(atp_input_desc, name) == 0);
static_assert(!lp64 || offsetof(atp_input_desc, kind) == 8);
static_assert(!lp64 || offsetof(atp_input_desc, flavor) == 12);
static_assert(!lp64 || offsetof(atp_input_desc, capacity) == 16);
static_assert(!lp64 || offsetof(atp_input_desc, overflow) == 20);

static_assert(!lp64 || sizeof(atp_output_desc) == 16);
static_assert(!lp64 || offsetof(atp_output_desc, name) == 0);
static_assert(!lp64 || offsetof(atp_output_desc, kind) == 8);

static_assert(!lp64 || sizeof(atp_property_desc) == 40);
static_assert(!lp64 || offsetof(atp_property_desc, name) == 0);
static_assert(!lp64 || offsetof(atp_property_desc, kind) == 8);
static_assert(!lp64 || offsetof(atp_property_desc, default_value) == 16);
static_assert(!lp64 || offsetof(atp_property_desc, options) == 24);
static_assert(!lp64 || offsetof(atp_property_desc, option_count) == 32);
static_assert(!lp64 || offsetof(atp_property_desc, persistent) == 36);

static_assert(!lp64 || sizeof(atp_module_desc) == 144);
static_assert(!lp64 || offsetof(atp_module_desc, struct_size) == 0);
static_assert(!lp64 || offsetof(atp_module_desc, name) == 8);
static_assert(!lp64 || offsetof(atp_module_desc, version) == 16);
static_assert(!lp64 || offsetof(atp_module_desc, version_count) == 32);
static_assert(!lp64 || offsetof(atp_module_desc, inputs) == 40);
static_assert(!lp64 || offsetof(atp_module_desc, input_count) == 48);
static_assert(!lp64 || offsetof(atp_module_desc, outputs) == 56);
static_assert(!lp64 || offsetof(atp_module_desc, output_count) == 64);
static_assert(!lp64 || offsetof(atp_module_desc, properties) == 72);
static_assert(!lp64 || offsetof(atp_module_desc, property_count) == 80);
static_assert(!lp64 || offsetof(atp_module_desc, user_data) == 88);
static_assert(!lp64 || offsetof(atp_module_desc, create) == 96);
static_assert(!lp64 || offsetof(atp_module_desc, destroy) == 104);
static_assert(!lp64 || offsetof(atp_module_desc, initialize) == 112);
static_assert(!lp64 || offsetof(atp_module_desc, start) == 120);
static_assert(!lp64 || offsetof(atp_module_desc, iterate) == 128);
static_assert(!lp64 || offsetof(atp_module_desc, stop) == 136);

static_assert(ATP_C_ABI == 1);

}  // namespace
