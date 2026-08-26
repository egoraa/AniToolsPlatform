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

--- What this module accepts under "config", declared once and checked by the host.
---
--- The bands are the kind of setting this channel exists for: a list of pairs no property could hold,
--- fixed before the module is connected rather than turned while it runs. `factor` stays a property for
--- exactly the opposite reasons.
---
--- Declaring buys three things at once: atp_studio edits the config as typed rows instead of raw JSON, a
--- document that does not fit is refused before the pipeline starts — naming the file and the field —
--- and every declared key arrives at its own default, so the reads below need no fallbacks.
---
--- **A group and a list of objects take a function, never a table.** `pairs` has no order and the order
--- of a config's fields is a contract — it is the order atp_studio draws the rows in — so a nested
--- object is collected by a proxy exactly as the declarations above are. A table literal would lose it.
--- The same caveat is why nothing in a Lua module may depend on the order an object was written in.
---
--- Declaring is optional. A module whose config is a format the platform does not parse declares
--- nothing, reads self.config_text itself, and checks self.config_opaque to know that is all there is.
M.config = atp.config(function(c)
    c.bands = atp.list(function(band)
        band.upto = atp.field(atp.i32)
        band.name = atp.field(atp.text)
    end)
    c.otherwise = atp.field(atp.text, "large")
end)

--- Reads the config, an ordinary Lua table this instance was handed at creation. Every key declared
--- above is already in it, at its own default when the document said nothing about it.
function M:initialize()
    self.bands = self.config.bands
    self.otherwise = self.config.otherwise
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
