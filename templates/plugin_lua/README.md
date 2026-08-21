# Lua module template

A platform module written in Lua and loaded through the **C path** of the ABI
(`include/atp/plugin_c.h`) by the `atp_lua_bridge` bridge. There is nothing to compile and nothing to
install: two scripts, `scale.lua` and `packer.lua`, and a config to go with them.

```bash
cp scale.lua packer.lua <next to atp_app>/plugins/lua/
cp pipeline.json <next to atp_app>/config/
atp_app config/pipeline.json
```

The bridge and the `atp` package already sit next to `atp_app` — they ship with the platform. Unlike
the Python bridge, this one needs no interpreter on the machine: **the interpreter is inside the
plugin file**. If `atp_app` runs, a Lua module runs.

The two scripts are deliberately unlike each other. `scale.lua` is the short one to start from: one
input, two outputs, one property. `packer.lua` shows the **blob** output, which is the escape hatch
for a payload the closed type set of the C path does not name — and in Lua it costs nothing, because
a Lua string is already a byte string.

## Declaring a module

```lua
local atp = require("atp")

local M = atp.module("lua_scale", { 1, 0 })

M.value = atp.input(atp.i32)
M.result = atp.output(atp.i32)
M.factor = atp.property(atp.i32, 3)

function M:iterate()
    local v = self.value:take()
    if v == nil then
        return atp.IDLE
    end
    self.result:write(v * self.factor:get())
    return atp.BUSY
end

return M
```

Ports are **assigned to the table `atp.module` returns, and the order of those assignments matters**:
the C ABI addresses a port by its index, so the order the script writes them in is the order the host
sees. Everything else assigned to that table is a method the instance will have.

Types are `atp.i32`, `atp.i64`, `atp.f64`, `atp.bool`, `atp.text` and `atp.blob`. They are the
platform's own types, not a parallel set — an `atp.i32` output connects to a C++ `output<int32_t>`
with no adapter in the config. A port meant to meet a C++ `std::int64_t` must say `atp.i64`: the
platform connects by exact payload type, and the two are different types rather than two sizes of one.

Reading is pull-only: `:get()` for state, `:take()` for events, both answering `nil` when there is
nothing. `iterate` returns `atp.BUSY` or `atp.IDLE`, which is what paces the runner.

An input can queue: `atp.input(atp.text, { queue = true, capacity = 4, overflow = atp.DROP_INCOMING })`.
Capacity 0 asks for the platform's own default limit rather than for no limit.

A property with a non-empty `options` is an enumeration, checked on every write including the default:
`atp.property(atp.text, "little", { options = { "little", "big" } })`. Pass `persistent = false` for a
setting that lives only while the pipeline runs.

`initialize`, `start` and `stop` are optional; `iterate` is required. `stop` must be correct after
`initialize` without `start`, because a pipeline whose start cascade fails rolls back by stopping
everything it had already initialised.

## The config, next to the properties

A property is one scalar with a default, edited live. A **config** is the other channel: a structure
— a list, a table, a nested object — handed to the instance at creation and never edited afterwards.
`scale.lua` declares both, `factor` as a property and a `bands` list as config:

```json
{
    "module": "lua_scale",
    "name": "scale",
    "properties": { "factor": 5 },
    "config": {
        "bands": [ { "upto": 20, "name": "small" }, { "upto": 60, "name": "medium" } ],
        "otherwise": "large"
    }
}
```

It arrives as an ordinary Lua table in `self.config`, readable from `initialize` onward — Lua has no
constructor, so there is nothing earlier to read it in. It is `nil` when the module's node named no
config, which is why every read in `scale.lua` has a fallback: a module that fell over on an absent
config would be one nobody could place, since placing it is how one would give it a config.

One caveat that is this bridge's alone: **an array keeps its order, the keys of a table do not.** The
host's own value and a Python dict preserve the order the entries were given in; a Lua table cannot,
so nothing in a Lua module may depend on the order an object was written in.

## Three things this bridge does differently

**Every instance gets its own interpreter.** Lua has no global interpreter and no lock around one, so
two instances of a module iterate on two threads at once with nothing to serialise against. The price
is that the file's top level runs once per instance: put expensive work in `initialize`, not beside
the declarations. Values at the top level are **not** shared between instances — if two modules must
share state, connect a port.

**A module may not be called `atp`.** The package is `atp.lua` in the same directory, and a script of
that name would be the same file. The bridge skips `atp.lua` when it walks a directory.

**The script is read twice**: once when the plugin loads, to describe the module, and again when an
instance is created. A file edited in between is refused with "the script declares different ports
than when it was loaded" rather than producing ports the host never connected.

## Where scripts are looked for

`lua/` next to the bridge, plus every directory in `ATP_LUA_PATH` (separated by `;` on Windows, `:`
elsewhere). A directory named twice is walked once. A script that does not load is skipped with a
message on the error stream, and its neighbours load normally.
