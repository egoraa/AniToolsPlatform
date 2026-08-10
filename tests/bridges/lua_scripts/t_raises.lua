local atp = require("atp")

local M = atp.module("lua_raises", { 1, 0 })

local function deep()
    error("deliberate")
end

function M:iterate()
    deep()
    return atp.IDLE
end

return M
