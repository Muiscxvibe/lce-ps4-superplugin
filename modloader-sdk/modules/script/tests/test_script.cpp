// Runs real Lua scripts through the sandbox against a fake lce_api.
#include "../lce_script.h"
#include "../../../loader/config_store.h"
#ifndef LCE_HAVE_EV_1_3
#define LCE_EV_ITEM_INVENTORY_TICK ((lce_event)(LCE_EV_ENTITY_TICK + 1))
#define LCE_EV_BLOCK_NEIGHBOR      ((lce_event)(LCE_EV_ENTITY_TICK + 2))
#define LCE_EV_BLOCK_ENTITY_INSIDE ((lce_event)(LCE_EV_ENTITY_TICK + 3))
#define LCE_EV_BLOCK_STEP_ON       ((lce_event)(LCE_EV_ENTITY_TICK + 4))
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <map>
#include <vector>

static int g_checks;
#define CHECK(c) do { ++g_checks; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

// ---- fake lce_api ----------------------------------------------------------
static std::vector<std::string> reg_items, reg_blocks, reg_recipes, reg_smelt, chats, subs, gives;
static lce_phase fake_phase(lce_mod_ctx *) { return LCE_PHASE_REGISTER; }
static void fake_log(lce_mod_ctx *, lce_log_level, const char *) {}
static lce_status fi(lce_mod_ctx *, const lce_item_desc *d) { reg_items.push_back(d->key ? d->key : ""); return LCE_OK; }
static std::vector<unsigned> item_tiers;
static lce_status ftier(lce_mod_ctx *, const lce_item_desc *d) {
    CHECK(d->struct_size == sizeof *d && d->key);
    item_tiers.push_back(d->tier);
    return d->tier == LCE_TIER_STONE ? LCE_E_COLLISION : LCE_OK;
}
static unsigned food_calls;
static lce_status ffood(lce_mod_ctx *, const lce_item_desc *d) {
    ++food_calls;
    CHECK(d->struct_size == sizeof *d && d->key);
    if (food_calls == 1) CHECK(d->kind == LCE_ITEM_GENERIC && !d->food_hunger && !d->food_saturation_milli && !d->use_ticks && !(d->flags & LCE_ITEM_FLAG_FOOD_ALWAYS));
    if (food_calls == 2) CHECK(d->kind == LCE_ITEM_FOOD && d->food_hunger == 4 && d->food_saturation_milli == 300 && d->use_ticks == 16 && (d->flags & LCE_ITEM_FLAG_FOOD_ALWAYS));
    if (food_calls == 3) CHECK(d->kind == LCE_ITEM_FOOD && d->food_hunger == 4 && d->use_ticks == 32 && !(d->flags & LCE_ITEM_FLAG_FOOD_ALWAYS));
    return food_calls == 3 ? LCE_E_COLLISION : LCE_OK;
}
static std::map<std::string, uint32_t> inline_fuels;
static unsigned inline_item_calls;
static lce_status ffitem(lce_mod_ctx *, const lce_item_desc *d) {
    ++inline_item_calls;
    CHECK(d->struct_size == sizeof *d && d->key);
    if (inline_fuels.count(d->key)) return LCE_E_COLLISION;
    inline_fuels[d->key] = d->fuel_ticks;
    return LCE_OK;
}
static lce_status fb(lce_mod_ctx *, const lce_block_desc *d) { reg_blocks.push_back(d->key ? d->key : ""); return LCE_OK; }
static lce_status fr(lce_mod_ctx *, const lce_recipe_desc *d) {
    char b[128]; snprintf(b, sizeof b, "%s->%s x%u %s w%u h%u", d->key, d->result, d->result_count, d->shapeless?"less":"shaped", d->width, d->height);
    reg_recipes.push_back(b); return LCE_OK; }
static lce_status fs(lce_mod_ctx *, const lce_smelt_desc *d) { reg_smelt.push_back(std::string(d->key)+":"+d->input.item+"->"+d->result); return LCE_OK; }
static std::vector<uint32_t> fuel_ticks;
static lce_status ffuel(lce_mod_ctx *, const lce_fuel_desc *d) {
    CHECK(d->struct_size == sizeof *d && !strcmp(d->item, "m:ember"));
    fuel_ticks.push_back(d->burn_ticks);
    return d->burn_ticks == 200 ? LCE_E_COLLISION : LCE_OK;
}
static int feature_calls;
static lce_status ffeature(lce_mod_ctx *, const lce_feature_desc *d) {
    ++feature_calls;
    CHECK(d->struct_size == sizeof *d && !strcmp(d->key, "m:ore") && d->kind == LCE_FEATURE_ORE);
    CHECK(!strcmp(d->block, "m:ruby_ore") && !strcmp(d->block2, "minecraft:stone"));
    CHECK(d->size == 8 && d->per_chunk == 4 && d->min_y == 5 && d->max_y == 60 && d->rarity == 2);
    CHECK(d->biome_count == 2 && !strcmp(d->biomes[0], "minecraft:plains") && !strcmp(d->biomes[1], "minecraft:forest"));
    return LCE_OK;
}
static lce_status fsub(lce_mod_ctx *, lce_event ev, lce_event_fn, void *) { subs.push_back(std::to_string(ev)); return LCE_OK; }
static lce_status fchat(lce_mod_ctx *, void *, const char *m) { chats.push_back(m ? m : ""); return LCE_OK; }
static lce_status fgive(lce_mod_ctx *, void *, const char *item, uint32_t n, int32_t) { gives.push_back(std::string(item)+"x"+std::to_string(n)); return LCE_OK; }
static std::vector<std::string> svc12;
static lce_status fsb(lce_mod_ctx *, void *, int32_t x, int32_t y, int32_t z, const char *b, uint32_t d) { svc12.push_back("set " + std::to_string(x)+","+std::to_string(y)+","+std::to_string(z)+"="+b+"@"+std::to_string(d)); return LCE_OK; }
static int32_t fgb(lce_mod_ctx *, void *, int32_t, int32_t, int32_t) { return 1 | (2 << 12); }
static lce_status fsnd(lce_mod_ctx *, void *, int32_t, int32_t, int32_t, const char *n) { svc12.push_back(std::string("sound ")+n); return LCE_OK; }
static lce_status fboom(lce_mod_ctx *, void *, double x, double, double, float r, int f, int d) { char b[64]; snprintf(b,sizeof b,"boom %.1f r%.1f f%d d%d",x,r,f,d); svc12.push_back(b); return LCE_OK; }
static lce_status ffire(lce_mod_ctx *, void *, uint32_t t) { svc12.push_back("fire "+std::to_string(t)); return LCE_OK; }
static lce_status fheal(lce_mod_ctx *, void *, uint32_t m) { svc12.push_back("heal "+std::to_string(m)); return LCE_OK; }
static lce_status fcons(lce_mod_ctx *, void *, uint32_t n) { svc12.push_back("consume "+std::to_string(n)); return LCE_OK; }
static const char *ftr(lce_mod_ctx *, const char *key) { return !strcmp(key, "m:ruby.name") ? "Ruby" : key; }

static lce_api make_api() {
    lce_api a; memset(&a, 0, sizeof a); a.struct_size = sizeof a; a.version = LCE_MOD_API_VERSION;
    a.phase = fake_phase; a.log = fake_log;
    a.register_item = fi; a.register_block = fb; a.register_recipe = fr; a.register_smelting = fs;
    a.subscribe = fsub; a.send_chat = fchat; a.give_item = fgive; a.translate = ftr;
    return a;
}
static int run(lce_script *s, const char *src, char *err, uint32_t cap) {
    return lce_script_run_main(s, "main.lua", src, strlen(src), err, cap);
}

int main() {
    lce_api api = make_api();

    { lce_api a = api; a.register_item = ftier;
      lce_script *s = lce_script_new(&a, nullptr, nullptr); CHECK(s); char err[256];
      CHECK(run(s, "local ok,rc=lce.register_item{key='m:default'}; assert(ok and rc==0); for _,v in ipairs({1,3,4,5}) do local a,b=lce.register_item{key='m:tool',kind=3,max_stack=1,tier=v}; assert(a and b==0) end; local a,b=lce.register_item{key='m:collision',kind=3,max_stack=1,tier=2}; assert(not a and b~=0)", err, sizeof err) == LCE_OK);
      CHECK(item_tiers.size()==6 && item_tiers[0]==0 && item_tiers[1]==1 && item_tiers[2]==3 && item_tiers[3]==4 && item_tiers[4]==5 && item_tiers[5]==2);
      CHECK(run(s, "for _,v in ipairs({-1,6,65536,1.5,'bad',true}) do assert(not pcall(function() lce.register_item{key='m:invalid',tier=v} end)) end", err, sizeof err) == LCE_OK);
      CHECK(item_tiers.size()==6); lce_script_free(s);
    }

    { lce_api a = api; a.register_item = ffood;
      lce_script *s = lce_script_new(&a, nullptr, nullptr); CHECK(s); char err[256];
      CHECK(run(s, "assert(lce.register_item{key='m:generic'}); local ok,rc=lce.register_item{key='m:food',kind=1,food_hunger=4,food_saturation_milli=300,use_ticks=16,always_edible=true}; assert(ok and rc==0); ok,rc=lce.register_item{key='m:collision',kind=1,food_hunger=4,use_ticks=32,always_edible=false}; assert(not ok and rc~=0)", err, sizeof err) == LCE_OK);
      CHECK(food_calls == 3);
      CHECK(run(s, "for _,f in ipairs({'food_hunger','food_saturation_milli','use_ticks'}) do for _,v in ipairs({-1,1.5,'bad',true}) do local t={key='m:bad'}; t[f]=v; assert(not pcall(lce.register_item,t)) end end; for _,f in ipairs({'food_hunger','food_saturation_milli'}) do local t={key='m:bad'};t[f]=65536;assert(not pcall(lce.register_item,t)) end; assert(not pcall(lce.register_item,{key='m:bad',use_ticks=4294967296})); assert(not pcall(lce.register_item,{key='m:bad',always_edible=1}))", err, sizeof err) == LCE_OK);
      CHECK(food_calls == 3); lce_script_free(s);
    }

    { lce_api a = api; a.register_item = ffitem; a.register_fuel = ffuel;
      lce_script *s = lce_script_new(&a, nullptr, nullptr); CHECK(s); char err[256];
      CHECK(run(s, "for _,v in ipairs({{key='m:absent'},{key='m:zero',fuel_ticks=0},{key='m:min',fuel_ticks=1},{key='m:max',fuel_ticks=72000}}) do local ok,rc=lce.register_item(v); assert(ok and rc==0) end", err, sizeof err) == LCE_OK);
      CHECK(inline_fuels.size()==4 && inline_fuels["m:absent"]==0 && inline_fuels["m:zero"]==0 && inline_fuels["m:min"]==1 && inline_fuels["m:max"]==72000);
      CHECK(run(s, "for _,v in ipairs({-1,72001,1.5,'abc',true}) do assert(not pcall(function() lce.register_item{key='m:invalid',fuel_ticks=v} end)) end", err, sizeof err) == LCE_OK);
      CHECK(inline_item_calls==4 && inline_fuels.count("m:invalid")==0 && fuel_ticks.empty());
      CHECK(run(s, "local ok,rc=lce.register_item{key='m:min',fuel_ticks=2}; assert(not ok and rc~=0)", err, sizeof err) == LCE_OK);
      CHECK(inline_item_calls==5 && inline_fuels["m:min"]==1 && fuel_ticks.empty());
      lce_script_free(s);
    }

    { lce_api a = api; a.register_fuel = ffuel;
      lce_script *s = lce_script_new(&a, nullptr, nullptr); CHECK(s); char err[256];
      CHECK(run(s, "for _,v in ipairs({1,72000}) do local ok,rc=lce.register_fuel{item='m:ember',burn_ticks=v}; assert(ok and rc==0) end; local ok,rc=lce.register_fuel{item='m:ember',burn_ticks=200}; assert(not ok and rc~=0)", err, sizeof err) == LCE_OK);
      CHECK(fuel_ticks.size()==3 && fuel_ticks[0]==1 && fuel_ticks[1]==72000 && fuel_ticks[2]==200);
      CHECK(run(s, "for _,v in ipairs({0,-1,72001,1.5}) do assert(not pcall(function() lce.register_fuel{item='m:ember',burn_ticks=v} end)) end; assert(not pcall(function() lce.register_fuel{burn_ticks=1} end))", err, sizeof err) == LCE_OK);
      CHECK(fuel_ticks.size()==3); lce_script_free(s);
      a.struct_size = offsetof(lce_api, register_fuel); s = lce_script_new(&a, nullptr, nullptr); CHECK(s);
      CHECK(run(s, "local ok,rc=lce.register_fuel{item='m:ember',burn_ticks=1}; assert(not ok and rc~=0)", err, sizeof err) == LCE_OK);
      CHECK(fuel_ticks.size()==3); lce_script_free(s);
    }

    { lce_api a = api; a.register_feature = ffeature;
      lce_script *s = lce_script_new(&a, nullptr, nullptr); CHECK(s); char err[256];
      CHECK(run(s, "local ok, rc = lce.register_feature{key='m:ore', kind=1, block='m:ruby_ore', block2='minecraft:stone', size=8, per_chunk=4, min_y=5, max_y=60, rarity=2, biomes={'minecraft:plains','minecraft:forest'}}; assert(ok and rc==0)", err, sizeof err) == LCE_OK);
      CHECK(feature_calls == 1);
      CHECK(run(s, "assert(not pcall(function() lce.register_feature{key='m:ore', size=65536} end)); assert(not pcall(function() lce.register_feature{key='m:ore', biomes={123}} end))", err, sizeof err) == LCE_OK);
      CHECK(feature_calls == 1); lce_script_free(s);
      a.register_feature = nullptr; s = lce_script_new(&a, nullptr, nullptr); CHECK(s);
      CHECK(run(s, "local ok = lce.register_feature{key='m:ore', kind=1}; assert(not ok)", err, sizeof err) == LCE_OK);
      lce_script_free(s);
    }

    // ---- 1. native lce.* bindings + Workshop shim both register content ----
    { lce_script *s = lce_script_new(&api, nullptr, nullptr); CHECK(s);
      char err[256];
      const char *src =
        "lce.register_item{key='m:ruby', name='Ruby', max_stack=64}\n"
        "registerItem('m:gem', 'Gem', 'res/gem.png')\n"
        "registerBlock('m:rubyblock', 'Ruby Block', 'res/rb.png', {hardness=5.0})\n"
        "registerShapedRecipe('m:rb', {'SSS','SSS','SSS'}, {S='m:ruby'}, 'm:rubyblock', {count=1})\n"
        "registerShapelessRecipe('m:r9', {'m:rubyblock'}, 'm:ruby', {count=9})\n"
        "registerSmeltingRecipe('m:smelt', 'm:rawruby', 'm:ruby', {xp=700})\n"
        "lce.subscribe('ServerTick', function(e) end)\n"
        "registerEvent('PlayerJoinEvent', function(e) lce.send_chat('welcome '..(e.name or '?')) end)\n"
        "registerItemTier('x', {})\n";   // unsupported -> denied, no crash
      CHECK(run(s, src, err, sizeof err) == LCE_OK);
      CHECK(reg_items.size() == 2 && reg_items[0] == "m:ruby" && reg_items[1] == "m:gem");
      CHECK(reg_blocks.size() == 1 && reg_blocks[0] == "m:rubyblock");
      CHECK(reg_recipes.size() == 2);
      CHECK(reg_recipes[0] == "m:rb->m:rubyblock x1 shaped w3 h3");
      CHECK(reg_recipes[1] == "m:r9->m:ruby x9 less w1 h1");
      CHECK(reg_smelt.size() == 1);
      CHECK(subs.size() == 2);   // ServerTick + PlayerJoin
      lce_script_stats st; lce_script_get_stats(s, &st);
      CHECK(st.items == 2 && st.blocks == 1 && st.recipes == 2 && st.smeltings == 1 && st.events == 2 && st.denied == 1);
      lce_script_free(s);
    }

    // ---- 2. event delivery into a Lua callback, with cancel ----
    { lce_script *s = lce_script_new(&api, nullptr, nullptr); CHECK(s);
      char err[256];
      CHECK(run(s,
        "hits = 0\n"
        "lce.subscribe('ServerTick', function(e) hits = hits + 1 end)\n"
        "lce.subscribe('Chat', function(e) if e.message == 'no' then return true end return false end)\n"
        "lce.subscribe(lce.EV.BLOCK_BREAK, function(e) lce.give_item('m:ruby', e.block_id // 100) end)\n", err, sizeof err) == LCE_OK);
      lce_ev_tick t{sizeof t, 42};
      CHECK(lce_script_event(s, LCE_EV_SERVER_TICK, &t) == 0);
      CHECK(lce_script_event(s, LCE_EV_SERVER_TICK, &t) == 0);
      lce_ev_chat c1{sizeof c1, nullptr, "hi"}; CHECK(lce_script_event(s, LCE_EV_CHAT, &c1) == 0);
      lce_ev_chat c2{sizeof c2, nullptr, "no"}; CHECK(lce_script_event(s, LCE_EV_CHAT, &c2) == 1);  // cancel
      gives.clear();
      lce_ev_block bb{}; bb.struct_size = sizeof bb; bb.block_id = 253; lce_script_event(s, LCE_EV_BLOCK_BREAK, &bb);
      CHECK(gives.size() == 1 && gives[0] == "m:rubyx2");  // lce.EV alias + C-field alias block_id
      // generated-port API: resolve, player-targeted give, add_effect/drop without a backend -> UNSUPPORTED code, never a crash
      CHECK(run(s, "assert(type(lce.resolve_item) == 'function' and type(lce.resolve_block) == 'function')\n"
                   "assert(lce.EV.ITEM_USE == 'ItemUse' and lce.EV.ENTITY_DEATH == 'EntityDeath')\n"
                   "assert(lce.add_effect(nil, {effect='minecraft:speed', duration_ticks=20}) ~= 0)\n"
                   "assert(lce.drop_item(nil, 1, 2, 3, 'm:ruby', 1) ~= 0)\n"
                   "assert(not pcall(lce.give_item, 'm:ruby', 999))\n"
                   "lce.give_item(nil, 'm:ruby', 3)\n", err, sizeof err) == LCE_OK);
      CHECK(gives.size() == 2 && gives[1] == "m:rubyx3");
      lce_script_free(s);
    }

    // ---- 2b. API 1.2: world services, new events and the extended payload fields ----
    { lce_api a12 = make_api();
      a12.set_block = fsb; a12.get_block = fgb; a12.play_sound = fsnd; a12.explode = fboom; a12.set_fire = ffire; a12.heal = fheal;
      a12.consume_held_item = fcons;   // damage_entity / teleport / set_motion left NULL: UNSUPPORTED, never a crash
      lce_script *s = lce_script_new(&a12, nullptr, nullptr); CHECK(s);
      char err[256];
      CHECK(run(s,
        "assert(lce.EV.BLOCK_USE == 'BlockUse' and lce.EV.ENTITY_TICK == 'EntityTick')\n"
        "lce.subscribe(lce.EV.BLOCK_USE, function(e)\n"
        "  assert(e.level and e.x == 7 and e.face == -1)\n"
        "  lce.set_block(e.level, e.x, e.y + 1, e.z, 'minecraft:stone', 3)\n"
        "  assert(lce.get_block(e.level, e.x, e.y, e.z) == 1 | (2 << 12))\n"
        "  lce.play_sound(e.level, e.x, e.y, e.z, 'random.orb')\n"
        "  lce.explode(e.level, e.x + 0.5, e.y, e.z + 0.5, 2.5, true)\n"
        "  lce.set_fire(e.player, 60); lce.heal(e.player, 2.5); lce.consume_held_item(e.player, 1)\n"
        "  assert(lce.teleport(e.player, 0, 64, 0) ~= 0 and lce.damage_entity(e.player, 1) ~= 0 and lce.set_motion(e.player, 0, 1, 0) ~= 0)\n"
        "  assert(not pcall(lce.explode, e.level, 0, 64, 0, 99))\n"
        "  return true\n"
        "end)\n", err, sizeof err) == LCE_OK);
      lce_ev_block bu{}; bu.struct_size = sizeof bu; bu.x = 7; bu.y = 64; bu.z = -2; bu.block_id = 253; bu.player = (void *)0x10;
      bu.level = (void *)0x20; bu.face = -1;
      CHECK(lce_script_event(s, LCE_EV_BLOCK_USE, &bu) == 1);   // handled
      CHECK(svc12.size() == 6);
      CHECK(svc12[0] == "set 7,65,-2=minecraft:stone@3" && svc12[1] == "sound random.orb" && svc12[2] == "boom 7.5 r2.5 f1 d1");
      CHECK(svc12[3] == "fire 60" && svc12[4] == "heal 2500" && svc12[5] == "consume 1");
      // a 1.1-sized payload carries no level: the handler sees nil and fails its own assert (error counted, no crash)
      lce_ev_block old{}; old.struct_size = offsetof(lce_ev_block, level); old.x = 7;
      CHECK(lce_script_event(s, LCE_EV_BLOCK_USE, &old) == 0);
      lce_script_free(s);
    }

    // ---- 2c. API 1.3 events: inventory tick, neighbour change, step-on, entity inside ----
    { lce_script *s = lce_script_new(&api, nullptr, nullptr); CHECK(s);
      char err[256];
      CHECK(run(s,
        "assert(lce.EV.ITEM_INVENTORY_TICK == 'InventoryTick' and lce.EV.BLOCK_STEP_ON == 'BlockStepOn')\n"
        "seen = ''\n"
        "lce.subscribe('InventoryTick', function(e) assert(e.item_id == 2300 and e.slot == 4 and e.selected and e.entity == e.player); seen = seen .. 'i' end)\n"
        "lce.subscribe('BlockNeighbor', function(e) assert(e.block_id == 253 and e.face == 1 and e.player == nil); seen = seen .. 'n' end)\n"
        "lce.subscribe('BlockStepOn', function(e) assert(e.entity and e.y == 64); seen = seen .. 's' end)\n"
        "lce.subscribe(lce.EV.BLOCK_ENTITY_INSIDE, function(e) assert(e.entity == e.player); seen = seen .. 'x' end)\n"
        "function check() return seen == 'inxs' end\n", err, sizeof err) == LCE_OK);
      lce_ev_item_use it{}; it.struct_size = sizeof it; it.item_id = 2300; it.aux = 4; it.hand = 0; it.player = (void *)0x10; it.level = (void *)0x20;
      CHECK(lce_script_event(s, LCE_EV_ITEM_INVENTORY_TICK, &it) == 0);
      lce_ev_block nb{}; nb.struct_size = sizeof nb; nb.x = 1; nb.y = 64; nb.z = 2; nb.block_id = 253; nb.level = (void *)0x20; nb.face = 1;
      CHECK(lce_script_event(s, LCE_EV_BLOCK_NEIGHBOR, &nb) == 0);
      nb.player = (void *)0x30; nb.face = -1;
      CHECK(lce_script_event(s, LCE_EV_BLOCK_ENTITY_INSIDE, &nb) == 0);
      CHECK(lce_script_event(s, LCE_EV_BLOCK_STEP_ON, &nb) == 0);
      lce_script_stats st{}; lce_script_get_stats(s, &st); CHECK(st.errors == 0);
      CHECK(run(s, "assert(check())\n", err, sizeof err) == LCE_OK);
      lce_script_free(s);
    }

    // ---- 2d. API 1.3/1.4: particles and item NBT ----
    { static std::map<std::string, lce_nbt_value> tags; static std::string last_p;
      lce_api a = make_api();
      a.spawn_particles = +[](lce_mod_ctx *, void *, int32_t id, double x, double, double, uint32_t n, double, double, double, double sp) -> lce_status {
          char b[64]; snprintf(b, sizeof b, "p%d x%.1f n%u s%.1f", id, x, n, sp); last_p = b; return LCE_OK; };
      a.item_tag_set = +[](lce_mod_ctx *, void *, const char *k, const lce_nbt_value *v) -> lce_status { tags[k] = *v; return LCE_OK; };
      a.item_tag_get = +[](lce_mod_ctx *, void *, const char *k, lce_nbt_value *o) -> lce_status {
          auto i = tags.find(k); if (i == tags.end()) return LCE_E_NOTFOUND; *o = i->second; return LCE_OK; };
      a.item_tag_remove = +[](lce_mod_ctx *, void *, const char *k) -> lce_status { return tags.erase(k) ? LCE_OK : LCE_E_NOTFOUND; };
      lce_script *s = lce_script_new(&a, nullptr, nullptr); CHECK(s);
      char err[256];
      CHECK(run(s,
        "assert(lce.spawn_particles(nil, 30, 1.5, 64, 2, 8, 0.5, 0.5, 0.5, 0.1) == 0)\n"
        "assert(lce.item_tag_set(nil, 'mana', 42) == 0 and lce.item_tag_set(nil, 'speed', 0.25) == 0 and lce.item_tag_set(nil, 'owner', 'Steve') == 0)\n"
        "assert(lce.item_tag_get(nil, 'mana') == 42 and math.type(lce.item_tag_get(nil, 'mana')) == 'integer')\n"
        "assert(lce.item_tag_get(nil, 'speed') == 0.25 and lce.item_tag_get(nil, 'owner') == 'Steve' and lce.item_tag_get(nil, 'none') == nil)\n"
        "assert(lce.item_tag_remove(nil, 'mana') == 0 and lce.item_tag_get(nil, 'mana') == nil)\n"
        "assert(not pcall(lce.item_tag_set, nil, 'x', {}))\n", err, sizeof err) == LCE_OK);
      CHECK(last_p == "p30 x1.5 n8 s0.1");
      CHECK(tags["speed"].type == LCE_NBT_DOUBLE && tags["owner"].type == LCE_NBT_STRING);
      lce_script_free(s);
    }

#ifdef LCE_HAVE_ENTITY_NBT_1_5
    // ---- 2e. API 1.5: entity NBT (read-only vanilla paths) and persistent entity mod data ----
    { static std::map<std::string, lce_nbt_value> data; static std::string last_path; static void *last_e;
      lce_api a = make_api();
      a.entity_nbt_get = +[](lce_mod_ctx *, void *e, const char *p, lce_nbt_value *o) -> lce_status {
          last_path = p; last_e = e; if (!strcmp(p, "Health")) { o->type = LCE_NBT_DOUBLE; o->d = 20.0; return LCE_OK; }
          if (!strcmp(p, "Pos[#]")) { o->type = LCE_NBT_INT; o->i = 3; return LCE_OK; } return LCE_E_NOTFOUND; };
      a.entity_data_set = +[](lce_mod_ctx *, void *e, const char *k, const lce_nbt_value *v) -> lce_status {
          if (!e) { return LCE_E_INVALID; }
          data[k] = *v; return LCE_OK; };
      a.entity_data_get = +[](lce_mod_ctx *, void *, const char *k, lce_nbt_value *o) -> lce_status {
          auto i = data.find(k); if (i == data.end()) return LCE_E_NOTFOUND; *o = i->second; return LCE_OK; };
      a.entity_data_remove = +[](lce_mod_ctx *, void *, const char *k) -> lce_status { return data.erase(k) ? LCE_OK : LCE_E_NOTFOUND; };
      lce_script *s = lce_script_new(&a, nullptr, nullptr); CHECK(s);
      char err[256];
      CHECK(run(s,
        "assert(lce.entity_nbt_get(nil, 'Health') == 20.0 and lce.entity_nbt_get(nil, 'Pos[#]') == 3 and lce.entity_nbt_get(nil, 'X') == nil)\n"
        "function on_hit(ev) assert(lce.entity_data_set(ev.entity, 'mymod:mana', 7) == 0) assert(lce.entity_data_set(ev.entity, 'mymod:owner', 'Steve') == 0)\n"
        "  assert(lce.entity_data_get(ev.entity, 'mymod:mana') == 7 and math.type(lce.entity_data_get(ev.entity, 'mymod:mana')) == 'integer')\n"
        "  assert(lce.entity_data_get(ev.entity, 'mymod:owner') == 'Steve' and lce.entity_data_remove(ev.entity, 'mymod:mana') == 0)\n"
        "  assert(lce.entity_data_get(ev.entity, 'mymod:mana') == nil and lce.entity_data_set(nil, 'k', 1) ~= 0)\n"
        "  assert(not pcall(lce.entity_data_set, ev.entity, 'k', {})) end\n"
        "lce.subscribe('EntityDamage', on_hit)\n", err, sizeof err) == LCE_OK);
      lce_ev_entity ee{}; ee.struct_size = sizeof ee; ee.entity = (void *)0x50; ee.amount_milli = 1000;
      CHECK(lce_script_event(s, LCE_EV_ENTITY_DAMAGE, &ee) == 0);
      lce_script_stats st{}; lce_script_get_stats(s, &st); CHECK(st.errors == 0);
      CHECK((last_path == "Pos[#]" || last_path == "X") && last_e == nullptr); CHECK(data.count("mymod:owner") == 1 && data["mymod:owner"].type == LCE_NBT_STRING);
      lce_script_free(s);
    }
#endif
#ifdef LCE_HAVE_PROJECTILES_1_9
    // ---- 2g. API 1.9: ProjectileHit (entity hit and block hit) ----
    { lce_script *s = lce_script_new(&api, nullptr, nullptr); CHECK(s);
      char err[256];
      CHECK(run(s,
        "assert(lce.EV.PROJECTILE_HIT == 'ProjectileHit')\n"
        "seen = ''\n"
        "lce.subscribe('ProjectileHit', function(e)\n"
        "  assert(e.item_id == 2290 and e.projectile and e.level and e.y == 64)\n"
        "  if e.entity then assert(e.damage == 4.5); seen = seen .. 'e' else seen = seen .. 'b' end\n"
        "end)\n"
        "function check() return seen == 'eb' end\n", err, sizeof err) == LCE_OK);
      lce_ev_entity h{}; h.struct_size = sizeof h; h.entity = (void *)0x40; h.source = (void *)0x50; h.type_id = 2290; h.amount_milli = 4500;
      h.level = (void *)0x20; h.x = 3; h.y = 64; h.z = -2;
      CHECK(lce_script_event(s, LCE_EV_PROJECTILE_HIT, &h) == 0);
      h.entity = nullptr;
      CHECK(lce_script_event(s, LCE_EV_PROJECTILE_HIT, &h) == 0);
      lce_script_stats st{}; lce_script_get_stats(s, &st); CHECK(st.errors == 0);
      CHECK(run(s, "assert(check())\n", err, sizeof err) == LCE_OK);
      lce_script_free(s);
    }
#endif
#ifdef LCE_HAVE_CONFIG_1_8
    // ---- 2f. API 1.8: lce.config backed by the loader's real config store ----
    { static lce_cfg_store cs;
      const char *txt = "[general]\nrate = 12\nspeed = 2.5\nfancy = false\nname = \"Ruby\"\nbig = 500\n";
      lce_cfg_parse(&cs, txt, strlen(txt));
      lce_api a = make_api();
      a.config_get = +[](lce_mod_ctx *, const char *k, lce_nbt_value *o) -> lce_status {
          const lce_cfg_entry *e = lce_cfg_find(&cs, k); if (!e) return LCE_E_NOTFOUND; *o = e->v; return LCE_OK; };
      a.config_int = +[](lce_mod_ctx *, const char *k, int64_t d, int64_t lo, int64_t hi) { return lce_cfg_int(&cs, k, d, lo, hi); };
      a.config_double = +[](lce_mod_ctx *, const char *k, double d, double lo, double hi) { return lce_cfg_double(&cs, k, d, lo, hi); };
      a.config_string = +[](lce_mod_ctx *, const char *k, const char *d) { return lce_cfg_string(&cs, k, d); };
      lce_script *s = lce_script_new(&a, nullptr, nullptr); CHECK(s);
      char err[256];
      chats.clear();
      const char *src =
        "local function say(v) lce.send_chat(tostring(v) .. ':' .. math.type(v) ) end\n"
        "local function sayv(v) lce.send_chat(tostring(v)) end\n"
        "say(lce.config('general.rate', 8))\n"            // 12
        "say(lce.config('general.rate', 8, 1, 10))\n"     // out of range -> 8
        "say(lce.config('general.rate', 8, 1))\n"         // min only: 12
        "say(lce.config('general.speed', 1.0))\n"         // 2.5
        "say(lce.config('general.speed', 1.0, 0, 2))\n"   // 1.0
        "say(lce.config('general.rate', 1.5))\n"          // int widens: 12.0
        "sayv(lce.config('general.fancy', true))\n"       // false
        "sayv(lce.config('general.missing', true))\n"     // true
        "sayv(lce.config('general.name', 'x'))\n"         // Ruby
        "sayv(lce.config('general.rate', 'x'))\n"         // wrong type -> x
        "sayv(lce.config('general.name'))\n"              // raw: Ruby
        "say(lce.config('general.big'))\n"                // raw: 500 integer
        "sayv(lce.config('general.nope'))\n"              // nil
        "sayv(pcall(lce.config, 'general.rate', {}))\n";  // false (bad default)
      CHECK(run(s, src, err, sizeof err) == LCE_OK);
      const char *want[] = {"12:integer", "8:integer", "12:integer", "2.5:float", "1.0:float", "12.0:float", "false", "true",
                            "Ruby", "x", "Ruby", "500:integer", "nil", "false"};
      CHECK(chats.size() == sizeof want / sizeof want[0]);
      for (size_t i = 0; i < chats.size() && i < sizeof want / sizeof want[0]; ++i) {
          if (chats[i] != want[i]) fprintf(stderr, "config %zu: got %s want %s\n", i, chats[i].c_str(), want[i]);
          CHECK(chats[i] == want[i]);
      }
      lce_script_free(s);
      // an older API table without config entries: lce.config returns the default
      lce_api old = make_api(); old.struct_size = (uint32_t)offsetof(lce_api, config_get);
      s = lce_script_new(&old, nullptr, nullptr); CHECK(s);
      chats.clear();
      CHECK(run(s, "lce.send_chat(tostring(lce.config('general.rate', 8)) .. tostring(lce.config('general.rate')))", err, sizeof err) == LCE_OK);
      CHECK(chats.size() == 1 && chats[0] == "8nil");
      lce_script_free(s);
    }
#endif

#ifdef LCE_HAVE_NET_1_6
    // ---- 2f. API 1.6: client payload event + send_to_server ----
    { static std::string sent;
      lce_api a = make_api();
      a.register_channel = +[](lce_mod_ctx *, const char *) -> lce_status { return LCE_OK; };
      a.send_to_server = +[](lce_mod_ctx *, const char *c, const void *d, uint32_t n) -> lce_status {
          sent = std::string(c) + "|" + std::string((const char *)d, n); return LCE_OK; };
      lce_script *s = lce_script_new(&a, nullptr, nullptr); CHECK(s);
      char err[256];
      CHECK(run(s,
        "lce.register_channel('ruby:sync')\n"
        "lce.subscribe('ClientPayload', function(ev) assert(ev.channel == 'ruby:sync' and ev.player == nil)\n"
        "  assert(lce.send_to_server(ev.channel, 'ack:' .. ev.data) == 0) end)\n"
        "assert(lce.EV.CLIENT_PAYLOAD == 'ClientPayload')\n", err, sizeof err) == LCE_OK);
      lce_ev_payload pv{}; pv.struct_size = sizeof pv; pv.channel = "ruby:sync"; pv.data = (const uint8_t *)"hi"; pv.len = 2;
      CHECK(lce_script_event(s, LCE_EV_CLIENT_PAYLOAD, &pv) == 0);
      lce_script_stats st{}; lce_script_get_stats(s, &st); CHECK(st.errors == 0);
      CHECK(sent == "ruby:sync|ack:hi");
      lce_script_free(s);
    }
#endif

    // ---- 3. sandbox escapes: io/os/debug/package/require/loadfile all absent ----
    { lce_script *s = lce_script_new(&api, nullptr, nullptr); CHECK(s);
      char err[256];
      const char *escapes[] = {
        "return io.open('/etc/passwd')",
        "return os.execute('id')",
        "return os.getenv('HOME')",
        "return debug.getinfo(1)",
        "require('os')",
        "return package.loadlib('x','y')",
        "return loadfile('/etc/passwd')",
        "return dofile('/etc/passwd')",
        "collectgarbage('collect')",
      };
      for (const char *e : escapes) {
          lce_script *v = lce_script_new(&api, nullptr, nullptr); CHECK(v);
          CHECK(run(v, e, err, sizeof err) == LCE_E_INVALID);   // nil-index / global missing
          lce_script_free(v);
      }
      // load() must refuse precompiled bytecode (text-only mode).
      CHECK(run(s, "local f = load('\\27Lua'); assert(f == nil)", err, sizeof err) == LCE_OK);
      lce_script_free(s);
    }

    // ---- 4. memory cap enforced ----
    { lce_script_config cfg{}; cfg.memory_cap = 2u*1024*1024;
      lce_script *s = lce_script_new(&api, nullptr, &cfg); CHECK(s);
      char err[256];
      // Grow a table past the cap; allocation fails -> error, VM survives for inspection.
      int rc = run(s, "local t = {} for i=1,100000000 do t[i] = i end", err, sizeof err);
      CHECK(rc == LCE_E_INVALID);
      CHECK(lce_script_memory(s) <= cfg.memory_cap);
      lce_script_free(s);
    }

    // ---- 5. step budget cannot be bypassed by pcall ----
    { lce_script_config cfg{}; cfg.step_budget = 200000; cfg.hook_interval = 1000;
      lce_script *s = lce_script_new(&api, nullptr, &cfg); CHECK(s);
      char err[256];
      // A pcall loop that swallows every error still cannot spin forever.
      const char *src =
        "local n = 0\n"
        "while true do\n"
        "  local ok = pcall(function() while true do n = n + 1 end end)\n"
        "  -- ok is false after the hook fires; keep trying to swallow it\n"
        "end\n";
      int rc = run(s, src, err, sizeof err);
      CHECK(rc == LCE_E_INVALID);
      CHECK(lce_script_is_dead(s) == 1);
      CHECK(strstr(err, "step budget") != nullptr);
      // A dead script refuses further runs and events.
      CHECK(run(s, "lce.register_item{key='m:late'}", err, sizeof err) == LCE_E_INVALID);
      lce_ev_tick t{sizeof t, 1};
      CHECK(lce_script_event(s, LCE_EV_SERVER_TICK, &t) == 0);
      lce_script_free(s);
    }

    // ---- 6. a syntax error is reported, not fatal to the host ----
    { lce_script *s = lce_script_new(&api, nullptr, nullptr); CHECK(s);
      char err[256]; CHECK(run(s, "this is not lua", err, sizeof err) == LCE_E_INVALID);
      CHECK(err[0] != 0);
      lce_script_free(s);
    }

    printf("script: ALL %d CHECKS PASSED\n", g_checks);
    return 0;
}
