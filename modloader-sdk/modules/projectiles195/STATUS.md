# projectiles195: status, evidence and integration contract

Thrown items (snowball-class projectiles) for CUSA00265 1.95. Every address is an RVA in `legacy-195-analysis.elf`
(SHA-256 `c675e6ce…029f`). **Nothing here has run on the console.** API 1.9 header and loader landed; the composite/build/package
diff is staged with the loader owner (2026-11-19, §3).

```
./test.sh   # fake-image unit test (ASan/UBSan, 63 checks), image pins (34), PS4 compile (imports memchr/memcpy/memset/snprintf)
```

## 1. Mechanism

| Step | What | RVAs | Evidence |
|---|---|---|---|
| Item | The SDK generic item (Item ctor `0x354d80`, private Item vtable copy with `+0x108` name) is adopted: its vptr is CASed to a copy of **SnowballItem**'s vtable `0x12cae00` with `+0x108` kept and `+0x40` = `p195_item_use` | SnowballItem = Item + vtable: the 57 slots differ only at `+0x10` (class id `0x3c0788a3`), `+0x18/+0x20` (destructors, which reset the Item vptr `0x126e638` and have no extra fields) and `+0x40` use `0x637970` | Static, pinned |
| Throw | `p195_item_use` sets a pending kind for the calling thread (CAS; a second concurrent thrower gets a plain snowball) and runs the stock `SnowballItem::use 0x637970` | use: `getItemInHand +0xae0`, shrink `0x3557d0`, sound, `isClientSide [level+0x248]`, `Snowball(level, shared_ptr<LivingEntity>) 0x6371a0` at `0x637b46`, `shoot` via vt `+0x860` at `0x637c3a` (rdi = projectile, rsi = shooter shared_ptr*, xmm0 xRot `[p+0x1a4]`, xmm1 yRot `[p+0x1a0]`, xmm2 0, xmm3 1.5, xmm4 1.0), `addFreshEntity` level vt `+0x1d8` | Static, pinned |
| Shoot | The one RELRO word `0x12ca530+0x860` (Snowball primary vtable, segment at `0x1200000`, outside segment 0) points at `p195_shoot_hook` (`patch_ro8`, the biomes195 precedent). Only while a kind is pending **on the same thread** and the object still has the stock Snowball vptr, the hook CASes the vptr to the kind's private copy (RTTI, offset-to-top 0 and all 273 slots up to `+0x880` kept; the next word is the secondary table's offset-to-top `-0x440`, the `+0x440` secondary vptr is untouched) and shoots with the kind's speed/inaccuracy. Every other Snowball (players' snowballs, dispensers) passes through unchanged | shoot `0x6e15a0` | Static, pinned; host |
| Hit | The kind vtable's `+0x880` is `p195_on_hit(this, HitResult*)`: on the server copy (`[[this+0x150]+0x248] == 0`) it reports kind, projectile, hit entity (`HitResult+0x20`, NULL for a block) and position (`this+0x170`) to the hit callback, then returns the stock `onHit 0x637490` (blaze 3 damage, 0-damage knockback hurt through `DamageSource::thrown 0x1f26e0` + vt `+0x248`, particles, removal) | onHit prologue `0x6374ab/0x6374ae`, `0x6374c5`, `0x6374d0/0x6374d7`, `0x637823` | Static, pinned; host |

## 2. Limits

- The projectile renders as a snowball: the 1.95 renderer is per entity type and has no item-sprite entity data (no ThrowableItemProjectile class).
- A projectile saved in flight reloads as a plain snowball. Client copies (remote PS4s, the host's client level) are plain snowballs.
- Damage attribution: the hit callback gets the raw entity; the composite is expected to use the existing services (`damage_entity`, `set_fire`, `explode`, `add_effect`) without an attacker (the owner is a shared_ptr behind `getOwner +0x878`).
- Gravity, size and the hit particle are the snowball's.
- `p195_adopt_item` refuses an item whose `+0x40` was already routed (adopt before the events195 ITEM_USE route).

## 3. Integration (API 1.9)

- **Header (landed):** `LCE_ITEM_FLAG_THROWABLE`, `projectile_*` fields of `lce_item_desc`, `LCE_EV_PROJECTILE_HIT` (payload `lce_ev_entity`: entity = hit
  entity or NULL, source = projectile, type_id = item id, amount_milli = damage, level, x/y/z = impact block), `LCE_CAP2_PROJECTILES` (bit 26).
  The loader validates the ranges and refuses THROWABLE items unless the backend sets the bit.
- **Item class (items195, live):** the composite calls `lce195x_set_throw_use(p195_item_use)`. A THROWABLE generic item is then constructed as a
  SnowballItem: Item ctor, private copy of the SnowballItem vtable with `+0x40` = `p195_item_use` (`use_base`). The events195 ITEM_USE route
  goes over it (`lce195_ev_allow_item_use_original`), so `ItemUse` still fires and the route's original is `p195_item_use`.
  `p195_bind_item` (no write) records the object after checking RTTI, class id and destructors. The older `p195_adopt_item` (vptr CAS to a
  module copy) stays for standalone use but is not used by the composite: it would break items195 identity.
- **Composite (staged diff):**
  - `check_item`: generic only, speed 0.1..4, inaccuracy 0..20.
  - `prepare`: `p195_configure` with the throwable kinds.
  - `after_items`: `p195_bind_item` for each kind, then `p195_install`, logged as `LCE_SDK phase=projectiles rc=…`.
  - Hit callback: reads the projectile's level, then `lce195_ev_level_call`, a level-only frame. It only works inside the server tick scope,
    the same context as ENTITY_TICK. Inside the frame it applies `damage_entity` (no attacker), `set_fire`, `add_effect` (with chance) and
    `explode` (fire if `fire_ticks`, blocks destroyed), then dispatches `LCE_EV_PROJECTILE_HIT` through `lce195c_event_sink`.
- **Console binding** (`sdk_boot.cpp`): natives from `p195_natives_bind`; memory `lce_ps4_mem_read` / `cas8` / `patch_ro8` / thread id.
  Bound only when events are ready.
- **Package:** `LCE_SEC_ITEM3` (20) `lce_pkg_item3` parallel to ITEM, written only when an item is throwable. `LCE_PKG_MAX_SECTIONS` is
  raised to 20.
- **Packer:** item `"throwable"`. **Lua:** `ProjectileHit`. **Porter:** `describe.throwable_info`. **Fixture:** `tests/fixtures/throwables`.
