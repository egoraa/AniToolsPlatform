local atp = require("atp")

local M = atp.module("lua_order", { 1, 0 })

M.first = atp.input(atp.i32)
M.second = atp.input(atp.i32)
M.third = atp.input(atp.i32)

M.mark = atp.output(atp.i32)

function M:iterate()
    local seen = 0
    if self.first:take() ~= nil then
        seen = seen + 1
    end
    if self.second:take() ~= nil then
        seen = seen + 2
    end
    if self.third:take() ~= nil then
        seen = seen + 4
    end
    if seen == 0 then
        return atp.IDLE
    end
    self.mark:write(seen)
    return atp.BUSY
end

return M
