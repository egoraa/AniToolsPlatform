// SPDX-License-Identifier: Apache-2.0
#ifndef ANITOOLSPLATFORM_BRIDGES_LUA_VALUES_HPP
#define ANITOOLSPLATFORM_BRIDGES_LUA_VALUES_HPP

#include <cstdint>
#include <string>

#include <atp/plugin_c.h>

#include "lua_api.hpp"

namespace atp::lua_bridge {

/// Pushes a value the host handed over onto the stack, as a fresh Lua value.
///
/// A copy and not a view, and that is what keeps the host's lifetime rule off the script's back: the
/// buffer behind a text or a blob is valid only until the next read on the same context, while a Lua
/// string is owned by the interpreter and may be kept for as long as its author likes.
/// @param state the instance's interpreter
/// @param value the host's value, whose kind decides what is pushed
void to_lua(lua_State* state, const atp_value& value);

/// Reads a Lua value into a value of the port's declared kind.
///
/// The conversion is chosen by the declared kind and never by the type of what the script passed, so
/// a wrong argument gets an error naming the expectation rather than a reinterpreted payload. Two of
/// those checks are stricter than the obvious call would be, both on purpose: a text or blob port
/// insists on a real string, because lua_tolstring **rewrites a number on the stack** into a string
/// and would accept it silently, and a bool port insists on a real boolean, because Lua's own notion
/// of truth would take any string or number for true.
///
/// It reports a mismatch by raising a Lua error, which does not return. Every caller therefore runs
/// under a lua_pcall, and no frame between the two may own an object with a non-trivial destructor.
/// @param state the instance's interpreter
/// @param index stack index of the value the script passed
/// @param kind the port's declared kind
/// @param out filled in on success
/// @param scratch storage for a text or blob payload; must outlive the call that consumes @p out
void from_lua(lua_State* state, int index, atp_kind kind, atp_value& out, std::string& scratch);

/// Pushes a config node and everything under it as an ordinary Lua value: an object becomes a table
/// with string keys, an array a table with keys 1..n, a scalar its own type, the null form nil.
///
/// Walked once, when the instance is created, rather than exposed as handles: the author of a script
/// indexes a table and never learns that a tree of numbered nodes was involved. The honest loss is
/// order — a Lua table does not keep the order of its keys, while the host's value and a Python dict
/// do. Nothing here addresses a node by position, so nothing depends on it, and the ordered-proxy
/// trick the package uses for declared ports would buy nothing.
///
/// Reports nothing and raises nothing: a node it cannot read becomes nil, which keeps it usable from
/// the frames between lua_pcall and luaL_error where an error must not be raised.
/// @param state the instance's interpreter
/// @param api the host's callback table, which must be new enough to carry the config accessors
/// @param ctx this instance's context
/// @param node handle of the node to push
void config_to_lua(lua_State* state, const atp_api& api, atp_ctx* ctx, std::uint32_t node);

/// Pushes the bytes of the file a config came from, or an empty string when it came from none.
///
/// A Lua string is a byte string, so nothing is decoded and nothing can fail here — the encoding
/// question the Python bridge has to answer does not arise. A config written as "file:rig.yaml" reaches
/// a module this way and this way only: the host parses .json and hands every other format over as it
/// found it.
void config_text_to_lua(lua_State* state, const atp_api& api, atp_ctx* ctx);

/// Pushes the path of that file, or an empty string when the config came from none.
void config_origin_to_lua(lua_State* state, const atp_api& api, atp_ctx* ctx);

/// Pushes whether the host left the file unparsed, which is the one thing the pair above cannot say: a
/// .json holding literally `null` also gives an empty config next to a non-empty text.
void config_opaque_to_lua(lua_State* state, const atp_api& api, atp_ctx* ctx);

}  // namespace atp::lua_bridge

#endif
