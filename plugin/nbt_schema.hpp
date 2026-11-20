#pragma once
// Field knowledge for the NBT editor: booleans, ranges, pick-lists (items, blocks,
// enchantments, effects, entity types, enums from the game's own tables) and
// typed templates for "Add". Pure data + lookups; host tested.
#include "nbt_doc.hpp"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/tab-menu/modal-binding/vanilla_catalog_data.hpp"

namespace lce_nbt_ed {

enum class Pick : uint8_t { None, Item, Block, Enchant, Effect, EntityType, GameMode, Difficulty, Dye, Profession, Attribute,
                            CatType, RabbitType, HorseType, SkeletonType, Operation, Biome, Dimension, TileEntityType, Count_ };
struct Hint {
    Pick pick = Pick::None;
    bool boolean = false, readonly = false, ranged = false;
    bool u8 = false;              // a byte shown and edited as 0..255 (stored as a signed byte): biome ids
    int64_t lo = 0, hi = 0;
    double step = 1;
    const char *note = nullptr;   // short explanation shown in the editor
};
struct Choice { int64_t id; const char *name; };

namespace tables {
static const Choice effects[] = {{1, "speed"}, {2, "slowness"}, {3, "haste"}, {4, "mining_fatigue"}, {5, "strength"}, {6, "instant_health"},
    {7, "instant_damage"}, {8, "jump_boost"}, {9, "nausea"}, {10, "regeneration"}, {11, "resistance"}, {12, "fire_resistance"},
    {13, "water_breathing"}, {14, "invisibility"}, {15, "blindness"}, {16, "night_vision"}, {17, "hunger"}, {18, "weakness"},
    {19, "poison"}, {20, "wither"}, {21, "health_boost"}, {22, "absorption"}, {23, "saturation"}};
// EntityIO save ids of this server (1.8 era); spawners and SpawnData use these strings.
static const char *const entity_ids[] = {"Bat", "Blaze", "CaveSpider", "Chicken", "Cow", "Creeper", "EnderDragon", "Enderman", "Endermite",
    "EntityHorse", "Ghast", "Giant", "Guardian", "LavaSlime", "MushroomCow", "Ozelot", "Pig", "PigZombie", "Rabbit", "Sheep", "Silverfish",
    "Skeleton", "Slime", "SnowMan", "Spider", "Squid", "Villager", "VillagerGolem", "Witch", "WitherBoss", "Wolf", "Zombie",
    "ArmorStand", "Arrow", "Boat", "EnderCrystal", "FallingSand", "Fireball", "FireworksRocketEntity", "Item", "ItemFrame", "LeashKnot",
    "MinecartChest", "MinecartFurnace", "MinecartHopper", "MinecartRideable", "MinecartSpawner", "MinecartTNT", "Painting", "PrimedTnt",
    "SmallFireball", "Snowball", "ThrownEnderpearl", "ThrownExpBottle", "ThrownPotion", "WitherSkull", "XPOrb"};
static const Choice game_modes[] = {{0, "survival"}, {1, "creative"}, {2, "adventure"}, {3, "spectator"}};
static const Choice difficulties[] = {{0, "peaceful"}, {1, "easy"}, {2, "normal"}, {3, "hard"}};
static const Choice dyes[] = {{0, "white"}, {1, "orange"}, {2, "magenta"}, {3, "light_blue"}, {4, "yellow"}, {5, "lime"}, {6, "pink"},
    {7, "gray"}, {8, "light_gray"}, {9, "cyan"}, {10, "purple"}, {11, "blue"}, {12, "brown"}, {13, "green"}, {14, "red"}, {15, "black"}};
static const Choice professions[] = {{0, "farmer"}, {1, "librarian"}, {2, "priest"}, {3, "blacksmith"}, {4, "butcher"}};
static const char *const attributes[] = {"generic.maxHealth", "generic.followRange", "generic.knockbackResistance", "generic.movementSpeed",
    "generic.attackDamage", "horse.jumpStrength", "zombie.spawnReinforcements"};
static const Choice cat_types[] = {{0, "wild_ocelot"}, {1, "tuxedo"}, {2, "tabby"}, {3, "siamese"}};
static const Choice rabbit_types[] = {{0, "brown"}, {1, "white"}, {2, "black"}, {3, "black_white"}, {4, "gold"}, {5, "salt_pepper"}, {99, "killer"}};
static const Choice horse_types[] = {{0, "horse"}, {1, "donkey"}, {2, "mule"}, {3, "zombie"}, {4, "skeleton"}};
static const Choice skeleton_types[] = {{0, "normal"}, {1, "wither"}};
static const Choice operations[] = {{0, "add"}, {1, "multiply_base"}, {2, "multiply"}};
// Biome ids of this server (Biome.cpp).
static const Choice biomes[] = {{0, "Ocean"}, {1, "Plains"}, {2, "Desert"}, {3, "Extreme Hills"}, {4, "Forest"}, {5, "Taiga"}, {6, "Swampland"},
    {7, "River"}, {8, "Hell"}, {9, "Sky"}, {10, "Frozen Ocean"}, {11, "Frozen River"}, {12, "Ice Plains"}, {13, "Ice Mountains"},
    {14, "Mushroom Island"}, {15, "Mushroom Island Shore"}, {16, "Beach"}, {17, "Desert Hills"}, {18, "Forest Hills"}, {19, "Taiga Hills"},
    {20, "Extreme Hills Edge"}, {21, "Jungle"}, {22, "Jungle Hills"}, {23, "Jungle Edge"}, {24, "Deep Ocean"}, {26, "Cold Beach"},
    {27, "Birch Forest"}, {28, "Birch Forest Hills"}, {29, "Roofed Forest"}, {30, "Cold Taiga"}, {31, "Cold Taiga Hills"}, {32, "Mega Taiga"},
    {33, "Mega Taiga Hills"}, {34, "Extreme Hills+"}, {35, "Savanna"}, {36, "Savanna Plateau"}, {37, "Mesa"}, {38, "Mesa Plateau F"},
    {39, "Mesa Plateau"}, {129, "Sunflowers Plains"}, {130, "Desert M"}, {132, "Flower Forest"}, {133, "Taiga M"}, {134, "Swampland M"},
    {140, "Ice Spikes"}, {149, "Jungle M"}, {151, "Jungle Edge M"}, {158, "Cold Taiga M"}, {160, "Mega Spruce Taiga"},
    {161, "Mega Spruce Taiga Hills"}, {163, "Savanna M"}, {164, "Savanna Plateau M"}, {165, "Mesa (Bryce)"}, {166, "Mesa Plateau F M"},
    {167, "Mesa Plateau M"}};
static const Choice dimensions[] = {{-1, "nether"}, {0, "overworld"}, {1, "the_end"}};
// Block entity save ids of this server (TileEntity.cpp).
static const char *const tile_entity_ids[] = {"Furnace", "Chest", "EnderChest", "RecordPlayer", "Trap", "Dropper", "Sign", "MobSpawner",
    "Music", "Piston", "Cauldron", "EnchantTable", "Airportal", "Control", "Beacon", "Skull", "DLDetector", "Hopper", "Comparator"};
}  // namespace tables

inline bool pick_is_string(Pick p) { return p == Pick::EntityType || p == Pick::Attribute || p == Pick::TileEntityType; }
inline size_t choice_count(Pick p)
{
    using namespace tables;
    switch (p) {
    case Pick::Item: return sizeof vanilla_lists::data::items / sizeof vanilla_lists::data::items[0];
    case Pick::Block: { size_t n = 0; for (const auto &e : vanilla_lists::data::items) if (e.id < 256) ++n; return n; }
    case Pick::Enchant: return sizeof vanilla_lists::data::enchantments / sizeof vanilla_lists::data::enchantments[0];
    case Pick::Effect: return sizeof effects / sizeof effects[0];
    case Pick::EntityType: return sizeof entity_ids / sizeof entity_ids[0];
    case Pick::GameMode: return sizeof game_modes / sizeof game_modes[0];
    case Pick::Difficulty: return sizeof difficulties / sizeof difficulties[0];
    case Pick::Dye: return sizeof dyes / sizeof dyes[0];
    case Pick::Profession: return sizeof professions / sizeof professions[0];
    case Pick::Attribute: return sizeof attributes / sizeof attributes[0];
    case Pick::CatType: return sizeof cat_types / sizeof cat_types[0];
    case Pick::RabbitType: return sizeof rabbit_types / sizeof rabbit_types[0];
    case Pick::HorseType: return sizeof horse_types / sizeof horse_types[0];
    case Pick::SkeletonType: return sizeof skeleton_types / sizeof skeleton_types[0];
    case Pick::Operation: return sizeof operations / sizeof operations[0];
    case Pick::Biome: return sizeof biomes / sizeof biomes[0];
    case Pick::Dimension: return sizeof dimensions / sizeof dimensions[0];
    case Pick::TileEntityType: return sizeof tile_entity_ids / sizeof tile_entity_ids[0];
    default: return 0;
    }
}
inline Choice choice_at(Pick p, size_t i)
{
    using namespace tables;
    switch (p) {
    case Pick::Item: return {vanilla_lists::data::items[i].id, vanilla_lists::data::items[i].name};
    case Pick::Block: { size_t n = 0; for (const auto &e : vanilla_lists::data::items) if (e.id < 256 && n++ == i) return {e.id, e.name}; return {0, "?"}; }
    case Pick::Enchant: return {vanilla_lists::data::enchantments[i].id, vanilla_lists::data::enchantments[i].name};
    case Pick::Effect: return effects[i];
    case Pick::EntityType: return {(int64_t)i, entity_ids[i]};
    case Pick::GameMode: return game_modes[i];
    case Pick::Difficulty: return difficulties[i];
    case Pick::Dye: return dyes[i];
    case Pick::Profession: return professions[i];
    case Pick::Attribute: return {(int64_t)i, attributes[i]};
    case Pick::CatType: return cat_types[i];
    case Pick::RabbitType: return rabbit_types[i];
    case Pick::HorseType: return horse_types[i];
    case Pick::SkeletonType: return skeleton_types[i];
    case Pick::Operation: return operations[i];
    case Pick::Biome: return biomes[i];
    case Pick::Dimension: return dimensions[i];
    case Pick::TileEntityType: return {(int64_t)i, tile_entity_ids[i]};
    default: return {0, "?"};
    }
}
// Name for a numeric id in a pick list (nullptr if unknown).
inline const char *choice_name(Pick p, int64_t id)
{
    if (pick_is_string(p)) return nullptr;
    const size_t n = choice_count(p);
    for (size_t i = 0; i < n; ++i) { const Choice c = choice_at(p, i); if (c.id == id) return c.name; }
    return nullptr;
}
// Index of a numeric id or string value in a pick list (0 if absent).
inline size_t choice_index(Pick p, int64_t id, const char *text, size_t text_len)
{
    const size_t n = choice_count(p);
    for (size_t i = 0; i < n; ++i) {
        const Choice c = choice_at(p, i);
        if (pick_is_string(p) ? (text && strlen(c.name) == text_len && !memcmp(c.name, text, text_len)) : c.id == id) return i;
    }
    return 0;
}
inline const char *item_name(int64_t id) { return choice_name(Pick::Item, id); }

// Target kinds (first letter of the protocol token).
enum TargetKind : char { K_PLAYER = 'p', K_ITEM = 'i', K_ENDER = 'x', K_ENTITY = 'e', K_BLOCK = 'b', K_LEVEL = 'l', K_SCORE = 's', K_WORLD = 'w', K_CHUNK = 'k' };

inline bool name_in(const Doc &d, uint32_t n, const char *const *names)
{
    for (; *names; ++names) if (d.name_eq(n, *names)) return true;
    return false;
}
// The compound that holds `n` (its parent if a compound child, nullptr else).
inline uint32_t holder(const Doc &d, uint32_t n) { const uint32_t p = d.at(n).parent; return p != kNil && d.at(p).type == T_COMPOUND ? p : kNil; }
inline bool holder_has(const Doc &d, uint32_t n, const char *key) { const uint32_t h = holder(d, n); return h != kNil && d.find(h, key) != kNil; }
inline bool parent_list_named(const Doc &d, uint32_t compound, const char *name)
{
    const uint32_t l = compound == kNil ? kNil : d.at(compound).parent;
    return l != kNil && d.at(l).type == T_LIST && d.at(l).parent != kNil && d.name_eq(l, name);
}

// Top-level keys the server keeps (LceNbtEdit.h protectedKey): only the identity of the
// target itself. Everything else is editable (Dimension travels, entity id respawns, block
// entity x/y/z moves the block, level seed/name/time/mode/hardcore apply, ...).
inline bool protected_key(char kind, const Doc &d, uint32_t n)
{
    if (d.at(n).parent != d.root) return false;
    static const char *const player[] = {"UUID", "UUIDMost", "UUIDLeast", nullptr};
    static const char *const block[] = {"id", nullptr};
    static const char *const chunk[] = {"xPos", "zPos", nullptr};
    switch (kind) {
    case K_PLAYER: return name_in(d, n, player);
    case K_BLOCK: return name_in(d, n, block);
    case K_CHUNK: return name_in(d, n, chunk);
    case K_SCORE: return d.name_eq(n, "Supported") || d.name_eq(n, "Note");
    default: return false;
    }
}
// Why a key is read-only (shown when it is selected).
inline const char *protected_reason(char kind)
{
    switch (kind) {
    case K_PLAYER: return "Read-only: account identity";
    case K_BLOCK: return "Read-only: change the block (World block)";
    case K_CHUNK: return "Read-only: chunk position";
    case K_SCORE: return "Read-only: server scoreboard support";
    default: return "Read-only";
    }
}

inline Hint hint_for(const Doc &d, uint32_t n, char kind)
{
    Hint h;
    const Node &x = d.at(n);
    h.readonly = protected_key(kind, d, n);
    if (x.parent == kNil) return h;
    const uint32_t hold = holder(d, n);
    const bool keyed = hold != kNil;
    auto ranged = [&](int64_t lo, int64_t hi) { h.ranged = true; h.lo = lo; h.hi = hi; };
    static const char *const bools[] = {"Invulnerable", "NoAI", "Silent", "OnGround", "CustomNameVisible", "PersistenceRequired", "CanPickUpLoot",
        "Unbreakable", "Sitting", "Saddle", "Sheared", "IsBaby", "raining", "thundering", "hardcore", "ChestedHorse", "Tame", "Bred",
        "EatingHaystack", "Angry", "powered", "ignited", "Invisible", "NoGravity", "ShowParticles", "Ambient", "Fixed", "Small", "ShowArms",
        "NoBasePlate", "Marker", "Elder", "IsChickenJockey", "Leashed", "Sleeping", "invulnerable", "flying", "mayfly", "instabuild", "mayBuild",
        "PlayerSpawned", "Willing", "Riches", "Supported", "inGround", "Glowing", "Trap", nullptr};
    if (keyed && x.type == T_BYTE && (name_in(d, n, bools) || (d.at(hold).parent != kNil && d.name_eq(hold, "GameRules")))) { h.boolean = true; return h; }
    if (!keyed) {
        // list elements of well known lists
        const uint32_t l = x.parent;
        if (d.name_eq(l, "Lore") && x.type == T_STRING) h.note = "lore line";
        return h;
    }
    // item ids / counts
    if (d.name_eq(n, "id") && x.type == T_SHORT && holder_has(d, n, "Count")) { h.pick = Pick::Item; ranged(1, 31999); return h; }
    if (d.name_eq(n, "id") && x.type == T_SHORT && holder_has(d, n, "lvl")) { h.pick = Pick::Enchant; ranged(0, 255); return h; }
    if (d.name_eq(n, "lvl") && x.type == T_SHORT) { ranged(1, 32767); return h; }
    if (d.name_eq(n, "Count") && x.type == T_BYTE && holder_has(d, n, "id")) { ranged(1, 127); return h; }
    if (d.name_eq(n, "Damage") && x.type == T_SHORT && holder_has(d, n, "id")) { ranged(-32768, 32767); return h; }
    if (d.name_eq(n, "Slot") && x.type == T_BYTE) { ranged(-128, 127); h.note = "inventory 0-35, armour 100-103"; return h; }
    if (d.name_eq(n, "Id") && x.type == T_BYTE && holder_has(d, n, "Amplifier")) { h.pick = Pick::Effect; ranged(1, 32); return h; }
    if (d.name_eq(n, "Amplifier") && x.type == T_BYTE) { ranged(0, 127); h.note = "level - 1"; return h; }
    if (d.name_eq(n, "Duration") && x.type == T_INT) { ranged(0, 2147483647); h.step = 20; h.note = "ticks (20 = 1 s)"; return h; }
    // block entity ids (block entity root, World block TileEntity) vs entity type strings (entities, riders, spawners)
    if (d.name_eq(n, "id") && x.type == T_STRING && ((kind == K_BLOCK && hold == d.root) || (kind == K_WORLD && d.name_eq(hold, "TileEntity")))) { h.pick = Pick::TileEntityType; return h; }
    if ((d.name_eq(n, "EntityId") || d.name_eq(n, "id") || d.name_eq(n, "Type")) && x.type == T_STRING) { h.pick = Pick::EntityType; return h; }
    if (d.name_eq(n, "Name") && x.type == T_STRING && (parent_list_named(d, hold, "Attributes"))) { h.pick = Pick::Attribute; return h; }
    if (d.name_eq(n, "AttributeName") && x.type == T_STRING) { h.pick = Pick::Attribute; return h; }
    if (d.name_eq(n, "Operation") && x.type == T_INT) { h.pick = Pick::Operation; ranged(0, 2); return h; }
    if ((d.name_eq(n, "playerGameType") || d.name_eq(n, "GameType")) && x.type == T_INT) { h.pick = Pick::GameMode; ranged(0, 2); return h; }
    if (d.name_eq(n, "Dimension") && x.type == T_INT) { h.pick = Pick::Dimension; ranged(-1, 1); h.note = "changing it moves to that dimension"; return h; }
    // world block document
    if (kind == K_WORLD && hold == d.root) {
        if (d.name_eq(n, "id") && x.type == T_SHORT) { h.pick = Pick::Block; ranged(0, 255); return h; }
        if (d.name_eq(n, "Data") && x.type == T_BYTE) { ranged(0, 15); h.note = "block state (0-15)"; return h; }
        if (d.name_eq(n, "Biome") && x.type == T_BYTE) { h.pick = Pick::Biome; h.u8 = true; ranged(0, 255); return h; }
        if ((d.name_eq(n, "x") || d.name_eq(n, "y") || d.name_eq(n, "z")) && x.type == T_INT) { if (d.name_eq(n, "y")) ranged(0, 255); h.note = "changing it moves the block"; return h; }
    }
    if (kind == K_BLOCK && hold == d.root && (d.name_eq(n, "x") || d.name_eq(n, "y") || d.name_eq(n, "z")) && x.type == T_INT) { if (d.name_eq(n, "y")) ranged(0, 255); h.note = "changing it moves the block"; return h; }
    if (kind == K_CHUNK && d.name_eq(n, "InhabitedTime") && x.type == T_LONG) { ranged(0, INT64_MAX); h.step = 1200; h.note = "ticks players spent here"; return h; }
    if (d.name_eq(n, "Sleeping") && kind == K_PLAYER) { h.boolean = true; h.note = "can only be cleared (wakes up)"; return h; }
    if (d.name_eq(n, "SleepTimer") && x.type == T_SHORT) { ranged(0, 100); return h; }
    if (d.name_eq(n, "RandomSeed") && x.type == T_LONG) { h.note = "new chunks only"; return h; }
    if (d.name_eq(n, "Difficulty") && x.type == T_BYTE) { h.pick = Pick::Difficulty; ranged(0, 3); return h; }
    if ((d.name_eq(n, "Color") || d.name_eq(n, "CollarColor")) && x.type == T_BYTE) { h.pick = Pick::Dye; ranged(0, 15); return h; }
    if (d.name_eq(n, "Profession") && x.type == T_INT) { h.pick = Pick::Profession; ranged(0, 4); return h; }
    if (d.name_eq(n, "CatType") && x.type == T_INT) { h.pick = Pick::CatType; ranged(0, 3); return h; }
    if (d.name_eq(n, "RabbitType") && x.type == T_INT) { h.pick = Pick::RabbitType; ranged(0, 99); return h; }
    if (d.name_eq(n, "Type") && x.type == T_INT && kind == K_ENTITY) { h.pick = Pick::HorseType; ranged(0, 4); return h; }
    if (d.name_eq(n, "SkeletonType") && x.type == T_BYTE) { h.pick = Pick::SkeletonType; ranged(0, 1); return h; }
    if ((d.name_eq(n, "Tile") || d.name_eq(n, "TileID") || d.name_eq(n, "inTile") || d.name_eq(n, "DisplayTile") || d.name_eq(n, "carried")) && is_integer(x.type)) { h.pick = Pick::Block; ranged(0, 255); return h; }
    if (d.name_eq(n, "Health") && x.type == T_FLOAT) { h.ranged = false; h.note = "half hearts"; return h; }
    if (d.name_eq(n, "foodLevel") && x.type == T_INT) { ranged(0, 20); return h; }
    if (d.name_eq(n, "XpLevel") && x.type == T_INT) { ranged(0, 2147483647); return h; }
    if (d.name_eq(n, "Fire") && x.type == T_SHORT) { h.note = "ticks on fire (-1 = none)"; return h; }
    if (d.name_eq(n, "Air") && x.type == T_SHORT) { ranged(-20, 300); return h; }
    if ((d.name_eq(n, "DayTime") || d.name_eq(n, "Time")) && x.type == T_LONG) { h.step = 1000; h.note = "0 sunrise, 6000 noon, 13000 night"; return h; }
    if ((d.name_eq(n, "rainTime") || d.name_eq(n, "thunderTime")) && x.type == T_INT) { h.step = 1200; ranged(0, 2147483647); h.note = "ticks until it changes"; return h; }
    if ((d.name_eq(n, "Delay") || d.name_eq(n, "MinSpawnDelay") || d.name_eq(n, "MaxSpawnDelay")) && x.type == T_SHORT) { h.step = 20; ranged(0, 32767); return h; }
    if (d.name_eq(n, "HideFlags") && x.type == T_INT) { ranged(0, 63); h.note = "bits: 1 ench 2 attr 4 unbreak 8 destroy 16 place 32 other"; return h; }
    if (d.name_eq(n, "Levels") && x.type == T_INT) { ranged(0, 4); return h; }
    if ((d.name_eq(n, "Primary") || d.name_eq(n, "Secondary")) && x.type == T_INT) { h.pick = Pick::Effect; return h; }
    return h;
}

// Hint for the elements of an array tag (Chunk Biomes: biome ids 0-255).
inline Hint array_elem_hint(const Doc &d, uint32_t a, char kind)
{
    Hint h;
    if (d.at(a).type == T_BYTE_ARRAY && d.name_eq(a, "Biomes") && (kind == K_CHUNK || kind == K_LEVEL || kind == K_WORLD)) { h.pick = Pick::Biome; h.u8 = true; h.ranged = true; h.lo = 0; h.hi = 255; }
    return h;
}
inline int64_t hint_value(const Hint &h, int64_t v) { return h.u8 ? (int64_t)(uint8_t)v : v; }
// Display name of a value for previews: "16 sharpness", "1 (creative)", "true".
inline void describe_value(const Doc &d, uint32_t n, char kind, char *out, size_t cap)
{
    const Node &x = d.at(n);
    const Hint h = hint_for(d, n, kind);
    if (h.boolean) { snprintf(out, cap, "%s", x.v.i ? "true" : "false"); return; }
    if (is_integer(x.type)) {
        const int64_t v = hint_value(h, x.v.i);
        const char *nm = h.pick != Pick::None ? choice_name(h.pick, v) : nullptr;
        if (nm) snprintf(out, cap, "%lld %s", (long long)v, nm); else snprintf(out, cap, "%lld", (long long)v);
        return;
    }
    if (is_real(x.type)) { fmt_short_real(x.v.d, out, cap); return; }
    if (x.type == T_STRING) {
        char t[64]; ascii_preview(d.bytes(x.v.b.off), x.v.b.len, t, sizeof t);
        snprintf(out, cap, "\"%s\"", t);
        return;
    }
    if (is_array(x.type)) { snprintf(out, cap, "[%u]", x.v.b.len); return; }
    if (x.type == T_LIST) { snprintf(out, cap, "%u %s", x.count, x.count ? type_name(x.elem) : "empty"); return; }
    // compounds: item summary or entry count
    const uint32_t id = d.find(n, "id"), cnt = d.find(n, "Count");
    if (id != kNil && d.at(id).type == T_SHORT && cnt != kNil) {
        const char *nm = item_name(d.at(id).v.i);
        if (nm) snprintf(out, cap, "%s x%lld", nm, (long long)d.at(cnt).v.i); else snprintf(out, cap, "item %lld x%lld", (long long)d.at(id).v.i, (long long)d.at(cnt).v.i);
        return;
    }
    const uint32_t lvl = d.find(n, "lvl");
    if (id != kNil && lvl != kNil) { const char *nm = choice_name(Pick::Enchant, d.at(id).v.i); snprintf(out, cap, "%s %lld", nm ? nm : "ench", (long long)d.at(lvl).v.i); return; }
    const uint32_t eff = d.find(n, "Id");
    if (eff != kNil && d.at(eff).type == T_BYTE && d.find(n, "Amplifier") != kNil) { const char *nm = choice_name(Pick::Effect, d.at(eff).v.i); snprintf(out, cap, "%s %lld", nm ? nm : "effect", (long long)d.at(d.find(n, "Amplifier")).v.i + 1); return; }
    if (id != kNil && d.at(id).type == T_STRING) { char t[40]; ascii_preview(d.bytes(d.at(id).v.b.off), d.at(id).v.b.len, t, sizeof t); snprintf(out, cap, "%s {%u}", t, x.count); return; }
    const uint32_t nm = d.find(n, "Name");
    if (nm != kNil && d.at(nm).type == T_STRING) { char t[40]; ascii_preview(d.bytes(d.at(nm).v.b.off), d.at(nm).v.b.len, t, sizeof t); snprintf(out, cap, "%s {%u}", t, x.count); return; }
    snprintf(out, cap, "{%u}", x.count);
}

// ---- templates ----------------------------------------------------------------------------
struct Template { const char *label; const char *name; const char *snbt; };
// Generic typed entries (always offered).
static const Template kGeneric[] = {
    {"Byte", "new_byte", "0b"}, {"Short", "new_short", "0s"}, {"Int", "new_int", "0"}, {"Long", "new_long", "0L"},
    {"Float", "new_float", "0.0f"}, {"Double", "new_double", "0.0d"}, {"String", "new_string", "\"\""},
    {"Compound", "new_compound", "{}"}, {"List of compounds", "new_list", "[;c]"}, {"List of strings", "new_list", "[;t]"},
    {"List of ints", "new_list", "[;i]"}, {"List of doubles", "new_list", "[;d]"}, {"List of floats", "new_list", "[;f]"},
    {"List of bytes", "new_list", "[;b]"}, {"List of shorts", "new_list", "[;s]"}, {"List of longs", "new_list", "[;l]"},
    {"List of lists", "new_list", "[;n]"}, {"Byte array", "new_bytes", "[B;]"}, {"Int array", "new_ints", "[I;]"},
};
// Contextual entries, chosen by the parent's name / the target kind.
inline size_t templates_for(const Doc &d, uint32_t parent, char kind, const Template **out, size_t max)
{
    size_t n = 0;
    auto add = [&](const Template *t) { if (n < max) out[n++] = t; };
    static const Template ench = {"Enchantment", "", "{id:16s,lvl:1s}"};
    static const Template effect = {"Potion effect", "", "{Id:1b,Amplifier:0b,Duration:600,Ambient:0b,ShowParticles:1b}"};
    static const Template item = {"Item stack", "", "{id:1s,Count:1b,Damage:0s,Slot:0b}"};
    static const Template equip = {"Equipment slot", "", "{id:1s,Count:1b,Damage:0s}"};
    static const Template lore = {"Lore line", "", "\"Lore\""};
    static const Template attr = {"Attribute", "", "{Name:\"generic.maxHealth\",Base:20.0d}"};
    static const Template modifier = {"Attribute modifier", "", "{AttributeName:\"generic.attackDamage\",Name:\"lce\",Amount:5.0d,Operation:0,UUIDMost:1L,UUIDLeast:1L}"};
    static const Template potential = {"Spawn potential", "", "{Type:\"Zombie\",Weight:1,Properties:{}}"};
    static const Template tag = {"tag (item data)", "tag", "{}"};
    static const Template display = {"display (name/lore)", "display", "{Name:\"Custom\",Lore:[;t]}"};
    static const Template name = {"Name", "Name", "\"Custom\""};
    static const Template lorelist = {"Lore", "Lore", "[\"Lore\"]"};
    static const Template enchlist = {"ench list", "ench", "[{id:16s,lvl:1s}]"};
    static const Template unbreak = {"Unbreakable", "Unbreakable", "1b"};
    static const Template hide = {"HideFlags", "HideFlags", "63"};
    static const Template repair = {"RepairCost", "RepairCost", "0"};
    static const Template mods = {"AttributeModifiers", "AttributeModifiers", "[{AttributeName:\"generic.attackDamage\",Name:\"lce\",Amount:5.0d,Operation:0,UUIDMost:1L,UUIDLeast:1L}]"};
    static const Template cname = {"CustomName", "CustomName", "\"Name\""};
    static const Template cvis = {"CustomNameVisible", "CustomNameVisible", "1b"};
    static const Template noai = {"NoAI", "NoAI", "1b"};
    static const Template inv = {"Invulnerable", "Invulnerable", "1b"};
    static const Template silent = {"Silent", "Silent", "1b"};
    static const Template persist = {"PersistenceRequired", "PersistenceRequired", "1b"};
    static const Template effects = {"ActiveEffects", "ActiveEffects", "[{Id:1b,Amplifier:0b,Duration:600,Ambient:0b,ShowParticles:1b}]"};
    static const Template spawndata = {"SpawnData", "SpawnData", "{}"};
    static const Template customcolor = {"display.color (leather)", "color", "16711680"};
    const Node &p = d.at(parent);
    if (p.type == T_LIST) {
        if (d.name_eq(parent, "ench") || d.name_eq(parent, "StoredEnchantments")) add(&ench);
        if (d.name_eq(parent, "ActiveEffects") || d.name_eq(parent, "CustomPotionEffects")) add(&effect);
        if (d.name_eq(parent, "Inventory") || d.name_eq(parent, "Items") || d.name_eq(parent, "EnderItems")) add(&item);
        if (d.name_eq(parent, "Equipment")) add(&equip);
        if (d.name_eq(parent, "Lore")) add(&lore);
        if (d.name_eq(parent, "Attributes")) add(&attr);
        if (d.name_eq(parent, "AttributeModifiers") || d.name_eq(parent, "Modifiers")) add(&modifier);
        if (d.name_eq(parent, "SpawnPotentials")) add(&potential);
    } else if (p.type == T_COMPOUND) {
        const bool is_item = d.find(parent, "Count") != kNil && d.find(parent, "id") != kNil;
        const bool is_tag = d.name_eq(parent, "tag") && p.parent != kNil;
        const bool is_display = d.name_eq(parent, "display");
        if (is_item && d.find(parent, "tag") == kNil) add(&tag);
        if (is_tag) { add(&display); add(&enchlist); add(&unbreak); add(&hide); add(&repair); add(&mods); }
        if (is_display) { add(&name); add(&lorelist); add(&customcolor); }
        if (parent == d.root && (kind == K_ENTITY)) { add(&cname); add(&cvis); add(&noai); add(&inv); add(&silent); add(&persist); add(&effects); }
        if (parent == d.root && kind == K_PLAYER) add(&effects);
        if (parent == d.root && kind == K_BLOCK && d.find(parent, "EntityId") != kNil) { add(&spawndata); }
    }
    for (const Template &t : kGeneric) {
        if (p.type == T_LIST && p.count) {
            // only element types the list can hold
            const char c = t.snbt[0];
            uint8_t ty = c == '{' ? T_COMPOUND : c == '"' ? T_STRING : c == '[' ? (t.snbt[1] == 'B' ? T_BYTE_ARRAY : t.snbt[1] == 'I' ? T_INT_ARRAY : T_LIST) : T_END;
            if (ty == T_END) { const char *q = t.snbt; while (*q == '-' || *q == '.' || (*q >= '0' && *q <= '9')) ++q;
                ty = *q == 'b' ? T_BYTE : *q == 's' ? T_SHORT : *q == 'L' ? T_LONG : *q == 'f' ? T_FLOAT : *q == 'd' ? T_DOUBLE : T_INT; }
            if (ty != p.elem) continue;
        }
        add(&t);
    }
    return n;
}

// Quick text presets for strings (colour codes are appended, others replace).
struct TextPreset { const char *label; const char *text; bool append; };
static const TextPreset kTextPresets[] = {
    {"Clear text", "", false}, {"Append colour: red", "\xC2\xA7" "c", true}, {"Append colour: gold", "\xC2\xA7" "6", true},
    {"Append colour: yellow", "\xC2\xA7" "e", true}, {"Append colour: green", "\xC2\xA7" "a", true}, {"Append colour: aqua", "\xC2\xA7" "b", true},
    {"Append colour: blue", "\xC2\xA7" "9", true}, {"Append colour: purple", "\xC2\xA7" "5", true}, {"Append colour: white", "\xC2\xA7" "f", true},
    {"Append colour: gray", "\xC2\xA7" "7", true}, {"Append colour: black", "\xC2\xA7" "0", true}, {"Append bold", "\xC2\xA7" "l", true},
    {"Append italic", "\xC2\xA7" "o", true}, {"Append reset", "\xC2\xA7" "r", true}, {"Text: Excalibur", "Excalibur", false},
    {"Text: Boss", "Boss", false}, {"Text: Steve", "Steve", false}, {"Text: Hello", "Hello", false},
};

}  // namespace lce_nbt_ed
