// SPDX-License-Identifier: Apache-2.0
#include "descriptors.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <string>
#include <system_error>

#include "instance.hpp"
#include "module_slot.hpp"
#include "script_file.hpp"
#include "state.hpp"

namespace atp::lua_bridge {
namespace {

std::deque<module_slot>& storage() {
    static std::deque<module_slot> value;
    return value;
}

std::deque<std::vector<atp_module_desc>>& batches() {
    static std::deque<std::vector<atp_module_desc>> value;
    return value;
}

std::vector<atp_module_desc>& batch() {
    if (batches().empty()) {
        batches().emplace_back();
    }
    return batches().back();
}

void skipped(const std::filesystem::path& script, const char* reason) {
    std::fprintf(stderr, "atp: %s was not read and is skipped: %s\n", script.string().c_str(),
                 reason == nullptr ? "?" : reason);
}

std::string text_at(lua_State* state, int table, int index) {
    lua_rawgeti(state, table, index);
    std::string value;
    if (lua_type(state, -1) == LUA_TSTRING) {
        std::size_t size = 0;
        const char* data = lua_tolstring(state, -1, &size);
        value.assign(data, size);
    }
    lua_pop(state, 1);
    return value;
}

long long int_at(lua_State* state, int table, int index) {
    lua_rawgeti(state, table, index);
    const long long value = static_cast<long long>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    return value;
}

bool bool_at(lua_State* state, int table, int index) {
    lua_rawgeti(state, table, index);
    const bool value = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return value;
}

const char* keep_at(lua_State* state, module_slot& slot, int table, int index) {
    slot.texts.push_back(text_at(state, table, index));
    return slot.texts.back().c_str();
}

void read_inputs(lua_State* state, module_slot& slot, int rows) {
    const std::size_t count = lua_rawlen(state, rows);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(state, rows, static_cast<lua_Integer>(i));
        const int row = lua_gettop(state);
        atp_input_desc desc{};
        desc.name = keep_at(state, slot, row, 1);
        desc.kind = static_cast<atp_kind>(int_at(state, row, 2));
        desc.flavor = static_cast<atp_flavor>(int_at(state, row, 3));
        desc.capacity = static_cast<std::uint32_t>(int_at(state, row, 4));
        desc.overflow = static_cast<atp_overflow>(int_at(state, row, 5));
        slot.inputs.push_back(desc);
        slot.input_kinds.push_back(desc.kind);
        lua_pop(state, 1);
    }
}

void read_outputs(lua_State* state, module_slot& slot, int rows) {
    const std::size_t count = lua_rawlen(state, rows);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(state, rows, static_cast<lua_Integer>(i));
        const int row = lua_gettop(state);
        atp_output_desc desc{};
        desc.name = keep_at(state, slot, row, 1);
        desc.kind = static_cast<atp_kind>(int_at(state, row, 2));
        slot.outputs.push_back(desc);
        slot.output_kinds.push_back(desc.kind);
        lua_pop(state, 1);
    }
}

void read_properties(lua_State* state, module_slot& slot, int rows) {
    const std::size_t count = lua_rawlen(state, rows);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(state, rows, static_cast<lua_Integer>(i));
        const int row = lua_gettop(state);
        atp_property_desc desc{};
        desc.name = keep_at(state, slot, row, 1);
        desc.kind = static_cast<atp_kind>(int_at(state, row, 2));
        desc.default_value = keep_at(state, slot, row, 3);

        lua_rawgeti(state, row, 4);
        const int options = lua_gettop(state);
        const std::size_t option_count = lua_type(state, options) == LUA_TTABLE ? lua_rawlen(state, options) : 0;
        std::vector<const char*>& pointers = slot.option_pointers.emplace_back();
        pointers.reserve(option_count);
        for (std::size_t o = 1; o <= option_count; ++o) {
            pointers.push_back(keep_at(state, slot, options, static_cast<int>(o)));
        }
        lua_pop(state, 1);
        desc.option_count = static_cast<std::uint32_t>(option_count);
        desc.options = pointers.empty() ? nullptr : pointers.data();
        desc.persistent = bool_at(state, row, 5) ? 1 : 0;

        slot.properties.push_back(desc);
        slot.property_kinds.push_back(desc.kind);
        lua_pop(state, 1);
    }
}

std::string field_text(lua_State* state, int table, const char* key) {
    lua_getfield(state, table, key);
    std::string value;
    if (lua_type(state, -1) == LUA_TSTRING) {
        std::size_t size = 0;
        const char* data = lua_tolstring(state, -1, &size);
        value.assign(data, size);
    }
    lua_pop(state, 1);
    return value;
}

void build_one(lua_State* state, const std::filesystem::path& script, int row) {
    module_slot& slot = storage().emplace_back();
    slot.name = field_text(state, row, "name");
    slot.source = to_utf8(script);
    slot.file = script;

    atp_module_desc desc{};
    desc.struct_size = static_cast<std::uint32_t>(sizeof(atp_module_desc));
    desc.name = slot.name.c_str();
    desc.source = slot.source.c_str();

    lua_getfield(state, row, "version");
    const int version = lua_gettop(state);
    const std::size_t parts = lua_type(state, version) == LUA_TTABLE ? lua_rawlen(state, version) : 0;
    const std::size_t used = parts > 4 ? 4 : parts;
    for (std::size_t i = 1; i <= used; ++i) {
        desc.version[i - 1] = static_cast<std::uint32_t>(int_at(state, version, static_cast<int>(i)));
    }
    desc.version_count = static_cast<std::uint32_t>(used);
    lua_pop(state, 1);

    lua_getfield(state, row, "inputs");
    read_inputs(state, slot, lua_gettop(state));
    lua_pop(state, 1);
    lua_getfield(state, row, "outputs");
    read_outputs(state, slot, lua_gettop(state));
    lua_pop(state, 1);
    lua_getfield(state, row, "properties");
    read_properties(state, slot, lua_gettop(state));
    lua_pop(state, 1);

    desc.inputs = slot.inputs.empty() ? nullptr : slot.inputs.data();
    desc.input_count = static_cast<std::uint32_t>(slot.inputs.size());
    desc.outputs = slot.outputs.empty() ? nullptr : slot.outputs.data();
    desc.output_count = static_cast<std::uint32_t>(slot.outputs.size());
    desc.properties = slot.properties.empty() ? nullptr : slot.properties.data();
    desc.property_count = static_cast<std::uint32_t>(slot.properties.size());
    desc.user_data = &slot;
    desc.create = &instance_create;
    desc.destroy = &instance_destroy;
    desc.initialize = &instance_initialize;
    desc.start = &instance_start;
    desc.stop = &instance_stop;
    desc.iterate = &instance_iterate;
    batch().push_back(desc);
}

void read_script(const std::filesystem::path& script) {
    std::string error;
    lua_State* state = new_state(error);
    if (state == nullptr) {
        skipped(script, error.c_str());
        return;
    }
    const int package = lua_gettop(state);
    std::string why;
    if (!load_chunk(state, script, why)) {
        skipped(script, why.c_str());
        lua_close(state);
        return;
    }
    if (lua_pcall(state, 0, 0, 0) != LUA_OK) {
        skipped(script, lua_tostring(state, -1));
        lua_close(state);
        return;
    }
    lua_getfield(state, package, "_declared");
    if (lua_pcall(state, 0, 1, 0) != LUA_OK) {
        skipped(script, lua_tostring(state, -1));
        lua_close(state);
        return;
    }
    const int rows = lua_gettop(state);
    if (lua_type(state, rows) != LUA_TTABLE) {
        skipped(script, "the atp package did not answer with a table of modules");
        lua_close(state);
        return;
    }
    const std::size_t count = lua_rawlen(state, rows);
    for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(state, rows, static_cast<lua_Integer>(i));
        const int row = lua_gettop(state);
        if (lua_type(state, row) == LUA_TTABLE) {
            build_one(state, script, row);
        } else {
            skipped(script, "a described module was not a table");
        }
        lua_pop(state, 1);
    }
    lua_close(state);
}

std::vector<std::filesystem::path> scripts_in(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> found;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".lua" && entry.path().filename() != "atp.lua") {
            found.push_back(entry.path());
        }
    }
    std::ranges::sort(found);
    return found;
}

}  // namespace

const std::vector<atp_module_desc>& last_batch() {
    return batch();
}

const std::vector<atp_module_desc>& discover() {
    batches().emplace_back();
    std::vector<std::filesystem::path> seen;
    for (const std::filesystem::path& root : scan_paths()) {
        std::error_code ec;
        const std::filesystem::path key = std::filesystem::weakly_canonical(root, ec);
        if (std::ranges::find(seen, key) != seen.end()) {
            continue;
        }
        seen.push_back(key);
        if (!std::filesystem::is_directory(root, ec)) {
            continue;
        }
        for (const std::filesystem::path& script : scripts_in(root)) {
            read_script(script);
        }
    }
    return batch();
}

}  // namespace atp::lua_bridge
