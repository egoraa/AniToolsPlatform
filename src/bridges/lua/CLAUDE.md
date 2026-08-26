# The Lua bridge

`src/bridges/lua/` is the third consumer of the C path, and it **vendors** its interpreter:
`cmake/BuildLua.cmake` fetches PUC-Lua and links it statically, so unlike the Python bridge it has no runtime
that can be missing — `ATP_BUILD_LUA_BRIDGE` (ON) only says whether this build wants it, and OFF skips the
download too. Three things about it are load-bearing. It is **compiled as C++**, because Lua built as C raises
errors with `longjmp` and every callback here is a frame with C++ destructors in it — hence also the rule that
**no frame between `lua_pcall` and `luaL_error` may own an object with a non-trivial destructor**, and hence
`lua_api.hpp` including the headers without `extern "C"`. Each module instance owns its **own `lua_State`**,
so instances run in parallel and there is no `PyEval_SaveThread` trap, no `pin_self`, and no "one bridge per
process" — the price is that the script's top level runs per instance and its values are not shared. And since
the script is therefore read twice, `atp._instantiate` is handed the port counts the host was promised and
refuses a file edited in between. The author package is one file, `lua/atp.lua` next to the library
(`ATP_LUA_PATH` adds scan directories); a module may not be named `atp`, and the directory walk skips that
file. The declaration order that the C ABI addresses ports by is kept by an `__newindex` proxy, because
`pairs` has no order — do not "simplify" it to a plain table. A script declares its config with
`atp.config(...)`; **`atp.group` and `atp.list` of objects take a function, never a table** — `pairs` has no
order and field order is a contract, so a nested object is collected by the same `__newindex` proxy the
declarations use. A module's config is an ordinary table on the instance, and beside it three fields for a
config attached as a file: `config_text` (**bytes**, not decoded — a Lua string is bytes, so unlike the Python
bridge there is no encoding to fail on), `config_origin` and `config_opaque`, the last one because a `.json`
holding literally `null` also leaves the table nil beside a non-empty text. Rationale in full:
`docs/architecture.md`, section «Мост для Lua».