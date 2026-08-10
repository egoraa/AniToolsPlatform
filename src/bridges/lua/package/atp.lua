--- Author-facing API of the AniToolsPlatform Lua bridge.
---
--- Declarations are assigned to the table atp.module returns, and they are collected **in assignment
--- order**. That ordering is the one thing this file exists to guarantee: the C ABI addresses a port
--- by its index, so the order a script wrote its ports in has to survive all the way to the host. A
--- plain table would not do it — pairs() has no order at all — hence the proxy with __newindex below.
local atp = {}

atp.i32, atp.i64, atp.f64, atp.bool, atp.text, atp.blob = 1, 2, 3, 4, 5, 6

atp.BUSY, atp.IDLE = 0, 1
atp.ERROR, atp.WARNING, atp.INFO, atp.DEBUG = 0, 1, 2, 3
atp.STATE, atp.QUEUE = 0, 1
atp.DROP_OLDEST, atp.DROP_INCOMING = 0, 1

local kind_name = { [1] = "i32", [2] = "i64", [3] = "f64", [4] = "bool", [5] = "text", [6] = "blob" }

local declared = {}

-- The platform's own string form for a property value: the same spelling its config parses, so a
-- malformed default is a load-time error rather than a surprise much later.
local function canonical(kind, value)
    if kind == atp.bool then
        return value and "true" or "false"
    elseif kind == atp.f64 then
        return string.format("%.17g", value)
    elseif kind == atp.i32 or kind == atp.i64 then
        return string.format("%d", value)
    elseif kind == atp.text then
        return tostring(value)
    end
    error("a blob property is not allowed")
end

local declaration = {}
declaration.__index = declaration

local function declare(what, kind, fields)
    if kind_name[kind] == nil then
        error("unknown port type " .. tostring(kind))
    end
    local d = setmetatable(fields, declaration)
    d.what, d.kind = what, kind
    return d
end

--- An input port.
---
--- A queueing input keeps values until they are taken. Capacity 0 asks for the platform's own default
--- limit rather than for no limit — an unbounded queue between threads of different pacing is how a
--- pipeline runs out of memory with no error along the way — and then the overflow policy is unread.
function atp.input(kind, options)
    options = options or {}
    return declare("inputs", kind, {
        flavor = options.queue and atp.QUEUE or atp.STATE,
        capacity = options.capacity or 0,
        overflow = options.overflow or atp.DROP_OLDEST,
    })
end

--- An output port.
function atp.output(kind)
    return declare("outputs", kind, {})
end

--- A setting with a default, edited live and read pull-only.
---
--- A non-empty options set makes an enumeration: every write is checked against it, the default
--- included, so a value outside the set cannot be reached from a config, from the CLI or from studio.
function atp.property(kind, default, options)
    options = options or {}
    if kind == atp.blob then
        error("a blob property is not allowed")
    end
    local allowed = {}
    for i, value in ipairs(options.options or {}) do
        allowed[i] = canonical(kind, value)
    end
    return declare("properties", kind, {
        default = canonical(kind, default),
        options = allowed,
        persistent = options.persistent ~= false,
    })
end

local base = {}

--- Writes one line to the platform's log.
function base:log(message, level)
    self._ctx:log(level or atp.INFO, tostring(message))
end

--- Asks this module's thread to iterate now. Callable from any thread.
function base:wake()
    self._ctx:wake()
end

--- Whether the pipeline is stopping.
function base:stop_requested()
    return self._ctx:stop_requested()
end

--- Declares a module and returns the table to describe it in.
---
--- Ports and properties are assigned to that table; everything else assigned to it is a method the
--- instance will have. Override iterate; initialize, start and stop are optional, and stop must be
--- correct after initialize without start — a pipeline whose start cascade fails rolls back by
--- stopping everything it had already initialised.
---
--- Expensive work belongs in initialize rather than at the top level of the script. Every instance of
--- this module gets its own interpreter and runs the file again, which is what lets two of them
--- iterate on two threads at once; the top level is therefore paid once per instance, not once.
function atp.module(name, version)
    if type(name) ~= "string" or name == "" then
        error("a module needs a non-empty name")
    end
    version = version or { 1, 0 }
    if #version < 1 or #version > 4 then
        error(name .. ": version takes one to four numbers")
    end
    local own = {
        name = name,
        version = version,
        inputs = {},
        outputs = {},
        properties = {},
        methods = setmetatable({}, { __index = base }),
    }
    declared[#declared + 1] = own
    return setmetatable({}, {
        __newindex = function(_, key, value)
            if getmetatable(value) == declaration then
                value.name = key
                local list = own[value.what]
                list[#list + 1] = value
            else
                own.methods[key] = value
            end
        end,
        __index = function(_, key)
            return own.methods[key]
        end,
    })
end

local function rows_of(list, render)
    local rows = {}
    for i, d in ipairs(list) do
        rows[i] = render(d)
    end
    return rows
end

-- Everything this state declared, in the shape the bridge reads it. Called by the bridge alone.
function atp._declared()
    local table_out = {}
    for i, own in ipairs(declared) do
        if own.methods.iterate == nil then
            error(own.name .. ": a module needs an iterate method")
        end
        table_out[i] = {
            name = own.name,
            version = own.version,
            inputs = rows_of(own.inputs, function(d)
                return { d.name, d.kind, d.flavor, d.capacity, d.overflow }
            end),
            outputs = rows_of(own.outputs, function(d)
                return { d.name, d.kind }
            end),
            properties = rows_of(own.properties, function(d)
                return { d.name, d.kind, d.default, d.options, d.persistent }
            end),
        }
    end
    return table_out
end

local bound_input = {}
bound_input.__index = bound_input

--- Value the input holds, or nil when it holds none.
function bound_input:get()
    return self._ctx:get(self._index)
end

--- Next value, removed from the input, or nil when there is none.
function bound_input:take()
    return self._ctx:take(self._index)
end

local bound_output = {}
bound_output.__index = bound_output

--- Delivers a value to every connected input.
function bound_output:write(value)
    self._ctx:write(self._index, value)
end

local bound_property = {}
bound_property.__index = bound_property

--- Current value.
function bound_property:get()
    return self._ctx:prop_get(self._index)
end

--- Value if it was written since the last take, otherwise nil.
function bound_property:take()
    return self._ctx:prop_take(self._index)
end

--- Edits the setting from inside the module.
function bound_property:set(value)
    self._ctx:prop_set(self._index, value)
end

local function bind(instance, list, ctx, meta)
    for i, d in ipairs(list) do
        instance[d.name] = setmetatable({ _ctx = ctx, _index = i - 1 }, meta)
    end
end

-- Builds the instance the host asked for, refusing a script that changed since it was described.
--
-- The shape check is what a per-instance state costs: the script is executed again here, so a file
-- edited between the load and the first create would produce ports the host never connected. The
-- Python bridge cannot drift this way — it keeps the class object — and the difference is worth
-- naming rather than discovering as a crash in the io layer.
function atp._instantiate(name, ctx, n_inputs, n_outputs, n_properties, config)
    for _, own in ipairs(declared) do
        if own.name == name then
            if #own.inputs ~= n_inputs or #own.outputs ~= n_outputs or #own.properties ~= n_properties then
                error(name .. ": the script declares different ports than when it was loaded")
            end
            local instance = setmetatable({ _ctx = ctx, config = config }, { __index = own.methods })
            bind(instance, own.inputs, ctx, bound_input)
            bind(instance, own.outputs, ctx, bound_output)
            bind(instance, own.properties, ctx, bound_property)
            return instance
        end
    end
    error("no module named " .. tostring(name) .. " in this script")
end

return atp
