#include "ext.h"
#include <stddef.h>

#define F(kind, ref, type, field, name) {kind, ref, (uint32_t)offsetof(type, field), 0, name}
#define A(kind, ref, type, field, cnt, name) {kind, ref, (uint32_t)offsetof(type, field), (uint32_t)offsetof(type, cnt), name}

namespace {
const lce_ext_field kEffect[] = {
    F(LCE_F_KEY, 0, lce_effect_desc, key, "key"), F(LCE_F_TEXT, 0, lce_effect_desc, name, "name"),
    F(LCE_F_U32, 0, lce_effect_desc, color_rgb, "color"), F(LCE_F_U8, 0, lce_effect_desc, beneficial, "beneficial"),
    F(LCE_F_U8, 0, lce_effect_desc, instant, "instant"), F(LCE_F_REF, LCE_REF_TEXTURE_ICON, lce_effect_desc, icon_texture, "icon"),
    F(LCE_F_REF, LCE_REF_EFFECT, lce_effect_desc, clone_of, "clone_of"),
};
const lce_ext_field kPotion[] = {
    F(LCE_F_KEY, 0, lce_potion_desc, key, "key"), F(LCE_F_TEXT, 0, lce_potion_desc, name, "name"),
    A(LCE_F_ARR_EFFECT, LCE_REF_EFFECT, lce_potion_desc, effects, effect_count, "effects"),
    F(LCE_F_U8, 0, lce_potion_desc, splash, "splash"), F(LCE_F_U8, 0, lce_potion_desc, lingering, "lingering"),
};
const lce_ext_field kBrew[] = {
    F(LCE_F_KEY, 0, lce_brew_desc, key, "key"), F(LCE_F_REF_REQ, LCE_REF_POTION, lce_brew_desc, input_potion, "input"),
    F(LCE_F_REF_REQ, LCE_REF_ITEM, lce_brew_desc, reagent, "reagent"), F(LCE_F_REF_REQ, LCE_REF_POTION, lce_brew_desc, output_potion, "output"),
};
const lce_ext_field kEnchant[] = {
    F(LCE_F_KEY, 0, lce_enchant_desc, key, "key"), F(LCE_F_TEXT, 0, lce_enchant_desc, name, "name"),
    F(LCE_F_U8, 0, lce_enchant_desc, max_level, "max_level"), F(LCE_F_U8, 0, lce_enchant_desc, weight, "weight"),
    F(LCE_F_U16, 0, lce_enchant_desc, applies_to, "applies_to"), F(LCE_F_U8, 0, lce_enchant_desc, treasure, "treasure"),
    F(LCE_F_REF, LCE_REF_NONE, lce_enchant_desc, clone_of, "clone_of"),
};
const lce_ext_field kFuel[] = {
    F(LCE_F_REF_REQ, LCE_REF_ITEM, lce_fuel_desc, item, "item"), F(LCE_F_U32, 0, lce_fuel_desc, burn_ticks, "burn_ticks"),
};
const lce_ext_field kEntity[] = {
    F(LCE_F_KEY, 0, lce_entity_desc2, key, "key"), F(LCE_F_REF_REQ, LCE_REF_ENTITY, lce_entity_desc2, base_mob, "base_mob"),
    F(LCE_F_REF, LCE_REF_TEXTURE_ENTITY, lce_entity_desc2, texture, "texture"), F(LCE_F_U32, 0, lce_entity_desc2, max_health_milli, "health"),
    F(LCE_F_U32, 0, lce_entity_desc2, spawn_weight, "spawn_weight"), F(LCE_F_TEXT, 0, lce_entity_desc2, name, "name"),
    F(LCE_F_U16, 0, lce_entity_desc2, category, "category"), F(LCE_F_U8, 0, lce_entity_desc2, min_group, "min_group"),
    F(LCE_F_U8, 0, lce_entity_desc2, max_group, "max_group"), F(LCE_F_U32, 0, lce_entity_desc2, attack_damage_milli, "attack_damage"),
    F(LCE_F_U32, 0, lce_entity_desc2, move_speed_milli, "move_speed"), F(LCE_F_U32, 0, lce_entity_desc2, width_milli, "width"),
    F(LCE_F_U32, 0, lce_entity_desc2, height_milli, "height"),
    A(LCE_F_ARR_GOAL, 0, lce_entity_desc2, goals, goal_count, "goals"),
    A(LCE_F_ARR_DROP, LCE_REF_ITEM, lce_entity_desc2, drops, drop_count, "drops"),
    A(LCE_F_ARR_REF, LCE_REF_BIOME, lce_entity_desc2, spawn_biomes, spawn_biome_count, "spawn_biomes"),
    F(LCE_F_U32, 0, lce_entity_desc2, egg_primary_rgb, "egg_primary"), F(LCE_F_U32, 0, lce_entity_desc2, egg_secondary_rgb, "egg_secondary"),
    F(LCE_F_REF, LCE_REF_SOUND, lce_entity_desc2, sound_ambient, "sound_ambient"), F(LCE_F_REF, LCE_REF_SOUND, lce_entity_desc2, sound_hurt, "sound_hurt"),
    F(LCE_F_REF, LCE_REF_SOUND, lce_entity_desc2, sound_death, "sound_death"),
};
const lce_ext_field kFeature[] = {
    F(LCE_F_KEY, 0, lce_feature_desc, key, "key"), F(LCE_F_U16, 0, lce_feature_desc, kind, "kind"),
    F(LCE_F_U8, 0, lce_feature_desc, dimension, "dimension"), F(LCE_F_REF, LCE_REF_BLOCK, lce_feature_desc, block, "block"),
    F(LCE_F_REF, LCE_REF_BLOCK, lce_feature_desc, block2, "block2"), F(LCE_F_REF, LCE_REF_STRUCTURE, lce_feature_desc, structure, "structure"),
    F(LCE_F_U16, 0, lce_feature_desc, size, "size"), F(LCE_F_U16, 0, lce_feature_desc, per_chunk, "per_chunk"),
    F(LCE_F_U16, 0, lce_feature_desc, min_y, "min_y"), F(LCE_F_U16, 0, lce_feature_desc, max_y, "max_y"),
    F(LCE_F_U16, 0, lce_feature_desc, rarity, "rarity"),
    A(LCE_F_ARR_REF, LCE_REF_BIOME, lce_feature_desc, biomes, biome_count, "biomes"),
};
const lce_ext_field kBiome[] = {
    F(LCE_F_KEY, 0, lce_biome_desc, key, "key"), F(LCE_F_TEXT, 0, lce_biome_desc, name, "name"),
    F(LCE_F_U32, 0, lce_biome_desc, temperature_milli, "temperature"), F(LCE_F_U32, 0, lce_biome_desc, rainfall_milli, "rainfall"),
    F(LCE_F_REF, LCE_REF_BLOCK, lce_biome_desc, top_block, "top_block"), F(LCE_F_REF, LCE_REF_BLOCK, lce_biome_desc, filler_block, "filler_block"),
    F(LCE_F_U32, 0, lce_biome_desc, grass_rgb, "grass"), F(LCE_F_U32, 0, lce_biome_desc, foliage_rgb, "foliage"),
    F(LCE_F_U32, 0, lce_biome_desc, water_rgb, "water"), F(LCE_F_U32, 0, lce_biome_desc, sky_rgb, "sky"),
    F(LCE_F_U16, 0, lce_biome_desc, weight, "weight"), F(LCE_F_U16, 0, lce_biome_desc, height_base_milli, "height_base"),
    F(LCE_F_U16, 0, lce_biome_desc, height_variation_milli, "height_variation"),
    A(LCE_F_ARR_REF, LCE_REF_FEATURE, lce_biome_desc, features, feature_count, "features"),
};
const lce_ext_field kStructure[] = {
    F(LCE_F_KEY, 0, lce_structure_desc, key, "key"), F(LCE_F_U16, 0, lce_structure_desc, size_x, "size_x"),
    F(LCE_F_U16, 0, lce_structure_desc, size_y, "size_y"), F(LCE_F_U16, 0, lce_structure_desc, size_z, "size_z"),
    A(LCE_F_ARR_REF, LCE_REF_BLOCK, lce_structure_desc, palette, palette_count, "palette"),
    F(LCE_F_ARR_U16_XYZ, 0, lce_structure_desc, blocks, "blocks"),
};
const lce_ext_field kSound[] = {
    F(LCE_F_KEY, 0, lce_sound_desc, key, "key"), F(LCE_F_REF, LCE_REF_SOUND, lce_sound_desc, clone_of, "clone_of"),
    A(LCE_F_BLOB, 0, lce_sound_desc, pcm, pcm_bytes, "pcm"), F(LCE_F_U32, 0, lce_sound_desc, sample_rate, "sample_rate"),
    F(LCE_F_U32, 0, lce_sound_desc, volume_milli, "volume"), F(LCE_F_U32, 0, lce_sound_desc, pitch_milli, "pitch"),
};
const lce_ext_field kParticle[] = {
    F(LCE_F_KEY, 0, lce_particle_desc, key, "key"), F(LCE_F_REF_REQ, LCE_REF_PARTICLE, lce_particle_desc, clone_of, "clone_of"),
    F(LCE_F_U32, 0, lce_particle_desc, tint_rgb, "tint"), F(LCE_F_REF, LCE_REF_TEXTURE_ICON, lce_particle_desc, texture, "texture"),
};
const lce_ext_field kCommand[] = {
    F(LCE_F_NAME, 0, lce_command_desc, name, "name"), F(LCE_F_TEXT, 0, lce_command_desc, usage, "usage"),
    F(LCE_F_U8, 0, lce_command_desc, op_only, "op_only"), F(LCE_F_PTR, 0, lce_command_desc, fn, "fn"), F(LCE_F_PTR, 0, lce_command_desc, user, "user"),
};
const lce_ext_field kKeybind[] = {
    F(LCE_F_KEY, 0, lce_keybind_desc, key, "key"), F(LCE_F_TEXT, 0, lce_keybind_desc, name, "name"),
    F(LCE_F_U16, 0, lce_keybind_desc, default_button, "button"), F(LCE_F_PTR, 0, lce_keybind_desc, fn, "fn"), F(LCE_F_PTR, 0, lce_keybind_desc, user, "user"),
};

#define S(n, t, cap, arr, pack) {n, (uint32_t)sizeof(t), cap, (uint32_t)(sizeof arr / sizeof arr[0]), arr, pack}
const lce_ext_schema kSchemas[LCE_EXT_TYPE_COUNT] = {
    {nullptr, 0, 0, 0, nullptr, 0},
    S("effect", lce_effect_desc, LCE_CAP_EFFECTS, kEffect, 1),
    S("potion", lce_potion_desc, LCE_CAP_EFFECTS, kPotion, 1),
    S("brewing", lce_brew_desc, LCE_CAP_BREWING, kBrew, 1),
    S("enchantment", lce_enchant_desc, LCE_CAP_ENCHANTS, kEnchant, 1),
    S("fuel", lce_fuel_desc, LCE_CAP_FUEL, kFuel, 1),
    S("entity", lce_entity_desc2, LCE_CAP_ENTITIES, kEntity, 1),
    S("feature", lce_feature_desc, LCE_CAP_WORLDGEN, kFeature, 1),
    S("biome", lce_biome_desc, LCE_CAP_BIOMES, kBiome, 1),
    S("structure", lce_structure_desc, LCE_CAP_STRUCTURES, kStructure, 1),
    S("sound", lce_sound_desc, LCE_CAP_SOUNDS, kSound, 1),
    S("particle", lce_particle_desc, LCE_CAP_PARTICLES, kParticle, 1),
    S("command", lce_command_desc, LCE_CAP_COMMANDS, kCommand, 0),
    S("keybind", lce_keybind_desc, LCE_CAP_KEYBINDS, kKeybind, 0),
};
}  // namespace

extern "C" const lce_ext_schema *lce_ext_schema_of(uint32_t type) {
    return type >= 1 && type < LCE_EXT_TYPE_COUNT ? &kSchemas[type] : nullptr;
}
