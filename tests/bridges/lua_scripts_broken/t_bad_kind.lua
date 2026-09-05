local atp = require("atp")

atp._declared = function()
    return {
        {
            name = "lua_bad_kind",
            version = { 1, 0 },
            inputs = { { "value", 99, 0, 0, 0 } },
            outputs = {},
            properties = {},
        },
    }
end
