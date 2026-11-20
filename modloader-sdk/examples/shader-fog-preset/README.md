# Stock fog preset status

Run `python3 tools/lce_modpack.py build examples/shader-fog-preset build/pkg`
from `modloader-sdk`. Install the example's `mod.lcm` and `config` together in
`/data/lce-mods/shader_fog_preset/`, enable the mod and restart. Its Lua script
logs the resolved preset status once during registration.

`lce.shader_preset_info([name])` uses `fog_color_v1` when the name is omitted
or nil. It reads `lce_visual.fog_color_v1` from the caller's own package config
in any script phase. The quoted color uses `#RRGGBBWW`. Each component has
two hex digits and `WW` is a blend weight from `00` through `60`.

| `status` | Meaning |
|---|---|
| `valid` | The package provides a valid color and a positive weight. |
| `disabled` | The package provides a valid color with weight `00`. |
| `invalid` | The parsed field has the wrong type, format or weight. |
| `unconfigured` | The config store has no entry for this key. |
| `config_unavailable` | The config API could not read the key. |
| `unsupported` | The requested name is outside the recognized preset list. |

The result contains `name`, `status`, `valid`, `available` and `active`.
The recognized preset also reports `config_key` and `max_weight`. For the
`valid` and `disabled` statuses, `valid` is true and the result includes
`red`, `green`, `blue` and `weight` as integers. The result is a fresh
Lua table on each call.

GPU rendering is disabled pending console validation. Both `available`
and `active` are false in this build, including for the valid sample.
