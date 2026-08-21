local atp = require("atp")

local M = atp.module("lua_config_text", { 1, 0 })

M.out_report = atp.output(atp.text)

function M:initialize()
    local rate = "none"
    if self.config ~= nil and self.config.audio ~= nil and self.config.audio.rate ~= nil then
        rate = tostring(self.config.audio.rate)
    end
    local origin = "none"
    if self.config_origin ~= nil and self.config_origin ~= "" then
        origin = self.config_origin
    end
    self.report = string.format("text-len=%d origin=%s opaque=%s rate=%s", #self.config_text, origin,
        tostring(self.config_opaque), rate)
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
