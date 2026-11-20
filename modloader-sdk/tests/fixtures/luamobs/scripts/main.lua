local key = "luamobs:grove_zombie"
local function describe_mob()
    return {
        key = key,
        base_mob = "minecraft:zombie",
        max_health_milli = 30000,
        move_speed_milli = 280,
        attack_damage_milli = 4000,
        category = 1,
        spawn_weight = 10,
        min_group = 1,
        max_group = 2,
        spawn_biomes = {"minecraft:plains"},
        goals = {
            {kind = 1, priority = 3, speed_milli = 1200},
            {kind = 10, priority = 4, speed_milli = 1000, param = "minecraft:player"}
        },
        sound_ambient = "minecraft:entity.zombie.ambient",
        spawn_egg = true
    }
end

local mob = describe_mob()
assert(lce.register_entity(mob))
mob.goals[2].param = nil
mob.spawn_biomes[1] = nil
local ok, status = lce.register_entity(describe_mob())
assert(not ok and status == -2)

lce.subscribe("ServerTick", function()
    local accepted, error_code = lce.register_entity{key = "luamobs:late", base_mob = "minecraft:zombie"}
    assert(not accepted and error_code == -3)
    lce.log("late mob registration refused")
end)
