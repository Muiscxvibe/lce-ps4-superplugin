"""Data files of a mod jar that lce_port.py does not read: old (1.7.10-1.12)
texture layouts, .lang files, sounds.json, loot tables, tags, 1.12 JSON recipes,
NeoForge data maps / biome modifiers and 1.21 data-driven enchantments."""
import io
import re
from pathlib import Path

from .jar import load_json_lenient, JarError

TEX_DIRS_ITEM = ('textures/items/', 'textures/item/')
TEX_DIRS_BLOCK = ('textures/blocks/', 'textures/block/')


def read_json(jar, name):
    try:
        return load_json_lenient(jar.text(name))
    except (ValueError, JarError, KeyError, UnicodeDecodeError):
        return None


def textures(jar, ns):
    """{'items': {stem_lower: path}, 'blocks': {...}, 'entity': {...}, 'armor': {...}} (case-insensitive lookup)."""
    out = {'items': {}, 'blocks': {}, 'entity': {}, 'armor': {}, 'all': {}}
    pre = f'assets/{ns}/'
    for n in jar.names():
        if not n.startswith(pre) or not n.lower().endswith('.png'):
            continue
        rel = n[len(pre):]
        stem = Path(rel).stem
        out['all'][rel.lower()] = n
        if rel.startswith(TEX_DIRS_ITEM):
            out['items'].setdefault(rel.split('/', 2)[2][:-4].lower(), n)
        elif rel.startswith(TEX_DIRS_BLOCK):
            out['blocks'].setdefault(rel.split('/', 2)[2][:-4].lower(), n)
        elif rel.startswith(('textures/entity/', 'textures/entities/', 'textures/mobs/', 'textures/mob/', 'textures/models/entity/')):
            out['entity'].setdefault(stem.lower(), n)
        elif rel.startswith('textures/models/armor/') or '/armor/' in rel:
            out['armor'].setdefault(stem.lower(), n)
    return out


def lang(jar, ns):
    """Merged en_US strings (old .lang and new .json)."""
    out = {}
    for cand in (f'assets/{ns}/lang/en_US.lang', f'assets/{ns}/lang/en_us.lang', f'assets/{ns}/lang/en_us.json', f'assets/{ns}/lang/en_US.json'):
        if cand not in jar:
            continue
        try:
            data = jar.text(cand)
        except JarError:
            continue
        if cand.endswith('.json'):
            try:
                d = load_json_lenient(data)
                out.update({k: v for k, v in d.items() if isinstance(v, str)})
            except ValueError:
                pass
        else:
            for line in data.splitlines():
                if '=' in line and not line.lstrip().startswith('#'):
                    k, v = line.split('=', 1)
                    out[k.strip()] = v.strip()
    return out


def sounds(jar, ns):
    """sounds.json events -> [(event, [ogg paths present], missing)]."""
    d = read_json(jar, f'assets/{ns}/sounds.json') or {}
    out = []
    for ev, body in d.items() if isinstance(d, dict) else []:
        files = []
        for s in (body or {}).get('sounds', []) if isinstance(body, dict) else []:
            name = s.get('name') if isinstance(s, dict) else s
            if not isinstance(name, str):
                continue
            sns, _, path = name.partition(':') if ':' in name else (ns, '', name)
            f = f'assets/{sns}/sounds/{path}.ogg'
            files.append((f, f in jar))
        out.append((ev, files))
    oggs = [n for n in jar.names() if n.startswith(f'assets/{ns}/sounds/') and n.endswith('.ogg')]
    return out, oggs


def loot_tables(jar, ns):
    """{'blocks': {name: table}, 'entities': {name: table}, 'other': [paths]}"""
    out = {'blocks': {}, 'entities': {}, 'other': []}
    for n in jar.names():
        m = re.match(rf'^(?:data|assets)/{re.escape(ns)}/loot_tables?/(.+)\.json$', n)
        if not m:
            continue
        rel = m.group(1)
        t = read_json(jar, n)
        if t is None:
            continue
        if rel.startswith('blocks/'):
            out['blocks'][rel[7:]] = (t, n)
        elif rel.startswith('entities/'):
            out['entities'][rel[9:]] = (t, n)
        else:
            out['other'].append(n)
    return out


def loot_simple(table):
    """A loot table that drops one item with a constant/uniform count -> (item, min, max, chance%, notes) or None."""
    pools = (table or {}).get('pools') or []
    entries = []
    for p in pools:
        rolls = p.get('rolls', 1)
        for e in p.get('entries') or []:
            t = str(e.get('type', '')).split(':')[-1]
            if t != 'item':
                if t in ('alternatives',):
                    kids = [k for k in e.get('children', []) if str(k.get('type', '')).split(':')[-1] == 'item']
                    if kids:
                        e = kids[-1]
                    else:
                        continue
                else:
                    continue
            item = e.get('name')
            lo = hi = 1
            chance = 100
            for fn in e.get('functions') or []:
                fname = str(fn.get('function', '')).split(':')[-1]
                if fname == 'set_count':
                    c = fn.get('count')
                    if isinstance(c, (int, float)):
                        lo = hi = int(c)
                    elif isinstance(c, dict):
                        lo, hi = int(c.get('min', 1)), int(c.get('max', c.get('min', 1)))
            for cond in (p.get('conditions') or []) + (e.get('conditions') or []):
                cn = str(cond.get('condition', '')).split(':')[-1]
                if cn in ('random_chance', 'random_chance_with_looting', 'random_chance_with_enchanted_bonus'):
                    ch = cond.get('chance')
                    if isinstance(ch, (int, float)):
                        chance = int(round(ch * 100))
            entries.append((item, lo, hi, chance, rolls))
    return entries


def tags(jar):
    """{('item'|'block', 'ns:path'): [values]} from data/*/tags/{item(s),block(s)}/**.json"""
    out = {}
    for n in jar.names():
        m = re.match(r'^data/([^/]+)/tags/(items?|blocks?)/(.+)\.json$', n)
        if not m:
            continue
        d = read_json(jar, n) or {}
        vals = []
        for v in d.get('values') or []:
            if isinstance(v, dict):
                v = v.get('id')
            if isinstance(v, str):
                vals.append(v)
        out[(m.group(2).rstrip('s'), f'{m.group(1)}:{m.group(3)}')] = vals
    return out


def harvest_from_tags(tagmap, ns):
    """Block name -> (tool, tier) from vanilla mineable/needs_*_tool tags."""
    out = {}
    for (kind, tag), vals in tagmap.items():
        if kind != 'block':
            continue
        tool = tier = None
        m = re.match(r'^minecraft:mineable/(pickaxe|axe|shovel|hoe)$', tag)
        if m:
            tool = m.group(1)
        m2 = re.match(r'^(?:minecraft|forge|c|neoforge):needs_(wood|stone|iron|diamond|gold|netherite)_tool$', tag)
        if m2:
            tier = 'diamond' if m2.group(1) == 'netherite' else m2.group(1)
        for v in vals:
            if v.startswith(ns + ':'):
                e = out.setdefault(v.split(':', 1)[1], {})
                if tool:
                    e['tool'] = tool
                if tier:
                    e['tier'] = tier
    return out


def old_json_recipes(jar, ns):
    """1.12 assets/<ns>/recipes/*.json (+ _constants.json) -> [(path, recipe dict)]."""
    out = []
    consts = {}
    cpath = f'assets/{ns}/recipes/_constants.json'
    if cpath in jar:
        for c in read_json(jar, cpath) or []:
            if isinstance(c, dict) and c.get('name'):
                consts['#' + c['name']] = c.get('ingredient')
    for n in sorted(jar.names()):
        if n.startswith(f'assets/{ns}/recipes/') and n.endswith('.json') and not n.endswith('/_constants.json') and not n.endswith('/_factories.json'):
            r = read_json(jar, n)
            if isinstance(r, dict):
                out.append((n, r))
    return out, consts


def data_map_fuels(jar):
    out = {}
    for n in jar.names():
        if re.match(r'^data/[^/]+/data_maps/item/furnace_fuels\.json$', n):
            d = read_json(jar, n) or {}
            for k, v in (d.get('values') or {}).items():
                if isinstance(v, dict) and isinstance(v.get('burn_time'), int):
                    out[k] = (v['burn_time'], n)
                elif isinstance(v, int):
                    out[k] = (v, n)
    return out


def biome_spawns(jar, ns):
    """NeoForge/Forge biome modifiers of type add_spawns -> [(entity, weight, min, max, biomes, file)]."""
    out = []
    for n in jar.names():
        if re.match(rf'^data/{re.escape(ns)}/(neoforge|forge)/biome_modifiers?/.+\.json$', n):
            d = read_json(jar, n) or {}
            t = str(d.get('type', '')).split(':')[-1]
            if t not in ('add_spawns',):
                continue
            sp = d.get('spawners')
            sp = sp if isinstance(sp, list) else [sp] if isinstance(sp, dict) else []
            biomes = d.get('biomes')
            for s in sp:
                out.append((s.get('type'), s.get('weight'), s.get('minCount'), s.get('maxCount'), biomes, n))
    return out


def enchant_json(jar, ns):
    out = {}
    for n in jar.names():
        m = re.match(rf'^data/{re.escape(ns)}/enchantments?/(.+)\.json$', n)
        if m:
            d = read_json(jar, n)
            if isinstance(d, dict):
                out[m.group(1)] = (d, n)
    return out


def image_size(jar, name):
    from PIL import Image
    try:
        with Image.open(io.BytesIO(jar.read(name))) as im:
            return im.size
    except Exception:  # noqa: BLE001 - untrusted image
        return None
