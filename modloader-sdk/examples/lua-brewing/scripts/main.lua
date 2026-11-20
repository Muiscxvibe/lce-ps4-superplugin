local first = {
    key = "luabrewing:brew_haste",
    input = "minecraft:awkward",
    reagent = "minecraft:glowstone_dust",
    output = "luabrewing:haste",
}
local second = {
    key = "luabrewing:brew_greater_haste",
    input = first.output,
    reagent = "minecraft:stick",
    output = "luabrewing:haste_plus",
}
assert(lce.register_brewing(first))
assert(lce.register_brewing(second))

local reported = false
lce.subscribe("ServerTick", function()
    if reported then return end
    local intermediate = lce.brewing_result(first.input, first.reagent, "splash")
    if not intermediate then return end
    local output, form = lce.brewing_result(intermediate, second.reagent, "splash")
    if output then
        reported = true
        lce.log("Lua brewing chain: " .. output .. " (" .. form .. ")")
    end
end)
