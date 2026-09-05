// SPDX-License-Identifier: Apache-2.0
#include "script_file.hpp"

#include <fstream>
#include <iterator>

namespace atp::lua_bridge {

std::string to_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path from_utf8(std::string_view text) {
    return {std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())};
}

bool load_chunk(lua_State* state, const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot open the file";
        return false;
    }
    const std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string name = "@" + to_utf8(file);
    if (luaL_loadbuffer(state, body.data(), body.size(), name.c_str()) != LUA_OK) {
        const char* said = lua_tostring(state, -1);
        error = said == nullptr ? "?" : said;
        lua_pop(state, 1);
        return false;
    }
    return true;
}

}  // namespace atp::lua_bridge
