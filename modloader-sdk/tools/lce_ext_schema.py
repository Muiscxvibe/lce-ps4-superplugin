"""Python mirror of loader/ext.cpp schemas (field order and kinds MUST match).

Each entry: manifest section -> (type id, [(field, kind, converter-hint)]).
tests/test_tools.py round-trips every type through the C parser to catch drift."""

GOALS = ['wander', 'look_at_player', 'panic', 'melee_attack', 'target_player', 'flee_sun', 'follow_tempt_item', 'breed', 'swim', 'avoid_entity']
FEATURE_KINDS = ['ore', 'tree', 'flower', 'lake', 'structure']
CATEGORIES = ['creature', 'monster', 'water', 'ambient']
DIMENSIONS = ['overworld', 'nether', 'end']
BUTTONS = ['l3', 'r3', 'touchpad', 'dpad_left_hold', 'dpad_right_hold']
ITEM_KINDS = ['generic', 'food', 'sword', 'pickaxe', 'axe', 'shovel', 'hoe', 'helmet', 'chestplate', 'leggings', 'boots']

# kinds: key text ref ref_req u8 u16 u32 milli rgb arr_effect arr_goal arr_drop arr_ref u16xyz blob
SCHEMAS = {
    'effects': (1, [('key', 'key'), ('name', 'text'), ('color', 'rgb'), ('beneficial', 'u8'), ('instant', 'u8'),
                    ('icon', 'ref'), ('clone_of', 'ref')]),
    'potions': (2, [('key', 'key'), ('name', 'text'), ('effects', 'arr_effect'), ('splash', 'u8'), ('lingering', 'u8')]),
    'brewing': (3, [('key', 'key'), ('input', 'ref_req'), ('reagent', 'ref_req'), ('output', 'ref_req')]),
    'enchantments': (4, [('key', 'key'), ('name', 'text'), ('max_level', 'u8'), ('weight', 'u8'), ('applies_to', 'kinds'),
                         ('treasure', 'u8'), ('clone_of', 'ref')]),
    'fuels': (5, [('item', 'ref_req'), ('burn_ticks', 'u32')]),
    'entities': (6, [('key', 'key'), ('base_mob', 'ref_req'), ('texture', 'ref'), ('health', 'milli'), ('spawn_weight', 'u32'),
                     ('name', 'text'), ('category', 'category'), ('min_group', 'u8'), ('max_group', 'u8'), ('attack_damage', 'milli'),
                     ('move_speed', 'milli'), ('width', 'milli'), ('height', 'milli'), ('goals', 'arr_goal'), ('drops', 'arr_drop'),
                     ('spawn_biomes', 'arr_ref'), ('egg_primary', 'rgb'), ('egg_secondary', 'rgb'), ('sound_ambient', 'ref'),
                     ('sound_hurt', 'ref'), ('sound_death', 'ref')]),
    'features': (7, [('key', 'key'), ('kind', 'feature_kind'), ('dimension', 'dimension'), ('block', 'ref'), ('block2', 'ref'),
                     ('structure', 'ref'), ('size', 'u16'), ('per_chunk', 'u16'), ('min_y', 'u16'), ('max_y', 'u16'),
                     ('rarity', 'u16'), ('biomes', 'arr_ref')]),
    'biomes': (8, [('key', 'key'), ('name', 'text'), ('temperature', 'milli'), ('rainfall', 'milli'), ('top_block', 'ref'),
                   ('filler_block', 'ref'), ('grass', 'rgb'), ('foliage', 'rgb'), ('water', 'rgb'), ('sky', 'rgb'), ('weight', 'u16'),
                   ('height_base', 'milli16'), ('height_variation', 'milli16'), ('features', 'arr_ref')]),
    'structures': (9, [('key', 'key'), ('size_x', 'u16'), ('size_y', 'u16'), ('size_z', 'u16'), ('palette', 'arr_ref'), ('blocks', 'u16xyz')]),
    'sounds': (10, [('key', 'key'), ('clone_of', 'ref'), ('pcm', 'blob'), ('sample_rate', 'u32'), ('volume', 'milli'), ('pitch', 'milli')]),
    'particles': (11, [('key', 'key'), ('clone_of', 'ref_req'), ('tint', 'rgb'), ('texture', 'ref')]),
}
ARR_WORDS = {'arr_effect': 4, 'arr_goal': 4, 'arr_drop': 5, 'arr_ref': 1}


def slots_for(kind):
    return 2 if kind in ARR_WORDS or kind == 'blob' else 1
