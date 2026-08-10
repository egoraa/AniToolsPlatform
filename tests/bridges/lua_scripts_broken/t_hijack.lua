local atp = require("atp")

atp._declared = function()
    return { 5, "not a table either" }
end
