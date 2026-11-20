assert(lce.register_structure{
  key = "luaworldgen:shrine", size_x = 2, size_y = 1, size_z = 3,
  palette = {"minecraft:bedrock", "minecraft:stone", "minecraft:dirt"},
  blocks = {0, 2, 1, 0, 1, 2},
})
assert(lce.register_biome{
  key = "luaworldgen:amber_grove", name = "Amber Grove", temperature_milli = 800,
  rainfall_milli = 400, weight = 20, features = {"luaworldgen:shrine_gen"},
})
assert(lce.register_feature{
  key = "luaworldgen:shrine_gen", kind = 5, dimension = 0,
  structure = "luaworldgen:shrine", per_chunk = 1, min_y = 64, max_y = 80,
  biomes = {"luaworldgen:amber_grove", "minecraft:plains"},
})
