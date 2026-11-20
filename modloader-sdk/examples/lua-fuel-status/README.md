# Lua Fuel Status

This example makes ordinary Paper burn for 600 ticks in a furnace. When a world opens, the script reads that duration from the committed SDK fuel table and writes it to the plugin log.

Build from the SDK directory.

```sh
python3 tools/lce_modpack.py build --profile retail195-experimental-fuel examples/lua-fuel-status build/pkg
```

On an offline retail 1.95 build with experimental fuel and `lce.fuel_ticks` available, install `build/pkg/luafuelstatus/mod.lcm` in `/data/lce-mods/luafuelstatus/`, enable it in the Mods list for the next launch, and reopen the game. Use one mod that registers Paper as fuel for the test. After opening a world, look for `minecraft:paper burns for 600 ticks; stock Coal entry = nil` in the plugin log.

The query returns a duration for a fuel entry published by the SDK. Coal’s built-in duration is outside that table and returns `nil`.
