# script (Lua 5.4.8 sandbox) status

A sandboxed Lua behaviour layer for LCE mods. A script ships inside the mod
package as `scripts/main.lua` in section **`LCE_SEC_SCRIPT = 14`** (the lead has
added `lce_pkg_script {name, data_offset, data_size}` with sources in DATA, and
`lce_loader_scripts()` / `lce_loader_mod_ctx()` expose them). The VM is bound to
one mod's `lce_api` + `lce_mod_ctx`, so `register_*`/`subscribe`/services calls
map one-to-one onto the SDK (`compatibility/ruby` reconstruction of the Workshop
API). Lua source: `lua/` = verbatim Lua 5.4.8 from lua.org
(`4f18ddae…629ae`, MIT, `lua/LICENSE.html`), minus the standalone `lua.c`/`luac.c`.

Evidence: **host** = `tests/test_script.cpp` (49 checks) runs real Lua.

## Sandbox (host-tested, incl. escape attempts)

| Control | Mechanism | Status |
|---|---|---|
| No `io` / `os` / `debug` / `package` / `require` / `dofile` / `loadfile` | only `base`, `table`, `string`, `math`, `utf8`, `coroutine` opened; dangerous base globals stripped | host-tested (9 escape attempts each fail) |
| No bytecode loading | `load` replaced with a text-only (`mode="t"`) version; `loadstring` removed | host-tested (`load('\27Lua')` → nil) |
| Memory cap | counting allocator refuses any allocation over the cap (default 16 MiB) → `LUA_ERRMEM`, VM survives for inspection | host-tested (2 MiB cap holds) |
| Instruction budget, **not bypassable by pcall** | a `LUA_MASKCOUNT` hook accumulates steps; when the budget trips it sets `dead`, re-arms a **1-instruction** hook that raises on every subsequent instruction, and marks the script dead so later `run`/`event` entries refuse immediately | host-tested (a `while true do pcall(function() while true do end end) end` loop terminates and the script is dead) |
| `print` routed to the mod log | `b_log` → `api->log` / `on_log` | host-tested |

Once `dead`, the VM stops forever: a pcall that swallows the budget error cannot
make forward progress (every instruction re-raises), and `lce_script_run_main` /
`lce_script_event` short-circuit.

## Bindings → `lce_api`

| Lua | Maps to |
|---|---|
| `lce.register_item{key,name,texture,max_stack,kind,tier,food_hunger,food_saturation_milli,use_ticks,always_edible,fuel_ticks}` | `register_item`; food fields map to the ordinary item descriptor and default to zero/false; `tier` defaults to 0, accepts numeric `lce_tool_tier` values 0..5 (tools require 1..5), `fuel_ticks` defaults to 0, accepts integers 0..72000, and needs the experimental fuel backend when nonzero. |
| `lce.register_block{key,name,texture,hardness_milli,…}` | `register_block` |
| `lce.register_recipe{key,result,count,shapeless,cells,width,height}` | `register_recipe` |
| `lce.register_smelting{key,input,result,count,xp_milli}` | `register_smelting` |
| `lce.register_fuel{item,burn_ticks}` | `register_fuel`; item is a namespaced key, burn time is an integer from 1 to 72000 ticks. Returns success and status; backend fuel support remains experimental. |
| `lce.register_feature{key,kind,dimension,block,block2,structure,size,per_chunk,min_y,max_y,rarity,biomes}` | `register_feature`; kind 1 = ore, 2 = tree, 4 = lake, 5 = structure. Biomes is an optional list of biome keys. Returns success and status. |
| `lce.subscribe(name, fn)` | `subscribe` (+ delivers via `lce_script_event`) |
| `lce.send_chat` / `give_item` / `translate` / `log` | the matching services |
| `lce.config(key[, default[, min, max]])` (API 1.8, `#ifdef LCE_HAVE_CONFIG_1_8`) | `config_int` / `config_double` / `config_string` chosen by the default's type (boolean = `config_int` 0..1); no default = `config_get` raw value or nil. Host: test §2f against the real `loader/config_store.h`, plus a pre-1.8 table (returns the default) |

### Workshop-compat shim (global)

`registerItem`, `registerBlock`, `registerShapedRecipe`,
`registerShapelessRecipe`, `registerSmeltingRecipe`, `registerEvent` map onto the
above (pattern/key → row-major cells; `registerEvent` → `subscribe`).
`registerItemTier` / `registerArmorMaterial` / `registerSapling` /
`registerOreFeature` / `registerTreeFeature` are recorded as **denied** no-ops
because these Workshop-style helpers have no shim implementation, so a Workshop
script loads without crashing and the loader reports what was dropped. This does not
mean the native item or feature APIs lack those paths; `lce.register_feature` is
available separately.

Event callbacks receive a payload table (`tick`, `x/y/z/block`, `item`, `name`,
`message`, `amount`); returning a truthy value cancels a cancellable event.

## Status by layer

| Layer | Status |
|---|---|
| VM, sandbox, memory cap, non-bypassable budget | **host-tested** |
| Bindings + Workshop shim against a fake `lce_api` | **host-tested** |
| PS4 compile (Lua core + `lce_script.c`, OpenOrbis flags) | **compiles** (`ps4check.sh`; decimal point pinned to `.` so `locale.h` is not needed) |
| Composite registration and event dispatch | **implemented and host-tested**: `lce195c_script_runner` creates the VM during registration and `lce195c_event_sink` dispatches to live scripts. Console execution of the Lua fuel bindings is not established by the fuel v3 test. |

## Integration

The composite runner loads `scripts/main.lua` during registration, before
`LCE_PHASE_COMMIT`, so its registration calls can be staged. The event sink
routes events to each live script at runtime. The runner uses a separate Lua
state per mod and refuses scripts when the build does not bind scripting.
Service/event calls are only valid at `RUNNING`.
