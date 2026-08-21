// SPDX-License-Identifier: Apache-2.0
#include "instance.hpp"

#include <cstdio>
#include <new>
#include <string>

#include "ctx_type.hpp"
#include "errors.hpp"
#include "module_slot.hpp"
#include "script_file.hpp"
#include "state.hpp"
#include "values.hpp"

namespace atp::lua_bridge {
namespace {

struct instance {
    lua_State* state = nullptr;
    const atp_api* api = nullptr;
    atp_ctx* ctx = nullptr;
    std::string scratch;
    int self_ref = LUA_NOREF;
    bool has_initialize = false;
    bool has_start = false;
    bool has_stop = false;
};

bool defines(lua_State* state, int table, const char* method) {
    lua_getfield(state, table, method);
    const bool present = !lua_isnil(state, -1);
    lua_pop(state, 1);
    return present;
}

bool call_method(instance* self, const char* method, int nresults) {
    lua_pushcfunction(self->state, &add_traceback);
    const int handler = lua_gettop(self->state);
    lua_rawgeti(self->state, LUA_REGISTRYINDEX, self->self_ref);
    lua_getfield(self->state, -1, method);
    lua_insert(self->state, -2);
    const int answer = lua_pcall(self->state, 1, nresults, handler);
    lua_remove(self->state, handler);
    return answer == LUA_OK;
}

atp_status call_lifecycle(instance* self, bool present, const char* method) {
    if (!present) {
        return ATP_OK;
    }
    if (!call_method(self, method, 0)) {
        report_error(*self->api, self->ctx, self->state, method);
        return 1;
    }
    return ATP_OK;
}

}  // namespace

extern "C" void* instance_create(const atp_api* api, atp_ctx* ctx, void* user_data) {
    const auto* slot = static_cast<const module_slot*>(user_data);
    auto* self = new (std::nothrow) instance{};
    if (self == nullptr) {
        return nullptr;
    }
    self->api = api;
    self->ctx = ctx;

    std::string error;
    self->state = new_state(error);
    if (self->state == nullptr) {
        std::fprintf(stderr, "atp: %s cannot be created: %s\n", slot->name.c_str(), error.c_str());
        delete self;
        return nullptr;
    }
    const int package = lua_gettop(self->state);

    std::string why;
    if (!load_chunk(self->state, slot->file, why)) {
        std::fprintf(stderr, "atp: %s cannot be created: %s\n", slot->name.c_str(), why.c_str());
        lua_close(self->state);
        delete self;
        return nullptr;
    }
    if (lua_pcall(self->state, 0, 0, 0) != LUA_OK) {
        const char* said = lua_tostring(self->state, -1);
        std::fprintf(stderr, "atp: %s cannot be created: %s\n", slot->name.c_str(), said == nullptr ? "?" : said);
        lua_close(self->state);
        delete self;
        return nullptr;
    }

    lua_getfield(self->state, package, "_instantiate");
    lua_pushlstring(self->state, slot->name.c_str(), slot->name.size());
    push_ctx(self->state, api, ctx, slot, &self->scratch);
    lua_pushinteger(self->state, static_cast<lua_Integer>(slot->inputs.size()));
    lua_pushinteger(self->state, static_cast<lua_Integer>(slot->outputs.size()));
    lua_pushinteger(self->state, static_cast<lua_Integer>(slot->properties.size()));
    config_to_lua(self->state, *api, ctx, atp_api_has_config(api) ? api->config_root(ctx) : 0u);
    config_text_to_lua(self->state, *api, ctx);
    config_origin_to_lua(self->state, *api, ctx);
    config_opaque_to_lua(self->state, *api, ctx);
    if (lua_pcall(self->state, 9, 1, 0) != LUA_OK) {
        const char* said = lua_tostring(self->state, -1);
        std::fprintf(stderr, "atp: %s cannot be created: %s\n", slot->name.c_str(), said == nullptr ? "?" : said);
        lua_close(self->state);
        delete self;
        return nullptr;
    }

    const int object = lua_gettop(self->state);
    self->has_initialize = defines(self->state, object, "initialize");
    self->has_start = defines(self->state, object, "start");
    self->has_stop = defines(self->state, object, "stop");
    self->self_ref = luaL_ref(self->state, LUA_REGISTRYINDEX);
    return self;
}

extern "C" void instance_destroy(void* self) {
    auto* state = static_cast<instance*>(self);
    if (state->state != nullptr) {
        luaL_unref(state->state, LUA_REGISTRYINDEX, state->self_ref);
        lua_close(state->state);
    }
    delete state;
}

extern "C" atp_status instance_initialize(void* self) {
    auto* state = static_cast<instance*>(self);
    return call_lifecycle(state, state->has_initialize, "initialize");
}

extern "C" atp_status instance_start(void* self) {
    auto* state = static_cast<instance*>(self);
    return call_lifecycle(state, state->has_start, "start");
}

extern "C" atp_status instance_stop(void* self) {
    auto* state = static_cast<instance*>(self);
    return call_lifecycle(state, state->has_stop, "stop");
}

extern "C" atp_work instance_iterate(void* self) {
    auto* state = static_cast<instance*>(self);
    if (!call_method(state, "iterate", 1)) {
        report_error(*state->api, state->ctx, state->state, "iterate");
        return ATP_WORK_ERROR;
    }
    const lua_Integer answer = lua_tointeger(state->state, -1);
    lua_pop(state->state, 1);
    return answer == 0 ? ATP_WORK_BUSY : ATP_WORK_IDLE;
}

}  // namespace atp::lua_bridge
