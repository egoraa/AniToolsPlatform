// SPDX-License-Identifier: Apache-2.0
#include "errors.hpp"

#include <string>

namespace atp::lua_bridge {

int add_traceback(lua_State* state) {
    const char* said = lua_tostring(state, -1);
    luaL_traceback(state, state, said == nullptr ? "error with no message" : said, 1);
    return 1;
}

void report_error(const atp_api& api, atp_ctx* ctx, lua_State* state, const char* where) {
    const char* said = lua_tostring(state, -1);
    const std::string text = std::string(where) + " raised: " + (said == nullptr ? "?" : said);
    api.set_error(ctx, text.c_str(), text.size());
    lua_pop(state, 1);
}

}  // namespace atp::lua_bridge
