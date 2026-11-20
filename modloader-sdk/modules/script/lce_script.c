/* Sandboxed Lua 5.4.8 host for LCE mods. See lce_script.h. */
#include "lce_script.h"
#include "../../include/lce_tick_pacing.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"

/* API 1.3 events (provisional ids, same guard and order as modules/events195/ev195.h until include/lce_mod_api.h
 * defines LCE_HAVE_EV_1_3 with the real enum entries). */
#ifndef LCE_HAVE_EV_1_3
#define LCE_EV_ITEM_INVENTORY_TICK ((lce_event)(LCE_EV_ENTITY_TICK + 1))
#define LCE_EV_BLOCK_NEIGHBOR      ((lce_event)(LCE_EV_ENTITY_TICK + 2))
#define LCE_EV_BLOCK_ENTITY_INSIDE ((lce_event)(LCE_EV_ENTITY_TICK + 3))
#define LCE_EV_BLOCK_STEP_ON       ((lce_event)(LCE_EV_ENTITY_TICK + 4))
#endif

#define LCE_DEFAULT_MEM  (16u * 1024 * 1024)
#define LCE_DEFAULT_STEP 20000000ull
#define LCE_DEFAULT_HOOK 1000u
#define LCE_MAX_EVENTS   32u

struct lce_script {
    lua_State *L;
    const lce_api *api;
    lce_mod_ctx *ctx;
    lce_script_config cfg;
    size_t mem_used, mem_cap;
    uint64_t steps, budget;
    uint32_t interval;
    uint32_t tick_interval, tick_offset;
    lua_Integer tick_callbacks_run, tick_callbacks_skipped;
    int dead;                 /* 1 = budget tripped / fatal: refuse all entry */
    int entered;             /* reentrancy guard (one outermost call) */
    lce_script_stats st;
};

/* ---- counting allocator ---------------------------------------------------- */
static void *l_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    lce_script *s = (lce_script *)ud;
    if (nsize == 0) {
        if (ptr) { s->mem_used -= osize; free(ptr); }
        return NULL;
    }
    size_t cur = s->mem_used - (ptr ? osize : 0);
    if (cur + nsize > s->mem_cap) return NULL;   /* over cap: Lua raises LUA_ERRMEM */
    void *np = realloc(ptr, nsize);
    if (!np) return NULL;
    s->mem_used = cur + nsize;
    return np;
}

/* ---- non-bypassable instruction budget ------------------------------------- */
static void l_hook(lua_State *L, lua_Debug *ar) {
    (void)ar;
    lce_script *s = *(lce_script **)lua_getextraspace(L);
    if (s->dead) { luaL_error(L, "lce: script terminated (step budget exhausted)"); return; }
    uint64_t add = s->interval;
    s->steps = (s->steps + add < s->steps) ? UINT64_MAX : s->steps + add;
    if (s->steps >= s->budget) {
        s->dead = 1;
        /* Re-arm a 1-instruction hook so any further execution (even inside a
         * pcall that swallowed this error) immediately raises again. */
        lua_sethook(L, l_hook, LUA_MASKCOUNT, 1);
        luaL_error(L, "lce: script terminated (step budget exhausted)");
    }
}

static lce_script *self(lua_State *L) { return *(lce_script **)lua_getextraspace(L); }

/* ---- helpers --------------------------------------------------------------- */
static const char *opt_field(lua_State *L, int t, const char *k, const char *def) {
    lua_getfield(L, t, k);
    const char *v = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return v;
}
static lua_Integer int_field(lua_State *L, int t, const char *k, lua_Integer def) {
    lua_getfield(L, t, k);
    lua_Integer v = lua_isnumber(L, -1) ? (lua_Integer)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

/* ---- registration bindings (lce.*) ----------------------------------------- */
/* lce.register_item{key=, name=, texture=, max_stack=, kind=, tier=, food_hunger=, food_saturation_milli=, use_ticks=, always_edible=, fuel_ticks=} */
static int b_register_item(lua_State *L) {
    lce_script *s = self(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lce_item_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d;
    d.key = opt_field(L, 1, "key", NULL);
    d.name = opt_field(L, 1, "name", d.key);
    d.texture = opt_field(L, 1, "texture", NULL);
    d.creative_tab = opt_field(L, 1, "creative_tab", NULL);
    d.kind = (uint16_t)int_field(L, 1, "kind", LCE_ITEM_GENERIC);
    d.max_stack = (uint16_t)int_field(L, 1, "max_stack", 64);
    d.max_damage = (uint16_t)int_field(L, 1, "max_damage", 0);
    lua_getfield(L, 1, "tier");
    lua_Integer tier = lua_isnil(L, -1) ? LCE_TIER_NONE : luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    if (tier < LCE_TIER_NONE || tier > LCE_TIER_GOLD) return luaL_error(L, "register_item: tier must be 0..5");
    d.tier = (uint16_t)tier;
    const char *food_fields[] = {"food_hunger", "food_saturation_milli", "use_ticks"};
    lua_Integer food_values[3];
    for (int i = 0; i < 3; ++i) {
        lua_getfield(L, 1, food_fields[i]);
        food_values[i] = lua_isnil(L, -1) ? 0 : luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        if (food_values[i] < 0 || (uint64_t)food_values[i] > (i == 2 ? UINT32_MAX : UINT16_MAX))
            return luaL_error(L, "register_item: %s out of range", food_fields[i]);
    }
    d.food_hunger = (uint16_t)food_values[0];
    d.food_saturation_milli = (uint16_t)food_values[1];
    d.use_ticks = (uint32_t)food_values[2];
    lua_getfield(L, 1, "always_edible");
    if (!lua_isnil(L, -1)) luaL_checktype(L, -1, LUA_TBOOLEAN);
    if (lua_toboolean(L, -1)) d.flags |= LCE_ITEM_FLAG_FOOD_ALWAYS;
    lua_pop(L, 1);
    lua_getfield(L, 1, "fuel_ticks");
    lua_Integer fuel = lua_isnil(L, -1) ? 0 : luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    if (fuel < 0 || fuel > 72000) return luaL_error(L, "register_item: fuel_ticks must be 0..72000");
    d.fuel_ticks = (uint32_t)fuel;
    if (!d.key) return luaL_error(L, "register_item: key required");
    lce_status rc = s->api->register_item ? s->api->register_item(s->ctx, &d) : LCE_E_UNSUPPORTED;
    if (rc == LCE_OK) ++s->st.items; else ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK); lua_pushinteger(L, rc); return 2;
}
static int block_field(lua_State *L, int table, const char *name) {
    table = lua_absindex(L, table);
    lua_pushstring(L, name);
    return lua_rawget(L, table);
}

/* Borrow strings from the descriptor table until register_block copies them. */
static const char *block_text(lua_State *L, int index, const char *field) {
    if (lua_isnil(L, index)) return NULL;
    size_t size;
    luaL_checktype(L, index, LUA_TSTRING);
    const char *text = lua_tolstring(L, index, &size);
    if (memchr(text, '\0', size)) luaL_error(L, "register_block: %s contains a NUL byte", field);
    return text;
}

static const char *block_text_field(lua_State *L, int table, const char *name) {
    block_field(L, table, name);
    const char *text = block_text(L, -1, name);
    lua_pop(L, 1);
    return text;
}

static uint32_t block_int(lua_State *L, int index, const char *field, uint32_t max) {
    lua_Integer n = luaL_checkinteger(L, index);
    if (n < 0 || (uint64_t)n > max)
        luaL_error(L, "register_block: %s out of range", field);
    return (uint32_t)n;
}

static uint32_t block_int_field(lua_State *L, const char *field, uint32_t max, uint32_t fallback) {
    int type = block_field(L, 1, field);
    uint32_t n = type == LUA_TNIL ? fallback : block_int(L, -1, field, max);
    lua_pop(L, 1);
    return n;
}

static int block_bool_field(lua_State *L, const char *field) {
    if (block_field(L, 1, field) != LUA_TNIL)
        luaL_checktype(L, -1, LUA_TBOOLEAN);
    int value = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return value;
}

static size_t block_array(lua_State *L, int index, const char *field, size_t min, size_t max) {
    index = lua_absindex(L, index);
    luaL_checktype(L, index, LUA_TTABLE);
    size_t n = lua_rawlen(L, index);
    if (n < min || n > max) luaL_error(L, "register_block: %s has the wrong length", field);
    size_t entries = 0;
    lua_pushnil(L);
    while (lua_next(L, index)) {
        if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1 ||
            (uint64_t)lua_tointeger(L, -2) > n)
            luaL_error(L, "register_block: %s must be a dense array", field);
        ++entries;
        lua_pop(L, 1);
    }
    if (entries != n) luaL_error(L, "register_block: %s must be a dense array", field);
    return n;
}

static void block_faces(lua_State *L, int index, const char **faces) {
    if (lua_isnil(L, index)) return;
    if (lua_type(L, index) == LUA_TSTRING) {
        const char *texture = block_text(L, index, "texture");
        for (int f = 0; f < 6; ++f) faces[f] = texture;
        return;
    }
    luaL_checktype(L, index, LUA_TTABLE);
    static const char *const keys[] = {"down", "up", "north", "south", "west", "east"};
    const char *all = block_text_field(L, index, "all");
    const char *side = block_text_field(L, index, "side");
    for (int f = 0; f < 6; ++f) {
        faces[f] = block_text_field(L, index, keys[f]);
        if (faces[f]) continue;
        if (f < 2) faces[f] = block_text_field(L, index, f == 0 ? "bottom" : "top");
        if (!faces[f]) faces[f] = f >= 2 && side ? side : all;
    }
}

static void block_box(lua_State *L, int index, uint8_t box[6]) {
    index = lua_absindex(L, index);
    block_array(L, index, "box", 6, 6);
    for (int c = 0; c < 6; ++c) {
        lua_rawgeti(L, index, c + 1);
        box[c] = (uint8_t)block_int(L, -1, "box coordinate", 16);
        lua_pop(L, 1);
    }
    for (int c = 0; c < 3; ++c)
        if (box[c] >= box[c + 3]) luaL_error(L, "register_block: box minima must be below maxima");
}

static size_t block_box_list(lua_State *L, int index, uint8_t *boxes) {
    index = lua_absindex(L, index);
    size_t n = block_array(L, index, "boxes", 1, 8);
    for (size_t i = 0; i < n; ++i) {
        lua_rawgeti(L, index, (lua_Integer)i + 1);
        block_box(L, -1, boxes + i * 6);
        lua_pop(L, 1);
    }
    return n;
}

static void block_shape(lua_State *L, lce_block_desc *d, uint8_t *coords) {
    int box = block_field(L, 1, "box") != LUA_TNIL;
    int boxes = block_field(L, 1, "boxes") != LUA_TNIL;
    int variant_boxes = block_field(L, 1, "variant_boxes") != LUA_TNIL;
    int rotate = block_bool_field(L, "rotate_boxes");
    if (box + boxes + variant_boxes > 1)
        luaL_error(L, "register_block: use one of box, boxes or variant_boxes");
    if (rotate && (!boxes || d->variant_count != 4))
        luaL_error(L, "register_block: rotate_boxes needs boxes and four variants");
    if (box) {
        d->shape = LCE_SHAPE_BOX;
        block_box(L, -3, d->box);
    } else if (boxes) {
        d->box_count = (uint8_t)block_box_list(L, -2, coords);
        d->box_sets = rotate ? d->variant_count : 1;
        for (size_t i = d->box_count; i < (size_t)d->box_sets * d->box_count; ++i) {
            const uint8_t *prev = coords + (i - d->box_count) * 6;
            uint8_t *next = coords + i * 6;
            next[0] = (uint8_t)(16 - prev[5]); next[1] = prev[1]; next[2] = prev[0];
            next[3] = (uint8_t)(16 - prev[2]); next[4] = prev[4]; next[5] = prev[3];
        }
    } else if (variant_boxes) {
        size_t sets = block_array(L, -1, "variant_boxes", 2, 16);
        if (sets != d->variant_count)
            luaL_error(L, "register_block: variant_boxes needs one box list per variant");
        d->box_sets = (uint8_t)sets;
        for (size_t v = 0; v < sets; ++v) {
            lua_rawgeti(L, -1, (lua_Integer)v + 1);
            size_t count = block_box_list(L, -1, coords + v * d->box_count * 6);
            lua_pop(L, 1);
            if (v == 0) d->box_count = (uint8_t)count;
            if (count != d->box_count)
                luaL_error(L, "register_block: every variant needs the same box count");
        }
    }
    if (d->box_sets) {
        d->shape = LCE_SHAPE_BOXES;
        d->boxes = coords;
    }
    lua_pop(L, 3);
}

static int b_register_block(lua_State *L) {
    lce_script *s = self(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    /* Root texture and variant tables on the stack through the API call, including
     * tables pulled from weak values. The shape parser needs at most 9 scratch slots. */
    luaL_checkstack(L, 2 + 16 * 2 + 9, "register_block");
    lce_block_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d;
    d.key = block_text_field(L, 1, "key");
    if (!d.key) return luaL_error(L, "register_block: key required");
    d.name = block_text_field(L, 1, "name");
    if (!d.name) d.name = d.key;
    block_field(L, 1, "texture");
    block_faces(L, -1, d.texture);
    d.creative_tab = block_text_field(L, 1, "creative_tab");
    d.drop = block_text_field(L, 1, "drop");
    d.harvest_tool = block_text_field(L, 1, "harvest_tool");
    d.hardness_milli = block_int_field(L, "hardness_milli", UINT32_MAX, 1500);
    d.resistance_milli = block_int_field(L, "resistance_milli", UINT32_MAX, 0);
    d.material = (uint16_t)block_int_field(L, "material", LCE_MAT_COUNT - 1, LCE_MAT_STONE);
    d.sound = (uint16_t)block_int_field(L, "sound", LCE_SOUND_COUNT - 1, LCE_SOUND_STONE);
    d.light_emission = (uint8_t)block_int_field(L, "light_emission", 15, 0);
    d.harvest_tier = (uint8_t)block_int_field(L, "harvest_tier", LCE_TIER_GOLD, LCE_TIER_NONE);
    d.drop_min = (uint8_t)block_int_field(L, "drop_min", 64, 0);
    d.drop_max = (uint8_t)block_int_field(L, "drop_max", 64, 0);
    d.block_entity_slots = (uint16_t)block_int_field(L, "storage_slots", 54, 0);
    d.placement = (uint8_t)block_int_field(L, "placement", LCE_PLACE_AXIS, LCE_PLACE_FIXED);
    d.flammability = (uint8_t)block_int_field(L, "flammability", UINT8_MAX, 0);
    d.slipperiness_milli = block_int_field(L, "slipperiness_milli", 1000, 0);
    static const struct { const char *name; uint32_t flag; } flags[] = {
        {"transparent", LCE_BLOCK_FLAG_TRANSPARENT}, {"no_item", LCE_BLOCK_FLAG_NO_ITEM},
        {"replaceable", LCE_BLOCK_FLAG_REPLACEABLE}, {"no_collision", LCE_BLOCK_FLAG_NO_COLLIDE},
        {"random_tick", LCE_BLOCK_FLAG_RANDOM_TICK},
    };
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; ++i)
        if (block_bool_field(L, flags[i].name)) d.flags |= flags[i].flag;

    const char *names[16] = {NULL};
    const char *textures[16 * 6] = {NULL};
    if (block_field(L, 1, "variants") != LUA_TNIL) {
        int variants = lua_gettop(L);
        d.variant_count = (uint8_t)block_array(L, variants, "variants", 2, 16);
        for (int v = 0; v < d.variant_count; ++v) {
            lua_rawgeti(L, variants, v + 1);
            luaL_checktype(L, -1, LUA_TTABLE);
            names[v] = block_text_field(L, -1, "name");
            int has_texture = block_field(L, -1, "texture") != LUA_TNIL;
            if (v == 0 && (names[v] || has_texture))
                return luaL_error(L, "register_block: variants[1] uses the block name and texture");
            block_faces(L, -1, textures + v * 6);
        }
        d.variant_names = names;
        d.variant_textures = textures;
    }
    uint8_t coords[16 * 8 * 6];
    block_shape(L, &d, coords);
    lce_status rc = s->api->register_block ? s->api->register_block(s->ctx, &d) : LCE_E_UNSUPPORTED;
    if (rc == LCE_OK) ++s->st.blocks; else ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK); lua_pushinteger(L, rc); return 2;
}
/* lce.register_recipe{key=, result=, count=, shapeless=bool, cells={...}, width=, height=} */
static int b_register_recipe(lua_State *L) {
    lce_script *s = self(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lce_recipe_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d;
    d.key = opt_field(L, 1, "key", NULL);
    d.result = opt_field(L, 1, "result", NULL);
    d.result_count = (uint16_t)int_field(L, 1, "count", 1);
    d.shapeless = (uint8_t)int_field(L, 1, "shapeless", 0);
    d.width = (uint8_t)int_field(L, 1, "width", 0);
    d.height = (uint8_t)int_field(L, 1, "height", 0);
    d.craft_tab = opt_field(L, 1, "craft_tab", NULL);
    for (int i = 0; i < 9; ++i) d.cells[i].aux = -1;
    lua_getfield(L, 1, "cells");
    if (lua_istable(L, -1)) {
        int n = (int)luaL_len(L, -1); if (n > 9) n = 9;
        for (int i = 0; i < n; ++i) {
            lua_geti(L, -1, i + 1);
            if (lua_isstring(L, -1)) d.cells[i].item = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    if (!d.key || !d.result) return luaL_error(L, "register_recipe: key and result required");
    lce_status rc = s->api->register_recipe ? s->api->register_recipe(s->ctx, &d) : LCE_E_UNSUPPORTED;
    if (rc == LCE_OK) ++s->st.recipes; else ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK); lua_pushinteger(L, rc); return 2;
}
static int b_register_smelting(lua_State *L) {
    lce_script *s = self(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lce_smelt_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d;
    d.key = opt_field(L, 1, "key", NULL);
    d.input.item = opt_field(L, 1, "input", NULL); d.input.aux = -1;
    d.result = opt_field(L, 1, "result", NULL);
    d.result_count = (uint16_t)int_field(L, 1, "count", 1);
    d.xp_milli = (uint32_t)int_field(L, 1, "xp_milli", 0);
    if (!d.key || !d.input.item || !d.result) return luaL_error(L, "register_smelting: key/input/result required");
    lce_status rc = s->api->register_smelting ? s->api->register_smelting(s->ctx, &d) : LCE_E_UNSUPPORTED;
    if (rc == LCE_OK) ++s->st.smeltings; else ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK); lua_pushinteger(L, rc); return 2;
}

/* Event subscription: lce.subscribe("ServerTick", function(e) ... end).
 * The callback is kept in a registry subtable keyed by event id. */
static const struct { const char *name; int ev; } kEvents[] = {
    {"ServerTick", LCE_EV_SERVER_TICK}, {"ClientTick", LCE_EV_CLIENT_TICK},
    {"WorldLoad", LCE_EV_WORLD_LOAD}, {"WorldUnload", LCE_EV_WORLD_UNLOAD},
    {"BlockBreak", LCE_EV_BLOCK_BREAK}, {"BlockPlace", LCE_EV_BLOCK_PLACE},
    {"ItemUse", LCE_EV_ITEM_USE}, {"EntityDamage", LCE_EV_ENTITY_DAMAGE},
    {"EntityDeath", LCE_EV_ENTITY_DEATH}, {"PlayerJoin", LCE_EV_PLAYER_JOIN},
    {"PlayerLeave", LCE_EV_PLAYER_LEAVE}, {"Chat", LCE_EV_CHAT},
    {"BlockUse", LCE_EV_BLOCK_USE}, {"EntityInteract", LCE_EV_ENTITY_INTERACT},
    {"BlockTick", LCE_EV_BLOCK_TICK}, {"EntityTick", LCE_EV_ENTITY_TICK},
    {"EffectTick", LCE_EV_EFFECT_TICK},
    {"InventoryTick", LCE_EV_ITEM_INVENTORY_TICK}, {"BlockNeighbor", LCE_EV_BLOCK_NEIGHBOR},
    {"BlockEntityInside", LCE_EV_BLOCK_ENTITY_INSIDE}, {"BlockStepOn", LCE_EV_BLOCK_STEP_ON},
#ifdef LCE_HAVE_NET_1_4
    {"CustomPayload", LCE_EV_CUSTOM_PAYLOAD},
#endif
#ifdef LCE_HAVE_NET_1_6
    {"ClientPayload", LCE_EV_CLIENT_PAYLOAD},
#endif
#ifdef LCE_HAVE_PROJECTILES_1_9
    {"ProjectileHit", LCE_EV_PROJECTILE_HIT},
#endif
    /* Workshop aliases */
    {"PlayerJoinEvent", LCE_EV_PLAYER_JOIN}, {"PlayerBlockBreakEvent", LCE_EV_BLOCK_BREAK},
    {"ItemCompleteUseEvent", LCE_EV_ITEM_USE}, {"ServerTickEvent", LCE_EV_SERVER_TICK},
};
static int event_id(const char *name) {
    for (unsigned i = 0; i < sizeof kEvents / sizeof kEvents[0]; ++i)
        if (!strcmp(kEvents[i].name, name)) return kEvents[i].ev;
    return 0;
}
/* registry["lce.cb"][ev] = callback */
static void store_event_callback(lua_State *L, lce_event ev, int index) {
    lua_getfield(L, LUA_REGISTRYINDEX, "lce.cb");
    lua_pushvalue(L, index);
    lua_seti(L, -2, ev);
    lua_pop(L, 1);
    ++self(L)->st.events;
}

static int b_subscribe(lua_State *L) {
    lce_script *s = self(L);
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int ev = event_id(name);
    if (!ev) return luaL_error(L, "subscribe: unknown event '%s'", name);
    store_event_callback(L, (lce_event)ev, 2);
    if (ev == LCE_EV_SERVER_TICK) s->tick_interval = 0;
    if (s->api->subscribe) s->api->subscribe(s->ctx, (lce_event)ev, NULL, NULL);  /* host forwards via lce_script_event */
    lua_pushboolean(L, 1); return 1;
}

/* lce.subscribe_tick(interval, callback[, offset]) -> ok, status */
static int b_subscribe_tick(lua_State *L) {
    lce_script *s = self(L);
    lua_Integer interval = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_Integer offset = luaL_optinteger(L, 3, 0);
    luaL_argcheck(L, interval > 0 && (uint64_t)interval <= UINT32_MAX, 1, "interval must be a positive 32-bit integer");
    luaL_argcheck(L, offset >= 0 && offset < interval, 3, "offset must be within the interval");
    if (!(s->api->capabilities & LCE_CAP_EV_TICK)) {
        lua_pushboolean(L, 0);
        lua_pushinteger(L, LCE_E_UNSUPPORTED);
        return 2;
    }
    store_event_callback(L, LCE_EV_SERVER_TICK, 2);
    s->tick_interval = (uint32_t)interval;
    s->tick_offset = (uint32_t)offset;
    lua_pushboolean(L, 1);
    lua_pushinteger(L, LCE_OK);
    return 2;
}

static int b_tick_stats(lua_State *L) {
    lce_script *s = self(L);
    lua_createtable(L, 0, 2);
    lua_pushinteger(L, s->tick_callbacks_run);
    lua_setfield(L, -2, "callbacks_run");
    lua_pushinteger(L, s->tick_callbacks_skipped);
    lua_setfield(L, -2, "callbacks_skipped");
    return 1;
}

/* ---- runtime services (lce.give_item, set_block, ...) ---------------------- */
/* Opaque game pointers (players, entities, levels) travel as light userdata; nil = none. */
static void *opt_ptr(lua_State *L, int i) {
    if (lua_isnoneornil(L, i)) return NULL;
    luaL_checktype(L, i, LUA_TLIGHTUSERDATA);
    return lua_touserdata(L, i);
}
/* lce.send_chat(text) broadcasts; lce.send_chat(player_or_nil, text) targets one player. */
static int b_send_chat(lua_State *L) {
    lce_script *s = self(L);
    void *player = NULL;
    const char *msg;
    if (lua_gettop(L) >= 2) { player = opt_ptr(L, 1); msg = luaL_checkstring(L, 2); }
    else msg = luaL_checkstring(L, 1);
    lce_status rc = s->api->send_chat ? s->api->send_chat(s->ctx, player, msg) : LCE_E_UNSUPPORTED;
    lua_pushinteger(L, rc); return 1;
}
/* lce.give_item(item[, count]) or lce.give_item(player, item[, count[, aux]]). */
static int b_give_item(lua_State *L) {
    lce_script *s = self(L);
    void *player = NULL; int a = 1;
    if (lua_type(L, 1) != LUA_TSTRING) { player = opt_ptr(L, 1); a = 2; }
    const char *item = luaL_checkstring(L, a);
    lua_Integer n = luaL_optinteger(L, a + 1, 1), aux = luaL_optinteger(L, a + 2, 0);
    if (n < 1 || n > 64) return luaL_error(L, "give_item: count 1..64");
    lce_status rc = s->api->give_item ? s->api->give_item(s->ctx, player, item, (uint32_t)n, (int32_t)aux) : LCE_E_UNSUPPORTED;
    lua_pushinteger(L, rc); return 1;
}
/* lce.drop_item(level, x, y, z, item[, count]) */
static int b_drop_item(lua_State *L) {
    lce_script *s = self(L);
    void *level = opt_ptr(L, 1);
    lua_Integer x = luaL_checkinteger(L, 2), y = luaL_checkinteger(L, 3), z = luaL_checkinteger(L, 4);
    const char *item = luaL_checkstring(L, 5);
    lua_Integer n = luaL_optinteger(L, 6, 1);
    if (n < 1 || n > 64) return luaL_error(L, "drop_item: count 1..64");
    lce_status rc = s->api->drop_item ? s->api->drop_item(s->ctx, level, (int32_t)x, (int32_t)y, (int32_t)z, item, (uint32_t)n) : LCE_E_UNSUPPORTED;
    lua_pushinteger(L, rc); return 1;
}
/* lce.add_effect(entity, {effect=, duration_ticks=, amplifier=}) */
static int b_add_effect(lua_State *L) {
    lce_script *s = self(L);
    void *entity = opt_ptr(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, 2, "effect"); const char *effect = luaL_checkstring(L, -1);
    lua_getfield(L, 2, "duration_ticks"); lua_Integer dur = luaL_optinteger(L, -1, 200);
    lua_getfield(L, 2, "amplifier"); lua_Integer amp = luaL_optinteger(L, -1, 0);
    if (dur < 1 || dur > 20 * 60 * 60 * 24 || amp < 0 || amp > 255) return luaL_error(L, "add_effect: duration 1..1728000, amplifier 0..255");
    lce_effect_ref e; memset(&e, 0, sizeof e);
    e.effect = effect; e.duration_ticks = (uint32_t)dur; e.amplifier = (uint8_t)amp;
    lce_status rc = s->api->add_effect ? s->api->add_effect(s->ctx, entity, &e) : LCE_E_UNSUPPORTED;
    lua_pop(L, 3);
    lua_pushinteger(L, rc); return 1;
}
/* ---- world services (API 1.1) ------------------------------------------------- */
#define API_HAS(s, f) ((s)->api && (s)->api->struct_size >= offsetof(lce_api, f) + sizeof(void *) && (s)->api->f)

/* Keep the key and binary pixel string on the Lua stack until the loader copies both. */
static int b_register_texture(lua_State *L) {
    lce_script *s = self(L);
    enum { width = 16, height = 16 };
    lce_texture_desc d = {0};
    d.struct_size = sizeof d;
    d.width = width;
    d.height = height;

    luaL_checktype(L, 1, LUA_TTABLE);
    size_t key_len, atlas_len, rgba_len;
    lua_getfield(L, 1, "key");
    luaL_checktype(L, -1, LUA_TSTRING);
    d.key = lua_tolstring(L, -1, &key_len);
    if (!key_len || key_len >= LCE_MAX_KEY || memchr(d.key, '\0', key_len))
        return luaL_error(L, "register_texture: invalid resource key");

    lua_getfield(L, 1, "atlas");
    luaL_checktype(L, -1, LUA_TSTRING);
    const char *atlas = lua_tolstring(L, -1, &atlas_len);
    if (atlas_len == sizeof "items" - 1 && !memcmp(atlas, "items", atlas_len)) {
        d.atlas = LCE_TEXTURE_ITEMS;
    } else if (atlas_len == sizeof "terrain" - 1 && !memcmp(atlas, "terrain", atlas_len)) {
        d.atlas = LCE_TEXTURE_TERRAIN;
    } else {
        return luaL_error(L, "register_texture: atlas must be items or terrain");
    }

    lua_getfield(L, 1, "rgba");
    luaL_checktype(L, -1, LUA_TSTRING);
    d.rgba = (const uint8_t *)lua_tolstring(L, -1, &rgba_len);
    if (rgba_len != width * height * 4)
        return luaL_error(L, "register_texture: rgba must contain 1024 bytes");

    lce_status rc = LCE_E_UNSUPPORTED;
    if (API_HAS(s, register_texture) && (s->api->capabilities & LCE_CAP_TEXTURES))
        rc = s->api->register_texture(s->ctx, &d);
    if (rc != LCE_OK) ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK);
    lua_pushinteger(L, rc);
    return 2;
}

/* lce.register_fuel{item="modid:item", burn_ticks=1600} */
static int b_register_fuel(lua_State *L) {
    lce_script *s = self(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "item");
    luaL_checktype(L, -1, LUA_TSTRING);
    const char *item = lua_tostring(L, -1); /* keep the Lua string anchored */
    lua_getfield(L, 1, "burn_ticks");
    lua_Integer ticks = luaL_checkinteger(L, -1);
    if (ticks < 1 || ticks > 72000) return luaL_error(L, "register_fuel: burn_ticks must be 1..72000");
    lce_fuel_desc d; memset(&d, 0, sizeof d);
    d.struct_size = sizeof d; d.item = item; d.burn_ticks = (uint32_t)ticks;
    lce_status rc = API_HAS(s, register_fuel) ? s->api->register_fuel(s->ctx, &d) : LCE_E_UNSUPPORTED;
    lua_pop(L, 2);
    if (rc != LCE_OK) ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK); lua_pushinteger(L, rc); return 2;
}

/* Keep descriptor strings on the stack until the loader has copied them. */
static const char *worldgen_string(lua_State *L, const char *field) {
    luaL_checktype(L, -1, LUA_TSTRING);
    size_t n;
    const char *value = lua_tolstring(L, -1, &n);
    if (memchr(value, '\0', n)) luaL_error(L, "%s contains an embedded NUL", field);
    return value;
}

static const char *feature_string_field(lua_State *L, const char *field) {
    lua_getfield(L, 1, field);
    return lua_isnil(L, -1) ? NULL : worldgen_string(L, field);
}
/* Worldgen descriptors are copied by register_feature during this call. */
static int b_register_feature(lua_State *L) {
    lce_script *s = self(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lce_feature_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d;
    d.key = feature_string_field(L, "key");
    d.block = feature_string_field(L, "block");
    d.block2 = feature_string_field(L, "block2");
    d.structure = feature_string_field(L, "structure");
    if (!d.key) return luaL_error(L, "register_feature: key required");
    const char *fields[] = {"kind", "dimension", "size", "per_chunk", "min_y", "max_y", "rarity"};
    lua_Integer values[7];
    for (int i = 0; i < 7; ++i) {
        lua_getfield(L, 1, fields[i]);
        values[i] = lua_isnil(L, -1) ? 0 : luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        if (values[i] < 0 || values[i] > (i == 1 ? 255 : 65535))
            return luaL_error(L, "register_feature: %s out of range", fields[i]);
    }
    d.kind = (uint16_t)values[0]; d.dimension = (uint8_t)values[1]; d.size = (uint16_t)values[2];
    d.per_chunk = (uint16_t)values[3]; d.min_y = (uint16_t)values[4];
    d.max_y = (uint16_t)values[5]; d.rarity = (uint16_t)values[6];
    const char *biomes[256];
    lua_getfield(L, 1, "biomes");
    if (!lua_isnil(L, -1)) {
        luaL_checktype(L, -1, LUA_TTABLE);
        int list = lua_absindex(L, -1);
        lua_Integer n = luaL_len(L, list);
        if (n < 0 || n > 256) return luaL_error(L, "register_feature: expected 0..256 biomes");
        luaL_checkstack(L, (int)n + LUA_MINSTACK, "feature biome list");
        for (lua_Integer i = 0; i < n; ++i) {
            lua_geti(L, list, i + 1);
            biomes[i] = worldgen_string(L, "biomes entry");
        }
        d.biomes = biomes; d.biome_count = (uint32_t)n;
    }
    lce_status rc = API_HAS(s, register_feature) ? s->api->register_feature(s->ctx, &d) : LCE_E_UNSUPPORTED;
    if (rc != LCE_OK) ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK); lua_pushinteger(L, rc); return 2;
}
/* A raw read avoids executing mod code during biome and structure marshaling. */
static void worldgen_field(lua_State *L, const char *field) {
    lua_pushstring(L, field);
    lua_rawget(L, 1);
}
static const char *worldgen_string_field(lua_State *L, const char *field, int optional) {
    worldgen_field(L, field);
    return optional && lua_isnil(L, -1) ? NULL : worldgen_string(L, field);
}
static uint32_t worldgen_integer(lua_State *L, const char *field, uint32_t min, uint32_t max) {
    worldgen_field(L, field);
    if (!lua_isnil(L, -1) && !lua_isinteger(L, -1)) luaL_error(L, "%s must be an integer", field);
    lua_Integer value = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (value < min || value > max) luaL_error(L, "%s out of range", field);
    return (uint32_t)value;
}
static uint32_t worldgen_array_size(lua_State *L, const char *field, uint32_t min, uint32_t max) {
    luaL_checktype(L, -1, LUA_TTABLE);
    size_t length = lua_rawlen(L, -1);
    if (length < min || length > max) luaL_error(L, "%s has the wrong number of entries", field);
    size_t count = 0;
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1 || (size_t)lua_tointeger(L, -2) > length)
            luaL_error(L, "%s must be a dense array", field);
        ++count;
        lua_pop(L, 1);
    }
    if (count != length) luaL_error(L, "%s must be a dense array", field);
    return (uint32_t)count;
}
static uint32_t worldgen_refs(lua_State *L, const char *field, const char **refs, int optional) {
    worldgen_field(L, field);
    if (optional && lua_isnil(L, -1)) return 0;
    uint32_t count = worldgen_array_size(L, field, optional ? 0 : 1, 256);
    int array = lua_absindex(L, -1);
    luaL_checkstack(L, (int)count + LUA_MINSTACK, "worldgen descriptor");
    for (uint32_t i = 0; i < count; ++i) {
        lua_rawgeti(L, array, i + 1);
        refs[i] = worldgen_string(L, field);
    }
    return count;
}
static int b_register_biome(lua_State *L) {
    lce_script *s = self(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lce_status rc = LCE_E_UNSUPPORTED;
    if (API_HAS(s, register_biome) && (s->api->capabilities & LCE_CAP_BIOMES)) {
        lce_biome_desc d = {0};
        d.struct_size = sizeof d;
        d.key = worldgen_string_field(L, "key", 0);
        d.name = worldgen_string_field(L, "name", 1);
        d.top_block = worldgen_string_field(L, "top_block", 1);
        d.filler_block = worldgen_string_field(L, "filler_block", 1);
        d.temperature_milli = worldgen_integer(L, "temperature_milli", 0, UINT32_MAX);
        d.rainfall_milli = worldgen_integer(L, "rainfall_milli", 0, UINT32_MAX);
        d.grass_rgb = worldgen_integer(L, "grass_rgb", 0, UINT32_MAX);
        d.foliage_rgb = worldgen_integer(L, "foliage_rgb", 0, UINT32_MAX);
        d.water_rgb = worldgen_integer(L, "water_rgb", 0, UINT32_MAX);
        d.sky_rgb = worldgen_integer(L, "sky_rgb", 0, UINT32_MAX);
        d.weight = (uint16_t)worldgen_integer(L, "weight", 0, UINT16_MAX);
        d.height_base_milli = (uint16_t)worldgen_integer(L, "height_base_milli", 0, UINT16_MAX);
        d.height_variation_milli = (uint16_t)worldgen_integer(L, "height_variation_milli", 0, UINT16_MAX);
        const char *features[256];
        d.feature_count = worldgen_refs(L, "features", features, 1);
        d.features = d.feature_count ? features : NULL;
        rc = s->api->register_biome(s->ctx, &d);
    }
    if (rc != LCE_OK) ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK);
    lua_pushinteger(L, rc);
    return 2;
}
static int b_register_structure(lua_State *L) {
    lce_script *s = self(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lce_status rc = LCE_E_UNSUPPORTED;
    if (API_HAS(s, register_structure) && (s->api->capabilities & LCE_CAP_STRUCTURES)) {
        lce_structure_desc d = {0};
        d.struct_size = sizeof d;
        d.key = worldgen_string_field(L, "key", 0);
        /* Match the core descriptor bounds. The backend checks its own chunk limits. */
        d.size_x = (uint16_t)worldgen_integer(L, "size_x", 1, 64);
        d.size_y = (uint16_t)worldgen_integer(L, "size_y", 1, 64);
        d.size_z = (uint16_t)worldgen_integer(L, "size_z", 1, 64);
        const char *palette[256];
        d.palette_count = worldgen_refs(L, "palette", palette, 0);
        d.palette = palette;
        uint32_t cells = (uint32_t)d.size_x * d.size_y * d.size_z;
        worldgen_field(L, "blocks");
        worldgen_array_size(L, "blocks", cells, cells);
        int array = lua_absindex(L, -1);
        uint16_t *blocks = lua_newuserdatauv(L, cells * sizeof *blocks, 0);
        for (uint32_t i = 0; i < cells; ++i) {
            lua_rawgeti(L, array, i + 1);
            lua_Integer index = lua_tointeger(L, -1);
            if (!lua_isinteger(L, -1) || index < 0 || index >= d.palette_count)
                return luaL_error(L, "blocks[%I] is outside the palette index range", (lua_Integer)i + 1);
            blocks[i] = (uint16_t)index;
            lua_pop(L, 1);
        }
        d.blocks = blocks;
        rc = s->api->register_structure(s->ctx, &d);
    }
    if (rc != LCE_OK) ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK);
    lua_pushinteger(L, rc);
    return 2;
}
static lua_Integer chk_int(lua_State *L, int i, lua_Integer lo, lua_Integer hi, const char *what) {
    lua_Integer v = luaL_checkinteger(L, i);
    if (v < lo || v > hi) return luaL_error(L, "%s out of range", what);
    return v;
}

static int entity_field(lua_State *L, int table, const char *field) {
    lua_pushstring(L, field);
    return lua_rawget(L, table);
}

/* Keep every borrowed string on the stack until register_entity2 copies it. */
static const char *entity_key(lua_State *L, const char *field) {
    size_t len;
    luaL_checktype(L, -1, LUA_TSTRING);
    const char *key = lua_tolstring(L, -1, &len);
    if (!len || len >= LCE_MAX_KEY || memchr(key, 0, len))
        luaL_error(L, "register_entity: invalid %s", field);
    return key;
}

static const char *entity_key_field(lua_State *L, int table, const char *field, int required) {
    if (entity_field(L, table, field) != LUA_TNIL || required) return entity_key(L, field);
    lua_pop(L, 1);
    return NULL;
}

static uint32_t entity_number(lua_State *L, int table, const char *field, uint32_t max) {
    if (entity_field(L, table, field) == LUA_TNIL) {
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_isinteger(L, -1)) luaL_error(L, "register_entity: %s must be an integer", field);
    uint32_t value = (uint32_t)chk_int(L, -1, 0, max, field);
    lua_pop(L, 1);
    return value;
}

static uint32_t entity_list(lua_State *L, int table, const char *field, uint32_t max) {
    if (entity_field(L, table, field) == LUA_TNIL) return 0;
    luaL_checktype(L, -1, LUA_TTABLE);
    int list = lua_absindex(L, -1);
    size_t len = lua_rawlen(L, list);
    if (len > max) luaL_error(L, "register_entity: %s has too many entries", field);
    lua_pushnil(L);
    while (lua_next(L, list)) {
        lua_Integer idx = lua_tointeger(L, -2);
        if (!lua_isinteger(L, -2) || idx < 1 || (uint64_t)idx > len)
            luaL_error(L, "register_entity: %s must be a dense array", field);
        lua_pop(L, 1);
    }
    return (uint32_t)len;
}

static int b_register_entity(lua_State *L) {
    enum { MAX_GOALS = 16, MAX_BIOMES = 256 };
    static const char *const unsupported[] = {
        "texture", "name", "width_milli", "height_milli", "drops",
        "egg_primary_rgb", "egg_secondary_rgb"
    };
    lce_script *s = self(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lce_status rc = LCE_E_UNSUPPORTED;
    if (!API_HAS(s, register_entity2)) goto done;
    for (size_t i = 0; i < sizeof unsupported / sizeof unsupported[0]; ++i) {
        int type = entity_field(L, 1, unsupported[i]);
        lua_pop(L, 1);
        if (type != LUA_TNIL) goto done;
    }

    luaL_checkstack(L, MAX_GOALS * 2 + MAX_BIOMES + 16, "register_entity descriptor");
    lce_entity_desc2 d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.key = entity_key_field(L, 1, "key", 1);
    d.base_mob = entity_key_field(L, 1, "base_mob", 1);
    d.sound_ambient = entity_key_field(L, 1, "sound_ambient", 0);
    d.sound_hurt = entity_key_field(L, 1, "sound_hurt", 0);
    d.sound_death = entity_key_field(L, 1, "sound_death", 0);
    d.max_health_milli = entity_number(L, 1, "max_health_milli", 1024000);
    d.move_speed_milli = entity_number(L, 1, "move_speed_milli", 1024000);
    d.attack_damage_milli = entity_number(L, 1, "attack_damage_milli", 2048000);
    d.spawn_weight = entity_number(L, 1, "spawn_weight", UINT32_MAX);
    d.category = (uint16_t)entity_number(L, 1, "category", LCE_MOB_AMBIENT);
    d.min_group = (uint8_t)entity_number(L, 1, "min_group", UINT8_MAX);
    d.max_group = (uint8_t)entity_number(L, 1, "max_group", UINT8_MAX);
    if (entity_field(L, 1, "spawn_egg") != LUA_TNIL) {
        luaL_checktype(L, -1, LUA_TBOOLEAN);
        d.egg_primary_rgb = lua_toboolean(L, -1) ? 1u : 0u;
    }
    lua_pop(L, 1);

    lce_ai_goal goals[MAX_GOALS];
    d.goal_count = entity_list(L, 1, "goals", MAX_GOALS);
    int list = lua_absindex(L, -1);
    for (uint32_t i = 0; i < d.goal_count; ++i) {
        lua_rawgeti(L, list, i + 1);
        luaL_checktype(L, -1, LUA_TTABLE);
        int goal = lua_absindex(L, -1);
        goals[i].kind = (uint16_t)entity_number(L, goal, "kind", LCE_AI_AVOID_ENTITY);
        if (!goals[i].kind) return luaL_error(L, "register_entity: goal kind required");
        goals[i].priority = (uint16_t)entity_number(L, goal, "priority", UINT16_MAX);
        goals[i].speed_milli = entity_number(L, goal, "speed_milli", UINT32_MAX);
        goals[i].param = entity_key_field(L, goal, "param", 0);
    }
    if (d.goal_count) d.goals = goals;

    const char *biomes[MAX_BIOMES];
    d.spawn_biome_count = entity_list(L, 1, "spawn_biomes", MAX_BIOMES);
    list = lua_absindex(L, -1);
    for (uint32_t i = 0; i < d.spawn_biome_count; ++i) {
        lua_rawgeti(L, list, i + 1);
        biomes[i] = entity_key(L, "spawn_biomes entry");
    }
    if (d.spawn_biome_count) d.spawn_biomes = biomes;
    rc = s->api->register_entity2(s->ctx, &d);

done:
    if (rc != LCE_OK) ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK);
    lua_pushinteger(L, rc);
    return 2;
}

/* lce.set_block(level, x, y, z, block[, data]) */
static int b_set_block(lua_State *L) {
    lce_script *s = self(L);
    void *lv = opt_ptr(L, 1);
    int32_t x = (int32_t)chk_int(L, 2, -30000000, 30000000, "x"), y = (int32_t)chk_int(L, 3, 0, 255, "y"), z = (int32_t)chk_int(L, 4, -30000000, 30000000, "z");
    const char *block = luaL_checkstring(L, 5);
    uint32_t data = (uint32_t)luaL_optinteger(L, 6, 0);
    lua_pushinteger(L, API_HAS(s, set_block) ? s->api->set_block(s->ctx, lv, x, y, z, block, data) : LCE_E_UNSUPPORTED); return 1;
}
/* lce.get_block(level, x, y, z) -> id | meta << 12, or -1 */
static int b_get_block(lua_State *L) {
    lce_script *s = self(L);
    void *lv = opt_ptr(L, 1);
    int32_t x = (int32_t)chk_int(L, 2, -30000000, 30000000, "x"), y = (int32_t)chk_int(L, 3, 0, 255, "y"), z = (int32_t)chk_int(L, 4, -30000000, 30000000, "z");
    lua_pushinteger(L, API_HAS(s, get_block) ? s->api->get_block(s->ctx, lv, x, y, z) : -1); return 1;
}
/* lce.spawn_entity(level, entity, x, y, z) */
static int b_spawn_entity(lua_State *L) {
    lce_script *s = self(L);
    void *lv = opt_ptr(L, 1);
    const char *e = luaL_checkstring(L, 2);
    int32_t x = (int32_t)chk_int(L, 3, -30000000, 30000000, "x"), y = (int32_t)chk_int(L, 4, 0, 255, "y"), z = (int32_t)chk_int(L, 5, -30000000, 30000000, "z");
    lua_pushinteger(L, API_HAS(s, spawn_entity) ? s->api->spawn_entity(s->ctx, lv, e, x, y, z) : LCE_E_UNSUPPORTED); return 1;
}
/* lce.play_sound(level, x, y, z, sound) */
static int b_play_sound(lua_State *L) {
    lce_script *s = self(L);
    void *lv = opt_ptr(L, 1);
    int32_t x = (int32_t)chk_int(L, 2, -30000000, 30000000, "x"), y = (int32_t)chk_int(L, 3, 0, 255, "y"), z = (int32_t)chk_int(L, 4, -30000000, 30000000, "z");
    const char *snd = luaL_checkstring(L, 5);
    lua_pushinteger(L, API_HAS(s, play_sound) ? s->api->play_sound(s->ctx, lv, x, y, z, snd) : LCE_E_UNSUPPORTED); return 1;
}
/* ---- API 1.2 services ----------------------------------------------------------- */
/* lce.explode(level, x, y, z, radius[, fire[, destroy_blocks]]) */
static int b_explode(lua_State *L) {
    lce_script *s = self(L);
    void *lv = opt_ptr(L, 1);
    double x = luaL_checknumber(L, 2), y = luaL_checknumber(L, 3), z = luaL_checknumber(L, 4), r = luaL_checknumber(L, 5);
    int fire = lua_toboolean(L, 6), destroy = lua_isnoneornil(L, 7) ? 1 : lua_toboolean(L, 7);
    if (!(r > 0 && r <= 16)) return luaL_error(L, "explode: radius 0..16");
    lua_pushinteger(L, API_HAS(s, explode) ? s->api->explode(s->ctx, lv, x, y, z, (float)r, fire, destroy) : LCE_E_UNSUPPORTED); return 1;
}
/* lce.set_fire(entity, ticks)  (0 extinguishes) */
static int b_set_fire(lua_State *L) {
    lce_script *s = self(L);
    void *e = opt_ptr(L, 1);
    uint32_t t = (uint32_t)chk_int(L, 2, 0, 20 * 60 * 60, "ticks");
    lua_pushinteger(L, API_HAS(s, set_fire) ? s->api->set_fire(s->ctx, e, t) : LCE_E_UNSUPPORTED); return 1;
}
/* lce.heal(entity, amount)  (health points, fractional allowed) */
static int b_heal(lua_State *L) {
    lce_script *s = self(L);
    void *e = opt_ptr(L, 1);
    double a = luaL_checknumber(L, 2);
    if (!(a > 0 && a <= 1024)) return luaL_error(L, "heal: amount 0..1024");
    lua_pushinteger(L, API_HAS(s, heal) ? s->api->heal(s->ctx, e, (uint32_t)(a * 1000.0 + 0.5)) : LCE_E_UNSUPPORTED); return 1;
}
/* lce.damage_entity(entity, amount[, attacker]) */
static int b_damage_entity(lua_State *L) {
    lce_script *s = self(L);
    void *e = opt_ptr(L, 1);
    double a = luaL_checknumber(L, 2);
    void *att = opt_ptr(L, 3);
    if (!(a > 0 && a <= 4096)) return luaL_error(L, "damage_entity: amount 0..4096");
    lua_pushinteger(L, API_HAS(s, damage_entity) ? s->api->damage_entity(s->ctx, e, (uint32_t)(a * 1000.0 + 0.5), att) : LCE_E_UNSUPPORTED); return 1;
}
/* lce.damage_held_item(player, amount) / lce.consume_held_item(player, count) */
static int b_damage_held(lua_State *L) {
    lce_script *s = self(L);
    void *p = opt_ptr(L, 1);
    uint32_t n = (uint32_t)chk_int(L, 2, 1, 32767, "amount");
    lua_pushinteger(L, API_HAS(s, damage_held_item) ? s->api->damage_held_item(s->ctx, p, n) : LCE_E_UNSUPPORTED); return 1;
}
static int b_consume_held(lua_State *L) {
    lce_script *s = self(L);
    void *p = opt_ptr(L, 1);
    uint32_t n = (uint32_t)chk_int(L, 2, 1, 64, "count");
    lua_pushinteger(L, API_HAS(s, consume_held_item) ? s->api->consume_held_item(s->ctx, p, n) : LCE_E_UNSUPPORTED); return 1;
}
/* lce.teleport(entity, x, y, z) / lce.set_motion(entity, dx, dy, dz) */
static int b_teleport(lua_State *L) {
    lce_script *s = self(L);
    void *e = opt_ptr(L, 1);
    double x = luaL_checknumber(L, 2), y = luaL_checknumber(L, 3), z = luaL_checknumber(L, 4);
    lua_pushinteger(L, API_HAS(s, teleport) ? s->api->teleport(s->ctx, e, x, y, z) : LCE_E_UNSUPPORTED); return 1;
}
static int b_set_motion(lua_State *L) {
    lce_script *s = self(L);
    void *e = opt_ptr(L, 1);
    double x = luaL_checknumber(L, 2), y = luaL_checknumber(L, 3), z = luaL_checknumber(L, 4);
    if (!(x >= -10 && x <= 10 && y >= -10 && y <= 10 && z >= -10 && z <= 10)) return luaL_error(L, "set_motion: components -10..10");
    lua_pushinteger(L, API_HAS(s, set_motion) ? s->api->set_motion(s->ctx, e, x, y, z) : LCE_E_UNSUPPORTED); return 1;
}
#ifdef LCE_HAVE_SVC_1_3
/* lce.spawn_particles(level, type_id, x, y, z[, count[, dx, dy, dz[, speed]]]) (API 1.3, ServerLevel::sendParticles) */
static int b_spawn_particles(lua_State *L) {
    lce_script *s = self(L);
    void *lv = opt_ptr(L, 1);
    lua_Integer id = luaL_checkinteger(L, 2);
    double x = luaL_checknumber(L, 3), y = luaL_checknumber(L, 4), z = luaL_checknumber(L, 5);
    lua_Integer n = luaL_optinteger(L, 6, 1);
    double dx = luaL_optnumber(L, 7, 0), dy = luaL_optnumber(L, 8, 0), dz = luaL_optnumber(L, 9, 0), sp = luaL_optnumber(L, 10, 0);
    if (n < 0 || n > 1000 || id < 0 || id > 0xffff) return luaL_error(L, "spawn_particles: count 0..1000, id 0..65535");
    lua_pushinteger(L, API_HAS(s, spawn_particles) ? s->api->spawn_particles(s->ctx, lv, (int32_t)id, x, y, z, (uint32_t)n, dx, dy, dz, sp)
                                                   : LCE_E_UNSUPPORTED); return 1;
}
#endif
#ifdef LCE_HAVE_TINTED_PARTICLES_1_12
/* lce.spawn_tinted_particles(level, rgb, x, y, z[, count[, spread]]) */
static int b_spawn_tinted_particles(lua_State *L) {
    lce_script *s = self(L);
    void *level = opt_ptr(L, 1);
    uint32_t rgb = (uint32_t)chk_int(L, 2, 0, 0xffffff, "rgb");
    double x = luaL_checknumber(L, 3), y = luaL_checknumber(L, 4), z = luaL_checknumber(L, 5);
    lua_Integer count = luaL_optinteger(L, 6, 1);
    double spread = luaL_optnumber(L, 7, 0);
    if (count < 1 || count > 1000) return luaL_error(L, "spawn_tinted_particles: count 1..1000");
    lua_pushinteger(L, API_HAS(s, spawn_tinted_particles) ?
        s->api->spawn_tinted_particles(s->ctx, level, rgb, x, y, z, (uint32_t)count, spread) : LCE_E_UNSUPPORTED);
    return 1;
}
#endif
#ifdef LCE_HAVE_NBT_1_4
/* lce.item_tag_get(player, key) -> integer | number | string | nil;  lce.item_tag_set(player, key, value) -> status;
 * lce.item_tag_remove(player, key) -> status. The main-hand stack's NBT tag (API 1.3). */
static int b_item_tag_get(lua_State *L) {
    lce_script *s = self(L);
    void *p = opt_ptr(L, 1);
    const char *k = luaL_checkstring(L, 2);
    lce_nbt_value v; memset(&v, 0, sizeof v);
    if (!API_HAS(s, item_tag_get) || s->api->item_tag_get(s->ctx, p, k, &v) != LCE_OK) { lua_pushnil(L); return 1; }
    if (v.type == LCE_NBT_INT) lua_pushinteger(L, (lua_Integer)v.i);
    else if (v.type == LCE_NBT_DOUBLE) lua_pushnumber(L, v.d);
    else if (v.type == LCE_NBT_STRING) { v.s[sizeof v.s - 1] = 0; lua_pushstring(L, v.s); }
    else lua_pushnil(L);
    return 1;
}
static int b_item_tag_set(lua_State *L) {
    lce_script *s = self(L);
    void *p = opt_ptr(L, 1);
    const char *k = luaL_checkstring(L, 2);
    lce_nbt_value v; memset(&v, 0, sizeof v);
    if (lua_isinteger(L, 3)) { v.type = LCE_NBT_INT; v.i = (int64_t)lua_tointeger(L, 3); }
    else if (lua_type(L, 3) == LUA_TNUMBER) { v.type = LCE_NBT_DOUBLE; v.d = lua_tonumber(L, 3); }
    else if (lua_type(L, 3) == LUA_TSTRING) {
        size_t n = 0; const char *t = lua_tolstring(L, 3, &n);
        if (n >= sizeof v.s) return luaL_error(L, "item_tag_set: string longer than %d", (int)sizeof v.s - 1);
        v.type = LCE_NBT_STRING; memcpy(v.s, t, n);
    } else return luaL_error(L, "item_tag_set: value must be an integer, number or string");
    lua_pushinteger(L, API_HAS(s, item_tag_set) ? s->api->item_tag_set(s->ctx, p, k, &v) : LCE_E_UNSUPPORTED); return 1;
}
static int b_item_tag_remove(lua_State *L) {
    lce_script *s = self(L);
    void *p = opt_ptr(L, 1);
    const char *k = luaL_checkstring(L, 2);
    lua_pushinteger(L, API_HAS(s, item_tag_remove) ? s->api->item_tag_remove(s->ctx, p, k) : LCE_E_UNSUPPORTED); return 1;
}
#endif
#ifdef LCE_HAVE_ENTITY_NBT_1_5
/* Entity NBT (API 1.5). lce.entity_nbt_get(entity_or_nil, "Health" | "Pos[1]" | "Attributes[0]/Base" | "Tags[#]") -> value | nil
 * (read-only vanilla save data). Persistent mod data saved with the entity (Forge getPersistentData):
 * lce.entity_data_get(entity, key) -> value | nil; lce.entity_data_set(entity, key, value) -> status (not on players);
 * lce.entity_data_remove(entity, key) -> status. Values: integer (int32), number, or ASCII string (<= 200 chars). */
static int push_nbt(lua_State *L, lce_status st, lce_nbt_value *v) {
    if (st != LCE_OK) lua_pushnil(L);
    else if (v->type == LCE_NBT_INT) lua_pushinteger(L, (lua_Integer)v->i);
    else if (v->type == LCE_NBT_DOUBLE) lua_pushnumber(L, v->d);
    else if (v->type == LCE_NBT_STRING) { v->s[sizeof v->s - 1] = 0; lua_pushstring(L, v->s); }
    else lua_pushnil(L);
    return 1;
}
static void check_nbt(lua_State *L, int i, lce_nbt_value *v, const char *fn) {
    memset(v, 0, sizeof *v);
    if (lua_isinteger(L, i)) { v->type = LCE_NBT_INT; v->i = (int64_t)lua_tointeger(L, i); }
    else if (lua_type(L, i) == LUA_TNUMBER) { v->type = LCE_NBT_DOUBLE; v->d = lua_tonumber(L, i); }
    else if (lua_type(L, i) == LUA_TSTRING) {
        size_t n = 0; const char *t = lua_tolstring(L, i, &n);
        if (n >= sizeof v->s) luaL_error(L, "%s: string longer than %d", fn, (int)sizeof v->s - 1);
        v->type = LCE_NBT_STRING; memcpy(v->s, t, n);
    } else luaL_error(L, "%s: value must be an integer, number or string", fn);
}
static int b_entity_nbt_get(lua_State *L) {
    lce_script *s = self(L);
    void *e = opt_ptr(L, 1);
    const char *path = luaL_checkstring(L, 2);
    lce_nbt_value v; memset(&v, 0, sizeof v);
    return push_nbt(L, API_HAS(s, entity_nbt_get) ? s->api->entity_nbt_get(s->ctx, e, path, &v) : LCE_E_UNSUPPORTED, &v);
}
static int b_entity_data_get(lua_State *L) {
    lce_script *s = self(L);
    void *e = opt_ptr(L, 1);
    const char *k = luaL_checkstring(L, 2);
    lce_nbt_value v; memset(&v, 0, sizeof v);
    return push_nbt(L, API_HAS(s, entity_data_get) ? s->api->entity_data_get(s->ctx, e, k, &v) : LCE_E_UNSUPPORTED, &v);
}
static int b_entity_data_set(lua_State *L) {
    lce_script *s = self(L);
    void *e = opt_ptr(L, 1);
    const char *k = luaL_checkstring(L, 2);
    lce_nbt_value v; check_nbt(L, 3, &v, "entity_data_set");
    lua_pushinteger(L, API_HAS(s, entity_data_set) ? s->api->entity_data_set(s->ctx, e, k, &v) : LCE_E_UNSUPPORTED); return 1;
}
static int b_entity_data_remove(lua_State *L) {
    lce_script *s = self(L);
    void *e = opt_ptr(L, 1);
    const char *k = luaL_checkstring(L, 2);
    lua_pushinteger(L, API_HAS(s, entity_data_remove) ? s->api->entity_data_remove(s->ctx, e, k) : LCE_E_UNSUPPORTED); return 1;
}
#endif
#ifdef LCE_HAVE_NET_1_4
/* lce.register_channel("modid:name") at load; lce.send_payload(player_or_nil, channel, bytes) (API 1.4) */
static int b_register_channel(lua_State *L) {
    lce_script *s = self(L);
    const char *c = luaL_checkstring(L, 1);
    lua_pushinteger(L, API_HAS(s, register_channel) ? s->api->register_channel(s->ctx, c) : LCE_E_UNSUPPORTED); return 1;
}
static int b_send_payload(lua_State *L) {
    lce_script *s = self(L);
    void *p = opt_ptr(L, 1);
    const char *c = luaL_checkstring(L, 2);
    size_t n = 0; const char *d = luaL_checklstring(L, 3, &n);
    if (n > 32000) return luaL_error(L, "send_payload: at most 32000 bytes");
    lua_pushinteger(L, API_HAS(s, send_payload) ? s->api->send_payload(s->ctx, p, c, d, (uint32_t)n) : LCE_E_UNSUPPORTED); return 1;
}
#endif
#ifdef LCE_HAVE_NET_1_6
/* lce.send_to_server(channel, bytes) (API 1.6): client -> server on a registered channel. Inside a ClientPayload handler it goes
 * out at once; otherwise it is queued for the next client pump (about 1 s); on the host it reaches its own server next tick. */
static int b_send_to_server(lua_State *L) {
    lce_script *s = self(L);
    const char *c = luaL_checkstring(L, 1);
    size_t n = 0; const char *d = luaL_checklstring(L, 2, &n);
    if (n > 32000) return luaL_error(L, "send_to_server: at most 32000 bytes");
    lua_pushinteger(L, API_HAS(s, send_to_server) ? s->api->send_to_server(s->ctx, c, d, (uint32_t)n) : LCE_E_UNSUPPORTED); return 1;
}
#endif
static int b_resolve_item(lua_State *L) {
    lce_script *s = self(L);
    const char *key = luaL_checkstring(L, 1);
    lua_pushinteger(L, s->api->resolve_item ? s->api->resolve_item(s->ctx, key) : -1); return 1;
}
static int b_resolve_block(lua_State *L) {
    lce_script *s = self(L);
    const char *key = luaL_checkstring(L, 1);
    lua_pushinteger(L, s->api->resolve_block ? s->api->resolve_block(s->ctx, key) : -1); return 1;
}
#ifdef LCE_HAVE_CONFIG_1_8
/* lce.config(key [, default [, min, max]]) -> value (API 1.8, /data/lce-mods/<modid>/config; any phase, also while main.lua
 * registers). The default's type picks the lookup: integer -> config_int, number -> config_double, boolean -> 0/1 integer,
 * string -> config_string; a missing key, a wrong type or a value outside [min, max] returns the default. Without a
 * default: the raw value (integer, number or string) or nil. */
static int b_config(lua_State *L) {
    lce_script *s = self(L);
    const char *k = luaL_checkstring(L, 1);
    int t = lua_type(L, 2);
    int ranged = !lua_isnoneornil(L, 3) || !lua_isnoneornil(L, 4);
    if (t == LUA_TNONE || t == LUA_TNIL) {
        lce_nbt_value v; memset(&v, 0, sizeof v);
        if (!API_HAS(s, config_get) || s->api->config_get(s->ctx, k, &v) != LCE_OK) { lua_pushnil(L); return 1; }
        if (v.type == LCE_NBT_INT) lua_pushinteger(L, (lua_Integer)v.i);
        else if (v.type == LCE_NBT_DOUBLE) lua_pushnumber(L, v.d);
        else if (v.type == LCE_NBT_STRING) { v.s[sizeof v.s - 1] = 0; lua_pushstring(L, v.s); }
        else lua_pushnil(L);
        return 1;
    }
    if (t == LUA_TBOOLEAN) {
        int def = lua_toboolean(L, 2);
        lua_pushboolean(L, API_HAS(s, config_int) ? s->api->config_int(s->ctx, k, def, 0, 1) != 0 : def);
        return 1;
    }
    if (t == LUA_TSTRING) {
        const char *def = lua_tostring(L, 2);
        const char *v = API_HAS(s, config_string) ? s->api->config_string(s->ctx, k, def) : def;
        lua_pushstring(L, v ? v : def); return 1;
    }
    if (t != LUA_TNUMBER) return luaL_error(L, "config: default must be an integer, number, boolean or string");
    if (lua_isinteger(L, 2)) {
        lua_Integer def = lua_tointeger(L, 2);
        int64_t lo = ranged ? (int64_t)luaL_optinteger(L, 3, LUA_MININTEGER) : 1, hi = ranged ? (int64_t)luaL_optinteger(L, 4, LUA_MAXINTEGER) : 0;
        lua_pushinteger(L, API_HAS(s, config_int) ? (lua_Integer)s->api->config_int(s->ctx, k, (int64_t)def, lo, hi) : def);
        return 1;
    }
    double def = lua_tonumber(L, 2);
    double lo = ranged ? luaL_optnumber(L, 3, -HUGE_VAL) : 1, hi = ranged ? luaL_optnumber(L, 4, HUGE_VAL) : 0;
    lua_pushnumber(L, API_HAS(s, config_double) ? s->api->config_double(s->ctx, k, def, lo, hi) : def);
    return 1;
}
#endif
static int b_translate(lua_State *L) {
    lce_script *s = self(L);
    const char *key = luaL_checkstring(L, 1);
    const char *v = s->api->translate ? s->api->translate(s->ctx, key) : key;
    lua_pushstring(L, v ? v : key); return 1;
}
static int b_log(lua_State *L) {
    lce_script *s = self(L);
    int n = lua_gettop(L);
    luaL_Buffer b; luaL_buffinit(L, &b);
    for (int i = 1; i <= n; ++i) {
        size_t l; const char *str = luaL_tolstring(L, i, &l);
        luaL_addlstring(&b, str, l); lua_pop(L, 1);
        if (i < n) luaL_addchar(&b, '\t');
    }
    luaL_pushresult(&b);
    if (s->cfg.on_log) s->cfg.on_log(s->cfg.host_user, LCE_LOG_INFO, lua_tostring(L, -1));
    else if (s->api->log) s->api->log(s->ctx, LCE_LOG_INFO, lua_tostring(L, -1));
    return 0;
}

/* text-only load (no bytecode) */
static int b_load(lua_State *L) {
    size_t len; const char *src = luaL_checklstring(L, 1, &len);
    const char *name = luaL_optstring(L, 2, "=(load)");
    int rc = luaL_loadbufferx(L, src, len, name, "t");   /* mode "t": text only */
    if (rc == LUA_OK) return 1;
    lua_pushnil(L); lua_insert(L, -2); return 2;
}

/* ---- Workshop compat shim (global register*) ------------------------------- */
/* registerItem(id, name, texture, def) */
static int w_register_item(lua_State *L) {
    lce_script *s = self(L);
    const char *id = luaL_checkstring(L, 1);
    lce_item_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d;
    d.key = id; d.name = luaL_optstring(L, 2, id); d.texture = luaL_optstring(L, 3, NULL);
    d.max_stack = 64; d.kind = LCE_ITEM_GENERIC;
    lce_status rc = s->api->register_item ? s->api->register_item(s->ctx, &d) : LCE_E_UNSUPPORTED;
    if (rc == LCE_OK) ++s->st.items; else ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK); return 1;
}
static int w_register_block(lua_State *L) {
    lce_script *s = self(L);
    const char *id = luaL_checkstring(L, 1);
    lce_block_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d;
    d.key = id; d.name = luaL_optstring(L, 2, id); d.texture[0] = luaL_optstring(L, 3, NULL);
    d.hardness_milli = 1500;
    if (lua_istable(L, 4)) {
        lua_getfield(L, 4, "hardness");
        if (lua_isnumber(L, -1)) d.hardness_milli = (uint32_t)(1000.0 * lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    lce_status rc = s->api->register_block ? s->api->register_block(s->ctx, &d) : LCE_E_UNSUPPORTED;
    if (rc == LCE_OK) ++s->st.blocks; else ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK); return 1;
}
/* registerShapedRecipe(id, pattern{rows}, key{sym=item}, result, opts{count,group}) */
static int w_register_shaped(lua_State *L) {
    lce_script *s = self(L);
    const char *id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE); luaL_checktype(L, 3, LUA_TTABLE);
    const char *result = luaL_checkstring(L, 4);
    lce_recipe_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d;
    d.key = id; d.result = result; d.result_count = (uint16_t)int_field(L, 5, "count", 1);
    for (int i = 0; i < 9; ++i) d.cells[i].aux = -1;
    int rows = (int)luaL_len(L, 2); if (rows > 3) rows = 3;
    int width = 0;
    for (int r = 0; r < rows; ++r) {
        lua_geti(L, 2, r + 1);
        const char *row = lua_tostring(L, -1);
        int c = 0;
        for (; row && row[c] && c < 3; ++c) {
            if (row[c] != ' ') {
                char sym[2] = {row[c], 0};
                lua_getfield(L, 3, sym);
                if (lua_isstring(L, -1)) d.cells[r * 3 + c].item = lua_tostring(L, -1);
                lua_pop(L, 1);
            }
        }
        if (c > width) width = c;
        lua_pop(L, 1);
    }
    /* compact to width*height row-major */
    lce_ingredient packed[9]; for (int i = 0; i < 9; ++i) { packed[i].item = NULL; packed[i].aux = -1; }
    for (int r = 0; r < rows; ++r) for (int c = 0; c < width; ++c) packed[r * width + c] = d.cells[r * 3 + c];
    memcpy(d.cells, packed, sizeof packed);
    d.width = (uint8_t)(width ? width : 1); d.height = (uint8_t)(rows ? rows : 1);
    lce_status rc = s->api->register_recipe ? s->api->register_recipe(s->ctx, &d) : LCE_E_UNSUPPORTED;
    if (rc == LCE_OK) ++s->st.recipes; else ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK); return 1;
}
/* registerShapelessRecipe(id, {ingredients}, result, opts{count}) */
static int w_register_shapeless(lua_State *L) {
    lce_script *s = self(L);
    const char *id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    const char *result = luaL_checkstring(L, 3);
    lce_recipe_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d;
    d.key = id; d.result = result; d.result_count = (uint16_t)int_field(L, 4, "count", 1);
    d.shapeless = 1;
    for (int i = 0; i < 9; ++i) d.cells[i].aux = -1;
    int n = (int)luaL_len(L, 2); if (n > 9) n = 9;
    for (int i = 0; i < n; ++i) { lua_geti(L, 2, i + 1); if (lua_isstring(L, -1)) d.cells[i].item = lua_tostring(L, -1); lua_pop(L, 1); }
    d.width = (uint8_t)(n ? n : 1); d.height = 1;
    lce_status rc = s->api->register_recipe ? s->api->register_recipe(s->ctx, &d) : LCE_E_UNSUPPORTED;
    if (rc == LCE_OK) ++s->st.recipes; else ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK); return 1;
}
/* registerSmeltingRecipe(id, input, result, opts{xp,count}) */
static int w_register_smelting(lua_State *L) {
    lce_script *s = self(L);
    const char *id = luaL_checkstring(L, 1);
    const char *input = luaL_checkstring(L, 2);
    const char *result = luaL_checkstring(L, 3);
    lce_smelt_desc d; memset(&d, 0, sizeof d); d.struct_size = sizeof d;
    d.key = id; d.input.item = input; d.input.aux = -1; d.result = result;
    d.result_count = (uint16_t)int_field(L, 4, "count", 1);
    d.xp_milli = (uint32_t)int_field(L, 4, "xp", 0);
    lce_status rc = s->api->register_smelting ? s->api->register_smelting(s->ctx, &d) : LCE_E_UNSUPPORTED;
    if (rc == LCE_OK) ++s->st.smeltings; else ++s->st.denied;
    lua_pushboolean(L, rc == LCE_OK); return 1;
}
/* registerEvent(name, callback) -> subscribe */
static int w_register_event(lua_State *L) { return b_subscribe(L); }
/* Unsupported Workshop calls: record and no-op (tier/material/ore/tree/sapling). */
static int w_unsupported(lua_State *L) { self(L)->st.denied++; lua_pushboolean(L, 0); return 1; }

/* ---- library setup --------------------------------------------------------- */
static void open_safe_libs(lua_State *L) {
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_COLIBNAME, luaopen_coroutine},
        {NULL, NULL},
    };
    for (const luaL_Reg *lib = libs; lib->func; ++lib) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    /* Strip dangerous globals from base. */
    static const char *strip[] = {"dofile", "loadfile", "require", "collectgarbage", "print", "loadstring", NULL};
    for (const char **g = strip; *g; ++g) { lua_pushnil(L); lua_setglobal(L, *g); }
    /* Replace load with text-only. */
    lua_pushcfunction(L, b_load); lua_setglobal(L, "load");
}

static void install_bindings(lua_State *L) {
    lua_newtable(L);                                   /* lce */
    static const luaL_Reg reg[] = {
        {"register_texture", b_register_texture},
        {"register_fuel", b_register_fuel},
        {"register_feature", b_register_feature},
        {"register_biome", b_register_biome}, {"register_structure", b_register_structure},
        {"register_item", b_register_item}, {"register_block", b_register_block},
        {"register_recipe", b_register_recipe}, {"register_smelting", b_register_smelting},
        {"subscribe", b_subscribe}, {"send_chat", b_send_chat}, {"give_item", b_give_item},
        {"subscribe_tick", b_subscribe_tick}, {"tick_stats", b_tick_stats},
        {"translate", b_translate}, {"log", b_log}, {"drop_item", b_drop_item}, {"add_effect", b_add_effect},
        {"resolve_item", b_resolve_item}, {"resolve_block", b_resolve_block},
        {"register_entity", b_register_entity},
        {"set_block", b_set_block}, {"get_block", b_get_block}, {"spawn_entity", b_spawn_entity}, {"play_sound", b_play_sound},
        {"explode", b_explode}, {"set_fire", b_set_fire}, {"heal", b_heal}, {"damage_entity", b_damage_entity},
        {"damage_held_item", b_damage_held}, {"consume_held_item", b_consume_held}, {"teleport", b_teleport},
        {"set_motion", b_set_motion},
#ifdef LCE_HAVE_SVC_1_3
        {"spawn_particles", b_spawn_particles},
#endif
#ifdef LCE_HAVE_TINTED_PARTICLES_1_12
        {"spawn_tinted_particles", b_spawn_tinted_particles},
#endif
#ifdef LCE_HAVE_NET_1_4
        {"register_channel", b_register_channel}, {"send_payload", b_send_payload},
#endif
#ifdef LCE_HAVE_NET_1_6
        {"send_to_server", b_send_to_server},
#endif
#ifdef LCE_HAVE_NBT_1_4
        {"item_tag_get", b_item_tag_get}, {"item_tag_set", b_item_tag_set}, {"item_tag_remove", b_item_tag_remove},
#endif
#ifdef LCE_HAVE_CONFIG_1_8
        {"config", b_config},
#endif
#ifdef LCE_HAVE_ENTITY_NBT_1_5
        {"entity_nbt_get", b_entity_nbt_get}, {"entity_data_get", b_entity_data_get}, {"entity_data_set", b_entity_data_set},
        {"entity_data_remove", b_entity_data_remove},
#endif
        {NULL, NULL},
    };
    luaL_setfuncs(L, reg, 0);
    /* lce.EV.<LCE_EV name> = the event name lce.subscribe accepts (used by generated ports). */
    static const struct { const char *c, *lua; } evn[] = {
        {"SERVER_TICK", "ServerTick"}, {"CLIENT_TICK", "ClientTick"}, {"WORLD_LOAD", "WorldLoad"}, {"WORLD_UNLOAD", "WorldUnload"},
        {"BLOCK_BREAK", "BlockBreak"}, {"BLOCK_PLACE", "BlockPlace"}, {"ITEM_USE", "ItemUse"}, {"ENTITY_DAMAGE", "EntityDamage"},
        {"ENTITY_DEATH", "EntityDeath"}, {"PLAYER_JOIN", "PlayerJoin"}, {"PLAYER_LEAVE", "PlayerLeave"}, {"CHAT", "Chat"},
        {"BLOCK_USE", "BlockUse"}, {"ENTITY_INTERACT", "EntityInteract"}, {"BLOCK_TICK", "BlockTick"}, {"ENTITY_TICK", "EntityTick"},
        {"EFFECT_TICK", "EffectTick"}, {"ITEM_INVENTORY_TICK", "InventoryTick"}, {"BLOCK_NEIGHBOR", "BlockNeighbor"},
        {"BLOCK_ENTITY_INSIDE", "BlockEntityInside"}, {"BLOCK_STEP_ON", "BlockStepOn"},
#ifdef LCE_HAVE_NET_1_4
        {"CUSTOM_PAYLOAD", "CustomPayload"},
#endif
#ifdef LCE_HAVE_NET_1_6
        {"CLIENT_PAYLOAD", "ClientPayload"},
#endif
#ifdef LCE_HAVE_PROJECTILES_1_9
        {"PROJECTILE_HIT", "ProjectileHit"},
#endif
    };
    lua_newtable(L);
    for (unsigned i = 0; i < sizeof evn / sizeof evn[0]; ++i) { lua_pushstring(L, evn[i].lua); lua_setfield(L, -2, evn[i].c); }
    lua_setfield(L, -2, "EV");
    lua_setglobal(L, "lce");
    lua_pushcfunction(L, b_log); lua_setglobal(L, "print");

    static const luaL_Reg shim[] = {
        {"registerItem", w_register_item}, {"registerBlock", w_register_block},
        {"registerShapedRecipe", w_register_shaped}, {"registerShapelessRecipe", w_register_shapeless},
        {"registerSmeltingRecipe", w_register_smelting}, {"registerEvent", w_register_event},
        {"registerItemTier", w_unsupported}, {"registerArmorMaterial", w_unsupported},
        {"registerSapling", w_unsupported}, {"registerOreFeature", w_unsupported},
        {"registerTreeFeature", w_unsupported}, {NULL, NULL},
    };
    for (const luaL_Reg *f = shim; f->func; ++f) { lua_pushcfunction(L, f->func); lua_setglobal(L, f->name); }
    /* Registry subtable for event callbacks. */
    lua_newtable(L); lua_setfield(L, LUA_REGISTRYINDEX, "lce.cb");
}

/* ---- lifecycle ------------------------------------------------------------- */
lce_script *lce_script_new(const lce_api *api, lce_mod_ctx *ctx, const lce_script_config *cfg) {
    if (!api) return NULL;
    lce_script *s = (lce_script *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->api = api; s->ctx = ctx;
    if (cfg) s->cfg = *cfg;
    s->mem_cap = s->cfg.memory_cap ? s->cfg.memory_cap : LCE_DEFAULT_MEM;
    s->budget = s->cfg.step_budget ? s->cfg.step_budget : LCE_DEFAULT_STEP;
    s->interval = s->cfg.hook_interval ? s->cfg.hook_interval : LCE_DEFAULT_HOOK;
    s->L = lua_newstate(l_alloc, s);
    if (!s->L) { free(s); return NULL; }
    *(lce_script **)lua_getextraspace(s->L) = s;
    lua_sethook(s->L, l_hook, LUA_MASKCOUNT, (int)s->interval);
    open_safe_libs(s->L);
    install_bindings(s->L);
    return s;
}
void lce_script_free(lce_script *s) { if (!s) return; if (s->L) lua_close(s->L); free(s); }

lce_status lce_script_run_main(lce_script *s, const char *name, const char *source, size_t len,
                               char *err, uint32_t err_cap) {
    if (!s || !source) return LCE_E_INVALID;
    if (s->dead) { if (err && err_cap) snprintf(err, err_cap, "script is dead"); return LCE_E_INVALID; }
    lua_State *L = s->L;
    char chunk[80];
    snprintf(chunk, sizeof chunk, "=%s", name ? name : "main.lua");
    if (luaL_loadbufferx(L, source, len, chunk, "t") != LUA_OK) {
        if (err && err_cap) snprintf(err, err_cap, "%s", lua_tostring(L, -1));
        ++s->st.errors; lua_pop(L, 1); return LCE_E_INVALID;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        if (err && err_cap) snprintf(err, err_cap, "%s", lua_tostring(L, -1));
        ++s->st.errors; lua_pop(L, 1);
        return LCE_E_INVALID;
    }
    if (err && err_cap) err[0] = 0;
    return LCE_OK;
}

/* Push a Lua table describing the payload for a given event. */
static void push_payload(lua_State *L, lce_event ev, const void *p) {
    lua_newtable(L);
    switch ((int)ev) {   /* int: the API 1.3 ids may still be provisional values outside the enum */
    case LCE_EV_SERVER_TICK: case LCE_EV_CLIENT_TICK: {
        const lce_ev_tick *t = (const lce_ev_tick *)p;
        lua_pushinteger(L, t ? (lua_Integer)t->tick : 0); lua_setfield(L, -2, "tick"); break;
        /* (C struct field names are also set below so generated ports can use ev.<field>) */
    }
    case LCE_EV_BLOCK_BREAK: case LCE_EV_BLOCK_PLACE: case LCE_EV_BLOCK_USE: case LCE_EV_BLOCK_TICK:
    case LCE_EV_BLOCK_NEIGHBOR: case LCE_EV_BLOCK_ENTITY_INSIDE: case LCE_EV_BLOCK_STEP_ON: {
        const lce_ev_block *b = (const lce_ev_block *)p;
        if (b) { lua_pushinteger(L, b->x); lua_setfield(L, -2, "x"); lua_pushinteger(L, b->y); lua_setfield(L, -2, "y");
                 lua_pushinteger(L, b->z); lua_setfield(L, -2, "z"); lua_pushinteger(L, b->block_id); lua_setfield(L, -2, "block");
                 lua_pushinteger(L, b->block_id); lua_setfield(L, -2, "block_id"); lua_pushinteger(L, b->aux); lua_setfield(L, -2, "aux");
                 if (b->player) { lua_pushlightuserdata(L, b->player); lua_setfield(L, -2, "player");
                     if ((int)ev == LCE_EV_BLOCK_ENTITY_INSIDE || (int)ev == LCE_EV_BLOCK_STEP_ON) {   /* any entity */
                         lua_pushlightuserdata(L, b->player); lua_setfield(L, -2, "entity"); } }
                 if (b->struct_size >= offsetof(lce_ev_block, held_item_id) + sizeof b->held_item_id) {   /* API 1.2 context */
                     if (b->level) { lua_pushlightuserdata(L, b->level); lua_setfield(L, -2, "level"); }
                     lua_pushinteger(L, b->face); lua_setfield(L, -2, "face");
                     lua_pushinteger(L, b->held_item_id); lua_setfield(L, -2, "held_item_id");
                 } }
        break;
    }
    case LCE_EV_ITEM_USE: case LCE_EV_ITEM_INVENTORY_TICK: {
        const lce_ev_item_use *u = (const lce_ev_item_use *)p;
        if (u) { lua_pushinteger(L, u->item_id); lua_setfield(L, -2, "item"); lua_pushinteger(L, u->item_id); lua_setfield(L, -2, "item_id");
                 lua_pushinteger(L, u->aux); lua_setfield(L, -2, "aux");
                 if (u->player) { lua_pushlightuserdata(L, u->player); lua_setfield(L, -2, "player"); }
                 if (u->struct_size >= offsetof(lce_ev_item_use, hand) + sizeof u->hand) {
                     if (u->level) { lua_pushlightuserdata(L, u->level); lua_setfield(L, -2, "level"); }
                     lua_pushinteger(L, u->x); lua_setfield(L, -2, "x"); lua_pushinteger(L, u->y); lua_setfield(L, -2, "y");
                     lua_pushinteger(L, u->z); lua_setfield(L, -2, "z"); lua_pushinteger(L, u->hand); lua_setfield(L, -2, "hand");
                 }
                 if ((int)ev == LCE_EV_ITEM_INVENTORY_TICK) {   /* aux = slot; hand 0 = selected */
                     lua_pushinteger(L, u->aux); lua_setfield(L, -2, "slot");
                     lua_pushboolean(L, u->hand == 0); lua_setfield(L, -2, "selected");
                     if (u->player) { lua_pushlightuserdata(L, u->player); lua_setfield(L, -2, "entity"); }
                 } }
        break;
    }
#ifdef LCE_HAVE_NET_1_4
#ifdef LCE_HAVE_NET_1_6
    case LCE_EV_CLIENT_PAYLOAD:     /* this client received ev.data on ev.channel from the server (no ev.player) */
#endif
    case LCE_EV_CUSTOM_PAYLOAD: {   /* ev.channel, ev.data (byte string), ev.player */
        const lce_ev_payload *m = (const lce_ev_payload *)p;
        if (m && m->channel) { lua_pushstring(L, m->channel); lua_setfield(L, -2, "channel"); }
        if (m && (m->data || !m->len)) { lua_pushlstring(L, m->len ? (const char *)m->data : "", m->len); lua_setfield(L, -2, "data"); }
        if (m && m->player) { lua_pushlightuserdata(L, m->player); lua_setfield(L, -2, "player"); }
        break;
    }
#endif
    case LCE_EV_PLAYER_JOIN: case LCE_EV_PLAYER_LEAVE: {
        const lce_ev_player *pl = (const lce_ev_player *)p;
        if (pl && pl->name) { lua_pushstring(L, pl->name); lua_setfield(L, -2, "name"); }
        if (pl && pl->player) { lua_pushlightuserdata(L, pl->player); lua_setfield(L, -2, "player"); }
        break;
    }
    case LCE_EV_CHAT: {
        const lce_ev_chat *c = (const lce_ev_chat *)p;
        if (c && c->message) { lua_pushstring(L, c->message); lua_setfield(L, -2, "message"); }
        if (c && c->player) { lua_pushlightuserdata(L, c->player); lua_setfield(L, -2, "player"); }
        break;
    }
    case LCE_EV_EFFECT_TICK: {   /* custom status effect tick: entity, amplifier, effect id, source = effect key */
        const lce_ev_entity *e = (const lce_ev_entity *)p;
        if (e) { lua_pushinteger(L, (lua_Integer)(e->amount_milli / 1000u)); lua_setfield(L, -2, "amplifier");
                 lua_pushinteger(L, e->type_id); lua_setfield(L, -2, "effect_id");
                 if (e->entity) { lua_pushlightuserdata(L, e->entity); lua_setfield(L, -2, "entity"); }
                 if (e->source) { lua_pushstring(L, (const char *)e->source); lua_setfield(L, -2, "effect"); } }
        break;
    }
#ifdef LCE_HAVE_PROJECTILES_1_9
    case LCE_EV_PROJECTILE_HIT: {   /* entity = what was hit (nil: a block), projectile, item_id = the thrown item, impact x/y/z */
        const lce_ev_entity *e = (const lce_ev_entity *)p;
        if (e) { lua_pushinteger(L, e->type_id); lua_setfield(L, -2, "item_id");
                 lua_pushnumber(L, e->amount_milli / 1000.0); lua_setfield(L, -2, "damage");
                 if (e->entity) { lua_pushlightuserdata(L, e->entity); lua_setfield(L, -2, "entity"); }
                 if (e->source) { lua_pushlightuserdata(L, e->source); lua_setfield(L, -2, "projectile"); }
                 if (e->level) { lua_pushlightuserdata(L, e->level); lua_setfield(L, -2, "level"); }
                 lua_pushinteger(L, e->x); lua_setfield(L, -2, "x"); lua_pushinteger(L, e->y); lua_setfield(L, -2, "y");
                 lua_pushinteger(L, e->z); lua_setfield(L, -2, "z"); }
        break;
    }
#endif
    case LCE_EV_ENTITY_DAMAGE: case LCE_EV_ENTITY_DEATH: case LCE_EV_ENTITY_INTERACT: case LCE_EV_ENTITY_TICK: {
        const lce_ev_entity *e = (const lce_ev_entity *)p;
        if (e) { lua_pushnumber(L, e->amount_milli / 1000.0); lua_setfield(L, -2, "amount");
                 lua_pushinteger(L, e->type_id); lua_setfield(L, -2, "type_id");
                 if (e->entity) { lua_pushlightuserdata(L, e->entity); lua_setfield(L, -2, "entity"); }
                 if (e->source) { lua_pushlightuserdata(L, e->source); lua_setfield(L, -2, "source"); }
                 if (e->struct_size >= offsetof(lce_ev_entity, attacker_item_id) + sizeof e->attacker_item_id) {
                     if (e->level) { lua_pushlightuserdata(L, e->level); lua_setfield(L, -2, "level"); }
                     lua_pushinteger(L, e->x); lua_setfield(L, -2, "x"); lua_pushinteger(L, e->y); lua_setfield(L, -2, "y");
                     lua_pushinteger(L, e->z); lua_setfield(L, -2, "z");
                     if (e->attacker) { lua_pushlightuserdata(L, e->attacker); lua_setfield(L, -2, "attacker"); }
                     lua_pushinteger(L, e->attacker_item_id); lua_setfield(L, -2, "attacker_item_id");
                 } }
        break;
    }
    default: break;
    }
}

int lce_script_event(lce_script *s, lce_event ev, const void *payload) {
    if (!s || s->dead) return 0;
    if (ev == LCE_EV_SERVER_TICK && s->tick_interval) {
        if (!lce_tick_pacing_due((const lce_ev_tick *)payload, s->tick_interval, s->tick_offset)) {
            if (s->tick_callbacks_skipped < LUA_MAXINTEGER) ++s->tick_callbacks_skipped;
            return 0;
        }
        if (s->tick_callbacks_run < LUA_MAXINTEGER) ++s->tick_callbacks_run;
    }
    lua_State *L = s->L;
    lua_getfield(L, LUA_REGISTRYINDEX, "lce.cb");
    lua_geti(L, -1, (lua_Integer)ev);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return 0; }
    push_payload(L, ev, payload);
    int cancel = 0;
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        ++s->st.errors;
        if (s->cfg.on_log) s->cfg.on_log(s->cfg.host_user, LCE_LOG_ERROR, lua_tostring(L, -1));
        lua_pop(L, 1);   /* error */
    } else {
        cancel = lua_toboolean(L, -1);
        lua_pop(L, 1);   /* result */
    }
    lua_pop(L, 1);       /* lce.cb */
    return cancel;
}

int lce_script_is_dead(const lce_script *s) { return s ? s->dead : 1; }
size_t lce_script_memory(const lce_script *s) { return s ? s->mem_used : 0; }
uint64_t lce_script_steps(const lce_script *s) { return s ? s->steps : 0; }
void lce_script_get_stats(const lce_script *s, lce_script_stats *out) { if (s && out) *out = s->st; }
