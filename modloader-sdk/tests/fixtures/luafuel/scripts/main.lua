local ok, status = lce.register_fuel{item = "luafuel:ember", burn_ticks = 1234}
assert(ok, "register_fuel failed: " .. status)
local item_ok, item_status = lce.register_item{
    key = "luafuel:inline", name = "Inline Lua Fuel", texture = "luafuel:icon", fuel_ticks = 2345
}
assert(item_ok, "register_item failed: " .. item_status)

assert(lce.fuel_ticks("luafuel:ember") == nil)
assert(lce.fuel_ticks("luafuel:inline") == nil)

lce.subscribe("WorldLoad", function()
    assert(lce.fuel_ticks("luafuel:ember") == 1234)
    assert(lce.fuel_ticks("luafuel:inline") == 2345)
    assert(lce.fuel_ticks("fueltest:gem") == 1600)
    assert(lce.fuel_ticks("fueltest:ember") == 800)
    assert(lce.fuel_ticks("minecraft:coal") == nil)
    assert(lce.fuel_ticks("missing:ember") == nil)
    lce.log("Lua fuel duration read from the committed table")
end)
