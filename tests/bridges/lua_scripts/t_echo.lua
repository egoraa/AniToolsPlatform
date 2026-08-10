local atp = require("atp")

local M = atp.module("lua_echo", { 1, 0 })

M.in_i32 = atp.input(atp.i32)
M.in_i64 = atp.input(atp.i64)
M.in_f64 = atp.input(atp.f64)
M.in_bool = atp.input(atp.bool)
M.in_text = atp.input(atp.text)
M.in_blob = atp.input(atp.blob)

M.out_i32 = atp.output(atp.i32)
M.out_i64 = atp.output(atp.i64)
M.out_f64 = atp.output(atp.f64)
M.out_bool = atp.output(atp.bool)
M.out_text = atp.output(atp.text)
M.out_blob = atp.output(atp.blob)
M.out_gain = atp.output(atp.f64)

M.gain = atp.property(atp.f64, 1.0)
M.overflow = atp.property(atp.bool, false)

function M:iterate()
    local n = self.in_i32:take()
    if n == nil then
        return atp.IDLE
    end
    if self.overflow:get() then
        self.out_i32:write(2147483648)
        return atp.BUSY
    end
    self.out_i32:write(n)
    self.out_gain:write(n * self.gain:get())

    local wide = self.in_i64:take()
    if wide ~= nil then
        self.out_i64:write(wide)
    end
    local number = self.in_f64:take()
    if number ~= nil then
        self.out_f64:write(number)
    end
    local flag = self.in_bool:take()
    if flag ~= nil then
        self.out_bool:write(not flag)
    end
    local text = self.in_text:take()
    if text ~= nil then
        self.out_text:write(text .. "!")
    end
    local bytes = self.in_blob:take()
    if bytes ~= nil then
        self.out_blob:write(string.reverse(bytes))
    end
    return atp.BUSY
end

return M
