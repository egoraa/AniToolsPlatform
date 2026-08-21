// SPDX-License-Identifier: Apache-2.0
#include "values.hpp"

#include <cstdint>
#include <limits>

namespace atp::lua_bridge {
namespace {

lua_Integer integer_at(lua_State* state, int index) {
    int ok = 0;
    const lua_Integer value = lua_tointegerx(state, index, &ok);
    if (ok == 0) {
        luaL_error(state, "an integer port was written a %s", luaL_typename(state, index));
    }
    return value;
}

void bytes_at(lua_State* state, int index, atp_value& out, std::string& scratch) {
    if (lua_type(state, index) != LUA_TSTRING) {
        luaL_error(state, "a text or blob port was written a %s", luaL_typename(state, index));
    }
    std::size_t size = 0;
    const char* data = lua_tolstring(state, index, &size);
    scratch.assign(data, size);
    out.as.bytes.data = scratch.data();
    out.as.bytes.size = scratch.size();
}

}  // namespace

void to_lua(lua_State* state, const atp_value& value) {
    switch (value.kind) {
        case ATP_KIND_I32:
            lua_pushinteger(state, value.as.i32);
            return;
        case ATP_KIND_I64:
            lua_pushinteger(state, static_cast<lua_Integer>(value.as.i64));
            return;
        case ATP_KIND_F64:
            lua_pushnumber(state, value.as.f64);
            return;
        case ATP_KIND_BOOL:
            lua_pushboolean(state, value.as.boolean);
            return;
        case ATP_KIND_TEXT:
        case ATP_KIND_BLOB:
            lua_pushlstring(state, value.as.bytes.data, value.as.bytes.size);
            return;
    }
    lua_pushnil(state);
}

void from_lua(lua_State* state, int index, atp_kind kind, atp_value& out, std::string& scratch) {
    out.kind = kind;
    switch (kind) {
        case ATP_KIND_I32: {
            const lua_Integer value = integer_at(state, index);
            if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
                luaL_error(state, "value does not fit an i32 port");
            }
            out.as.i32 = static_cast<std::int32_t>(value);
            return;
        }
        case ATP_KIND_I64:
            out.as.i64 = static_cast<std::int64_t>(integer_at(state, index));
            return;
        case ATP_KIND_F64: {
            int ok = 0;
            const lua_Number value = lua_tonumberx(state, index, &ok);
            if (ok == 0) {
                luaL_error(state, "an f64 port was written a %s", luaL_typename(state, index));
            }
            out.as.f64 = static_cast<double>(value);
            return;
        }
        case ATP_KIND_BOOL:
            if (!lua_isboolean(state, index)) {
                luaL_error(state, "a bool port was written a %s", luaL_typename(state, index));
            }
            out.as.boolean = lua_toboolean(state, index);
            return;
        case ATP_KIND_TEXT:
        case ATP_KIND_BLOB:
            bytes_at(state, index, out, scratch);
            return;
    }
    luaL_error(state, "unknown port kind");
}

void config_to_lua(lua_State* state, const atp_api& api, atp_ctx* ctx, std::uint32_t node) {
    if (!atp_api_has_config(&api) || lua_checkstack(state, 4) == 0) {
        lua_pushnil(state);
        return;
    }
    const int kind = api.config_kind(ctx, node);
    if (kind == ATP_CONFIG_ARRAY) {
        const std::uint32_t count = api.config_size(ctx, node);
        lua_createtable(state, static_cast<int>(count), 0);
        for (std::uint32_t i = 0; i < count; ++i) {
            config_to_lua(state, api, ctx, api.config_child_at(ctx, node, i));
            lua_rawseti(state, -2, static_cast<lua_Integer>(i) + 1);
        }
        return;
    }
    if (kind == ATP_CONFIG_OBJECT) {
        const std::uint32_t count = api.config_size(ctx, node);
        lua_createtable(state, 0, static_cast<int>(count));
        for (std::uint32_t i = 0; i < count; ++i) {
            const char* key = nullptr;
            std::size_t length = 0;
            if (api.config_key_at(ctx, node, i, &key, &length) == 0) {
                continue;
            }
            lua_pushlstring(state, key, length);
            config_to_lua(state, api, ctx, api.config_child_at(ctx, node, i));
            lua_rawset(state, -3);
        }
        return;
    }
    atp_value scalar;
    if (kind == ATP_CONFIG_NULL || api.config_value_of(ctx, node, &scalar) == 0) {
        lua_pushnil(state);
        return;
    }
    to_lua(state, scalar);
}

void config_text_to_lua(lua_State* state, const atp_api& api, atp_ctx* ctx) {
    const char* text = nullptr;
    std::size_t length = 0;
    if (!atp_api_has_config_text(&api) || api.config_text(ctx, &text, &length) == 0) {
        lua_pushliteral(state, "");
        return;
    }
    lua_pushlstring(state, text, length);
}

void config_origin_to_lua(lua_State* state, const atp_api& api, atp_ctx* ctx) {
    const char* origin = nullptr;
    std::size_t length = 0;
    if (!atp_api_has_config_text(&api) || api.config_origin(ctx, &origin, &length) == 0) {
        lua_pushliteral(state, "");
        return;
    }
    lua_pushlstring(state, origin, length);
}

void config_opaque_to_lua(lua_State* state, const atp_api& api, atp_ctx* ctx) {
    lua_pushboolean(state, atp_api_has_config_text(&api) && api.config_is_opaque(ctx) != 0);
}

}  // namespace atp::lua_bridge
