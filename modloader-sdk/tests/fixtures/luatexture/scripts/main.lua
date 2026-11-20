local function make_pixels(pixel)
    local rgba = {}
    for y = 0, 15 do
        for x = 0, 15 do rgba[#rgba + 1] = string.char(pixel(x, y)) end
    end
    return table.concat(rgba)
end

local item_rgba = make_pixels(function(x, y) return x * 17, y * 17, 34, 255 end)
local terrain_rgba = make_pixels(function(x, y) return 240, x * 17, y * 17, 191 end)
assert(lce.register_texture{key = "luatexture:item_icon", atlas = "items", rgba = item_rgba})
assert(lce.register_texture{key = "luatexture:block_face", atlas = "terrain", rgba = terrain_rgba})

local ok, status = lce.register_texture{key = "luatexture:item_icon", atlas = "terrain", rgba = terrain_rgba}
assert(not ok and status == -2)
ok, status = lce.register_texture{key = "foreign:icon", atlas = "items", rgba = item_rgba}
assert(not ok and status == -1)

assert(lce.register_item{key = "luatexture:gem", name = "Lua Gem", texture = "luatexture:item_icon"})
assert(lce.register_block{key = "luatexture:ore", name = "Lua Ore", texture = "luatexture:block_face"})

lce.subscribe("ServerTick", function()
    local applied, code = lce.register_texture{key = "luatexture:late_icon", atlas = "items", rgba = item_rgba}
    if not applied and code == -3 then lce.log("texture registration refused during tick") end
end)
