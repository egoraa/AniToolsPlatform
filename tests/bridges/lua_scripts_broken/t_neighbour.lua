local atp = require("atp")

local M = atp.module("lua_neighbour", { 1, 0 })

function M:iterate()
    return atp.IDLE
end

return M
