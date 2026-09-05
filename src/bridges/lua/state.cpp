// SPDX-License-Identifier: Apache-2.0
#include "state.hpp"

#include <cstdlib>
#include <filesystem>

#include "ctx_type.hpp"
#include "script_file.hpp"
#include "self_module.hpp"

namespace atp::lua_bridge {
namespace {

template <typename TChar>
void split_into(const TChar* value, TChar separator, std::vector<std::filesystem::path>& paths) {
    const std::basic_string<TChar> text(value);
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(separator, start);
        const std::basic_string<TChar> piece =
            text.substr(start, end == std::basic_string<TChar>::npos ? end : end - start);
        if (!piece.empty()) {
            paths.emplace_back(piece);
        }
        if (end == std::basic_string<TChar>::npos) {
            break;
        }
        start = end + 1;
    }
}

void set_package_path(lua_State* state, const std::vector<std::filesystem::path>& dirs) {
    std::string prefix;
    for (const std::filesystem::path& dir : dirs) {
        prefix += to_utf8(dir);
        prefix += "/?.lua;";
    }
    lua_getglobal(state, "package");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return;
    }
    lua_getfield(state, -1, "path");
    const char* inherited = lua_tostring(state, -1);
    const std::string composed = prefix + (inherited == nullptr ? "" : inherited);
    lua_pop(state, 1);
    lua_pushlstring(state, composed.c_str(), composed.size());
    lua_setfield(state, -2, "path");
    lua_pop(state, 1);
}

bool load_package(lua_State* state, std::string& error) {
    const std::filesystem::path file = self_directory() / "lua" / "atp.lua";
    std::string why;
    if (!load_chunk(state, file, why)) {
        error = "the atp package was not loadable from " + to_utf8(file) + ": " + why;
        return false;
    }
    if (lua_pcall(state, 0, 1, 0) != LUA_OK) {
        const char* said = lua_tostring(state, -1);
        error = std::string("the atp package failed to run: ") + (said == nullptr ? "?" : said);
        lua_pop(state, 1);
        return false;
    }
    luaL_getsubtable(state, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    lua_pushvalue(state, -2);
    lua_setfield(state, -2, "atp");
    lua_pop(state, 1);
    return true;
}

}  // namespace

std::vector<std::filesystem::path> scan_paths() {
    std::vector<std::filesystem::path> paths;
#if defined(_WIN32)
    wchar_t* env = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&env, &length, L"ATP_LUA_PATH") == 0 && env != nullptr) {
        split_into(env, L';', paths);
        std::free(env);
    }
#else
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (const char* env = std::getenv("ATP_LUA_PATH"); env != nullptr) {
        split_into(env, ':', paths);
    }
#endif
    if (const std::filesystem::path own = self_directory(); !own.empty()) {
        paths.push_back(own / "lua");
    }
    return paths;
}

lua_State* new_state(std::string& error) {
    lua_State* state = luaL_newstate();
    if (state == nullptr) {
        error = "the interpreter could not be created";
        return nullptr;
    }
    luaL_openlibs(state);
    luaL_requiref(state, "_atp", &open_ctx_module, 0);
    lua_pop(state, 1);
    set_package_path(state, scan_paths());
    if (!load_package(state, error)) {
        lua_close(state);
        return nullptr;
    }
    return state;
}

}  // namespace atp::lua_bridge
