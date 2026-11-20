local ok, status = lce.register_fuel{item = "luafuel:ember", burn_ticks = 1234}
assert(ok, "register_fuel failed: " .. status)
local item_ok, item_status = lce.register_item{
    key = "luafuel:inline", name = "Inline Lua Fuel", texture = "luafuel:icon", fuel_ticks = 2345
}
assert(item_ok, "register_item failed: " .. item_status)
