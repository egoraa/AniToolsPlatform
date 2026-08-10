// SPDX-License-Identifier: Apache-2.0
#include "ctx_type.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "values.hpp"

namespace atp::lua_bridge {
namespace {

constexpr const char* metatable_name = "atp.Ctx";

struct ctx_object {
    const atp_api* api;
    atp_ctx* ctx;
    const module_slot* slot;
    std::string* scratch;
};

ctx_object* self_of(lua_State* state) {
    return static_cast<ctx_object*>(luaL_checkudata(state, 1, metatable_name));
}

std::uint32_t port_at(lua_State* state, const std::vector<atp_kind>& kinds, atp_kind& kind) {
    const lua_Integer index = luaL_checkinteger(state, 2);
    if (index < 0 || static_cast<std::size_t>(index) >= kinds.size()) {
        luaL_error(state, "port index %d out of range", static_cast<int>(index));
    }
    kind = kinds[static_cast<std::size_t>(index)];
    return static_cast<std::uint32_t>(index);
}

int read_port(lua_State* state, bool consuming) {
    ctx_object* object = self_of(state);
    atp_kind kind = ATP_KIND_I32;
    const std::uint32_t port = port_at(state, object->slot->input_kinds, kind);
    atp_value value{};
    const int answer = consuming ? object->api->take_input(object->ctx, port, &value)
                                 : object->api->get_input(object->ctx, port, &value);
    if (answer != 1) {
        lua_pushnil(state);
        return 1;
    }
    to_lua(state, value);
    return 1;
}

int read_property(lua_State* state, bool consuming) {
    ctx_object* object = self_of(state);
    atp_kind kind = ATP_KIND_I32;
    const std::uint32_t port = port_at(state, object->slot->property_kinds, kind);
    atp_value value{};
    const int answer = consuming ? object->api->take_property(object->ctx, port, &value)
                                 : object->api->get_property(object->ctx, port, &value);
    if (answer != 1) {
        lua_pushnil(state);
        return 1;
    }
    to_lua(state, value);
    return 1;
}

int ctx_get(lua_State* state) {
    return read_port(state, false);
}

int ctx_take(lua_State* state) {
    return read_port(state, true);
}

int ctx_prop_get(lua_State* state) {
    return read_property(state, false);
}

int ctx_prop_take(lua_State* state) {
    return read_property(state, true);
}

int ctx_write(lua_State* state) {
    ctx_object* object = self_of(state);
    atp_kind kind = ATP_KIND_I32;
    const std::uint32_t port = port_at(state, object->slot->output_kinds, kind);
    atp_value value{};
    from_lua(state, 3, kind, value, *object->scratch);
    if (object->api->write_output(object->ctx, port, &value) != 1) {
        luaL_error(state, "the host refused the value written to this output");
    }
    return 0;
}

int ctx_prop_set(lua_State* state) {
    ctx_object* object = self_of(state);
    atp_kind kind = ATP_KIND_I32;
    const std::uint32_t port = port_at(state, object->slot->property_kinds, kind);
    atp_value value{};
    from_lua(state, 3, kind, value, *object->scratch);
    if (object->api->set_property(object->ctx, port, &value) != 1) {
        luaL_error(state, "the host refused the value written to this property");
    }
    return 0;
}

int ctx_log(lua_State* state) {
    ctx_object* object = self_of(state);
    const lua_Integer level = luaL_checkinteger(state, 2);
    std::size_t size = 0;
    const char* text = luaL_checklstring(state, 3, &size);
    object->api->log(object->ctx, static_cast<atp_log_level>(level), text, size);
    return 0;
}

int ctx_wake(lua_State* state) {
    ctx_object* object = self_of(state);
    object->api->wake(object->ctx);
    return 0;
}

int ctx_stop_requested(lua_State* state) {
    ctx_object* object = self_of(state);
    lua_pushboolean(state, object->api->stop_requested(object->ctx));
    return 1;
}

const luaL_Reg methods[] = {
    {"get", &ctx_get},           {"take", &ctx_take},           {"write", &ctx_write},
    {"prop_get", &ctx_prop_get}, {"prop_take", &ctx_prop_take}, {"prop_set", &ctx_prop_set},
    {"log", &ctx_log},           {"wake", &ctx_wake},           {"stop_requested", &ctx_stop_requested},
    {nullptr, nullptr},
};

}  // namespace

int open_ctx_module(lua_State* state) {
    luaL_newmetatable(state, metatable_name);
    lua_pushvalue(state, -1);
    lua_setfield(state, -2, "__index");
    luaL_setfuncs(state, methods, 0);
    lua_pop(state, 1);
    lua_newtable(state);
    return 1;
}

void push_ctx(lua_State* state, const atp_api* api, atp_ctx* ctx, const module_slot* slot, std::string* scratch) {
    auto* object = static_cast<ctx_object*>(lua_newuserdatauv(state, sizeof(ctx_object), 0));
    object->api = api;
    object->ctx = ctx;
    object->slot = slot;
    object->scratch = scratch;
    luaL_getmetatable(state, metatable_name);
    lua_setmetatable(state, -2);
}

}  // namespace atp::lua_bridge
