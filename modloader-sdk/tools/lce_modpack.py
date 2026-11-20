#!/usr/bin/env python3
"""Build, inspect and verify LCE mod packages (mod.lcm, format 1).

  lce_modpack.py new   <id> [--name "My Mod"] [--dir .]   scaffold a working example mod you can edit
  lce_modpack.py check <srcdir> [--profile retail195]     validate; the profile lists what today's console backend drops
  lce_modpack.py build <srcdir> <outdir> [--profile ...]  srcdir/mod.json (+ textures) -> outdir/<id>/{mod.lcm,mod.json}
  lce_modpack.py dump  <mod.lcm>                          print the decoded package as JSON

The source manifest format is documented in PORTING.md ("mod.json reference").
The binary layout mirrors include/lce_mod_package.h exactly; the C parser
(loader/package.cpp) is the authority and the host tests cross-check both.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
API_MAJOR, API_MINOR, FORMAT = 1, 1, 1
TARGET = 'CUSA00265-1.95'
MODID_RE = re.compile(r'^[a-z0-9_]{2,31}$')
KEY_RE = re.compile(r'^[a-z0-9_]{2,31}:[a-z0-9_./#-]+$')
RESERVED_IDS = {'minecraft', 'packs', 'lce', 'reservations', 'worlds', 'downloads', 'maps', 'updates', 'cache', 'staged'}

SEC = dict(STRS=1, META=2, TEX=3, DATA=4, ITEM=5, BLCK=6, RCPE=7, SMLT=8, TABS=9, LANG=10, ENTY=11, ORES=12, DEPS=13, SCRIPT=14, EXT=15, ARRS=16, ITEM2=17, BLCK2=18, BLCK3=19, ITEM3=20, BLCK4=21, WAYP=22)
import lce_ext_schema as XS
ATLASES = {'items': (0, [(16, 16)]), 'terrain': (1, [(16, 16)]), 'armor': (2, [(64, 32)]), 'entity': (3, [(64, 32), (64, 64)]), 'icon': (4, [(16, 16)])}
ITEM_KINDS = ['generic', 'food', 'sword', 'pickaxe', 'axe', 'shovel', 'hoe', 'helmet', 'chestplate', 'leggings', 'boots']
TIERS = ['none', 'wood', 'stone', 'iron', 'diamond', 'gold']
BOAT_TYPES = ['oak', 'spruce', 'birch', 'jungle', 'acacia', 'dark_oak']   # Boat::Type (Items::init 0x399422..0x3a2424)
MATERIALS = ['stone', 'wood', 'metal', 'dirt', 'sand', 'glass', 'plant', 'wool']
SOUNDS = ['stone', 'wood', 'gravel', 'grass', 'metal', 'glass', 'cloth', 'sand']
FACES = ['down', 'up', 'north', 'south', 'west', 'east']
LIMITS = dict(textures=1024, items=512, blocks=256, recipes=1024, smelting=512, tabs=8, lang=8192, entities=64, ores=64, deps=16)


class PackError(Exception):
    pass


def vanilla():
    data = json.loads((HERE / 'data' / 'vanilla_195.json').read_text())
    items, blocks, ids = {}, {}, set()
    for it in data['items']:
        ids.add(it['id'])
        if 'block' in it:
            blocks.setdefault(it['name'], it['block'])
            items.setdefault(it['name'], it['id'])
        else:
            items[it['name']] = it['id']  # a separate item wins over the block-item ("bed")
    return items, blocks, ids


VANILLA_ITEMS, VANILLA_BLOCKS, VANILLA_IDS = vanilla()


def vanilla_item_ok(key):
    name = key.split(':', 1)[1]
    if name.startswith('#'):
        return name[1:].isdigit() and int(name[1:]) in VANILLA_IDS
    return name in VANILLA_ITEMS


class Strings:
    def __init__(self):
        self.buf = bytearray(b'\0')
        self.index = {'': 0}

    def ref(self, s):
        if s is None:
            s = ''
        if s not in self.index:
            raw = s.encode('utf-8')
            if b'\0' in raw or any(c < 0x20 or c == 0x7f for c in raw):
                raise PackError(f'string contains control characters: {s!r}')
            self.index[s] = len(self.buf)
            self.buf += raw + b'\0'
        return self.index[s]


def load_png(path, resize=False, sizes=((16, 16),)):
    from PIL import Image
    im = Image.open(path).convert('RGBA')
    if im.size not in sizes:
        if not resize or len(sizes) != 1:
            want = ' or '.join(f'{w}x{h}' for w, h in sizes)
            raise PackError(f'{path}: texture must be {want} (got {im.size[0]}x{im.size[1]}){"; use --resize to downscale" if len(sizes) == 1 else ""}')
        im = im.resize(sizes[0], Image.NEAREST)
    return im.size, im.tobytes()


def parse_ingredient(spec):
    """'mod:item' or 'mod:item@aux' -> (key, aux); aux -1 = any."""
    if spec is None:
        return None, 0
    if isinstance(spec, dict):
        return spec['item'], int(spec.get('aux', -1))
    key, _, aux = spec.partition('@')
    return key, int(aux) if aux else -1


def milli(v, name, hi, lo=0):
    """value -> int(value * 1000); raises ValueError (callers report it and keep validating)."""
    try:
        v = float(v)
    except (TypeError, ValueError):
        raise ValueError(f'{name} must be a number, not {v!r}') from None
    if not lo <= v <= hi:  # also rejects NaN
        raise ValueError(f'{name} out of range {lo}..{hi}: {v}')
    return int(round(v * 1000))


def ranged(v, name, lo, hi, default=0):
    """Integer field packed into a fixed-width slot -> int; raises ValueError when it does not fit."""
    if v is None:
        v = default
    if isinstance(v, bool):
        v = int(v)
    try:
        f = float(v)
    except (TypeError, ValueError):
        raise ValueError(f'{name} must be a number, not {v!r}') from None
    if not (lo <= f <= hi) or f != int(f):
        raise ValueError(f'{name} must be an integer {lo}..{hi}: {v!r}')
    return int(f)



# ---- API 1.8 per-mod config ("config" in mod.json -> <out>/<id>/config, a TOML subset; loader/config_store.h) ----
CONFIG_KEY_RE = re.compile(r'^[A-Za-z0-9_-]+(\.[A-Za-z0-9_-]+)*$')
CONFIG_MAX_ENTRIES = 128
CONFIG_MAX_FILE = 16384


def _cfg_scalar(v):
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        r = repr(v)
        return r if ('.' in r or 'e' in r) else r + '.0'
    return '"' + v.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\t', '\\t') + '"'


def validate_config(cfg, err):
    """mod.json "config": {"section.key": {"default": v, "min": n, "max": n, "comment": "..."}}. Returns [(key, spec)]."""
    out = []
    if cfg is None:
        return out
    if not isinstance(cfg, dict):
        err('config must be an object of "section.key": {"default": ...}')
        return out
    if len(cfg) > CONFIG_MAX_ENTRIES:
        err(f'config: too many keys ({len(cfg)} > {CONFIG_MAX_ENTRIES})')
    for key, spec in cfg.items():
        if not isinstance(key, str) or not CONFIG_KEY_RE.match(key) or len(key) >= 64:
            err(f'config: invalid key {key!r} (dotted parts of [A-Za-z0-9_-], below 64 bytes)')
            continue
        if not isinstance(spec, dict):
            spec = {'default': spec}
        unknown = set(spec) - {'default', 'min', 'max', 'comment'}
        if unknown:
            err(f'config {key}: unknown field(s) {sorted(unknown)}')
        d = spec.get('default')
        if isinstance(d, float) and (d != d or d in (float('inf'), float('-inf'))):
            err(f'config {key}: default must be finite')
            continue
        if isinstance(d, list):
            if not all(isinstance(x, (str, int, float, bool)) for x in d):
                err(f'config {key}: list defaults hold strings, numbers or booleans')
                continue
            raw = '[' + ', '.join(_cfg_scalar(x) for x in d) + ']'
            if len(raw.encode()) > 255:
                err(f'config {key}: list default longer than 255 bytes')
        elif isinstance(d, str):
            if len(d.encode()) > 255 or any(ord(c) < 32 and c not in '\n\t' for c in d):
                err(f'config {key}: string default must be at most 255 bytes without control characters')
        elif isinstance(d, int) and not isinstance(d, bool) and not -2**63 <= d < 2**63:
            err(f'config {key}: integer default outside 64-bit range')
            continue
        elif not isinstance(d, (bool, int, float)):
            err(f'config {key}: default must be a boolean, integer, number, string or list')
            continue
        lo, hi = spec.get('min'), spec.get('max')
        for b in (lo, hi):
            if b is not None and (isinstance(b, bool) or not isinstance(b, (int, float)) or isinstance(d, (bool, str, list))):
                err(f'config {key}: min/max only apply to numeric defaults')
                lo = hi = None
                break
        if lo is not None and hi is not None and lo > hi:
            err(f'config {key}: min > max')
        if isinstance(d, (int, float)) and not isinstance(d, bool) and ((lo is not None and d < lo) or (hi is not None and d > hi)):
            err(f'config {key}: default {d} outside [{lo}, {hi}]')
        c = spec.get('comment')
        if c is not None and (not isinstance(c, str) or len(c) > 512):
            err(f'config {key}: comment must be a string of at most 512 characters')
            c = None
        out.append((key, dict(default=d, min=lo, max=hi, comment=c)))
    return out


def render_config(entries, modid=''):
    """The default config file the user edits on the console (/data/lce-mods/<id>/config)."""
    lines = [f'# {modid} config. Edit the values; delete a line to get its default back.' if modid else '# mod config',
             '# Read once when the game starts (restart after editing). Booleans: true/false. Strings: "quoted".', '']
    sections = {}
    for key, spec in entries:
        sec, _, name = key.rpartition('.')
        sections.setdefault(sec, []).append((name, spec))
    for sec in sorted(sections, key=lambda x: (x != '', x)):
        if sec:
            lines.append(f'[{sec}]')
        for name, spec in sections[sec]:
            for cl in (spec['comment'] or '').splitlines():
                lines.append(f'# {cl}'.rstrip())
            d = spec['default']
            if spec['min'] is not None or spec['max'] is not None:
                lo = '' if spec['min'] is None else spec['min']
                hi = '' if spec['max'] is None else spec['max']
                lines.append(f'# Range: {lo} ~ {hi}')
            val = '[' + ', '.join(_cfg_scalar(x) for x in d) + ']' if isinstance(d, list) else _cfg_scalar(d)
            lines.append(f'{name} = {val}')
        lines.append('')
    text = '\n'.join(lines)
    if len(text.encode()) > CONFIG_MAX_FILE:
        raise PackError(f'config: generated file is {len(text.encode())} bytes (> {CONFIG_MAX_FILE}); shorten the comments')
    return text


WAYPOINT_LIMIT = 16
WAYPOINT_COORD_LIMIT = 30_000_000
WAYPOINT_RECORD = '<BBH5iI25s3x'


def validate_waypoints(value, err):
    if not isinstance(value, list) or len(value) > WAYPOINT_LIMIT:
        err(f'waypoints: expected an array with at most {WAYPOINT_LIMIT} entries')
        return []

    def position(value, size):
        return (isinstance(value, list) and len(value) == size and
                all(type(c) is int and -WAYPOINT_COORD_LIMIT <= c <= WAYPOINT_COORD_LIMIT for c in value))

    pins = []
    for index, pin in enumerate(value):
        name = f'waypoints[{index}]'
        if not isinstance(pin, dict) or set(pin) - {'kind', 'layer', 'position', 'end', 'label', 'color'}:
            err(f'{name}: expected an authored waypoint object')
            continue
        kind, layer = pin.get('kind', 'point'), pin.get('layer')
        label, color = pin.get('label'), pin.get('color', '#55ccff')
        xyz, end = pin.get('position'), pin.get('end')
        if kind not in ('point', 'area') or type(layer) is not int or not 0 <= layer < 3:
            err(f'{name}: kind must be point or area, and layer must be 0..2')
            continue
        if not position(xyz, 3):
            err(f'{name}: position needs three integer coordinates in ±{WAYPOINT_COORD_LIMIT}')
            continue
        if kind == 'area':
            if not position(end, 2) or xyz[0] == end[0] or xyz[2] == end[1]:
                err(f'{name}: an area needs a nondegenerate opposite [x, z] corner')
                continue
        elif 'end' in pin:
            err(f'{name}: end is accepted only for an area')
            continue
        else:
            end = [xyz[0], xyz[2]]
        if not isinstance(label, str) or not re.fullmatch(r'[ -~]{1,24}', label):
            err(f'{name}: label must be 1..24 printable ASCII characters')
            continue
        if not isinstance(color, str) or not re.fullmatch(r'#[0-9a-fA-F]{6}', color):
            err(f'{name}: color must be a #RRGGBB string')
            continue
        pins.append((1 if kind == 'point' else 2, layer, 0, *xyz, *end, int(color[1:], 16), label.encode('ascii')))
    return pins


def validate(m, srcdir, resize=False):
    """Returns a normalized dict; raises PackError with every problem found."""
    errs = []

    def err(msg):
        errs.append(msg)

    modid = m.get('id', '')
    if not MODID_RE.match(modid) or modid in RESERVED_IDS:
        raise PackError(f'invalid mod id {modid!r} ([a-z0-9_]{{2,31}}, not {sorted(RESERVED_IDS)})')
    if m.get('target', TARGET) != TARGET:
        raise PackError(f"target must be {TARGET}")
    for f in ('name', 'version'):
        if not m.get(f):
            err(f'missing {f}')
    if m.get('version') and not re.match(r'^[0-9]+(\.[0-9]+){0,3}$', m['version']):
        err('version must be dotted digits, e.g. 1.0.0')

    def own(key, what, keys=None):
        if not isinstance(key, str) or not KEY_RE.match(key) or len(key) >= 64 or '..' in key:
            err(f'{what}: invalid key {key!r}')
            return False
        if not key.startswith(modid + ':'):
            err(f'{what}: key {key!r} must start with "{modid}:"')
            return False
        if keys is not None:
            if key in keys:
                err(f'duplicate {what} key {key}')
                return False
            keys.add(key)
        return True

    textures = {}
    for key, t in (m.get('textures') or {}).items():
        if not own(key, 'texture'):
            continue
        if isinstance(t, str):
            t = {'file': t}
        atlas = t.get('atlas', 'items')
        if atlas not in ATLASES:
            err(f'texture {key}: atlas must be one of {sorted(ATLASES)}')
            continue
        try:
            size, rgba = load_png(Path(srcdir) / t['file'], resize, ATLASES[atlas][1])
        except (PackError, OSError, KeyError) as e:
            err(f'texture {key}: {e}')
            continue
        textures[key] = (ATLASES[atlas][0], rgba, size)

    item_keys = set()
    block_keys = set()
    blocks = []
    for b in m.get('blocks') or []:
        if not own(b.get('key'), 'block', block_keys):
            continue
        def face_list(tex):
            if isinstance(tex, str):
                return [tex] * 6
            if isinstance(tex, dict):
                allt = tex.get('all')
                side = tex.get('side', allt)
                return [tex.get('down', tex.get('bottom', allt)), tex.get('up', tex.get('top', allt))] + [tex.get(f, side) for f in FACES[2:]]
            return [None] * 6
        faces = face_list(b.get('texture'))
        # API 1.7: "variants" (2..16 entries; entry 0 is the block itself) and "box" [x0,y0,z0,x1,y1,z1] in 1/16
        vnames, vtex = [], []
        variants = b.get('variants') or []
        if variants:
            if not isinstance(variants, list) or not 2 <= len(variants) <= 16:
                err(f'block {b["key"]}: variants must list 2..16 entries (entry 0 is the block itself)')
                variants = []
            elif not isinstance(variants[0], dict) or variants[0].get('name') or variants[0].get('texture'):
                err(f'block {b["key"]}: variants[0] is the block itself: give its name/texture on the block, not in the variant')
                variants = []
        for vi, v in enumerate(variants):
            if not isinstance(v, dict):
                err(f'block {b["key"]}: variant {vi} must be an object')
                continue
            vnames.append(v.get('name'))
            vf = face_list(v['texture']) if vi and v.get('texture') else [None] * 6
            for f, k in zip(FACES, vf):
                if k is not None and (k not in textures or textures[k][0] != 1):
                    err(f'block {b["key"]}: variant {vi} face {f} texture {k!r} missing or not a terrain texture')
            vtex += vf
        box = b.get('box')
        if box is not None:
            if (not isinstance(box, list) or len(box) != 6 or not all(isinstance(x, int) and 0 <= x <= 16 for x in box) or
                    not all(box[a] < box[a + 3] for a in range(3)) or box == [0, 0, 0, 16, 16, 16]):
                err(f'block {b["key"]}: box must be [x0,y0,z0,x1,y1,z1] in 1/16 with min < max per axis and not the full cube')
                box = None
        # API 1.11 multi-box: "boxes" (1..8 boxes, every state), "variant_boxes" (one list per variant) or
        # "boxes" + "rotate_boxes": true (4 variants: variant v = the boxes turned v x 90 degrees clockwise seen from above)
        bsets = None
        def box_ok(x):
            return isinstance(x, list) and len(x) == 6 and all(isinstance(c, int) and not isinstance(c, bool) and 0 <= c <= 16 for c in x) and all(x[a] < x[a + 3] for a in range(3))
        if b.get('boxes') is not None or b.get('variant_boxes') is not None:
            nv = len(variants) if variants else 1
            if b.get('variant_boxes') is not None:
                bsets = b['variant_boxes']
                if not (isinstance(bsets, list) and nv >= 2 and len(bsets) == nv):
                    err(f'block {b["key"]}: variant_boxes needs one box list per variant'); bsets = None
            elif b.get('rotate_boxes'):
                if nv != 4:
                    err(f'block {b["key"]}: rotate_boxes needs exactly 4 variants (the four facings)'); bsets = None
                else:
                    bsets = [b['boxes']]
                    for _ in range(3):
                        bsets.append([[16 - x[5], x[1], x[0], 16 - x[2], x[4], x[3]] for x in bsets[-1]] if isinstance(bsets[-1], list) and all(box_ok(x) for x in bsets[-1]) else bsets[-1])
            else:
                bsets = [b['boxes']]
            if bsets is not None:
                if box is not None:
                    err(f'block {b["key"]}: use either box or boxes'); bsets = None
                elif not all(isinstance(l, list) and 1 <= len(l) <= 8 and len(l) == len(bsets[0]) and all(box_ok(x) for x in l) for l in bsets):
                    err(f'block {b["key"]}: boxes: 1..8 boxes [x0,y0,z0,x1,y1,z1] in 1/16 (min < max), the same count in every variant'); bsets = None
        for f, k in zip(FACES, faces):
            if k not in textures or textures[k][0] != 1:
                err(f'block {b["key"]}: face {f} texture {k!r} missing or not a terrain texture')
        try:
            entry = dict(key=b['key'], name=b.get('name') or b['key'], faces=faces, drop=b.get('drop'),
                         tab=b.get('creative_tab'), hardness=milli(b.get('hardness', 1.5), 'hardness', 100000),
                         resistance=milli(b.get('resistance', 6.0), 'resistance', 100000),
                         material=MATERIALS.index(b.get('material', 'stone')), sound=SOUNDS.index(b.get('sound', 'stone')),
                         light=int(b.get('light', 0)), harvest=TIERS.index(b.get('harvest_tier', 'none')),
                         drops=b.get('drops', [1, 1]), flags=(1 if b.get('transparent') else 0) | (2 if b.get('no_item') else 0) |
                         (4 if b.get('replaceable') else 0) | (8 if b.get('no_collision') else 0),
                         storage=int(b.get('storage_slots', 0)), placement=['fixed', 'facing', 'axis'].index(b.get('placement', 'fixed')),
                         flammability=ranged(b.get('flammability', 0), 'flammability', 0, 255),
                         slip=milli(b.get('slipperiness', 0), 'slipperiness', 1000),
                         tool=b.get('harvest_tool'), vnames=vnames, vtex=vtex, box=box, boxes=bsets)
            entry['light'] = ranged(entry['light'], 'light', 0, 15)
            entry['storage'] = ranged(entry['storage'], 'storage_slots', 0, 65535)
        except (ValueError, TypeError) as e:
            err(f'block {b["key"]}: {e}')
            continue
        if not (isinstance(entry['drops'], list) and len(entry['drops']) == 2 and all(isinstance(x, int) and not isinstance(x, bool) for x in entry['drops'])
                and 0 <= entry['drops'][0] <= entry['drops'][1] <= 64):
            err(f'block {b["key"]}: drops must be [min,max] within 0..64')
            continue
        blocks.append(entry)
        if not b.get('no_item'):
            item_keys.add(b['key'])

    items = []
    declared_items = set()
    for it in m.get('items') or []:
        if not own(it.get('key'), 'item', declared_items):
            continue
        if it['key'] in block_keys:
            err(f'item {it["key"]}: key already used by a block')
            continue
        tex = it.get('texture')
        if tex not in textures or textures[tex][0] != 0:
            err(f'item {it["key"]}: texture {tex!r} missing or not an items texture')
        kind = it.get('kind', 'generic')
        if kind not in ITEM_KINDS:
            err(f'item {it["key"]}: kind must be one of {ITEM_KINDS}')
            continue
        if it.get('tier', 'none') not in TIERS:
            err(f'item {it["key"]}: tier must be one of {TIERS}')
            continue
        tool = ITEM_KINDS.index(kind) >= 2
        food = it.get('food') or {}
        if not isinstance(food, dict):
            err(f'item {it["key"]}: food must be an object')
            continue
        # every value is range-checked against the slot it is packed into (no struct.error at build time)
        try:
            stack = ranged(it.get('max_stack', 1 if tool else 64), 'max_stack', 1, 64)
            dmg = ranged(it.get('max_damage', 0), 'max_damage', 0, 65535)
            hunger = ranged(food.get('hunger', 0), 'food.hunger', 0, 65535)
            sat = milli(food.get('saturation', 0), 'food.saturation', 65.535)
            attack = milli(it.get('attack_bonus', 0), 'attack_bonus', 1000, lo=-1000)
            mining = milli(it.get('mining_speed', 0), 'mining_speed', 1000)
            armor_points = ranged(it.get('armor_points', 0), 'armor_points', 0, 65535)
            enchantability = ranged(it.get('enchantability', 0), 'enchantability', 0, 65535)
            fuel = ranged(it.get('fuel_ticks', 0), 'fuel_ticks', 0, 0xffffffff)
            use_ticks = ranged(food.get('eat_ticks', 0), 'food.eat_ticks', 0, 0xffffffff)
            def effect_list(lst, where):
                out = []
                if not isinstance(lst, list):
                    raise ValueError(f'{where} must be a list')
                for e in lst:
                    if not isinstance(e, dict) or not isinstance(e.get('effect'), str):
                        raise ValueError(f'{where} entries need an "effect" key')
                    out.append((e['effect'], ranged(e.get('duration', 200), 'effect duration', 0, 0xffffffff),
                                ranged(e.get('amplifier', 0), 'effect amplifier', 0, 255), ranged(e.get('chance', 100), 'effect chance', 0, 100)))
                return out
            effects = effect_list(food.get('effects', []), 'food.effects')
            # API 1.9 throwables: "throwable": true or {speed, inaccuracy, damage, fire_ticks, explode, effects}
            thr = it.get('throwable')
            if thr is True:
                thr = {}
            if thr is not None and thr is not False and not isinstance(thr, dict):
                raise ValueError('throwable must be true or an object')
            proj = None
            # API 1.10 boat items: "boat": "oak" | "spruce" | "birch" | "jungle" | "acacia" | "dark_oak" (or 0..5)
            boat = it.get('boat')
            if boat is not None:
                if isinstance(boat, str) and boat in BOAT_TYPES:
                    boat = BOAT_TYPES.index(boat)
                elif not (isinstance(boat, int) and not isinstance(boat, bool) and 0 <= boat <= 5):
                    raise ValueError(f'boat must be one of {BOAT_TYPES} (or 0..5)')
            if isinstance(thr, dict):
                unknown = set(thr) - {'speed', 'inaccuracy', 'damage', 'fire_ticks', 'explode', 'effects'}
                if unknown:
                    raise ValueError(f'throwable: unknown field(s) {sorted(unknown)}')
                proj = dict(speed=milli(thr.get('speed', 0), 'throwable.speed', 10), inaccuracy=milli(thr.get('inaccuracy', 0), 'throwable.inaccuracy', 50),
                            damage=milli(thr.get('damage', 0), 'throwable.damage', 1000), fire=ranged(thr.get('fire_ticks', 0), 'throwable.fire_ticks', 0, 12000),
                            explode=milli(thr.get('explode', 0), 'throwable.explode', 8),
                            effects=effect_list(thr.get('effects', []), 'throwable.effects'))
                if len(proj['effects']) > 8:
                    raise ValueError('throwable.effects: at most 8')
        except (ValueError, TypeError) as e:
            err(f'item {it["key"]}: {e}')
            continue
        if tool and (stack != 1 or dmg <= 0):
            err(f'item {it["key"]}: tools need max_stack 1 and max_damage > 0; stack 1..64')
        if proj is not None and kind != 'generic':
            err(f'item {it["key"]}: throwable needs kind "generic" (it becomes a snowball-class item)')
        if boat is not None and (kind != 'generic' or proj is not None):
            err(f'item {it["key"]}: boat needs kind "generic" and no "throwable" (it becomes a BoatItem)')
        if boat is not None and 'max_stack' not in it:
            stack = 1   # BoatItem 0xd7667
        if kind == 'food' and hunger <= 0:
            err(f'item {it["key"]}: food needs food.hunger > 0')
        if kind in ('helmet', 'chestplate', 'leggings', 'boots'):
            if not 1 <= armor_points <= 20:
                err(f'item {it["key"]}: armour needs armor_points 1..20')
            at = it.get('armor_texture')
            if at and (at not in textures or textures[at][0] != 2):
                err(f'item {it["key"]}: armor_texture {at!r} must be an "armor" atlas texture (64x32)')
        items.append(dict(key=it['key'], name=it.get('name') or it['key'], texture=tex, tab=it.get('creative_tab'),
                          kind=ITEM_KINDS.index(kind), stack=stack, damage=dmg, tier=TIERS.index(it.get('tier', 'none')),
                          hunger=hunger, sat=sat,
                          flags=(1 if it.get('handheld') or kind in ('sword', 'pickaxe', 'axe', 'shovel', 'hoe') else 0) | (2 if it.get('foil') else 0) | (4 if food.get('always_edible') else 0) | (8 if proj is not None else 0) | (16 if boat is not None else 0),
                          attack=attack, armor_points=armor_points, enchantability=enchantability,
                          armor_texture=it.get('armor_texture'), repair=it.get('repair_item'), fuel=fuel,
                          use_ticks=use_ticks, mining=mining, food_effects=effects, proj=proj, boat=boat))
        item_keys.add(it['key'])

    deps = dict(m.get('dependencies') or {})
    for d in deps:
        if not MODID_RE.match(d) or d in RESERVED_IDS:
            err(f'invalid dependency id {d!r}')

    def ref_ok(key):
        if not isinstance(key, str) or not KEY_RE.match(key):
            return False
        ns = key.split(':', 1)[0]
        if ns == 'minecraft':
            return vanilla_item_ok(key)
        if ns == modid:
            return key in item_keys
        return ns in deps  # other mods: must be declared dependencies; checked again on console

    for b in blocks:
        if b['drop'] and not ref_ok(b['drop']):
            err(f'block {b["key"]}: unknown drop {b["drop"]!r}')

    tabs = []
    tab_keys = set()
    for t in m.get('tabs') or []:
        if not own(t.get('key'), 'tab', tab_keys):
            continue
        if not ref_ok(t.get('icon')):
            err(f'tab {t["key"]}: unknown icon item {t.get("icon")!r}')
        creative, crafting = bool(t.get('creative', True)), bool(t.get('crafting', True))
        if not creative and not crafting:
            err(f'tab {t["key"]}: must be creative and/or crafting')
        tabs.append(dict(key=t['key'], name=t.get('name') or t['key'], icon=t.get('icon'), creative=creative, crafting=crafting))

    def tab_ok(key, crafting):
        if key is None:
            return True
        if key.startswith('minecraft:'):
            return key in GROUPS
        return key in tab_keys and any(t['key'] == key and (t['crafting'] if crafting else t['creative']) for t in tabs)

    for e in items + blocks:
        if not tab_ok(e['tab'], False):
            err(f'{e["key"]}: unknown creative tab {e["tab"]!r}')

    recipes = []
    seen = set()
    for r in m.get('recipes') or []:
        if not own(r.get('key'), 'recipe', seen):
            continue
        kind = r.get('type', 'shaped')
        cells = [(None, 0)] * 9
        if kind == 'shaped':
            pat = r.get('pattern') or []
            if not 1 <= len(pat) <= 3 or not all(1 <= len(row) <= 3 for row in pat):
                err(f'recipe {r["key"]}: pattern must be 1..3 rows of 1..3 chars')
                continue
            w, h = max(len(row) for row in pat), len(pat)
            keys = r.get('keys') or {}
            for y, row in enumerate(pat):
                for x, ch in enumerate(row.ljust(w)):
                    if ch == ' ':
                        continue
                    if ch not in keys:
                        err(f'recipe {r["key"]}: symbol {ch!r} not in keys')
                        continue
                    try:
                        cells[y * 3 + x] = parse_ingredient(keys[ch])
                    except (ValueError, TypeError, KeyError) as e:
                        err(f'recipe {r["key"]}: bad ingredient {keys[ch]!r} ({e})')
            shapeless = 0
        elif kind == 'shapeless':
            ing = r.get('ingredients') or []
            if not 1 <= len(ing) <= 9:
                err(f'recipe {r["key"]}: 1..9 ingredients')
                continue
            w, h = min(len(ing), 3), (len(ing) + 2) // 3
            for i, spec in enumerate(ing):
                try:
                    cells[(i // 3) * 3 + i % 3] = parse_ingredient(spec)
                except (ValueError, TypeError, KeyError) as e:
                    err(f'recipe {r["key"]}: bad ingredient {spec!r} ({e})')
            shapeless = 1
        else:
            err(f'recipe {r["key"]}: unsupported type {kind!r} (shaped, shapeless)')
            continue
        for k, aux in cells:
            if k is not None and (not ref_ok(k) or aux < -1):
                err(f'recipe {r["key"]}: unknown ingredient {k!r}')
        if not any(k for k, _ in cells):
            err(f'recipe {r["key"]}: no ingredients')
        try:
            res, raux = parse_ingredient(r.get('result'))
            count = ranged(r.get('count', 1), 'count', 1, 64)
        except (ValueError, TypeError, KeyError) as e:
            err(f'recipe {r["key"]}: bad result: {e}')
            continue
        raux = max(raux, 0)
        if raux > 32767 or any(aux > 32767 for _k, aux in cells):
            err(f'recipe {r["key"]}: data values must be -1..32767')
            continue
        if not ref_ok(res) or not 1 <= count <= 64:
            err(f'recipe {r["key"]}: bad result {r.get("result")!r} x{count}')
        if not tab_ok(r.get('craft_tab'), True):
            err(f'recipe {r["key"]}: unknown crafting tab {r.get("craft_tab")!r}')
        recipes.append(dict(key=r['key'], w=w, h=h, shapeless=shapeless, cells=cells, result=res, count=count, aux=raux, tab=r.get('craft_tab')))

    smelts = []
    smelt_keys = set()
    inputs = set()
    for s in m.get('smelting') or []:
        if not own(s.get('key'), 'smelting', smelt_keys):
            continue
        try:
            ik, iaux = parse_ingredient(s.get('input'))
            rk, raux = parse_ingredient(s.get('result'))
            count = ranged(s.get('count', 1), 'count', 1, 64)
            xp = milli(s.get('xp', 0), 'xp', 100)
            if not -1 <= iaux <= 32767 or not -1 <= raux <= 32767:
                raise ValueError('data values must be -1..32767')
        except (ValueError, TypeError, KeyError) as e:
            err(f'smelting {s["key"]}: {e}')
            continue
        if not ref_ok(ik) or not ref_ok(rk):
            err(f'smelting {s["key"]}: unknown input/result')
        if (ik, iaux) in inputs:
            err(f'smelting {s["key"]}: duplicate input {ik}')
        inputs.add((ik, iaux))
        smelts.append(dict(key=s['key'], input=ik, iaux=iaux, result=rk, raux=max(raux, 0), count=count, xp=xp))

    lang = []
    for loc, table in (m.get('lang') or {}).items():
        if not re.match(r'^[a-zA-Z]{2}([_-][a-zA-Z0-9]{2,8})?$', loc):
            err(f'bad locale {loc!r}')
            continue
        for k, v in table.items():
            if not own(k, f'lang {loc}'):
                continue
            if len(v.encode()) > 255:
                err(f'lang {loc} {k}: value longer than 255 bytes')
            lang.append((loc, k, v))

    ext = validate_ext(m, modid, srcdir, textures, item_keys, {b['key'] for b in blocks}, deps, err)
    scripts = []
    for name, f in (m.get('scripts') or {}).items():
        if not re.match(r'^[a-z0-9_./-]{1,63}$', name) or '..' in name:
            err(f'script name {name!r} invalid')
            continue
        try:
            src = (Path(srcdir) / f).read_bytes()
        except OSError as e:
            err(f'script {name}: {e}')
            continue
        if len(src) > 1 << 20 or b'\0' in src:
            err(f'script {name}: must be text below 1 MiB')
        scripts.append((name, src))

    config = validate_config(m.get('config'), err)
    waypoints = validate_waypoints(m.get('waypoints', []), err)

    for name, lst in (('textures', textures), ('items', items), ('blocks', blocks), ('recipes', recipes), ('smelting', smelts), ('tabs', tabs), ('lang', lang), ('deps', deps)):
        if len(lst) > LIMITS[name]:
            err(f'too many {name}: {len(lst)} > {LIMITS[name]}')
    if errs:
        raise PackError('\n'.join(errs))
    return dict(meta=m, textures=textures, items=items, blocks=blocks, recipes=recipes, smelts=smelts, tabs=tabs, lang=lang, deps=deps,
                ext=ext, scripts=scripts, config=config, waypoints=waypoints)



def _rgb(v):
    if v is None:
        return 0
    if isinstance(v, str):
        v = v.lstrip('#')
        if not re.fullmatch(r'[0-9a-fA-F]{6}', v):
            raise ValueError(f'colour {v!r} must be "#rrggbb"')
        return int(v, 16)
    return int(v) & 0xffffff


def validate_ext(m, modid, srcdir, textures, item_keys, block_keys, deps, err):
    """API 1.1 sections -> list of (type_id, section, {field: value}) in schema order."""
    out = []
    sections = {k: list(m.get(k) or []) for k in XS.SCHEMAS}
    for o in m.get('ores') or []:  # sugar: ores -> ore features
        f = dict(o)
        f.setdefault('kind', 'ore')
        sections['features'].append(f)
    own = {sec: {e.get('key') for e in lst} for sec, lst in sections.items()}
    ref_sections = {'effect': 'effects', 'potion': 'potions', 'sound': 'sounds', 'structure': 'structures', 'biome': 'biomes',
                    'feature': 'features', 'entity': 'entities', 'particle': 'particles'}

    def ref_ok(key, section=None):
        if key is None:
            return True
        if not isinstance(key, str) or not KEY_RE.match(key.split('@')[0]):
            return False
        ns = key.split(':', 1)[0]
        if ns != modid:
            return True  # vanilla / dependency: checked by the console loader
        if section is None:
            return key in item_keys or key in block_keys or key in textures or any(key in v for v in own.values())
        return key in own.get(section, set())

    for sec, (tid, fields) in XS.SCHEMAS.items():
        seen = set()
        for e in sections[sec]:
            where = f'{sec[:-1]} {e.get("key", e.get("item", "?"))}'
            vals = {}
            try:
                for name, kind in fields:
                    v = e.get(name)
                    if kind == 'key':
                        if not isinstance(v, str) or not KEY_RE.match(v) or not v.startswith(modid + ':') or len(v) >= 64:
                            raise ValueError(f'key {v!r} must be "{modid}:<name>"')
                    elif kind in ('ref', 'ref_req'):
                        if v is None and kind == 'ref_req':
                            raise ValueError(f'{name} is required')
                        if not ref_ok(v):
                            raise ValueError(f'{name} {v!r} is not defined in this mod')
                    elif kind == 'text':
                        if v is not None and len(str(v).encode()) > 255:
                            raise ValueError(f'{name} too long')
                    elif kind in ('u8', 'u16', 'u32'):
                        if v is None and name in vals:  # e.g. sample_rate taken from the WAV header
                            continue
                        v = int(v or 0)
                        if not 0 <= v < {'u8': 256, 'u16': 65536, 'u32': 1 << 32}[kind]:
                            raise ValueError(f'{name} out of range')
                    elif kind == 'milli':
                        v = int(round(float(v or 0) * 1000))
                    elif kind == 'milli16':
                        v = int(round(float(v or 0) * 1000))
                        if not 0 <= v < 65536:
                            raise ValueError(f'{name} out of range')
                    elif kind == 'rgb':
                        v = _rgb(v)
                    elif kind == 'kinds':
                        v = sum(1 << XS.ITEM_KINDS.index(k) for k in (v or []))
                    elif kind == 'category':
                        v = XS.CATEGORIES.index(v or 'creature')
                    elif kind == 'feature_kind':
                        v = XS.FEATURE_KINDS.index(v) + 1
                    elif kind == 'dimension':
                        v = XS.DIMENSIONS.index(v or 'overworld')
                    elif kind == 'arr_effect':
                        v = [(x['effect'], int(x.get('duration', 200)), int(x.get('amplifier', 0)), int(x.get('chance', 100) if sec != 'potions' else 0)) for x in (v or [])]
                        for x in v:
                            if not ref_ok(x[0], 'effects'):
                                raise ValueError(f'unknown effect {x[0]!r}')
                    elif kind == 'arr_goal':
                        v = [(XS.GOALS.index(x['goal']) + 1, int(x.get('priority', i)), int(round(float(x.get('speed', 1.0)) * 1000)), x.get('param'))
                             for i, x in enumerate(v or [])]
                    elif kind == 'arr_drop':
                        v = [(x['item'], int(x.get('min', 1)), int(x.get('max', 1)), int(x.get('chance', 100)), int(x.get('looting', 0))) for x in (v or [])]
                        for x in v:
                            if not ref_ok(x[0]):
                                raise ValueError(f'unknown drop item {x[0]!r}')
                    elif kind == 'arr_ref':
                        v = list(v or [])
                        for x in v:
                            if not ref_ok(x):
                                raise ValueError(f'{name}: {x!r} is not defined in this mod')
                    elif kind == 'u16xyz':
                        sx, sy, sz = (int(e.get(k, 0)) for k in ('size_x', 'size_y', 'size_z'))
                        if 'layers' in e:  # layers[y][z] = row string of x symbols; legend symbol -> palette index
                            legend = e.get('legend') or {}
                            pal = list(e.get('palette') or [])
                            v = []
                            for y in range(sy):
                                for z in range(sz):
                                    row = e['layers'][y][z]
                                    for x in range(sx):
                                        sym = row[x]
                                        target = legend.get(sym, 'keep' if sym == ' ' else None)
                                        if target is None:
                                            raise ValueError(f'layer symbol {sym!r} not in legend')
                                        if target == 'keep':
                                            v.append(0)
                                        else:
                                            v.append(pal.index(target))
                            v = [v[(y * sz + z) * sx + x] for z in range(sz) for y in range(sy) for x in range(sx)] if False else v
                        else:
                            v = list(v or [])
                        if len(v) != sx * sy * sz or not v or max(v) >= len(e.get('palette') or []):
                            raise ValueError('blocks must have size_x*size_y*size_z palette indices')
                    elif kind == 'blob':
                        f = e.get('file')
                        if f:
                            import wave
                            with wave.open(str(Path(srcdir) / f)) as w:
                                if w.getsampwidth() != 2 or w.getnchannels() != 1:
                                    raise ValueError('sound file must be 16-bit mono WAV')
                                vals['sample_rate'] = w.getframerate()
                                v = w.readframes(w.getnframes())
                        else:
                            v = b''
                    vals[name] = v
                if sec == 'structures':
                    vals['size_x'], vals['size_y'], vals['size_z'] = (int(e.get(k, 0)) for k in ('size_x', 'size_y', 'size_z'))
                if sec == 'sounds' and 'sample_rate' in vals and isinstance(vals['sample_rate'], int):
                    pass
            except (ValueError, KeyError, IndexError, OSError) as ex:
                err(f'{where}: {ex}')
                continue
            # The native registrar also tracks fuel by item; keyed sections
            # have independent registries, so cross-section names can repeat.
            key = vals['item'] if sec == 'fuels' else vals['key']
            if key in seen:
                err(f'{where}: duplicate {"fuel item" if sec == "fuels" else "key"}')
                continue
            seen.add(key)
            out.append((tid, sec, vals))
    return out


def encode_ext(ext, S, blob):
    words, arrs = [], []
    for tid, sec, vals in ext:
        fields = XS.SCHEMAS[sec][1]
        slots = []
        for name, kind in fields:
            v = vals.get(name)
            if kind in ('key', 'text', 'ref', 'ref_req'):
                slots.append(S.ref(v) if v is not None else 0)
            elif kind in XS.ARR_WORDS:
                idx = len(arrs)
                for x in v or []:
                    if kind == 'arr_effect':
                        arrs += [S.ref(x[0]), x[1], x[2], x[3]]
                    elif kind == 'arr_goal':
                        arrs += [x[0], x[1], x[2], S.ref(x[3]) if x[3] else 0]
                    elif kind == 'arr_drop':
                        arrs += [S.ref(x[0]), x[1], x[2], x[3], x[4]]
                    else:
                        arrs.append(S.ref(x))
                slots += [idx, len(v or [])]
            elif kind == 'u16xyz':
                slots.append(len(arrs))
                arrs += list(v)
            elif kind == 'blob':
                slots += [len(blob), len(v)]
                blob += v
            else:
                slots.append(int(v or 0))
        words += [tid, len(slots)] + slots
    return words, arrs

GROUPS = {'minecraft:' + g for g in ('tools', 'combat', 'weapons', 'materials', 'food', 'armour', 'armor', 'redstone', 'mechanisms',
                                     'transportation', 'transport', 'building_blocks', 'structures', 'decorations', 'brewing', 'misc')}


def encode(n):
    """Normalized dict -> bytes of mod.lcm."""
    S = Strings()
    m = n['meta']
    secs = {}
    secs['META'] = (struct.pack('<6II', S.ref(m['id']), S.ref(m['name']), S.ref(m['version']), S.ref(m.get('author', '')),
                                S.ref(m.get('description', '')), S.ref(TARGET), 0), 1)
    blob = bytearray()
    tex = bytearray()
    for key, (atlas, rgba, size) in n['textures'].items():
        tex += struct.pack('<IHHB3xII', S.ref(key), size[0], size[1], atlas, len(blob), len(rgba))
        blob += rgba
    secs['TEX'] = (bytes(tex), len(n['textures']))
    secs['DATA'] = (bytes(blob), 0)
    secs['ITEM'] = (b''.join(struct.pack('<4I6HIi', S.ref(i['key']), S.ref(i['name']), S.ref(i['texture']), S.ref(i['tab']),
                                         i['kind'], i['stack'], i['damage'], i['tier'], i['hunger'], i['sat'], i['flags'], i['attack'])
                             for i in n['items']), len(n['items']))
    secs['BLCK'] = (b''.join(struct.pack('<10I2I2H4BI', S.ref(b['key']), S.ref(b['name']), *[S.ref(f) for f in b['faces']],
                                         S.ref(b['drop']), S.ref(b['tab']), b['hardness'], b['resistance'], b['material'], b['sound'],
                                         b['light'], b['harvest'], b['drops'][0], b['drops'][1], b['flags'])
                             for b in n['blocks']), len(n['blocks']))
    rc = bytearray()
    for r in n['recipes']:
        rc += struct.pack('<I4B', S.ref(r['key']), r['w'], r['h'], r['shapeless'], 0)
        for k, aux in r['cells']:
            rc += struct.pack('<IhH', S.ref(k), aux if k else 0, 0)
        rc += struct.pack('<IHhI', S.ref(r['result']), r['count'], r['aux'], S.ref(r['tab']))
    secs['RCPE'] = (bytes(rc), len(n['recipes']))
    secs['SMLT'] = (b''.join(struct.pack('<IIhHIHhI', S.ref(s['key']), S.ref(s['input']), s['iaux'], 0, S.ref(s['result']),
                                         s['count'], s['raux'], s['xp']) for s in n['smelts']), len(n['smelts']))
    secs['TABS'] = (b''.join(struct.pack('<3I4B', S.ref(t['key']), S.ref(t['name']), S.ref(t['icon']), int(t['creative']), int(t['crafting']), 0, 0)
                             for t in n['tabs']), len(n['tabs']))
    secs['LANG'] = (b''.join(struct.pack('<3I', S.ref(a), S.ref(b), S.ref(c)) for a, b, c in n['lang']), len(n['lang']))
    secs['DEPS'] = (b''.join(struct.pack('<2I', S.ref(d), S.ref(v)) for d, v in sorted(n['deps'].items())), len(n['deps']))
    i2 = bytearray()
    item_arr_base = []
    for i in n['items']:
        item_arr_base.append(i)
    secs['ITEM2'] = (b'', 0)
    secs['BLCK2'] = (b''.join(struct.pack('<HBBII', b['storage'], b['placement'], b['flammability'], b['slip'], S.ref(b['tool']))
                              for b in n['blocks']), len(n['blocks']))
    # scripts live in DATA
    sc = bytearray()
    for name, src in n['scripts']:
        sc += struct.pack('<3I', S.ref(name), len(blob), len(src))
        blob += src
    secs['SCRIPT'] = (bytes(sc), len(n['scripts']))
    secs['DATA'] = (bytes(blob), 0)
    ext_words, arrs = encode_ext(n['ext'], S, blob)
    for i in n['items']:
        idx = len(arrs)
        for e in i['food_effects']:
            arrs += [S.ref(e[0]), e[1], e[2], e[3]]
        i2 += struct.pack('<HHIIIIIII', i['armor_points'], i['enchantability'], S.ref(i['armor_texture']), S.ref(i['repair']),
                          i['fuel'], i['use_ticks'], i['mining'], idx, len(i['food_effects']))
    secs['ITEM2'] = (bytes(i2), len(n['items']))
    i3 = bytearray()
    for i in n['items']:
        p = i.get('proj') or dict(speed=0, inaccuracy=0, damage=0, fire=0, explode=0, effects=[])
        idx = len(arrs) if p['effects'] else 0
        for e in p['effects']:
            arrs += [S.ref(e[0]), e[1], e[2], e[3]]
        i3 += struct.pack('<8I', p['speed'], p['inaccuracy'], p['damage'], p['fire'], p['explode'], idx, len(p['effects']), i.get('boat') or 0)
    secs['ITEM3'] = (bytes(i3) if any(i.get('proj') or i.get('boat') is not None for i in n['items']) else b'', len(n['items']))
    b3 = bytearray()
    for b in n['blocks']:
        names_idx = tex_idx = 0xffffffff
        vn, vt = b.get('vnames') or [], b.get('vtex') or []
        if any(vn[1:]):
            names_idx = len(arrs)
            arrs += [S.ref(x) for x in vn]
        if any(vt):
            tex_idx = len(arrs)
            arrs += [S.ref(x) for x in vt]
        box = b.get('box')
        b3 += struct.pack('<8BII', len(vn), 2 if b.get('boxes') else 1 if box else 0, *(box or [0] * 6), names_idx, tex_idx)
    secs['BLCK3'] = (bytes(b3) if any(b.get('vnames') or b.get('box') or b.get('boxes') for b in n['blocks']) else b'', len(n['blocks']))
    b4 = bytearray()
    for b in n['blocks']:   # API 1.11: one ARRS word per box, 6 x 5 bits
        sets = b.get('boxes') or []
        idx = len(arrs) if sets else 0
        for l in sets:
            arrs += [sum(c << (5 * a) for a, c in enumerate(x)) for x in l]
        b4 += struct.pack('<BBHI', len(sets[0]) if sets else 0, len(sets), 0, idx)
    secs['BLCK4'] = (bytes(b4) if any(b.get('boxes') for b in n['blocks']) else b'', len(n['blocks']))
    secs['DATA'] = (bytes(blob), 0)
    secs['EXT'] = (struct.pack(f'<{len(ext_words)}I', *ext_words), 0)
    secs['ARRS'] = (struct.pack(f'<{len(arrs)}I', *arrs), 0)
    waypoints = n.get('waypoints', [])
    secs['WAYP'] = (b''.join(struct.pack(WAYPOINT_RECORD, *pin) for pin in waypoints), len(waypoints))
    secs['STRS'] = (bytes(S.buf), 0)
    order = ['STRS', 'META', 'TEX', 'DATA', 'ITEM', 'BLCK', 'RCPE', 'SMLT', 'TABS', 'LANG', 'DEPS', 'SCRIPT', 'EXT', 'ARRS', 'ITEM2', 'BLCK2', 'BLCK3', 'ITEM3', 'BLCK4', 'WAYP']
    order = [o for o in order if o in ('STRS', 'META') or secs[o][0]]
    table_end = 64 + 16 * len(order)
    body = bytearray()
    table = bytearray()
    off = table_end
    for o in order:
        data, count = secs[o]
        pad = (-off) % 4
        body += b'\0' * pad
        off += pad
        table += struct.pack('<4I', SEC[o], off, len(data), count)
        body += data
        off += len(data)
    payload = bytes(table) + bytes(body)
    size = 64 + len(payload)
    if size > 8 * 1024 * 1024:
        raise PackError('package larger than 8 MiB')
    header = b'LCEMODPK' + struct.pack('<4HII', FORMAT, API_MAJOR, API_MINOR, len(order), size, 0) + hashlib.sha256(payload).digest() + b'\0' * 8
    assert len(header) == 64
    return header + payload


def decode(data):
    """mod.lcm -> JSON-able dict (inspection; the C parser is authoritative)."""
    if data[:8] != b'LCEMODPK':
        raise PackError('bad magic')
    fmt, amaj, amin, nsec, size, flags = struct.unpack_from('<4HII', data, 8)
    if size != len(data) or hashlib.sha256(data[64:]).digest() != data[24:56]:
        raise PackError('size/hash mismatch')
    secs = {}
    for i in range(nsec):
        t, off, sz, cnt = struct.unpack_from('<4I', data, 64 + 16 * i)
        secs[t] = (off, sz, cnt)
    so, ss, _ = secs[SEC['STRS']]

    def s(ref):
        end = data.index(b'\0', so + ref)
        return data[so + ref:end].decode()
    out = {'format': fmt, 'api': f'{amaj}.{amin}', 'flags': flags}
    mo = secs[SEC['META']][0]
    f = struct.unpack_from('<6II', data, mo)
    out['meta'] = dict(id=s(f[0]), name=s(f[1]), version=s(f[2]), author=s(f[3]), description=s(f[4]), target=s(f[5]))

    def recs(name, fmtstr):
        if SEC[name] not in secs:
            return []
        off, sz, cnt = secs[SEC[name]]
        n = struct.calcsize(fmtstr)
        return [struct.unpack_from(fmtstr, data, off + i * n) for i in range(cnt)]
    out['textures'] = [dict(key=s(r[0]), atlas=['items', 'terrain', 'armor', 'entity', 'icon'][r[3]], size=[r[1], r[2]]) for r in recs('TEX', '<IHHB3xII')]
    out['items'] = [dict(key=s(r[0]), name=s(r[1]), texture=s(r[2]), tab=s(r[3]), kind=ITEM_KINDS[r[4]], max_stack=r[5], max_damage=r[6])
                    for r in recs('ITEM', '<4I6HIi')]
    for it, base, r in zip(out['items'], recs('ITEM', '<4I6HIi'), recs('ITEM3', '<8I')):   # API 1.9 throwables
        if base[10] & 8:
            it['throwable'] = dict(speed=r[0] / 1000, inaccuracy=r[1] / 1000, damage=r[2] / 1000, fire_ticks=r[3], explode=r[4] / 1000, effects=r[6])
        if base[10] & 16:   # API 1.10 boat items
            it['boat'] = BOAT_TYPES[r[7]] if r[7] < len(BOAT_TYPES) else r[7]
    out['blocks'] = [dict(key=s(r[0]), name=s(r[1]), faces=[s(x) for x in r[2:8]], drop=s(r[8]), hardness=r[10] / 1000)
                     for r in recs('BLCK', '<10I2I2H4BI')]
    rcs = []
    if SEC['RCPE'] in secs:
        off, sz, cnt = secs[SEC['RCPE']]
        for i in range(cnt):
            p = off + i * 92
            key, w, h, sl, _ = struct.unpack_from('<I4B', data, p)
            cells = [struct.unpack_from('<IhH', data, p + 8 + 8 * c) for c in range(9)]
            res, cnt2, aux, tab = struct.unpack_from('<IHhI', data, p + 80)
            rcs.append(dict(key=s(key), size=[w, h], shapeless=bool(sl), cells=[s(c[0]) + (f'@{c[1]}' if c[0] and c[1] >= 0 else '') for c in cells],
                            result=s(res), count=cnt2, tab=s(tab)))
    out['recipes'] = rcs
    out['smelting'] = [dict(key=s(r[0]), input=s(r[1]), result=s(r[4]), count=r[5], xp=r[7] / 1000) for r in recs('SMLT', '<IIhHIHhI')]
    out['tabs'] = [dict(key=s(r[0]), name=s(r[1]), icon=s(r[2]), creative=bool(r[3]), crafting=bool(r[4])) for r in recs('TABS', '<3I4B')]
    out['lang'] = [dict(locale=s(r[0]), key=s(r[1]), value=s(r[2])) for r in recs('LANG', '<3I')]
    out['dependencies'] = {s(r[0]): s(r[1]) for r in recs('DEPS', '<2I')}
    out['scripts'] = [dict(name=s(r[0]), bytes=r[2]) for r in recs('SCRIPT', '<3I')]
    if SEC['WAYP'] in secs:
        out['waypoints'] = []
        for kind, layer, _, x, y, z, end_x, end_z, color, label in recs('WAYP', WAYPOINT_RECORD):
            marker = dict(kind='point' if kind == 1 else 'area', layer=layer, position=[x, y, z],
                          label=label.split(b'\0', 1)[0].decode('ascii'), color=f'#{color:06x}')
            if kind == 2:
                marker['end'] = [end_x, end_z]
            out['waypoints'].append(marker)
    if SEC['EXT'] in secs:  # API 1.1 content: list keys per section
        off, sz, _ = secs[SEC['EXT']]
        words = struct.unpack_from(f'<{sz // 4}I', data, off)
        names = {tid: sec for sec, (tid, _f) in XS.SCHEMAS.items()}
        pos = 0
        while pos < len(words):
            tid, n = words[pos], words[pos + 1]
            sec = names.get(tid, f'type{tid}')
            fields = XS.SCHEMAS[sec][1] if sec in XS.SCHEMAS else []
            first = words[pos + 2] if n else 0
            label = s(first) if fields and fields[0][1] in ('key', 'ref_req') and first else '?'
            out.setdefault(sec, []).append(label)
            pos += 2 + n
    return out


# What each backend can publish today. PROFILES['retail195'] mirrors the
# composite retail195 backend (loader/retail195/composite195.cpp) EXACTLY:
# capabilities + every check_* hook it delegates to the modules. tests/test_tools.py
# runs build/host/test_composite195 --verdicts on tests/fixtures/allcontent and
# retailall and asserts that both sides refuse the same keys.
TOOL_KINDS = ('sword', 'pickaxe', 'axe', 'shovel', 'hoe')
ARMOUR_KINDS = ('helmet', 'chestplate', 'leggings', 'boots')
STOCK_EFFECTS = ['speed', 'slowness', 'haste', 'mining_fatigue', 'strength', 'instant_health', 'instant_damage', 'jump_boost', 'nausea',
                 'regeneration', 'resistance', 'fire_resistance', 'water_breathing', 'invisibility', 'blindness', 'night_vision', 'hunger',
                 'weakness', 'poison', 'wither', 'health_boost', 'absorption', 'saturation', 'glowing', 'levitation', 'luck', 'unluck',
                 'slow_falling', 'conduit_power', 'dolphins_grace', 'bad_omen', 'village_hero']
STOCK_POTIONS = ['empty', 'water', 'mundane', 'thick', 'awkward', 'night_vision', 'long_night_vision', 'invisibility', 'long_invisibility',
                 'leaping', 'long_leaping', 'strong_leaping', 'fire_resistance', 'long_fire_resistance', 'swiftness', 'long_swiftness',
                 'strong_swiftness', 'slowness', 'long_slowness', 'water_breathing', 'long_water_breathing', 'healing', 'strong_healing',
                 'harming', 'strong_harming', 'poison', 'long_poison', 'strong_poison', 'regeneration', 'long_regeneration',
                 'strong_regeneration', 'strength', 'long_strength', 'strong_strength', 'weakness', 'long_weakness', 'luck',
                 'turtle_master', 'long_turtle_master', 'strong_turtle_master', 'wither', 'slow_falling', 'long_slow_falling']
TINTABLE_PARTICLES = {'reddust', 'mobSpell', 'mobSpellAmbient', 'spell', 'instantSpell', 'blockdust', 'fallingdust', 'note', 'happyVillager'}
ARMOUR_LAYERS = {None, 'minecraft:chainmail', 'minecraft:chain', 'minecraft:iron', 'minecraft:diamond', 'minecraft:gold', 'minecraft:golden'}


def _sdk_root():
    return HERE.parent


def _base_mobs():
    """name -> mask, from modules/entities195/base_mobs195.inc (generated from the 1.95 image)."""
    out = {}
    f = _sdk_root() / 'modules' / 'entities195' / 'base_mobs195.inc'
    if f.exists():
        for m in re.finditer(r'\{"([a-z_]+)", "(\w+)", \d+u, (0x[0-9a-f]+)ull', f.read_text()):
            out[m.group(1)] = int(m.group(3), 16)
    return out


def _sounds():
    f = _sdk_root() / 'modules' / 'events195' / 'sound_ids_195.inc'
    return set(re.findall(r'\{"([^"]+)", \d+\}', f.read_text())) if f.exists() else set()


MOB_ALIASES = {'iron_golem': 'villager_golem', 'snow_golem': 'snowman', 'mooshroom': 'mushroom_cow', 'ocelot': 'ozelot', 'zombie_pigman': 'zombie_pigman'}
MASK_MONSTER, MASK_ANIMAL = 0x4d540000, 0x0d680000


def _mob(key, mobs):
    if not key:
        return None
    name = key.split(':', 1)[1] if key.startswith('minecraft:') else key
    return mobs.get(name, mobs.get(MOB_ALIASES.get(name, ''), None))


# effects195 / enchants195 rules (modules/effects195/effects195.cpp kKind, modules/enchants195 kClone/kIdentity)
EFFECT_IDENTITY = {'jump_boost', 'nausea', 'resistance', 'fire_resistance', 'water_breathing', 'invisibility', 'blindness', 'night_vision',
                   'glowing', 'levitation', 'slow_falling', 'conduit_power', 'dolphins_grace', 'bad_omen', 'village_hero'}
EFFECT_INSTANT = {'instant_health', 'instant_damage', 'saturation'}
ENCHANT_CLONES = {'sharpness', 'smite', 'bane_of_arthropods', 'impaling', 'protection', 'fire_protection', 'feather_falling',
                  'blast_protection', 'projectile_protection'}


def _effect_why(v):
    c = v.get('clone_of')
    if c:
        name = c[10:] if c.startswith('minecraft:') else None
        if name not in STOCK_EFFECTS:
            return 'clone_of must be a stock 1.95 effect'
        if name in EFFECT_IDENTITY:
            return 'this vanilla effect works only through identity checks (hasEffect(MobEffects::X)): a clone would do nothing'
    if v.get('instant') and not (c and c[10:] in EFFECT_INSTANT):
        return 'instant effects need clone_of instant_health, instant_damage or saturation'
    icon = v.get('icon')
    if icon and not (icon.startswith('minecraft:') and icon[10:] in STOCK_EFFECTS):
        return 'effect icons are frames of the fourj.MobEffect UI movie: icon must be a stock effect key'
    return None


def _enchant_why(v):
    c = v.get('clone_of')
    if c:
        if not c.startswith('minecraft:') and ':' in c:
            return 'clone_of must be a vanilla enchantment'
        if c.split(':', 1)[-1] not in ENCHANT_CLONES:
            return 'clone_of is refused: 1.95 reads that enchantment through its static pointer (identity check), or it is unknown'
    if not 1 <= v['max_level'] <= 10:
        return 'max_level must be 1..10'
    if not v['weight']:
        return 'weight 0'
    return None


def retail195_verdicts(n, *, fuel=False):
    """[(kind, key, reason or None)] for every content entry, as the composite retail195 backend decides."""
    out = []
    mobs, sounds = _base_mobs(), _sounds()
    for i in n['items']:
        kind = ITEM_KINDS[i['kind']]
        why = None
        food_fields = i['hunger'] or i['sat'] or i['use_ticks'] or i['food_effects'] or (i['flags'] & 4)
        pr = i.get('proj')
        if pr is not None and kind != 'generic':
            why = 'a throwable must be a generic item (projectiles195: it becomes a SnowballItem)'
        elif i.get('boat') is not None and (kind != 'generic' or pr is not None):
            why = 'a boat item must be a generic, non-throwable item (items195: it becomes a BoatItem)'
        elif pr is not None and ((pr['speed'] and not 100 <= pr['speed'] <= 4000) or pr['inaccuracy'] > 20000):
            why = 'projectiles195: projectile speed 0.1..4 (snowball 1.5), inaccuracy 0..20 (snowball 1)'
        elif i['fuel'] and not fuel:
            why = 'fuel: this build requires experimental fuel support'
        elif i['fuel'] > 72000:
            why = 'fuel_ticks must be 0..72000'
        elif kind != 'food' and food_fields:
            why = 'food fields on a non-food item'
        elif kind not in ARMOUR_KINDS and (i['armor_points'] or i['armor_texture']):
            why = 'armour fields on a non-armour item'
        elif kind not in TOOL_KINDS and (i['tier'] or i['mining'] or i['attack']):
            why = 'tier/speed/attack need a tool kind'
        elif kind == 'generic' and i['damage']:
            why = 'durability needs a tool/armour kind'
        elif kind in TOOL_KINDS + ARMOUR_KINDS and i['stack'] > 1:
            why = 'tools and armour stack to 1'
        elif kind in TOOL_KINDS and not 1 <= i['tier'] <= 5:
            why = 'tools need a tier (wood..gold)'
        elif kind in ARMOUR_KINDS and i['armor_texture'] not in ARMOUR_LAYERS:
            why = 'custom armour layer textures are not possible (fixed 6-name table at 0x145d7e0)'
        elif kind == 'food':
            if i['damage']:
                why = 'food has no durability'
            elif i['use_ticks'] and i['use_ticks'] not in (16, 32):
                why = 'eat duration is 32 or 16 ticks only'
            elif len(i['food_effects']) > 1:
                why = 'FoodItem holds one eat effect (+0xa8/+0xb0)'
            elif i['food_effects'] and i['food_effects'][0][0].startswith('minecraft:') and i['food_effects'][0][0][10:] not in STOCK_EFFECTS:
                why = 'a food effect must be a stock 1.95 effect or a mod effect (effects195)'
        out.append(('item', i['key'], why))
    for b in n['blocks']:
        why = None
        rock = b['material'] in (0, 2)
        if b['storage'] and b['storage'] not in (9, 27):
            why = 'storage_slots must be 9 (DispenserBlockEntity, 3x3 screen) or 27 (ChestBlockEntity, chest screen; two side by side make 54)'
        elif b['storage'] == 27 and (MATERIALS[b['material']] != 'wood' or b['flags'] & 9 or b.get('vnames') or b.get('box') or b.get('boxes')):
            why = '27-slot storage blocks are wooden opaque ChestBlocks (material wood, no transparent/no_collision/variants/box)'
        elif b['placement']:
            why = 'facing/axis placement needs a directional block class'
        elif b['flags'] & 4:
            why = 'replaceable blocks need the material/replace virtuals'
        elif MATERIALS[b['material']] == 'glass' and not b['flags'] & 1:
            why = 'material glass needs "transparent": true (GlassBlock render set)'
        elif MATERIALS[b['material']] == 'plant' and not b['flags'] & 8:
            why = 'material plant needs "no_collision": true'
        elif b['harvest'] > 1:
            why = 'harvest tiers above wood are hard-coded per block'
        elif b['tool'] and not ((b['tool'] == 'pickaxe' and rock) or (b['tool'] == 'axe' and b['material'] == 1)):
            why = 'harvest tool must follow the material (pickaxe: stone/metal, axe: wood)'
        elif b['hardness'] > 1_000_000 or b['resistance'] > 10_000_000:
            why = 'hardness/resistance out of range (hardness up to 1000, resistance up to 10000)'
        elif b['slip'] > 1000:
            why = 'slipperiness above 1.0'
        out.append(('block', b['key'], why))
    for r in n['recipes']:
        why = None
        if not 1 <= r['count'] <= 64:
            why = 'result count must be 1..64'
        out.append(('recipe', r['key'], why))
    for sm in n['smelts']:
        out.append(('smelting', sm['key'], None))
    for tid, sec, v in n['ext']:
        key = v.get('key') or f"{n['meta']['id']}:fuel/{v['item'].split(':', 1)[1]}"
        why = None
        if sec == 'effects':
            why = _effect_why(v)
        elif sec == 'potions':
            if len(v['effects']) > 8:
                why = 'a potion has at most 8 effects'
            else:
                for e in v['effects']:
                    if e[0].startswith('minecraft:') and e[0][10:] not in STOCK_EFFECTS:
                        why = 'potion effect must be a stock 1.95 effect or a mod effect key'
                    elif not 1 <= e[1] <= 576000 or e[2] > 127:
                        why = 'potion effect duration 1..576000 ticks, amplifier 0..127'
        elif sec == 'enchantments':
            why = _enchant_why(v)
        elif sec == 'fuels':
            if not fuel:
                why = 'fuel: this build requires experimental fuel support'
            elif not 1 <= v['burn_ticks'] <= 72000:
                why = 'burn_ticks must be 1..72000'
        elif sec == 'brewing':
            ok = lambda k: isinstance(k, str) and (k[10:] in STOCK_POTIONS if k.startswith('minecraft:') else ':' in k)
            if not (ok(v['input']) and ok(v['output'])):
                why = 'brewing potion unknown (not a stock 1.95 potion or a mod potion key)'
            elif v['input'] == v['output'] or v['input'] == 'minecraft:empty' or v['output'] == 'minecraft:empty':
                why = 'brewing mix from/to must differ and not be minecraft:empty'
        elif sec == 'biomes':
            if v['temperature'] > 2000 or v['rainfall'] > 1000:
                why = 'biome temperature 0..2.0, rainfall 0..1.0'
            elif v.get('height_base', 0) > 2000 or v.get('height_variation', 0) > 2000:
                why = 'biome height base/variation 0..2.0 (land biomes only)'
        elif sec == 'structures':
            if not (1 <= v['size_x'] <= 16 and 1 <= v['size_z'] <= 16 and 1 <= v['size_y'] <= 255):
                why = 'structure templates must fit one chunk (x,z <= 16, y <= 255)'
        elif sec == 'features':
            kname = XS.FEATURE_KINDS[v['kind'] - 1]
            if v['dimension'] == 1:
                why = 'nether features are not possible (0x4b8090 never calls decorate)'
            elif v['dimension'] == 2 and any(not key.startswith('minecraft:') for key in v['biomes']):
                why = 'custom biomes are generated only in the Overworld'
            elif kname == 'flower':
                why = 'flowers need a FlowerBlock'
        elif sec == 'sounds':
            if v.get('pcm'):
                rate = v.get('sample_rate') or 0
                if len(v['pcm']) < 2 or len(v['pcm']) % 2 or len(v['pcm']) > 4 << 20:
                    why = 'custom audio: 16-bit mono PCM, at most 4 MiB'
                elif not 8000 <= rate <= 48000:
                    why = 'custom audio: sample_rate must be 8000..48000'
                elif not 0 < (v.get('volume') or 1000) <= 4000 or not 500 <= (v.get('pitch') or 1000) <= 2000:
                    why = 'custom audio: volume 0..4, pitch 0.5..2'
            elif not v.get('clone_of') or v['clone_of'].split(':', 1)[-1] not in sounds:
                why = 'clone_of is not a 1.95 sound event'
        elif sec == 'particles':
            if v.get('texture'):
                why = 'a custom particle texture is not possible'
            elif v['clone_of'].split(':', 1)[-1] not in TINTABLE_PARTICLES:
                why = 'clone_of is not a tintable 1.95 particle'
        elif sec == 'entities':
            base = _mob(v['base_mob'], mobs)
            if base is None:
                why = 'base_mob is not a clonable vanilla 1.95 LivingEntity'
            elif v['width'] or v['height']:
                why = 'custom hitbox size is not supported'
            elif v['drops']:
                why = 'custom drops need a loot table'
            elif v['texture']:
                why = 'custom mob skins are not uploadable on 1.95 yet'
            elif v['attack_damage'] and (base & MASK_MONSTER) != MASK_MONSTER:
                why = 'attack damage needs a Monster base'
            else:
                for g in v['goals']:
                    gname = XS.GOALS[g[0] - 1]
                    if gname in ('panic', 'flee_sun', 'follow_tempt_item'):
                        why = 'PANIC/FLEE_SUN/TEMPT goal constructors are not callable on 1.95'
                    elif gname == 'melee_attack' and (base & MASK_MONSTER) != MASK_MONSTER:
                        why = 'MELEE_ATTACK needs a Monster base'
                    elif gname == 'breed' and (base & MASK_ANIMAL) != MASK_ANIMAL:
                        why = 'BREED needs an Animal base'
                    if why:
                        break
                for snd in (v['sound_ambient'], v['sound_hurt'], v['sound_death']):
                    if not why and snd and not snd.startswith('minecraft:'):
                        why = 'sounds can only reuse a vanilla mob sound (custom audio: gap G11)'
        out.append(({'brewing': 'brewing', 'entities': 'entity', 'fuels': 'fuel', 'enchantments': 'enchantment'}.get(sec, sec[:-1]), key, why))
    for name, src in n['scripts']:
        # lce_script.c kEvents: an unknown event name makes scripts/main.lua fail, which rejects the mod.
        known = {'ServerTick', 'ClientTick', 'WorldLoad', 'WorldUnload', 'BlockBreak', 'BlockPlace', 'ItemUse', 'EntityDamage', 'EntityDeath',
                 'PlayerJoin', 'PlayerLeave', 'Chat', 'BlockUse', 'EntityInteract', 'BlockTick', 'EntityTick', 'EffectTick', 'CustomPayload', 'ClientPayload', 'ProjectileHit', 'InventoryTick', 'BlockNeighbor', 'BlockEntityInside', 'BlockStepOn', 'PlayerJoinEvent', 'PlayerBlockBreakEvent', 'ItemCompleteUseEvent', 'ServerTickEvent'}
        text = src.decode('utf-8', 'replace') if isinstance(src, (bytes, bytearray)) else str(src)
        bad = [e for e in re.findall(r'(?:subscribe|registerEvent)\s*\(\s*["\']([^"\']+)["\']', text) if e not in known]
        out.append(('script', f"{n['meta']['id']}:{name}", f"unknown event name(s) {', '.join(sorted(set(bad)))} (lce_script.c kEvents)" if bad else None))
    return out


PROFILES = {
    'full': None,  # everything the format allows (mock backend / future backends)
    'retail195': dict(verdicts=retail195_verdicts, max_items=32, max_recipes=128, max_smelts=64, max_blocks=19, max_entities=16, max_features=32, max_throwables=16),
}
PROFILES['retail195-experimental-fuel'] = dict(
    PROFILES['retail195'], verdicts=lambda n: retail195_verdicts(n, fuel=True), max_fuels=64,
)


def profile_report(n, profile):
    """Return a list of human-readable problems for this backend profile (empty = all content publishes)."""
    p = PROFILES[profile]
    if p is None:
        return []
    out = []
    refused = [(k, key, why) for k, key, why in p['verdicts'](n) if why]
    for k, key, why in refused:
        out.append(f"{k} {key}: {why} -> the WHOLE mod is skipped on the console (API_STATUS.md section 0).")
    if len(n['items']) > p['max_items']:
        out.append(f"{len(n['items'])} items > {p['max_items']} (shared 64-entry native catalog).")
    thrown = sum(1 for i in n['items'] if i.get('proj') is not None)
    if thrown > p.get('max_throwables', 1 << 30):
        out.append(f"{thrown} throwable items > {p['max_throwables']} (projectiles195 kinds).")
    if 'max_fuels' in p:
        fuels = [i['key'] for i in n['items'] if i['fuel']]
        fuels.extend(v['item'] for _, sec, v in n['ext'] if sec == 'fuels')
        if len(fuels) > p['max_fuels']:
            out.append(f"{len(fuels)} fuels > {p['max_fuels']} (furnace fuel table).")
        seen_fuels = set()
        for item in fuels:
            if item in seen_fuels:
                out.append(f"fuel {item}: only one burn time per item is allowed.")
            seen_fuels.add(item)
    if len(n['recipes']) > p['max_recipes']:
        out.append(f"{len(n['recipes'])} recipes > {p['max_recipes']} (one ABI-2 publisher batch).")
    if len(n['smelts']) > p['max_smelts']:
        out.append(f"{len(n['smelts'])} smelting recipes > {p['max_smelts']}.")
    if len(n['blocks']) > p['max_blocks']:
        out.append(f"{len(n['blocks'])} blocks > {p['max_blocks']} (live-probed free block IDs).")
    seen = {}
    for sm in n['smelts']:
        if sm['input'] in seen:
            out.append(f"smelting {sm['key']}: same input as {seen[sm['input']]} (the native lookup only sees the first).")
        seen.setdefault(sm['input'], sm['key'])
    return out


def scaffold(modid, name, root):
    """Write a small, valid, retail195-compatible mod you can edit."""
    if not MODID_RE.match(modid) or modid in RESERVED_IDS:
        raise PackError(f'invalid mod id {modid!r}: use 2-31 of a-z 0-9 _')
    from PIL import Image, ImageDraw
    d = Path(root) / modid
    if d.exists():
        raise PackError(f'{d} already exists')
    (d / 'textures').mkdir(parents=True)
    im = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
    g = ImageDraw.Draw(im)
    g.polygon([(8, 1), (14, 6), (8, 15), (2, 6)], fill=(40, 200, 90, 255), outline=(10, 90, 30, 255))
    im.save(d / 'textures' / 'gem.png')
    im2 = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
    ImageDraw.Draw(im2).ellipse([5, 5, 10, 10], fill=(40, 200, 90, 255))
    im2.save(d / 'textures' / 'shard.png')
    m = {
        'format': 1, 'id': modid, 'name': name or modid.replace('_', ' ').title(), 'version': '1.0.0',
        'author': 'you', 'description': 'Describe your mod here.', 'target': TARGET,
        'textures': {f'{modid}:gem': 'textures/gem.png', f'{modid}:shard': 'textures/shard.png'},
        'items': [
            {'key': f'{modid}:gem', 'name': 'Green Gem', 'texture': f'{modid}:gem', 'creative_tab': 'minecraft:materials'},
            {'key': f'{modid}:shard', 'name': 'Gem Shard', 'texture': f'{modid}:shard', 'creative_tab': 'minecraft:materials'},
        ],
        'recipes': [
            {'key': f'{modid}:gem_from_shards', 'type': 'shaped', 'pattern': ['SS', 'SS'], 'keys': {'S': f'{modid}:shard'},
             'result': f'{modid}:gem', 'craft_tab': 'minecraft:materials'},
            {'key': f'{modid}:shard', 'type': 'shaped', 'pattern': ['G', 'F'], 'keys': {'G': 'minecraft:glass', 'F': 'minecraft:flint'},
             'result': f'{modid}:shard', 'craft_tab': 'minecraft:materials'},
        ],
        'lang': {'en_US': {f'{modid}:gem': 'Green Gem', f'{modid}:shard': 'Gem Shard'}},
    }
    (d / 'mod.json').write_text(json.dumps(m, indent=2) + '\n')
    return d


def build(srcdir, outdir, resize=False):
    srcdir = Path(srcdir)
    m = json.loads((srcdir / 'mod.json').read_text())
    n = validate(m, srcdir, resize)
    data = encode(n)
    decode(data)  # self-check
    dst = Path(outdir) / m['id']
    dst.mkdir(parents=True, exist_ok=True)
    (dst / 'mod.lcm').write_bytes(data)
    (dst / 'mod.json').write_text(json.dumps(m, indent=2) + '\n')
    cfg = dst / 'config'
    if n['config']:
        cfg.write_text(render_config(n['config'], m['id']))   # copied next to mod.lcm; the user edits it on the console
    elif cfg.exists():
        cfg.unlink()
    return dst, data


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    n_ = sub.add_parser('new'); n_.add_argument('id'); n_.add_argument('--name'); n_.add_argument('--dir', default='.')
    b = sub.add_parser('build'); b.add_argument('src'); b.add_argument('out'); b.add_argument('--resize', action='store_true')
    b.add_argument('--profile', choices=sorted(PROFILES), default=None)
    d = sub.add_parser('dump'); d.add_argument('lcm')
    c = sub.add_parser('check'); c.add_argument('src'); c.add_argument('--resize', action='store_true')
    c.add_argument('--profile', choices=sorted(PROFILES), default='retail195')
    a = p.parse_args(argv)
    try:
        if a.cmd == 'new':
            d = scaffold(a.id, a.name, a.dir)
            print(f'created {d}/mod.json - edit it, then: lce_modpack.py check {d} && lce_modpack.py build {d} out/')
        elif a.cmd == 'build':
            src = Path(a.src)
            if a.profile:
                problems = profile_report(validate(json.loads((src / 'mod.json').read_text()), src, a.resize), a.profile)
                for msg in problems:
                    print(f'warning [{a.profile}]: {msg}', file=sys.stderr)
            dst, data = build(a.src, a.out, a.resize)
            print(f'built {dst}/mod.lcm ({len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()})')
        elif a.cmd == 'dump':
            print(json.dumps(decode(Path(a.lcm).read_bytes()), indent=2))
        else:
            src = Path(a.src)
            n = validate(json.loads((src / 'mod.json').read_text()), src, a.resize)
            problems = profile_report(n, a.profile)
            extra = {}
            for _tid, sec, _v in n['ext']:
                extra[sec] = extra.get(sec, 0) + 1
            print(f"ok: {len(n['items'])} items, {len(n['blocks'])} blocks, {len(n['recipes'])} recipes, "
                  f"{len(n['smelts'])} smelting, {len(n['textures'])} textures, {len(n['lang'])} strings"
                  + ''.join(f', {c} {k}' for k, c in sorted(extra.items())) + (f", {len(n['scripts'])} scripts" if n['scripts'] else ''))
            if problems:
                print(f'profile {a.profile}: {len(problems)} problem(s) - this content will NOT load on that backend as-is:')
                for msg in problems:
                    print(f'  - {msg}')
                return 2
            print(f'profile {a.profile}: everything in this mod is publishable')
    except PackError as e:
        print(f'error: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
