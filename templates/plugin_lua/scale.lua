--- A platform module written in Lua.
---
--- The bridge reads this file when it loads and builds real platform ports from the declarations
--- below, so nothing here is compiled and nothing links the SDK.
---
--- Expensive work belongs in initialize rather than at the top level: every instance of this module
--- gets an interpreter of its own and runs the file again, which is what lets two of them iterate on
--- two threads at once.
local atp = require("atp")

local M = atp.module("lua_scale", { 1, 0 })

M.value = atp.input(atp.i32)

M.result = atp.output(atp.i32)
M.label = atp.output(atp.text)

M.factor = atp.property(atp.i32, 3)

--- Raises from inside iterate when set, which is how the error path is demonstrated and tested:
--- `atp_app -p scale.fail=true config/pipeline_lua.json` stops the pipeline with this file and line
--- in the message.
M.fail = atp.property(atp.bool, false)

--- Reads the config, an ordinary Lua table this instance was handed at creation — `nil` when the
--- module's node named none, which is why every read below has a fallback.
---
--- The bands are the kind of setting this channel exists for: a list of pairs no property could
--- hold, fixed before the module is connected rather than turned while it runs. `factor` stays a
--- property for exactly the opposite reasons. Note that an array keeps its order here while the keys
--- of a table do not, so nothing in a Lua module may depend on the order an object was written in.
function M:initialize()
    local config = self.config or {}
    self.bands = config.bands or {}
    self.otherwise = config.otherwise or "large"
    self:log("lua_scale ready with " .. #self.bands .. " bands")
end

function M:band_of(value)
    for _, band in ipairs(self.bands) do
        if value <= band.upto then
            return band.name
        end
    end
    return self.otherwise
end

function M:iterate()
    local v = self.value:take()
    if v == nil then
        return atp.IDLE
    end
    if self.fail:get() then
        error("the fail property was set")
    end
    local scaled = v * self.factor:get()
    self.result:write(scaled)
    self.label:write(string.format("%d x %d = %d (%s)", v, self.factor:get(), scaled, self:band_of(scaled)))
    return atp.BUSY
end

return M
