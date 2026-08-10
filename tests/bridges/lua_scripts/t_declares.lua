local atp = require("atp")

local M = atp.module("lua_declares", { 2, 1 })

M.value = atp.input(atp.i32)
M.queued = atp.input(atp.text, { queue = true, capacity = 4, overflow = atp.DROP_INCOMING })
M.flag = atp.input(atp.bool)

M.result = atp.output(atp.i32)
M.label = atp.output(atp.text)

M.factor = atp.property(atp.i32, 2)
M.channels = atp.property(atp.i32, 2, { options = { 1, 2, 6 } })
M.transient = atp.property(atp.text, "x", { persistent = false })

function M:iterate()
    return atp.IDLE
end

return M
