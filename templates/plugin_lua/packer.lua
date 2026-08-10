--- A module that turns a number into bytes, which is what a blob port is for.
---
--- Lua strings are byte strings, so a blob needs no encoding step and no dependency: string.pack
--- produces exactly the bytes the platform will carry as atp::io::blob.
local atp = require("atp")

local M = atp.module("lua_packer", { 1, 0 })

M.value = atp.input(atp.i32)

M.bytes = atp.output(atp.blob)

M.byte_order = atp.property(atp.text, "little", { options = { "little", "big" } })

function M:iterate()
    local v = self.value:take()
    if v == nil then
        return atp.IDLE
    end
    local format = self.byte_order:get() == "big" and ">i4" or "<i4"
    self.bytes:write(string.pack(format, v))
    return atp.BUSY
end

return M
