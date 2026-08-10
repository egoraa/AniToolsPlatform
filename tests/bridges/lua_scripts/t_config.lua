local atp = require("atp")

local M = atp.module("lua_config", { 1, 0 })

M.out_report = atp.output(atp.text)

function M:initialize()
    if self.config == nil then
        self.report = "config=nil"
        return
    end
    local keys = {}
    for key in pairs(self.config) do
        keys[#keys + 1] = key
    end
    table.sort(keys)
    self.report = string.format("channels=%d name=%s nested=%s keys=%s", self.config.channels[3], self.config.name,
        tostring(self.config.nested.deep), table.concat(keys, ","))
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
