#!/usr/bin/env python3
"""Convert a simple Java (Forge/Fabric/NeoForge resource layout) or Bedrock
add-on content mod into an LCE SDK source package (mod.json + 16x16 textures),
which lce_modpack.py then compiles into mod.lcm.

  lce_port.py java    <mod-root-or-jar> <out-src-dir> [--modid ID] [--hints port.json] [--shapeless-as-shaped]
  lce_port.py bedrock <behavior-pack> <resource-pack> <out-src-dir> [--hints port.json]

Only DATA is converted: items, blocks, shaped/shapeless/smelting recipes,
textures and en_us names. Java code (custom behaviour, entities, worldgen,
GUIs) cannot be converted automatically; the converter writes PORT_REPORT.md
listing everything it skipped or guessed so the porter can decide. Properties
that Java mods set in code (hardness, tool kind, durability, food) come from
the optional hints file (see PORTING.md) or fall back to documented defaults.

Vanilla references are translated from modern flattened names
(minecraft:oak_planks) to LCE 1.95 names with data values (planks@0) using
the table below; anything unknown is reported, never guessed silently.
"""
import argparse
import io
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import lce_modpack  # noqa: E402

COLORS = ['white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink', 'gray', 'light_gray', 'cyan', 'purple', 'blue',
          'brown', 'green', 'red', 'black']
WOODS = ['oak', 'spruce', 'birch', 'jungle', 'acacia', 'dark_oak']
DYE_ITEMS = {  # LCE dye data value = 15 - wool colour index
    'black_dye': 0, 'ink_sac': 0, 'red_dye': 1, 'green_dye': 2, 'cocoa_beans': 3, 'brown_dye': 3, 'lapis_lazuli': 4, 'blue_dye': 4,
    'purple_dye': 5, 'cyan_dye': 6, 'light_gray_dye': 7, 'gray_dye': 8, 'pink_dye': 9, 'lime_dye': 10, 'yellow_dye': 11,
    'light_blue_dye': 12, 'magenta_dye': 13, 'orange_dye': 14, 'bone_meal': 15, 'white_dye': 15}


def java_to_lce_table():
    t = {}
    for i, w in enumerate(WOODS):
        t[f'{w}_planks'] = f'planks@{i}'
        t[f'{w}_log'] = f'log@{i}' if i < 4 else f'log2@{i - 4}'
        t[f'{w}_sapling'] = f'sapling@{i}'
        t[f'{w}_slab'] = f'wooden_slab@{i}'
        t[f'{w}_boat'] = 'boat' if w == 'oak' else f'{w}_boat'
        t[f'{w}_door'] = 'wooden_door' if w == 'oak' else f'{w}_door'
        t[f'{w}_fence'] = 'fence' if w == 'oak' else f'{w}_fence'
        t[f'{w}_stairs'] = f'{w}_stairs' if w != 'oak' else 'oak_stairs'
    for i, c in enumerate(COLORS):
        cc = 'silver' if c == 'light_gray' else c
        t[f'{c}_wool'] = f'wool@{i}'
        t[f'{c}_carpet'] = f'carpet@{i}'
        t[f'{c}_stained_glass'] = f'stained_glass@{i}'
        t[f'{c}_terracotta'] = f'stained_hardened_clay@{i}'
        t[f'{c}_concrete'] = f'concrete@{i}'
        t[f'{c}_bed'] = f'bed@{i}'
        t[f'{c}_glazed_terracotta'] = f'{cc}_glazed_terracotta'
    for k, v in DYE_ITEMS.items():
        t[k] = f'dye@{v}'
    t.update({
        'granite': 'stone@1', 'polished_granite': 'stone@2', 'diorite': 'stone@3', 'polished_diorite': 'stone@4',
        'andesite': 'stone@5', 'polished_andesite': 'stone@6', 'grass_block': 'grass', 'coarse_dirt': 'dirt@1', 'podzol': 'dirt@2',
        'charcoal': 'coal@1', 'stone_bricks': 'stonebrick@0', 'mossy_stone_bricks': 'stonebrick@1', 'cracked_stone_bricks': 'stonebrick@2',
        'chiseled_stone_bricks': 'stonebrick@3', 'bricks': 'brick_block', 'nether_bricks': 'nether_brick', 'terracotta': 'hardened_clay',
        'cobweb': 'web', 'sugar_cane': 'reeds', 'melon': 'melon_block', 'melon_slice': 'melon', 'snow_block': 'snow', 'snow': 'snow_layer',
        'gunpowder': 'gunpowder', 'clock': 'clock', 'oak_pressure_plate': 'wooden_pressure_plate', 'oak_button': 'wooden_button',
        'enchanted_golden_apple': 'golden_apple@1', 'cod': 'fish@0', 'salmon': 'fish@1', 'tropical_fish': 'fish@2', 'pufferfish': 'fish@3',
        'cooked_cod': 'cooked_fish@0', 'cooked_salmon': 'cooked_fish@1', 'glistering_melon_slice': 'speckled_melon', 'nether_quartz_ore': 'quartz_ore',
        'quartz': 'quartz', 'smooth_stone': 'smoothstone', 'crafting_table': 'crafting_table', 'oak_sign': 'sign', 'torch': 'torch',
        'repeater': 'repeater', 'comparator': 'comparator', 'redstone_torch': 'redstone_torch', 'magma_block': 'magma',
        'firework_rocket': 'fireworks', 'firework_star': 'firework_charge', 'map': 'map', 'filled_map': 'filled_map',
        'experience_bottle': 'experience_bottle', 'wheat': 'wheat', 'sandstone': 'sandstone@0', 'chiseled_sandstone': 'sandstone@1',
        'cut_sandstone': 'sandstone@2', 'red_sand': 'sand@1', 'wet_sponge': 'sponge@1', 'lily_pad': 'waterlily', 'poppy': 'red_flower@0',
        'dandelion': 'yellow_flower', 'jack_o_lantern': 'lit_pumpkin', 'spawner': 'mob_spawner', 'iron_bars': 'iron_bars',
        'note_block': 'noteblock', 'powered_rail': 'golden_rail', 'nether_wart': 'nether_wart', 'end_stone_bricks': 'end_bricks',
        'slime_block': 'slime', 'scute': 'turtle_shell_piece', 'string': 'string', 'cobblestone_slab': 'stone_slab@3', 'stone_slab': 'stone_slab@0',
    })
    return t


# Modern (1.13+ flattened) names whose 1.95 name differs (merged into JAVA_TO_LCE below).
# Every target is asserted to exist in tools/data/vanilla_195.json by the tests.
MODERN_EXTRA = {
    # items whose 1.13+ name differs from the 1.95 one
    'nether_brick': 'netherbrick', 'popped_chorus_fruit': 'chorus_fruit_popped', 'heart_of_the_sea': 'nautilus_core',
    'nautilus_shell': 'nautilus', 'cod_bucket': 'fish_bucket', 'pufferfish_bucket': 'puffer_bucket',
    'tropical_fish_bucket': 'tropical_bucket', 'turtle_scute': 'turtle_shell_piece', 'potion': 'potion',
    'red_nether_bricks': 'red_nether_brick', 'end_stone_bricks': 'end_bricks', 'carved_pumpkin': 'carved_pumpkin',
    'pumpkin': 'pumpkin', 'seagrass': 'sea_grass', 'blast_furnace': 'blastfurnace', 'stonecutter': 'stonecutter2',
    'dead_bush': 'deadbush', 'grass': 'tallgrass@1', 'short_grass': 'tallgrass@1', 'fern': 'tallgrass@2',
    'sunflower': 'double_plant@0', 'lilac': 'double_plant@1', 'tall_grass': 'double_plant@2', 'large_fern': 'double_plant@3',
    'rose_bush': 'double_plant@4', 'peony': 'double_plant@5',
    'blue_orchid': 'red_flower@1', 'allium': 'red_flower@2', 'azure_bluet': 'red_flower@3', 'red_tulip': 'red_flower@4',
    'orange_tulip': 'red_flower@5', 'white_tulip': 'red_flower@6', 'pink_tulip': 'red_flower@7', 'oxeye_daisy': 'red_flower@8',
    'prismarine_bricks': 'prismarine@1', 'dark_prismarine': 'prismarine@2', 'chiseled_quartz_block': 'quartz_block@1',
    'quartz_pillar': 'quartz_block@2', 'infested_stone': 'monster_egg@0', 'infested_cobblestone': 'monster_egg@1',
    'infested_stone_bricks': 'monster_egg@2', 'mossy_cobblestone_wall': 'cobblestone_wall@1', 'cobblestone_wall': 'cobblestone_wall@0',
    'skeleton_skull': 'skull@0', 'wither_skeleton_skull': 'skull@1', 'zombie_head': 'skull@2', 'player_head': 'skull@3',
    'creeper_head': 'skull@4', 'dragon_head': 'skull@5', 'oak_leaves': 'leaves@0', 'spruce_leaves': 'leaves@1',
    'birch_leaves': 'leaves@2', 'jungle_leaves': 'leaves@3', 'acacia_leaves': 'leaves2@0', 'dark_oak_leaves': 'leaves2@1',
    'oak_fence_gate': 'fence_gate', 'oak_trapdoor': 'trapdoor', 'sandstone_slab': 'stone_slab@1', 'petrified_oak_slab': 'stone_slab@2',
    'brick_slab': 'stone_slab@4', 'stone_brick_slab': 'stone_slab@5', 'nether_brick_slab': 'stone_slab@6', 'quartz_slab': 'stone_slab@7',
    'brown_mushroom_block': 'brown_mushroom_block', 'red_mushroom_block': 'red_mushroom_block', 'spawner': 'mob_spawner',
    'enchanting_table': 'enchanting_table', 'end_portal_frame': 'end_portal_frame', 'dirt_path': 'grass_path', 'grass_path': 'grass_path',
    'mushroom_stew': 'mushroom_stew', 'wheat_seeds': 'wheat_seeds', 'writable_book': 'writable_book', 'shulker_box': 'undyed_shulker_box',
    'light_gray_shulker_box': 'silver_shulker_box', 'light_gray_glazed_terracotta': 'silver_glazed_terracotta',
    'scaffolding': 'scaffolding', 'bamboo': 'bamboo', 'lantern': 'lantern', 'bell': 'bell', 'campfire': 'campfire',
    'barrel': 'barrel', 'loom': 'loom', 'composter': 'composter', 'lectern': 'lectern', 'smoker': 'smoker',
    'grindstone': 'grindstone', 'cartography_table': 'cartography_table', 'fletching_table': 'fletching_table',
    'smithing_table': 'smithing_table', 'sweet_berries': 'sweet_berries', 'crossbow': 'crossbow', 'conduit': 'conduit',
    'trident': 'trident', 'kelp': 'kelp', 'dried_kelp': 'dried_kelp', 'dried_kelp_block': 'dried_kelp_block',
    'turtle_egg': 'turtle_egg', 'turtle_helmet': 'turtle_helmet', 'phantom_membrane': 'phantom_membrane', 'sea_pickle': 'sea_pickle',
    'blue_ice': 'blue_ice', 'leather_horse_armor': 'leather_horse_armor', 'stone_stairs': 'stone_stairs',
    'mossy_cobblestone_stairs': 'mossy_cobblestone_stairs', 'andesite_stairs': 'andesite_stairs', 'diorite_stairs': 'diorite_stairs',
    'granite_stairs': 'granite_stairs', 'polished_andesite_stairs': 'smooth_andesite_stairs', 'polished_diorite_stairs': 'smooth_diorite_stairs',
    'polished_granite_stairs': 'smooth_granite_stairs', 'smooth_quartz_stairs': 'smooth_quartz_stairs',
    'mossy_stone_brick_stairs': 'mossy_stone_brick_stairs', 'smooth_sandstone_stairs': 'smooth_sandstone_stairs',
    'smooth_red_sandstone_stairs': 'smooth_red_sandstone_stairs', 'red_nether_brick_stairs': 'red_nether_brick_stairs',
    'end_stone_brick_stairs': 'end_brick_stairs', 'prismarine_stairs': 'prismarine_stairs', 'prismarine_brick_stairs': 'prismarine_bricks_stairs',
    'dark_prismarine_stairs': 'dark_prismarine_stairs', 'red_sandstone': 'red_sandstone', 'stone_bricks': 'stonebrick@0',
    'music_disc_13': 'record_13', 'music_disc_cat': 'record_cat', 'music_disc_blocks': 'record_blocks', 'music_disc_chirp': 'record_chirp',
    'music_disc_far': 'record_far', 'music_disc_mall': 'record_mall', 'music_disc_mellohi': 'record_mellohi', 'music_disc_stal': 'record_stal',
    'music_disc_strad': 'record_strad', 'music_disc_ward': 'record_ward', 'music_disc_11': 'record_11', 'music_disc_wait': 'record_wait',
    'iron_nugget': 'iron_nugget', 'shulker_shell': 'shulker_shell', 'totem_of_undying': 'totem_of_undying',
    'tube_coral_block': 'coral_block@0', 'brain_coral_block': 'coral_block@1', 'bubble_coral_block': 'coral_block@2',
    'fire_coral_block': 'coral_block@3', 'horn_coral_block': 'coral_block@4', 'tube_coral': 'coral@0', 'brain_coral': 'coral@1',
    'bubble_coral': 'coral@2', 'fire_coral': 'coral@3', 'horn_coral': 'coral@4', 'tube_coral_fan': 'coral_fan@0',
    'brain_coral_fan': 'coral_fan@1', 'bubble_coral_fan': 'coral_fan@2', 'fire_coral_fan': 'coral_fan@3', 'horn_coral_fan': 'coral_fan@4',
    'creeper_banner_pattern': 'banner_pattern@0', 'skull_banner_pattern': 'banner_pattern@1', 'flower_banner_pattern': 'banner_pattern@2',
    'mojang_banner_pattern': 'banner_pattern@3', 'bamboo_sapling': 'bamboo_sapling', 'smooth_stone': 'smoothstone',
    'nether_wart_block': 'nether_wart_block', 'bone_block': 'bone_block', 'scute': 'turtle_shell_piece', 'oak_wood': 'wood@0',
}
for _i, _w in enumerate(WOODS):
    MODERN_EXTRA[f'stripped_{_w}_log'] = f'stripped_log_{_w}'
    if _w != 'oak':
        MODERN_EXTRA[f'{_w}_trapdoor'] = f'{_w}_trapdoor'
        MODERN_EXTRA[f'{_w}_sign'] = f'sign_{_w}'
        MODERN_EXTRA[f'{_w}_button'] = f'{_w}_button'
        MODERN_EXTRA[f'{_w}_pressure_plate'] = f'{_w}_pressure_plate'
        MODERN_EXTRA[f'{_w}_fence_gate'] = f'{_w}_fence_gate'
for _c in COLORS:
    _cc = 'silver' if _c == 'light_gray' else _c
    MODERN_EXTRA[f'{_c}_shulker_box'] = f'{_cc}_shulker_box'
    MODERN_EXTRA[f'{_c}_concrete_powder'] = f'concrete_powder@{COLORS.index(_c)}'
    MODERN_EXTRA[f'{_c}_stained_glass_pane'] = f'stained_glass_pane@{COLORS.index(_c)}'
    MODERN_EXTRA[f'{_c}_banner'] = f'banner@{15 - COLORS.index(_c)}'

for _i, _w in enumerate(WOODS[1:], 1):
    MODERN_EXTRA[f'{_w}_wood'] = f'wood@{_i}'
MODERN_EXTRA.update({
    'chipped_anvil': 'anvil@1', 'damaged_anvil': 'anvil@2', 'infested_mossy_stone_bricks': 'monster_egg@3',
    'infested_cracked_stone_bricks': 'monster_egg@4', 'infested_chiseled_stone_bricks': 'monster_egg@5',
    'chiseled_red_sandstone': 'red_sandstone@1', 'cut_red_sandstone': 'red_sandstone@2', 'red_sandstone_slab': 'stone_slab_3@0',
    'purpur_slab': 'stone_slab_4@0', 'smooth_stone_slab': 'stone_slab@0',
})

# Pre-flattening (1.7.10-1.12) registry/field names whose 1.95 name differs.
# 1.95 numeric IDs equal the Java 1.12 IDs (checked against tools/data/vanilla_195.json),
# so these are exact by ID: 86 pumpkin -> carved_pumpkin, 182 stone_slab2 -> stone_slab_3 ...
OLD_TO_LCE = {
    'pumpkin': 'carved_pumpkin', 'stone_slab2': 'stone_slab_3', 'double_stone_slab2': 'double_stone_slab_3',
    'purpur_slab': 'stone_slab_4', 'slime_block': 'slime', 'potionitem': 'potion', 'cooked_fished': 'cooked_fish',
    'cooked_porkchop': 'cooked_porkchop', 'magma': 'magma', 'red_nether_brick': 'red_nether_brick', 'stonebrick': 'stonebrick',
    'noteblock': 'noteblock', 'golden_rail': 'golden_rail', 'mob_spawner': 'mob_spawner', 'speckled_melon': 'speckled_melon',
    'shulker_box': 'undyed_shulker_box', 'banner': 'banner', 'standing_banner': 'banner', 'wall_banner': 'banner',
    'lit_redstone_lamp': 'lit_redstone_lamp', 'brick_block': 'brick_block', 'record_13': 'record_13',
}

JAVA_TO_LCE = {**java_to_lce_table(), **MODERN_EXTRA}
JAVA_TAGS = {  # common item tags -> LCE ingredient with wildcard aux
    'minecraft:planks': 'minecraft:planks', 'minecraft:logs': 'minecraft:log', 'minecraft:wool': 'minecraft:wool',
    'minecraft:wooden_slabs': 'minecraft:wooden_slab', 'minecraft:coals': 'minecraft:coal', 'minecraft:sand': 'minecraft:sand',
    'forge:ingots/iron': 'minecraft:iron_ingot', 'c:iron_ingots': 'minecraft:iron_ingot', 'forge:ingots/gold': 'minecraft:gold_ingot',
    'c:gold_ingots': 'minecraft:gold_ingot', 'forge:gems/diamond': 'minecraft:diamond', 'c:diamonds': 'minecraft:diamond',
    'forge:rods/wooden': 'minecraft:stick', 'c:wooden_rods': 'minecraft:stick', 'forge:dusts/redstone': 'minecraft:redstone',
    'c:redstone_dusts': 'minecraft:redstone', 'forge:cobblestone': 'minecraft:cobblestone', 'c:cobblestones': 'minecraft:cobblestone',
    'forge:string': 'minecraft:string', 'c:strings': 'minecraft:string', 'forge:gems/emerald': 'minecraft:emerald', 'c:emeralds': 'minecraft:emerald',
    'forge:stone': 'minecraft:stone', 'forge:glass': 'minecraft:glass', 'forge:leather': 'minecraft:leather', 'c:leathers': 'minecraft:leather',
}
WILDCARD_TAGS = {'minecraft:planks', 'minecraft:logs', 'minecraft:wool', 'minecraft:wooden_slabs', 'minecraft:coals', 'minecraft:sand'}


class Report:
    def __init__(self):
        self.lines = {'converted': [], 'guessed': [], 'skipped': [], 'errors': []}

    def add(self, kind, msg):
        self.lines[kind].append(msg)

    def write(self, path, title):
        out = [f'# Port report: {title}', '']
        for k in ('errors', 'skipped', 'guessed', 'converted'):
            out.append(f'## {k.title()} ({len(self.lines[k])})')
            out += [f'- {m}' for m in self.lines[k]] or ['- none']
            out.append('')
        Path(path).write_text('\n'.join(out))


def vanilla_ref(name, rep, where, old=False, data=None):
    """'minecraft:x' -> 'minecraft:lce_name[@aux]' or None.

    old: a 1.7.10-1.12 name (assets/<ns>/recipes): pre-flattening names map almost 1:1 (OLD_TO_LCE), and the 1.13
    renames must not apply ("nether_brick" is the block there, the brick item is "netherbrick").
    data: the 1.12 JSON "data" value (sub-type), appended as @aux."""
    n = name.split(':', 1)[1]
    if old:
        lce = OLD_TO_LCE.get(n) or (n if n in lce_modpack.VANILLA_ITEMS else None)
        if lce is None or lce.split('@')[0] not in lce_modpack.VANILLA_ITEMS:
            rep.add('errors', f'{where}: vanilla item minecraft:{n} (1.12 name) has no LCE 1.95 equivalent; add it to hints "aliases"')
            return None
        if data is not None and int(data) not in (-1, 32767) and '@' not in lce:
            lce = f'{lce}@{int(data)}'
        if lce != n:
            rep.add('converted', f'{where}: minecraft:{n}{"" if data is None else f" data {data}"} -> minecraft:{lce}')
        return 'minecraft:' + lce
    if n in JAVA_TO_LCE:
        lce = JAVA_TO_LCE[n]
        base = lce.split('@')[0]
        if base in lce_modpack.VANILLA_ITEMS:
            if lce != n:
                rep.add('converted', f'{where}: minecraft:{n} -> minecraft:{lce}')
            return 'minecraft:' + lce
    if n in lce_modpack.VANILLA_ITEMS:
        return 'minecraft:' + n
    rep.add('errors', f'{where}: vanilla item minecraft:{n} has no LCE 1.95 equivalent in the table; add it to hints "aliases"')
    return None


class Source:
    """Read files from a directory or a .jar/.zip uniformly."""

    def __init__(self, path):
        self.path = Path(path)
        self.zip = zipfile.ZipFile(self.path) if self.path.is_file() else None

    def names(self):
        if self.zip:
            return [n for n in self.zip.namelist() if not n.endswith('/')]
        return [p.relative_to(self.path).as_posix() for p in self.path.rglob('*') if p.is_file()]

    def read(self, name):
        if self.zip:
            return self.zip.read(name)
        return (self.path / name).read_bytes()

    def exists(self, name):
        try:
            self.read(name)
            return True
        except (KeyError, FileNotFoundError, IsADirectoryError):
            return False


def save_texture(src_bytes, dst, rep, where):
    from PIL import Image
    im = Image.open(io.BytesIO(src_bytes)).convert('RGBA')
    if im.size[0] != im.size[1] * 1 and im.size[1] % im.size[0] == 0:
        rep.add('guessed', f'{where}: animated strip {im.size}; using first frame')
        im = im.crop((0, 0, im.size[0], im.size[0]))
    if im.size != (16, 16):
        rep.add('guessed', f'{where}: {im.size[0]}x{im.size[1]} downscaled to 16x16 (nearest)')
        im = im.resize((16, 16), Image.NEAREST)
    dst.parent.mkdir(parents=True, exist_ok=True)
    im.save(dst)


def load_json(data):
    # Java mods commonly ship JSON with comments/trailing commas; strip line comments conservatively.
    text = data.decode('utf-8-sig')
    text = re.sub(r'^\s*//.*$', '', text, flags=re.M)
    text = re.sub(r',(\s*[}\]])', r'\1', text)
    return json.loads(text)


def convert_java(src_path, out, modid=None, hints=None, shapeless_as_shaped=False):
    src = Source(src_path)
    rep = Report()
    hints = hints or {}
    names = src.names()
    namespaces = sorted({n.split('/')[1] for n in names if n.startswith('assets/') and n.count('/') >= 2} - {'minecraft'})
    if modid is None:
        if len(namespaces) != 1:
            raise SystemExit(f'cannot infer mod id from asset namespaces {namespaces}; pass --modid')
        modid = namespaces[0]
    ns = modid
    lce_id = hints.get('lce_id', re.sub(r'[^a-z0-9_]', '_', ns.lower()))[:31]
    aliases = hints.get('aliases', {})
    meta = {}
    for name in ('META-INF/mods.toml', 'META-INF/neoforge.mods.toml'):
        if name in names:
            toml = src.read(name).decode()
            for key in ('displayName', 'version', 'authors', 'description'):
                mm = re.search(rf'^\s*{key}\s*=\s*"""(.*?)"""|^\s*{key}\s*=\s*"([^"]*)"', toml, re.M | re.S)
                if mm:
                    meta[key] = (mm.group(1) or mm.group(2) or '').strip()
    if 'fabric.mod.json' in names:
        fj = load_json(src.read('fabric.mod.json'))
        meta.update(displayName=fj.get('name'), version=fj.get('version'), description=fj.get('description'),
                    authors=', '.join(a if isinstance(a, str) else a.get('name', '') for a in fj.get('authors', [])))
    if 'mcmod.info' in names:
        mi = load_json(src.read('mcmod.info'))
        mi = mi[0] if isinstance(mi, list) else mi.get('modList', [{}])[0]
        meta.update(displayName=mi.get('name'), version=mi.get('version'), description=mi.get('description'))
    version = re.sub(r'[^0-9.]', '', str(meta.get('version') or '1.0.0')).strip('.') or '1.0.0'
    if not re.match(r'^[0-9]+(\.[0-9]+){0,3}$', version):
        version = '1.0.0'
    out = Path(out)
    if out.exists():
        shutil.rmtree(out)
    (out / 'textures').mkdir(parents=True)

    lang = {}
    for cand in (f'assets/{ns}/lang/en_us.json', f'assets/{ns}/lang/en_US.lang', f'assets/{ns}/lang/en_us.lang'):
        if cand in names:
            data = src.read(cand)
            if cand.endswith('.json'):
                lang = load_json(data)
            else:
                for line in data.decode('utf-8-sig').splitlines():
                    if '=' in line and not line.lstrip().startswith('#'):
                        k, v = line.split('=', 1)
                        lang[k.strip()] = v.strip()
            break

    def display(kind, path):
        for k in (f'{kind}.{ns}.{path}', f'{kind}.{path}.name', f'{kind}.{ns}:{path}.name', f'{kind}.{ns}.{path}.name'):
            if k in lang:
                return lang[k]
        rep.add('guessed', f'{ns}:{path}: no en_us name; using a title-cased id')
        return path.replace('_', ' ').title()

    textures = {}

    def texture(ref, atlas, where):
        """'modid:item/ruby' or 'modid:items/ruby' -> texture key."""
        if ref is None:
            return None
        tns, _, tpath = ref.partition(':') if ':' in ref else ('minecraft', '', ref)
        if tns != ns:
            rep.add('errors', f'{where}: references texture {ref} outside the mod; vanilla textures cannot be re-used by key in API 1.0')
            return None
        key = f'{lce_id}:{"t_" if atlas == "terrain" else "i_"}{tpath.split("/")[-1]}'
        if key in textures:
            return key
        file = f'assets/{ns}/textures/{tpath}.png'
        if not src.exists(file):
            rep.add('errors', f'{where}: texture file {file} missing')
            return None
        dst = f'textures/{atlas}/{tpath.split("/")[-1]}.png'
        save_texture(src.read(file), out / dst, rep, file)
        textures[key] = {'file': dst, 'atlas': atlas}
        return key

    def model(path):
        f = f'assets/{ns}/models/{path}.json'
        return load_json(src.read(f)) if f in names else None

    # ---- blocks: blockstates/<name>.json -> models/block/...
    blocks, block_keys = [], set()
    item_hints = hints.get('items', {})
    block_hints = hints.get('blocks', {})
    for f in sorted(n for n in names if n.startswith(f'assets/{ns}/blockstates/') and n.endswith('.json')):
        bname = Path(f).stem
        st = load_json(src.read(f))
        variants = st.get('variants') or {}
        if 'multipart' in st or len(variants) > 1:
            rep.add('skipped', f'block {ns}:{bname}: multi-variant/multipart blockstate (stairs, slabs, rotation) not supported in API 1.0')
            continue
        v = next(iter(variants.values()), None)
        v = v[0] if isinstance(v, list) else v
        mref = (v or {}).get('model', f'{ns}:block/{bname}')
        mpath = mref.split(':', 1)[1] if ':' in mref else mref
        mdl = model(mpath) or {}
        tex = mdl.get('textures', {})
        parent = mdl.get('parent', '')
        faces = None
        if 'all' in tex:
            faces = texture(tex['all'], 'terrain', f'block {bname}')
        elif {'top', 'bottom', 'side'} <= set(tex) or {'end', 'side'} <= set(tex):
            faces = {'up': texture(tex.get('top', tex.get('end')), 'terrain', bname),
                     'down': texture(tex.get('bottom', tex.get('end')), 'terrain', bname),
                     'side': texture(tex['side'], 'terrain', bname)}
        else:
            rep.add('skipped', f'block {ns}:{bname}: model {mpath} (parent {parent}) is not a simple cube; skipped')
            continue
        if faces is None or (isinstance(faces, dict) and None in faces.values()):
            continue
        h = block_hints.get(bname, {})
        key = f'{lce_id}:{bname}'
        if not h:
            rep.add('guessed', f'block {key}: hardness/material/tool from defaults (stone-like, hardness 1.5); add hints.blocks.{bname}')
        entry = {'key': key, 'name': display('block', bname), 'texture': faces,
                 'hardness': h.get('hardness', 1.5), 'resistance': h.get('resistance', 6.0), 'material': h.get('material', 'stone'),
                 'sound': h.get('sound', 'stone'), 'light': h.get('light', 0), 'harvest_tier': h.get('harvest_tier', 'none'),
                 'creative_tab': h.get('creative_tab', 'minecraft:building_blocks')}
        if 'drop' in h:
            entry['drop'] = h['drop']
        if 'drops' in h:
            entry['drops'] = h['drops']
        blocks.append(entry)
        block_keys.add(bname)
        rep.add('converted', f'block {key}')

    # ---- items: models/item/<name>.json with layer0 (skip block items)
    items = []
    for f in sorted(n for n in names if n.startswith(f'assets/{ns}/models/item/') and n.endswith('.json')):
        iname = Path(f).stem
        if iname in block_keys:
            continue
        mdl = load_json(src.read(f))
        tex = (mdl.get('textures') or {}).get('layer0')
        parent = mdl.get('parent', '')
        if not tex:
            if parent.startswith(f'{ns}:block/') or parent.startswith('block/'):
                rep.add('skipped', f'item {ns}:{iname}: block item for a block that was not converted')
            else:
                rep.add('skipped', f'item {ns}:{iname}: model without layer0 (custom/3D model) not supported')
            continue
        if (mdl.get('textures') or {}).get('layer1'):
            rep.add('guessed', f'item {ns}:{iname}: layer1 overlay dropped (single-layer icons only)')
        tkey = texture(tex, 'items', f'item {iname}')
        if not tkey:
            continue
        h = item_hints.get(iname, {})
        kind = h.get('kind')
        if kind is None:
            kind = 'generic'
            for k in ('sword', 'pickaxe', 'axe', 'shovel', 'hoe', 'helmet', 'chestplate', 'leggings', 'boots'):
                if iname.endswith('_' + k) or iname == k:
                    kind = k
            if kind != 'generic' and 'max_damage' not in h:
                rep.add('guessed', f'item {lce_id}:{iname}: looks like a {kind}; durability 250 / tier iron assumed; add hints.items.{iname}')
        entry = {'key': f'{lce_id}:{iname}', 'name': display('item', iname), 'texture': tkey, 'kind': kind,
                 'creative_tab': h.get('creative_tab', 'minecraft:tools' if kind != 'generic' and kind != 'food' else
                                       'minecraft:food' if kind == 'food' else 'minecraft:materials')}
        if kind not in ('generic', 'food'):
            entry.update(max_stack=1, max_damage=h.get('max_damage', 250), tier=h.get('tier', 'iron'), handheld=True)
        else:
            entry['max_stack'] = h.get('max_stack', 64)
        if kind == 'food':
            entry['food'] = h.get('food', {'hunger': 4, 'saturation': 0.3})
        if kind in ('helmet', 'chestplate', 'leggings', 'boots'):
            entry['armor_points'] = h.get('armor_points', {'helmet': 2, 'chestplate': 6, 'leggings': 5, 'boots': 2}[kind])
            entry['creative_tab'] = h.get('creative_tab', 'minecraft:armor')
            mat = h.get('armor_material', iname.rsplit('_', 1)[0])
            layer = f'assets/{ns}/textures/models/armor/{mat}_layer_{2 if kind == "leggings" else 1}.png'
            if layer in names:
                tk = f'{lce_id}:armor_{mat}_{2 if kind == "leggings" else 1}'
                if tk not in textures:
                    dst = f'textures/armor/{Path(layer).name}'
                    from PIL import Image
                    im = Image.open(io.BytesIO(src.read(layer))).convert('RGBA')
                    if im.size != (64, 32):
                        rep.add('guessed', f'{layer}: {im.size} resized to 64x32')
                        im = im.resize((64, 32), Image.NEAREST)
                    (out / dst).parent.mkdir(parents=True, exist_ok=True)
                    im.save(out / dst)
                    textures[tk] = {'file': dst, 'atlas': 'armor'}
                entry['armor_texture'] = tk
            else:
                rep.add('guessed', f'item {entry["key"]}: armour layer texture {layer} not found; renders with the base material layer')
            if 'armor_points' not in h:
                rep.add('guessed', f'item {entry["key"]}: armour points {entry["armor_points"]} (iron-like) assumed')
        for k in ('foil', 'attack_bonus', 'fuel_ticks', 'repair_item', 'enchantability', 'mining_speed'):
            if k in h:
                entry[k] = h[k]
        items.append(entry)
        rep.add('converted', f'item {entry["key"]} ({kind})')

    known = {e['key'] for e in items} | {b['key'] for b in blocks}

    def ingredient(spec, where):
        """Java ingredient JSON/str -> 'key[@aux]' or None."""
        if isinstance(spec, list):
            if len(spec) != 1:
                rep.add('guessed', f'{where}: alternative ingredients {spec}; using the first only')
            spec = spec[0] if spec else None
        if isinstance(spec, str):
            spec = {'tag': spec[1:]} if spec.startswith('#') else {'item': spec}
        if not isinstance(spec, dict):
            return None
        if 'tag' in spec:
            t = spec['tag']
            if t in aliases:
                return aliases[t]
            if t in JAVA_TAGS:
                base = JAVA_TAGS[t]
                rep.add('converted', f'{where}: tag #{t} -> {base}{"@-1 (any)" if t in WILDCARD_TAGS else ""}')
                return base
            rep.add('errors', f'{where}: item tag #{t} has no mapping; add hints "aliases"')
            return None
        name = spec.get('item') or spec.get('id')
        if not name:
            return None
        if name in aliases:
            return aliases[name]
        if ':' not in name:
            name = 'minecraft:' + name
        rns, rpath = name.split(':', 1)
        data = spec.get('data')
        if data is not None and not isinstance(data, int):
            try:
                data = int(data)
            except (TypeError, ValueError):
                data = None
        if rns == 'minecraft':
            return vanilla_ref(name, rep, where, old=old_names[0], data=data)
        if rns == ns:
            key = f'{lce_id}:{rpath}'
            if key not in known:
                rep.add('errors', f'{where}: references {name}, which was not converted')
                return None
            if data not in (None, 0, -1, 32767):
                rep.add('errors', f'{where}: references {name} with data {data} (mod sub-items are not converted)')
                return None
            return key
        rep.add('errors', f'{where}: references another mod ({name}); port that mod and add it as a dependency')
        return None

    recipes, smelting = [], []
    rdirs = (f'data/{ns}/recipes/', f'data/{ns}/recipe/', f'assets/{ns}/recipes/')
    old_names = [False]
    for f in sorted(n for n in names if n.startswith(rdirs) and n.endswith('.json')):
        rname = re.sub(r'[^a-z0-9_./-]', '_', Path(f).stem.lower())
        if rname.startswith('_'):
            continue  # 1.12 _constants.json / _factories.json are not recipes
        old_names[0] = f.startswith('assets/')   # assets/<ns>/recipes = 1.12 format and names
        r = load_json(src.read(f))
        rtype = r.get('type', '').split(':')[-1]
        where = f'recipe {rname}'
        if r.get('conditions') or r.get('fabric:load_conditions') or r.get('neoforge:conditions'):
            rep.add('guessed', f'{where}: load conditions ignored')
        res = r.get('result')
        if isinstance(res, str):
            res = {'item': res}
        count = (res or {}).get('count', 1)
        if rtype in ('crafting_shaped', 'shaped'):
            keys, ok = {}, True
            for sym, spec in (r.get('key') or {}).items():
                keys[sym] = ingredient(spec, where)
                ok &= keys[sym] is not None
            out_key = ingredient(res, where + ' result')
            if not ok or not out_key:
                rep.add('skipped', f'{where}: unresolved ingredients')
                continue
            pattern = r.get('pattern') or []
            recipes.append({'key': f'{lce_id}:{rname}', 'type': 'shaped', 'pattern': pattern, 'keys': keys,
                            'result': out_key, 'count': count, 'craft_tab': hints.get('craft_tab', 'minecraft:misc')})
            rep.add('converted', where + ' (shaped)')
        elif rtype in ('crafting_shapeless', 'shapeless'):
            ings = [ingredient(s, where) for s in r.get('ingredients') or []]
            out_key = ingredient(res, where + ' result')
            if None in ings or not out_key:
                rep.add('skipped', f'{where}: unresolved ingredients')
                continue
            entry = {'key': f'{lce_id}:{rname}', 'result': out_key, 'count': count, 'craft_tab': hints.get('craft_tab', 'minecraft:misc')}
            if shapeless_as_shaped:
                syms = 'ABCDEFGHI'
                cells = [syms[i] for i in range(len(ings))]
                pattern = [''.join(cells[i:i + 3]) for i in range(0, len(cells), 3)]
                entry.update(type='shaped', pattern=pattern, keys={syms[i]: g for i, g in enumerate(ings)})
                rep.add('guessed', f'{where}: shapeless emitted as a fixed shaped layout {pattern} (--shapeless-as-shaped)')
            else:
                entry.update(type='shapeless', ingredients=ings)
                rep.add('converted', where + ' (shapeless; the retail 1.95 backend cannot publish shapeless yet, see API_STATUS.md)')
            recipes.append(entry)
        elif rtype in ('smelting', 'blasting', 'smoking'):
            inp = ingredient(r.get('ingredient'), where)
            out_key = ingredient(res if isinstance(res, dict) else {'item': r.get('result')}, where + ' result')
            if not inp or not out_key:
                rep.add('skipped', f'{where}: unresolved ingredients')
                continue
            if rtype != 'smelting':
                rep.add('guessed', f'{where}: {rtype} converted to furnace smelting')
            if any(s['input'] == inp for s in smelting):
                rep.add('skipped', f'{where}: duplicate smelting input {inp}')
                continue
            smelting.append({'key': f'{lce_id}:{rname}', 'input': inp, 'result': out_key, 'count': count, 'xp': r.get('experience', 0)})
            rep.add('converted', where + ' (smelting)')
        else:
            rep.add('skipped', f'{where}: recipe type {r.get("type")} not supported (stonecutting, smithing, custom serializers)')

    # Java code is never converted; flag it so nobody thinks it was.
    classes = [n for n in names if n.endswith('.class')]
    if classes:
        rep.add('skipped', f'{len(classes)} Java class files: behaviour (custom item/block logic, entities, worldgen, GUIs, events) must be reimplemented natively or via the event API')
    ores = []
    placed = {}
    for n in names:
        if n.startswith(f'data/{ns}/worldgen/placed_feature/') and n.endswith('.json'):
            pf = load_json(src.read(n))
            placed[str(pf.get('feature', '')).split(':')[-1]] = pf
    for n in sorted(names):
        if n.startswith(f'data/{ns}/worldgen/configured_feature/') and n.endswith('.json'):
            cf = load_json(src.read(n))
            fname = Path(n).stem
            if cf.get('type', '').split(':')[-1] not in ('ore', 'scattered_ore'):
                rep.add('skipped', f'worldgen feature {fname}: type {cf.get("type")} not converted (only ores)')
                continue
            cfg = cf.get('config', {})
            targets = cfg.get('targets', [])
            state = (targets[0].get('state', {}) if targets else {}).get('Name', '')
            if not state.startswith(ns + ':') or f'{lce_id}:{state.split(":")[1]}' not in {b['key'] for b in blocks}:
                rep.add('skipped', f'worldgen feature {fname}: ore block {state!r} not converted')
                continue
            entry = {'key': f'{lce_id}:{fname}_gen', 'block': f'{lce_id}:{state.split(":")[1]}', 'size': int(cfg.get('size', 8)),
                     'per_chunk': 8, 'min_y': 0, 'max_y': 64}
            for mod in (placed.get(fname) or {}).get('placement', []):
                t = mod.get('type', '').split(':')[-1]
                if t == 'count':
                    c = mod.get('count')
                    entry['per_chunk'] = int(c if isinstance(c, int) else (c or {}).get('value', {}).get('max_inclusive', 8) if isinstance(c, dict) else 8)
                elif t == 'height_range':
                    hgt = mod.get('height', {})
                    lo, hi = hgt.get('min_inclusive', {}), hgt.get('max_inclusive', {})
                    entry['min_y'] = max(0, int(lo.get('absolute', lo.get('above_bottom', 0) if isinstance(lo, dict) else 0)))
                    entry['max_y'] = min(255, max(entry['min_y'], int(hi.get('absolute', 64) if isinstance(hi, dict) else 64)))
            placed.pop(fname, None)
            ores.append(entry)
            rep.add('converted', f'worldgen ore {entry["key"]} (size {entry["size"]}, {entry["per_chunk"]}/chunk, y {entry["min_y"]}-{entry["max_y"]}; 1.18+ negative heights clamped to 0)')
        elif n.startswith(f'data/{ns}/worldgen/') and not n.startswith((f'data/{ns}/worldgen/placed_feature/', f'data/{ns}/worldgen/configured_feature/')):
            rep.add('skipped', f'worldgen file {n}: only ore features are converted automatically')
        elif n.startswith(f'data/{ns}/forge/biome_modifier'):
            rep.add('guessed', f'{n}: biome modifier ignored; converted ores generate in all overworld biomes')
        elif n.startswith(f'assets/{ns}/sounds/') and n.endswith('.ogg'):
            rep.add('skipped', f'sound {n}: .ogg must be re-encoded to 16-bit mono WAV and declared under "sounds" by hand')
    for orphan in sorted(placed):
        rep.add('skipped', f'worldgen placed feature {orphan}: no matching ore configured_feature')
    lang_out = {f'{lce_id}:{e["key"].split(":")[1]}': e['name'] for e in items + blocks}
    manifest = {
        'format': 1, 'id': lce_id, 'name': meta.get('displayName') or lce_id, 'version': version,
        'author': meta.get('authors', ''), 'description': (meta.get('description') or '')[:500].replace('\n', ' '),
        'target': lce_modpack.TARGET, 'ported_from': {'kind': 'java', 'namespace': ns, 'source': Path(src_path).name},
        'textures': textures, 'items': items, 'blocks': blocks, 'recipes': recipes, 'smelting': smelting,
        'lang': {'en_US': lang_out},
    }
    if ores:
        manifest['ores'] = ores
    if 'tabs' in hints:
        manifest['tabs'] = hints['tabs']
    (out / 'mod.json').write_text(json.dumps(manifest, indent=2) + '\n')
    rep.write(out / 'PORT_REPORT.md', f'{ns} -> {lce_id}')
    return manifest, rep


def convert_bedrock(bp_path, rp_path, out, hints=None):
    bp, rp = Source(bp_path), Source(rp_path)
    rep = Report()
    hints = hints or {}
    aliases = hints.get('aliases', {})
    out = Path(out)
    if out.exists():
        shutil.rmtree(out)
    (out / 'textures').mkdir(parents=True)
    bpn, rpn = bp.names(), rp.names()
    manifest_bp = load_json(bp.read('manifest.json'))
    header = manifest_bp.get('header', {})
    lang = {}
    for cand in ('texts/en_US.lang', 'texts/en_us.lang'):
        if cand in rpn:
            for line in rp.read(cand).decode('utf-8-sig').splitlines():
                line = line.split('\t#')[0].split('##')[0]
                if '=' in line:
                    k, v = line.split('=', 1)
                    lang[k.strip()] = v.strip()
    item_tex = load_json(rp.read('textures/item_texture.json')).get('texture_data', {}) if 'textures/item_texture.json' in rpn else {}
    terrain_tex = load_json(rp.read('textures/terrain_texture.json')).get('texture_data', {}) if 'textures/terrain_texture.json' in rpn else {}
    namespaces = set()
    for f in bpn:
        if f.startswith(('items/', 'blocks/')) and f.endswith('.json'):
            d = load_json(bp.read(f))
            body = d.get('minecraft:item') or d.get('minecraft:block') or {}
            ident = body.get('description', {}).get('identifier', '')
            if ':' in ident:
                namespaces.add(ident.split(':')[0])
    if len(namespaces) != 1:
        raise SystemExit(f'expected one identifier namespace, found {sorted(namespaces)}')
    ns = namespaces.pop()
    lce_id = hints.get('lce_id', re.sub(r'[^a-z0-9_]', '_', ns.lower()))[:31]
    textures = {}

    def tex_from(table, short, atlas, where):
        entry = table.get(short)
        if not entry:
            rep.add('errors', f'{where}: texture "{short}" not in {"item" if atlas == "items" else "terrain"}_texture.json')
            return None
        path = entry.get('textures')
        if isinstance(path, list):
            path = path[0]
        if isinstance(path, dict):
            path = path.get('path')
        key = f'{lce_id}:{"t_" if atlas == "terrain" else "i_"}{re.sub(r"[^a-z0-9_]", "_", short.lower())}'
        if key in textures:
            return key
        for ext in ('.png', '.tga'):
            if path + ext in rpn:
                dst = f'textures/{atlas}/{Path(path).name}.png'
                save_texture(rp.read(path + ext), out / dst, rep, path + ext)
                textures[key] = {'file': dst, 'atlas': atlas}
                return key
        rep.add('errors', f'{where}: texture file {path}.png missing')
        return None

    def name_of(kind, ident):
        for k in (f'{kind}.{ident}.name', f'{kind}.{ident}'):
            if k in lang:
                return lang[k]
        rep.add('guessed', f'{ident}: no en_US name')
        return ident.split(':')[1].replace('_', ' ').title()

    items, blocks = [], []
    for f in sorted(n for n in bpn if n.startswith('blocks/') and n.endswith('.json')):
        d = load_json(bp.read(f))['minecraft:block']
        ident = d['description']['identifier']
        path = re.sub(r'[^a-z0-9_]', '_', ident.split(':')[1].lower())
        comp = d.get('components', {})
        if d.get('permutations') or d['description'].get('states') or d['description'].get('properties'):
            rep.add('skipped', f'block {ident}: states/permutations not supported')
            continue
        mi = comp.get('minecraft:material_instances', {})
        star = mi.get('*', {})
        tex = star.get('texture')
        if not tex:
            rep.add('skipped', f'block {ident}: no material_instances "*" texture (geometry/custom model?)')
            continue
        faces = {'all': tex_from(terrain_tex, tex, 'terrain', f'block {ident}')}
        for face, k in (('up', 'up'), ('down', 'down'), ('side', 'north')):
            if k in mi and mi[k].get('texture'):
                faces[face] = tex_from(terrain_tex, mi[k]['texture'], 'terrain', f'block {ident}')
        if None in faces.values():
            continue
        if comp.get('minecraft:geometry') not in (None, 'minecraft:geometry.full_block') and not isinstance(comp.get('minecraft:geometry'), dict):
            rep.add('guessed', f'block {ident}: custom geometry rendered as a full cube')
        destr = comp.get('minecraft:destructible_by_mining')
        secs = destr.get('seconds_to_destroy', 1.5) if isinstance(destr, dict) else 1.5
        exp = comp.get('minecraft:destructible_by_explosion')
        res = exp.get('explosion_resistance', 6.0) if isinstance(exp, dict) else 6.0
        light = comp.get('minecraft:light_emission', 0)
        entry = {'key': f'{lce_id}:{path}', 'name': name_of('tile', ident), 'texture': faces if len(faces) > 1 else faces['all'],
                 'hardness': secs, 'resistance': res, 'light': int(light if light <= 15 else 15),
                 'creative_tab': 'minecraft:building_blocks'}
        h = hints.get('blocks', {}).get(path, {})
        entry.update({k: v for k, v in h.items() if k in ('material', 'sound', 'harvest_tier', 'drop', 'drops', 'creative_tab')})
        blocks.append(entry)
        rep.add('converted', f'block {entry["key"]}')
    for f in sorted(n for n in bpn if n.startswith('items/') and n.endswith('.json')):
        d = load_json(bp.read(f))['minecraft:item']
        ident = d['description']['identifier']
        path = re.sub(r'[^a-z0-9_]', '_', ident.split(':')[1].lower())
        comp = d.get('components', {})
        icon = comp.get('minecraft:icon')
        if isinstance(icon, dict):
            icon = icon.get('texture') or (icon.get('textures') or {}).get('default')
        if not icon:
            rep.add('skipped', f'item {ident}: no minecraft:icon')
            continue
        tkey = tex_from(item_tex, icon, 'items', f'item {ident}')
        if not tkey:
            continue
        entry = {'key': f'{lce_id}:{path}', 'name': name_of('item', ident), 'texture': tkey, 'kind': 'generic',
                 'max_stack': int(comp.get('minecraft:max_stack_size', 64)), 'creative_tab': 'minecraft:materials'}
        if 'minecraft:food' in comp:
            fd = comp['minecraft:food']
            entry.update(kind='food', creative_tab='minecraft:food',
                         food={'hunger': int(fd.get('nutrition', 2)), 'saturation': float(fd.get('saturation_modifier', 0.6)) if isinstance(fd.get('saturation_modifier'), (int, float)) else 0.6,
                               'always_edible': bool(fd.get('can_always_eat'))})
        if 'minecraft:durability' in comp:
            dur = comp['minecraft:durability'].get('max_durability', 250)
            dig = comp.get('minecraft:digger') or {}
            kind = hints.get('items', {}).get(path, {}).get('kind') or next((k for k in ('pickaxe', 'axe', 'shovel', 'hoe', 'sword') if path.endswith(k)), 'sword' if 'minecraft:damage' in comp else 'pickaxe')
            if not dig:
                rep.add('guessed', f'item {ident}: tool kind {kind} inferred from name')
            entry.update(kind=kind, max_stack=1, max_damage=int(dur), tier='iron', handheld=True, creative_tab='minecraft:tools')
        if comp.get('minecraft:foil') or comp.get('minecraft:glint'):
            entry['foil'] = True
        if 'minecraft:fuel' in comp:
            entry['fuel_ticks'] = int(float(comp['minecraft:fuel'].get('duration', 0)) * 20)
        if 'minecraft:wearable' in comp:
            slot = str(comp['minecraft:wearable'].get('slot', '')).split('.')[-1]
            kind = {'head': 'helmet', 'chest': 'chestplate', 'legs': 'leggings', 'feet': 'boots'}.get(slot)
            if kind:
                entry.update(kind=kind, max_stack=1, armor_points=int(comp['minecraft:wearable'].get('protection', 2)),
                             max_damage=int((comp.get('minecraft:durability') or {}).get('max_durability', 200)), creative_tab='minecraft:armor')
                entry.pop('tier', None); entry.pop('handheld', None)
                rep.add('guessed', f'item {ident}: armour layer texture comes from RP attachables, not converted; add "armor_texture" by hand')
        if entry.get('kind') == 'food':
            fx = (comp.get('minecraft:food') or {}).get('effects') or []
            if fx:
                entry['food']['effects'] = [{'effect': 'minecraft:' + e.get('name', ''), 'duration': int(e.get('duration', 10)) * 20,
                                             'amplifier': int(e.get('amplifier', 0)), 'chance': int(float(e.get('chance', 1)) * 100)} for e in fx]
        for unsupported in ('minecraft:throwable', 'minecraft:projectile', 'minecraft:shooter', 'minecraft:custom_components'):
            if unsupported in comp:
                rep.add('skipped', f'item {ident}: component {unsupported} has no 1.95 equivalent in API 1.0')
        items.append(entry)
        rep.add('converted', f'item {entry["key"]} ({entry["kind"]})')

    known = {e['key'] for e in items} | {b['key'] for b in blocks}

    def ingredient(spec, where):
        if isinstance(spec, dict):
            name = spec.get('item')
            if 'data' in spec and name and name.startswith('minecraft:'):
                v = vanilla_ref(name, rep, where)
                return v and v.split('@')[0] + f'@{spec["data"]}'
            if spec.get('tag'):
                t = spec['tag']
                return aliases.get(t) or (JAVA_TAGS.get('minecraft:' + t.split(':')[-1]) if t.split(':')[-1] in ('planks', 'logs', 'wool', 'coals', 'sand') else None) \
                    or rep.add('errors', f'{where}: tag {t} unmapped')
        else:
            name = spec
        if not name:
            return None
        if name in aliases:
            return aliases[name]
        if ':' not in name:
            name = 'minecraft:' + name
        rns, rpath = name.split(':', 1)
        if rns == 'minecraft':
            return vanilla_ref(name, rep, where)
        key = f'{lce_id}:{re.sub(r"[^a-z0-9_]", "_", rpath.lower())}'
        if rns != ns or key not in known:
            rep.add('errors', f'{where}: unknown {name}')
            return None
        return key

    recipes, smelting = [], []
    for f in sorted(n for n in bpn if n.startswith('recipes/') and n.endswith('.json')):
        d = load_json(src_bytes := bp.read(f))
        for rtype, body in d.items():
            if not rtype.startswith('minecraft:recipe_'):
                continue
            rid = re.sub(r'[^a-z0-9_./-]', '_', body.get('description', {}).get('identifier', Path(f).stem).split(':')[-1].lower())
            where = f'recipe {rid}'
            tags = body.get('tags', ['crafting_table'])
            if rtype == 'minecraft:recipe_shaped':
                keys = {s: ingredient(v, where) for s, v in body.get('key', {}).items()}
                res = body.get('result')
                res = res[0] if isinstance(res, list) else res
                out_key = ingredient(res, where)
                if None in keys.values() or not out_key:
                    rep.add('skipped', f'{where}: unresolved')
                    continue
                recipes.append({'key': f'{lce_id}:{rid}', 'type': 'shaped', 'pattern': body.get('pattern'), 'keys': keys,
                                'result': out_key, 'count': (res or {}).get('count', 1) if isinstance(res, dict) else 1,
                                'craft_tab': hints.get('craft_tab', 'minecraft:misc')})
                rep.add('converted', where)
            elif rtype == 'minecraft:recipe_shapeless':
                ings = []
                for v in body.get('ingredients', []):
                    n = v.get('count', 1) if isinstance(v, dict) else 1
                    ings += [ingredient(v, where)] * n
                res = body.get('result')
                res = res[0] if isinstance(res, list) else res
                out_key = ingredient(res, where)
                if None in ings or not out_key:
                    rep.add('skipped', f'{where}: unresolved')
                    continue
                recipes.append({'key': f'{lce_id}:{rid}', 'type': 'shapeless', 'ingredients': ings, 'result': out_key,
                                'count': res.get('count', 1) if isinstance(res, dict) else 1, 'craft_tab': hints.get('craft_tab', 'minecraft:misc')})
                rep.add('converted', where + ' (shapeless)')
            elif rtype == 'minecraft:recipe_furnace':
                if 'furnace' not in tags:
                    rep.add('skipped', f'{where}: furnace recipe for {tags} only')
                    continue
                inp, out_key = ingredient(body.get('input'), where), ingredient(body.get('output'), where)
                if inp and out_key:
                    smelting.append({'key': f'{lce_id}:{rid}', 'input': inp, 'result': out_key, 'xp': 0.1})
                    rep.add('guessed', f'{where}: furnace XP not stored by Bedrock; 0.1 assumed')
            else:
                rep.add('skipped', f'{where}: {rtype} not supported')
    for n in bpn:
        if n.startswith(('features/', 'entities/')) and n.endswith('.json'):
            continue  # converted below
        if n.startswith(('spawn_rules/', 'feature_rules/', 'loot_tables/', 'scripts/', 'functions/')):
            rep.add('skipped', f'{n}: no API 1.0 backend (entities/worldgen/loot/scripts/functions)')
    ores, entities = [], []
    block_keys = {b['key'] for b in blocks}
    for f in sorted(n for n in bpn if n.startswith('features/') and n.endswith('.json')):
        d = load_json(bp.read(f))
        body = d.get('minecraft:ore_feature')
        if not body:
            rep.add('skipped', f'{f}: only minecraft:ore_feature is converted')
            continue
        fid = re.sub(r'[^a-z0-9_]', '_', body.get('description', {}).get('identifier', Path(f).stem).split(':')[-1].lower())
        place = (body.get('replace_rules') or [{}])[0].get('places_block')
        place = place.get('name') if isinstance(place, dict) else place
        key = f'{lce_id}:{re.sub(r"[^a-z0-9_]", "_", str(place).split(":")[-1].lower())}'
        if key not in block_keys:
            rep.add('skipped', f'{f}: ore block {place!r} not converted')
            continue
        ores.append({'key': f'{lce_id}:{fid}', 'block': key, 'size': int(body.get('count', 8)), 'per_chunk': 8, 'min_y': 0, 'max_y': 64})
        rep.add('guessed', f'{f}: ore {fid} converted; placement (feature_rules) not parsed, 8 per chunk at y 0-64 assumed')
    ent_hints = hints.get('entities', {})
    for f in sorted(n for n in bpn if n.startswith('entities/') and n.endswith('.json')):
        d = load_json(bp.read(f)).get('minecraft:entity', {})
        ident = d.get('description', {}).get('identifier', '')
        path = re.sub(r'[^a-z0-9_]', '_', ident.split(':')[-1].lower())
        h = ent_hints.get(path, {})
        if not h.get('base_mob'):
            rep.add('skipped', f'entity {ident}: needs hints.entities.{path}.base_mob (a vanilla mob whose model/AI it reuses)')
            continue
        comp = d.get('components', {})
        e = {'key': f'{lce_id}:{path}', 'name': name_of('entity', ident), 'base_mob': h['base_mob'],
             'health': float((comp.get('minecraft:health') or {}).get('max', (comp.get('minecraft:health') or {}).get('value', 20))),
             'move_speed': float((comp.get('minecraft:movement') or {}).get('value', 0.25))}
        dmg = comp.get('minecraft:attack', {}).get('damage')
        if dmg is not None:
            e['attack_damage'] = float(dmg if not isinstance(dmg, list) else dmg[-1])
        goal_map = {'minecraft:behavior.random_stroll': 'wander', 'minecraft:behavior.look_at_player': 'look_at_player',
                    'minecraft:behavior.panic': 'panic', 'minecraft:behavior.melee_attack': 'melee_attack',
                    'minecraft:behavior.nearest_attackable_target': 'target_player', 'minecraft:behavior.tempt': 'follow_tempt_item',
                    'minecraft:behavior.breed': 'breed', 'minecraft:behavior.float': 'swim', 'minecraft:behavior.avoid_mob_type': 'avoid_entity'}
        goals = [{'goal': g, 'priority': int(comp[c].get('priority', 5))} for c, g in goal_map.items() if c in comp]
        dropped = [c for c in comp if c.startswith('minecraft:behavior.') and c not in goal_map]
        if dropped:
            rep.add('skipped', f'entity {ident}: behaviours without a 1.95 goal: {", ".join(dropped)}')
        if goals:
            e['goals'] = goals
        if h.get('texture'):
            e['texture'] = h['texture']
        for k in ('spawn_weight', 'spawn_biomes', 'category', 'egg_primary', 'egg_secondary', 'drops'):
            if k in h:
                e[k] = h[k]
        entities.append(e)
        rep.add('converted', f'entity {e["key"]} (clone of {h["base_mob"]}; model/renderer are the base mob\'s)')
    lang_out = {e['key']: e['name'] for e in items + blocks}
    version = '.'.join(str(x) for x in header.get('version', [1, 0, 0]))
    manifest = {'format': 1, 'id': lce_id, 'name': lang.get('pack.name', header.get('name', lce_id)), 'version': version,
                'author': '', 'description': lang.get('pack.description', header.get('description', ''))[:500],
                'target': lce_modpack.TARGET, 'ported_from': {'kind': 'bedrock', 'namespace': ns},
                'textures': textures, 'items': items, 'blocks': blocks, 'recipes': recipes, 'smelting': smelting,
                'lang': {'en_US': lang_out}}
    if ores:
        manifest['ores'] = ores
    if entities:
        manifest['entities'] = entities
    if hints.get('textures'):
        manifest['textures'].update(hints['textures'])
    (out / 'mod.json').write_text(json.dumps(manifest, indent=2) + '\n')
    rep.write(out / 'PORT_REPORT.md', f'{ns} (bedrock) -> {lce_id}')
    return manifest, rep


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    j = sub.add_parser('java'); j.add_argument('src'); j.add_argument('out'); j.add_argument('--modid'); j.add_argument('--hints')
    j.add_argument('--shapeless-as-shaped', action='store_true')
    b = sub.add_parser('bedrock'); b.add_argument('bp'); b.add_argument('rp'); b.add_argument('out'); b.add_argument('--hints')
    a = p.parse_args(argv)
    hints = json.loads(Path(a.hints).read_text()) if a.hints else None
    if a.cmd == 'java':
        _, rep = convert_java(a.src, a.out, a.modid, hints, a.shapeless_as_shaped)
    else:
        _, rep = convert_bedrock(a.bp, a.rp, a.out, hints)
    errs = rep.lines['errors']
    print(f'wrote {a.out}/mod.json; converted {len(rep.lines["converted"])}, guessed {len(rep.lines["guessed"])}, '
          f'skipped {len(rep.lines["skipped"])}, errors {len(errs)} (see PORT_REPORT.md)')
    return 1 if errs else 0


if __name__ == '__main__':
    sys.exit(main())
