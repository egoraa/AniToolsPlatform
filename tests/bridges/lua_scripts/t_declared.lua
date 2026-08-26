--- Declares its config and reports what arrived, so the declaration channel is exercised end to end.
---
--- Every key below is read without a fallback on purpose: that is the whole point of declaring them.
--- Only `rate` appears in the document the test writes, so the rest arriving at all is what proves the
--- host filled the declaration before the instance was built.

local atp = require("atp")

local M = atp.module("lua_declared", { 1, 0 })

M.out_report = atp.output(atp.text)

M.config = atp.config(function(c)
    c.rate = atp.field(atp.i64)
    c.engine = atp.field(atp.text, "fm", { options = { "fm", "additive" } })
    c.master = atp.group(function(g)
        g.note = atp.field(atp.i64, 60)
    end)
    c.voices = atp.list(function(v)
        v.note = atp.field(atp.i64, 60)
    end)
    c.taps = atp.list(atp.f64)
end)

function M:initialize()
    local config = self.config
    self.report = string.format("rate=%s engine=%s note=%s voices=%d taps=%d", tostring(config.rate),
        tostring(config.engine), tostring(config.master.note), #config.voices, #config.taps)
end

function M:iterate()
    if self.report == nil then
        return atp.IDLE
    end
    self.out_report:write(self.report)
    self.report = nil
    return atp.BUSY
end

return M
