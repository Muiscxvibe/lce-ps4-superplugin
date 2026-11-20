"""Port one mod jar into an SDK source package (mod.json + textures + scripts + PORT_REPORT.md)."""
import contextlib
import io
import json
import re
import shutil
import tempfile
import time
import zipfile
from pathlib import Path

from . import jar as J, extract as X, describe as D, assets as A, behaviour as B, mappings as M, vanilla as VN, configs as CFG
from .interp import Obj, Ret, SField, Const, ClassRef, Lambda, str_val, Unknown
from .extract import simple, inner, num, unwrap, classify, Holder
import port_compat

lce_modpack = VN.lce_modpack
lce_port = VN.lce_port

SDK = VN.TOOLS.parent
CATS = ('auto', 'guessed', 'manual', 'impossible')
CAT_TITLES = {'auto': 'Ported automatically', 'guessed': 'Guessed (check these)', 'manual': 'Needs manual work',
              'impossible': 'Impossible on 1.95'}


class Report:
    def __init__(self):
        self.lines = {k: [] for k in CATS}
        self._seen = set()

    def add(self, kind, msg):
        kind = {'converted': 'auto', 'skipped': 'manual', 'errors': 'manual'}.get(kind, kind)
        if (kind, msg) in self._seen:
            return
        self._seen.add((kind, msg))
        self.lines[kind].append(msg)

    def extend(self, notes):
        for k, m in notes:
            self.add(k, m)


def placeholder_png(path, seed):
    from PIL import Image
    h = abs(hash(seed)) % 360
    im = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
    import colorsys
    r, g, b = (int(c * 255) for c in colorsys.hsv_to_rgb(h / 360.0, 0.6, 0.9))
    for y in range(16):
        for x in range(16):
            if 2 <= x <= 13 and 2 <= y <= 13:
                im.putpixel((x, y), (r, g, b, 255) if (x + y) % 4 else (r // 2, g // 2, b // 2, 255))
    path.parent.mkdir(parents=True, exist_ok=True)
    im.save(path)


def save_png(data, dst, size, rep, where):
    """Resize/crop an untrusted PNG to the atlas size; returns True on success."""
    from PIL import Image
    try:
        im = Image.open(io.BytesIO(data))
        if im.size[0] * im.size[1] > 4096 * 4096:
            rep.add('manual', f'{where}: image too large')
            return False
        im = im.convert('RGBA')
    except Exception as e:  # noqa: BLE001 - untrusted image
        rep.add('manual', f'{where}: unreadable image ({type(e).__name__})')
        return False
    w, h = im.size
    if size == (16, 16) and h > w and h % w == 0:
        rep.add('guessed', f'{where}: animated strip {w}x{h}; first frame used')
        im = im.crop((0, 0, w, w))
    if im.size != size:
        rep.add('guessed', f'{where}: {im.size[0]}x{im.size[1]} resized to {size[0]}x{size[1]} (nearest)')
        im = im.resize(size, Image.NEAREST)
    dst.parent.mkdir(parents=True, exist_ok=True)
    im.save(dst)
    return True


def choose_namespace(jar, info):
    ns = J.asset_namespaces(jar)
    cands = [m for m in ([info.modid] + list(info.mods or [])) if m]
    for c in cands:
        if c in ns:
            return c
        if c.lower() in ns:
            return c.lower()
    if ns:
        return max(ns, key=ns.get)
    return (info.modid or jar.path.stem).lower()


class Porter:
    def __init__(self, jar_path, out, hints=None, decompile=False, work=None, mappings=None, fallback=None, log=print, ffmpeg=None):
        self.jar_path = Path(jar_path)
        self.out = Path(out)
        self.hints = hints or {}
        self.decompile = decompile
        self.work = Path(work) if work else Path(tempfile.mkdtemp(prefix='lce_autoport_'))
        self.user_mappings = mappings
        self.log = log
        self.rep = Report()
        self.stats = {}
        self.fallback = fallback
        self.ffmpeg = ffmpeg

    # ------------------------------------------------------------------
    def run(self):
        t0 = time.time()
        jar = self.jar = J.Jar(self.jar_path)
        info = self.info = J.detect(jar)
        rep = self.rep
        ns = self.ns = self.hints.get('namespace') or choose_namespace(jar, info)
        lid = self.lid = self.hints.get('lce_id') or J.lce_id(ns)
        era = self.era = self.hints.get('era') or info.era
        rep.add('auto', f'loader {info.loader}, Minecraft {info.mc_version or "unknown"} ({info.mc_range or "-"}), era {era}; '
                        f'evidence: {", ".join(info.evidence) or "class layout"}')
        rep.add('auto', f'namespace {ns} -> LCE mod id {lid}')
        if info.loader in ('fabric', 'quilt'):
            rep.add('guessed', f'{info.loader} mod: supported on a best-effort basis (assets/data, bytecode through intermediary mappings when found)')
        if info.nested_jars:
            rep.add('manual', f'{len(info.nested_jars)} nested jar(s) (jar-in-jar libraries/modules) not analysed: {", ".join(info.nested_jars[:5])}')
        if self.out.exists():
            shutil.rmtree(self.out)
        self.out.mkdir(parents=True)
        mapper = self._mapper()
        facts = self.facts = X.analyze(jar, info, mapper)
        if facts.class_errors:
            rep.add('manual', f'{len(facts.class_errors)} class file(s) could not be parsed (first: {facts.class_errors[0]})')
        self.resolver = VN.VanillaResolver(era, fallbacks=self.hints.get('newer_fallbacks'), aliases=self.hints.get('aliases'))
        if self.fallback:
            for k, v in self.fallback.items():
                self.resolver.fallbacks.setdefault(k.split(':', 1)[-1], v)
        self.texfiles = A.textures(jar, ns)
        self.lang = A.lang(jar, ns)
        self.tagmap = A.tags(jar)
        self.harvest = A.harvest_from_tags(self.tagmap, ns)
        self.textures = {}
        self.items, self.blocks = [], []
        self._assign_keys()
        self.rr = D.RefResolver(facts.prog, lid, self.resolver, self.regmap, mapper)
        self.rr.mc = J.mc_tuple(info.mc_version) or ((1, 12, 2) if era == 'old' else (1, 14, 4))
        self.rr.ns = ns
        self.rr.oredict = facts.oredict
        self._describe()
        self._variants()
        self._modern_assets()
        self._old_textures()
        self._names()
        self.recipes, self.smelting = [], []
        self._code_recipes()
        self._json_recipes()
        self.lua = B.LuaWriter(lid, info.name or lid)
        self.ext = {k: [] for k in ('effects', 'potions', 'brewing', 'enchantments', 'fuels', 'entities', 'ores', 'features', 'sounds')}
        self._fuels()
        self._worldgen()
        self._entities()
        self._effects_potions_enchants()
        self._sounds()
        self._loot()
        self._apply_user_hints()
        self._configs()
        self.rep.extend(self.rr.log)
        if mapper is not None and mapper.unmapped:
            top = sorted(mapper.unmapped.items(), key=lambda x: -x[1])[:12]
            rep.add('manual', f'{len(mapper.unmapped)} unmapped obfuscated names (no mappings for them); most used: '
                              + ', '.join(f'{k} x{v}' for k, v in top))
        self._behaviour()
        manifest = self._manifest()
        manifest = self._validate_fix(self._limits(self._sanitize(manifest)))
        (self.out / 'mod.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')
        self.manifest = manifest
        self.stats.update(self._counts(manifest))
        self.stats['seconds'] = round(time.time() - t0, 2)
        self.stats['steps'] = facts.prog.steps
        return manifest

    # ------------------------------------------------------------------
    def _mapper(self):
        rep, info = self.rep, self.info
        mappers = []
        mc = J.mc_tuple(info.mc_version)
        srg_era = self.era == 'old' or (info.loader in ('forge', 'forge-legacy', 'forge-legacy?') and mc is not None and mc < (1, 17))
        self.mcp = None
        if srg_era and not self.user_mappings:
            ver = info.mc_version or ('1.12.2' if self.era == 'old' else None)
            self.mcp = M.load_mcp_auto(ver)
            if self.mcp is not None:
                mappers.append(self.mcp)
                rep.add('auto', f'SRG names resolved with the official {self.mcp.source} mappings (auto-detected for Minecraft {ver}'
                                + ('' if info.mc_version else ', version assumed') + f'; {len(self.mcp.methods)} methods, {len(self.mcp.fields)} fields)')
            else:
                mappers.append(M.builtin_srg())
                rep.add('guessed', 'SRG names resolved with the built-in partial SRG->MCP table (hand-compiled; no official MCP archive '
                                   'found in ' + ', '.join(str(d) for d in M.mcp_dirs()[:2]) + '); vanilla references found through it are marked below')
        if info.loader in ('fabric', 'quilt'):
            tiny = M.find_local_tiny()
            if tiny:
                try:
                    mappers.append(M.load_tiny(tiny))
                    rep.add('auto', f'intermediary names mapped with the local Tiny v2 file {tiny} (read-only)')
                except (OSError, ValueError) as e:
                    rep.add('manual', f'Tiny mappings {tiny} unreadable: {e}')
            else:
                rep.add('manual', 'Fabric intermediary names cannot be resolved: no Tiny v2 mappings found (download the intermediary+mojmap/yarn mappings)')
        if self.user_mappings:
            ps = [Path(p) for p in self.user_mappings]
            try:
                csvs = [p for p in ps if p.suffix == '.csv']
                if csvs:
                    mappers.append(M.load_mcp_csv(csvs))
                for p in ps:
                    if p.suffix == '.zip':
                        mm, ff = M._read_mcp_zip(p)
                        mp = M.Mapper(methods=mm, fields=ff, source=f'MCP {p.name}')
                        mp.verified = True
                        mappers.append(mp)
                    elif p.suffix not in ('.csv',):
                        mappers.append(M.load_tiny(p))
                rep.add('auto', f'user mappings: {", ".join(str(p) for p in ps)}')
            except (OSError, ValueError) as e:
                rep.add('manual', f'user mappings unreadable: {e}')
        if not mappers:
            return None
        if len(mappers) == 1:
            return mappers[0]
        return M.merge(*mappers)

    def _assign_keys(self):
        """Registrations -> LCE keys (items/blocks), with collision handling."""
        self.regmap = {}
        used = set()
        skip = set(self.hints.get('skip', []))
        self.regs_by_kind = {}
        seen_names = set()
        order = {'block': 0, 'item': 1}
        first = {}
        for r in sorted(self.facts.registrations, key=lambda r: order.get(r.kind, 2)):
            path = D.normalize_name(r.name.split(':')[-1])
            if (r.kind, path) in seen_names:
                # the same registration reached through another code path (a method run both as a root and inlined):
                # its object resolves to the same key
                prev = first.get((r.kind, path))
                if prev is not None and D.site_key(r.obj) not in self.regmap:
                    self.regmap[D.site_key(r.obj)] = prev
                continue
            seen_names.add((r.kind, path))
            r.path = path
            key = None
            if r.kind in ('item', 'block', 'effect', 'potion', 'enchantment', 'entity_type'):
                key = f'{self.lid}:{path}'
                if r.kind in ('item', 'block') and key in used:
                    # a block and its item share the name: the block-item covers it
                    if r.kind == 'item' and any(x.get('kind') == 'block' and x.get('key') == key for x in self.regmap.values()):
                        self.regmap[D.site_key(r.obj)] = {'kind': 'item', 'name': r.name, 'key': key, 'reg': r}
                        continue
                    key = f'{self.lid}:{path}_{r.kind}'
                if path in skip or key in skip:
                    self.rep.add('manual', f'{r.kind} {key}: skipped by hints')
                    key = None
                if key:
                    used.add(key)
            site = D.site_key(r.obj)
            previous = self.regmap.get(site)
            if previous and previous['key'] and key and previous['key'] != key:
                self.rep.add('manual', f'Registrations {previous["key"]} and {key} share an analysed factory site. '
                                       'Verify references resolved from that factory.')
            r.port_registration = {'kind': r.kind, 'name': r.name, 'key': key, 'reg': r}
            self.regmap[site] = first[(r.kind, path)] = r.port_registration
            self.regs_by_kind.setdefault(r.kind, []).append(r)
            if key and r.name != path and r.kind in ('item', 'block'):
                self.rep.add('auto', f'{r.kind} registry name "{r.name}" -> key {key}')
        for k, lst in self.regs_by_kind.items():
            if k not in ('item', 'block', 'effect', 'potion', 'enchantment', 'entity_type'):
                names = ', '.join(sorted({r.name for r in lst})[:8])
                cat = 'impossible' if k in ('other',) else 'manual'
                self.rep.add(cat, f'{len(lst)} {k} registration(s) with no mod.json equivalent ({names}{"..." if len(lst) > 8 else ""}); '
                                  f'evidence {lst[0].where}')

    def _describe(self):
        self.item_entries, self.block_entries = {}, {}
        prog = self.facts.prog
        for r in self.regs_by_kind.get('block', []):
            info = r.port_registration
            if not info.get('key'):
                continue
            f, notes = D.describe_block(prog, r, self.rr, self.era)
            hv = self.harvest.get(r.path)
            if hv:
                if 'tool' in hv:
                    f['harvest_tool'] = hv['tool']
                if 'tier' in hv:
                    f['harvest_tier'] = hv['tier']
                notes.append(('auto', f'block {r.name}: harvest {hv} from data/minecraft/tags'))
            if f.get('drop') == info['key']:
                f.pop('drop')
            self.rep.extend(notes)
            self.block_entries[r.path] = dict(key=info['key'], reg=r, fields=f)
        for r in self.regs_by_kind.get('item', []):
            info = r.port_registration
            if not info.get('key'):
                continue
            if any(b['key'] == info['key'] for b in self.block_entries.values()):
                continue
            f, notes = D.describe_item(prog, r, self.rr, self.era, self.ns)
            self.rep.extend(notes)
            if f is None:
                blk = self.block_entries.get(r.path)
                if blk is None:
                    info['key'] = None  # block item of an unported block
                else:
                    info['key'] = blk['key']
                continue
            self.item_entries[r.path] = dict(key=info['key'], reg=r, fields=f)

    def _variants(self):
        """Metadata sub-items (1.7.10-1.12 setHasSubtypes) -> one 1.95 item per data value (mod items have no data values
        on 1.95). Recipes and drops naming item@meta resolve to the sub-item's own key."""
        self.rr.variant_keys = {}
        if self.era != 'old':
            return
        prog = self.facts.prog
        mv_events = D.model_variants(prog)
        used = {e['key'] for e in list(self.item_entries.values()) + list(self.block_entries.values())}
        for path, e in list(self.item_entries.items()):
            r = e['reg']
            if r is None:
                continue
            try:
                vs = D.item_variants(prog, r)
            except Exception as ex:  # noqa: BLE001 - untrusted bytecode
                self.rep.add('manual', f'item {e["key"]}: sub-items could not be evaluated ({type(ex).__name__})')
                continue
            if not vs:
                if D.has_subtypes(r.obj):
                    self.rep.add('manual', f'item {e["key"]}: has sub-types (setHasSubtypes) but its getSubItems list is not statically '
                                           'provable; only the base item is ported')
                continue
            base_u = r.unloc or ''
            vmap = {}
            for v in vs:
                model = v.get('model') or mv_events.get((D.site_key(r.obj), v['meta']))
                u = v.get('unloc') or ''
                suffix = None
                if model:
                    mp = model[0].split(':')[-1]
                    if model[1] not in (None, 'inventory') and '=' in str(model[1]):
                        suffix = str(model[1]).split('=')[-1]
                    elif mp != path:
                        suffix = mp[len(path) + 1:] if mp.startswith(path + '_') else mp
                if not suffix and u:
                    tail = re.sub(r'^(item|tile)\.', '', u)
                    tail = tail[len(base_u):].lstrip('._') if base_u and tail.startswith(base_u) and len(tail) > len(base_u) else tail
                    suffix = tail.split('.')[-1] if tail and tail != base_u else None
                if not suffix:
                    suffix = str(v['meta'])
                if v is vs[0] and v['meta'] == 0:
                    key, vpath, ent = e['key'], path, e
                else:
                    vpath = f'{path}_{D.normalize_name(suffix)}'
                    key = f'{self.lid}:{vpath}'
                    n = 2
                    while key in used:
                        vpath = f'{path}_{D.normalize_name(suffix)}_{n}'
                        key, n = f'{self.lid}:{vpath}', n + 1
                    ent = dict(key=key, reg=r, fields=dict(e['fields']))
                    self.item_entries[vpath] = ent
                used.add(key)
                ent['variant'] = v
                ent['unloc'] = u or None
                ent['model'] = model
                if 'heal' in v and ent['fields'].get('kind') == 'food':
                    ent['fields']['food'] = dict(ent['fields']['food'], hunger=int(v['heal']))
                    if 'sat' in v:
                        ent['fields']['food']['saturation'] = round(float(v['sat']), 4)
                    if v.get('eat_ticks') and v['eat_ticks'] != 32:
                        ent['fields']['food']['eat_ticks'] = v['eat_ticks']
                    elif v.get('eat_ticks') == 32:
                        ent['fields']['food'].pop('eat_ticks', None)
                vmap[v['meta']] = key
            self.rr.variant_keys[e['key']] = vmap
            self.rep.add('auto', f'item {e["key"]}: {len(vs)} sub-items (data values {", ".join(str(m) for m in vmap)}) ported as separate items '
                                 f'({", ".join(vmap.values())[:300]}); evidence {simple(r.obj.cls)}.getSubItems')

    def _variant_texture(self, e):
        """Texture of a sub-item: its 1.7.10 icon, or its 1.8-1.12 item model (ModelLoader.setCustomModelResourceLocation)."""
        v = e.get('variant') or {}
        items_tex = self.texfiles['items']
        if v.get('icon'):
            f = self._tex_lookup(items_tex, v['icon']) or self._tex_lookup(self.texfiles['blocks'], v['icon'])
            if f:
                return self._add_texture(f, 'items', Path(f).stem)
        model = e.get('model')
        if model:
            mns, _, mp = model[0].partition(':') if ':' in model[0] else (self.ns, '', model[0])
            mdl = None
            if model[1] not in (None, 'inventory'):
                bs = A.read_json(self.jar, f'assets/{mns}/blockstates/{mp}.json')
                var = (bs or {}).get('variants', {}).get(model[1]) if isinstance(bs, dict) else None
                var = var[0] if isinstance(var, list) and var else var
                if isinstance(var, dict) and var.get('model'):
                    mref = var['model']
                    rns, _, rp = mref.partition(':') if ':' in mref else (mns, '', mref)
                    mdl = A.read_json(self.jar, f'assets/{rns}/models/item/{rp}.json') or A.read_json(self.jar, f'assets/{rns}/models/{rp}.json')
                    if mdl is None and isinstance(var.get('textures'), dict):
                        mdl = {'textures': var['textures']}
                elif isinstance(bs, dict) and isinstance(bs.get('variants'), dict):
                    # forge blockstate v1: "variants": {"type": {"name": {"textures": {...}}}}
                    k, _, val = model[1].partition('=')
                    sub = bs['variants'].get(k)
                    if isinstance(sub, dict) and isinstance(sub.get(val), dict):
                        mdl = {'textures': sub[val].get('textures') or {}}
            else:
                mdl = A.read_json(self.jar, f'assets/{mns}/models/item/{mp}.json')
            for _ in range(4):
                if not isinstance(mdl, dict):
                    break
                tex = mdl.get('textures') or {}
                ref = tex.get('layer0') or tex.get('all') or tex.get('texture')
                if isinstance(ref, str):
                    tns, _, tp = ref.partition(':') if ':' in ref else ('minecraft', '', ref)
                    f = f'assets/{tns}/textures/{tp}.png'
                    if f in self.jar:
                        return self._add_texture(f, 'items', tp.split('/')[-1])
                    break
                par = mdl.get('parent')
                if not isinstance(par, str) or not par.startswith(mns + ':'):
                    break
                mdl = A.read_json(self.jar, f'assets/{mns}/models/{par.split(":", 1)[1]}.json')
        return None

    def _hint_tables(self):
        items = {}
        for path, e in self.item_entries.items():
            items[path] = {k: v for k, v in e['fields'].items()}
        blocks = {path: dict(e['fields']) for path, e in self.block_entries.items()}
        return items, blocks

    def _modern_assets(self):
        """Run lce_port.convert_java on a filtered copy of the jar's model/blockstate/texture/lang/recipe/worldgen files."""
        jar, ns, rep = self.jar, self.ns, self.rep
        keep = (f'assets/{ns}/models/', f'assets/{ns}/blockstates/', f'assets/{ns}/textures/', f'assets/{ns}/lang/en_us.json',
                f'assets/{ns}/lang/en_US.lang', f'assets/{ns}/lang/en_us.lang')
        if self.era == 'modern':
            keep += (f'data/{ns}/recipe/', f'data/{ns}/recipes/', f'data/{ns}/worldgen/', f'data/{ns}/forge/biome_modifier', 'META-INF/mods.toml',
                     'META-INF/neoforge.mods.toml', 'fabric.mod.json')
        names = [n for n in jar.names() if n.startswith(keep)]
        if not any(n.startswith((f'assets/{ns}/models/item', f'assets/{ns}/blockstates/')) for n in names):
            if self.era == 'modern' and any(n.startswith((f'data/{ns}/recipe', f'data/{ns}/worldgen')) for n in names):
                pass
            else:
                self.port_manifest = None
                return
        fz = self.work / f'filtered-{ns}.zip'
        self.work.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(fz, 'w', zipfile.ZIP_DEFLATED) as z:
            for n in names:
                if jar.size(n) > 8 << 20:
                    continue
                try:
                    z.writestr(n, jar.read(n))
                except J.JarError:
                    continue
        items_h, blocks_h = self._hint_tables()
        aliases = dict(self.hints.get('aliases') or {})
        aliases.update(self._recipe_aliases(names))
        for path, e in list(self.item_entries.items()) + list(self.block_entries.items()):
            aliases.setdefault(f'{ns}:{path}', e['key'])
            if e['reg'] is not None:
                aliases.setdefault(f'{ns}:{e["reg"].name}', e['key'])
        hints = {'lce_id': self.lid, 'items': items_h, 'blocks': blocks_h, 'aliases': aliases, 'craft_tab': self.hints.get('craft_tab', 'minecraft:misc')}
        tmp = self.work / f'lce_port-{ns}'
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf):
                m, prep = lce_port.convert_java(fz, tmp, modid=ns, hints=hints,
                                               shapeless_as_shaped=bool(self.hints.get('shapeless_as_shaped')), target_profile=None)
        except (SystemExit, Exception) as e:  # noqa: BLE001 - lce_port on untrusted JSON
            rep.add('manual', f'lce_port.convert_java failed on the asset layout ({type(e).__name__}: {e}); models/blockstates not converted')
            self.port_manifest = None
            return
        self.port_manifest = m
        for k, lines in prep.lines.items():
            for l in lines:
                if k == 'errors' and ('no LCE 1.95 equivalent' in l or 'has no mapping' in l):
                    rep.add('manual', 'lce_port: ' + l)
                else:
                    rep.add(k, 'lce_port: ' + l)
        # copy textures into our output
        for key, t in (m.get('textures') or {}).items():
            src = tmp / t['file']
            dst = self.out / t['file']
            dst.parent.mkdir(parents=True, exist_ok=True)
            if src.exists():
                shutil.copyfile(src, dst)
                self.textures[key] = dict(t)
        port_items = {i['key']: i for i in m.get('items') or []}
        port_blocks = {b['key']: b for b in m.get('blocks') or []}
        for path, e in list(self.item_entries.items()):
            pi = port_items.pop(e['key'], None)
            if pi:
                e['texture'] = pi['texture']
                e['port_name'] = pi.get('name')
        for path, e in list(self.block_entries.items()):
            pb = port_blocks.pop(e['key'], None)
            if pb:
                e['texture'] = pb['texture']
                e['port_name'] = pb.get('name')
        # asset-only content (no bytecode registration found); item models used by ported sub-items are not separate items
        vmodels = {f'{self.lid}:{D.normalize_name(e["model"][0].split(":")[-1])}' for e in self.item_entries.values() if e.get('model')}
        for key, pi in port_items.items():
            if key in vmodels:
                continue
            path = key.split(':', 1)[1]
            self.item_entries[path] = dict(key=key, reg=None, fields={k: v for k, v in pi.items() if k not in ('key', 'name', 'texture')},
                                           texture=pi['texture'], port_name=pi.get('name'))
            if self.facts.registrations:
                rep.add('guessed', f'item {key}: found only in assets/models (no matching registration in bytecode); properties are lce_port defaults')
        for key, pb in port_blocks.items():
            path = key.split(':', 1)[1]
            self.block_entries[path] = dict(key=key, reg=None, fields={k: v for k, v in pb.items() if k not in ('key', 'name', 'texture')},
                                            texture=pb['texture'], port_name=pb.get('name'))
            if self.facts.registrations:
                rep.add('guessed', f'block {key}: found only in blockstates (no matching registration in bytecode); properties are defaults')
        self.port_recipes = m.get('recipes') or []
        self.port_smelting = m.get('smelting') or []
        self.port_ores = m.get('ores') or []
        for k in [k for k, v in self.regmap.items() if v.get('kind') == 'item' and v.get('key') and v['key'] in port_blocks]:
            pass

    def _recipe_aliases(self, names):
        """Pre-resolve every vanilla name / tag used by modern JSON recipes with the autoport tables (incl. 'not in 1.95')."""
        out = {}
        jar = self.jar
        for n in names:
            if not (n.startswith(f'data/{self.ns}/recipe') and n.endswith('.json')):
                continue
            r = A.read_json(jar, n)
            if not isinstance(r, dict):
                continue
            for ref in _json_refs(r):
                if ref in out:
                    continue
                if ref.startswith('#'):
                    t = ref[1:]
                    if t in VN.MODERN_TAGS and t not in lce_port.JAVA_TAGS:
                        out[t] = VN.MODERN_TAGS[t]
                    elif ('item', t) in self.tagmap:
                        vals = self.tagmap[('item', t)]
                        tgt = None
                        for v in vals:
                            if v.startswith('#'):
                                continue
                            k = self._vanilla_or_mod(v, f'tag #{t}')
                            if k:
                                tgt = k
                                break
                        if tgt:
                            out[t] = tgt
                            self.rep.add('guessed', f'recipe tag #{t} -> {tgt} (first resolvable member of the tag)')
                    continue
                if ref.startswith('minecraft:'):
                    key, status, note = self.resolver.resolve(ref)
                    if key and status != 'same':
                        out[ref] = key
                        if status == 'fallback':
                            self.rep.add('guessed', f'recipes: {note}')
                    elif key is None:
                        self.rep.add('impossible' if status == 'newer' else 'manual', f'recipes ({Path(n).name}): {note}')
        return out

    def _vanilla_or_mod(self, ref, where):
        if ref.startswith('minecraft:'):
            k, st, note = self.resolver.resolve(ref)
            return k
        if ref.startswith(self.ns + ':'):
            k = f'{self.lid}:{D.normalize_name(ref.split(":", 1)[1])}'
            if any(e['key'] == k for e in list(self.item_entries.values()) + list(self.block_entries.values())):
                return k
            self.rep.add('manual', f'{where}: references {ref}, which was not ported')
        return None

    def _tex_lookup(self, table, *cands):
        for c in cands:
            if not c:
                continue
            c = c.split(':')[-1]
            for v in (c, c.lower(), D.normalize_name(c)):
                if v.lower() in table:
                    return table[v.lower()]
        return None

    def _add_texture(self, src_name, atlas, stem):
        size = {'items': (16, 16), 'terrain': (16, 16), 'icon': (16, 16), 'armor': (64, 32)}.get(atlas, (16, 16))
        key = f'{self.lid}:{"t_" if atlas == "terrain" else "i_" if atlas == "items" else atlas[:3] + "_"}{D.normalize_name(stem)}'
        if key in self.textures:
            return key
        dst = f'textures/{atlas}/{D.normalize_name(stem)}.png'
        try:
            data = self.jar.read(src_name)
        except (J.JarError, KeyError):
            return None
        if not save_png(data, self.out / dst, size, self.rep, src_name):
            return None
        self.textures[key] = {'file': dst, 'atlas': atlas}
        return key

    def _old_textures(self):
        """Icons for registrations without a model (1.7.10 layout) from setTextureName/registry/unlocalized names."""
        items_tex, blocks_tex = self.texfiles['items'], self.texfiles['blocks']
        for path, e in self.item_entries.items():
            if e.get('variant') is not None:
                t = self._variant_texture(e)
                if t:
                    e['texture'] = t
                    continue
                e.pop('texture', None)  # the base item's texture is not the sub-item's
                v = e['variant']
                cands = [path, (e.get('unloc') or '').split('.')[-1], f'{e["reg"].name}_{v["meta"]}', f'{e["reg"].name}{v["meta"]}']
                f = self._tex_lookup(items_tex, *cands)
                if f:
                    e['texture'] = self._add_texture(f, 'items', Path(f).stem)
                    self.rep.add('guessed', f'item {e["key"]}: sub-item icon {f} matched by name')
                    continue
                self._placeholder(e, 'items', 'item')
                continue
            if e.get('texture'):
                continue
            r = e['reg']
            icons = D.icon_strings(self.facts.prog, r.obj.cls) if r else []
            cands = (icons[:1] + [r.texture, r.name, r.unloc, path]) if r else [path]
            f = self._tex_lookup(items_tex, *cands) or self._tex_lookup(blocks_tex, *cands)
            if f and icons and len(icons) > 1:
                self.rep.add('guessed', f'item {e["key"]}: {len(icons)} icons registered (animation/damage states); the first ({icons[0]}) is used')
            if f:
                e['texture'] = self._add_texture(f, 'items', Path(f).stem)
                if r and not r.texture:
                    self.rep.add('guessed', f'item {e["key"]}: icon {f} matched by name (no setTextureName)')
            if not e.get('texture'):
                self._placeholder(e, 'items', 'item')
        for path, e in self.block_entries.items():
            if e.get('texture'):
                continue
            if self._blockstate_fallback(path, e):
                continue
            r = e['reg']
            icons = D.icon_strings(self.facts.prog, r.obj.cls) if r else []
            cands = ([r.texture] + icons + [r.name, r.unloc, path]) if r else [path]
            base = self._tex_lookup(blocks_tex, *cands)
            faces = {}
            for ic in icons:
                low = ic.lower()
                f = self._tex_lookup(blocks_tex, ic)
                if f:
                    for face, suff in (('up', 'top'), ('down', 'bottom'), ('side', 'side'), ('side', 'front')):
                        if low.endswith(suff) and face not in faces:
                            faces[face] = f
            for c in ([] if faces else cands):
                if not c:
                    continue
                c = c.split(':')[-1]
                for face, suff in (('up', '_top'), ('down', '_bottom'), ('side', '_side'), ('side', '_front')):
                    f = self._tex_lookup(blocks_tex, c + suff)
                    if f and face not in faces:
                        faces[face] = f
                if faces:
                    break
            if base and not faces:
                e['texture'] = self._add_texture(base, 'terrain', Path(base).stem)
            elif faces:
                tex = {}
                for face, f in faces.items():
                    tex[face] = self._add_texture(f, 'terrain', Path(f).stem)
                allt = self._add_texture(base, 'terrain', Path(base).stem) if base else None
                if 'side' not in tex:
                    tex['side'] = allt or next(iter(tex.values()))
                for face in ('up', 'down'):
                    tex.setdefault(face, allt or tex['side'])
                if None not in tex.values():
                    e['texture'] = tex
                    self.rep.add('guessed', f'block {e["key"]}: faces from {", ".join(Path(f).name for f in faces.values())} (registerBlockIcons not interpreted)')
            if not e.get('texture'):
                self._placeholder(e, 'terrain', 'block')

    def _blockstate_fallback(self, path, e):
        """Blockstates whose variants only rotate the block (facing/axis): use the first variant's cube model."""
        bs = A.read_json(self.jar, f'assets/{self.ns}/blockstates/{path}.json')
        if not isinstance(bs, dict) or not isinstance(bs.get('variants'), dict) or not bs['variants']:
            return False
        props = set()
        for k in bs['variants']:
            for kv in filter(None, k.split(',')):
                props.add(kv.split('=')[0])
        if not props <= {'facing', 'axis', 'horizontal_facing', 'rotation', 'powered', 'waterlogged', ''}:
            return False
        v = next(iter(bs['variants'].values()))
        v = v[0] if isinstance(v, list) else v
        mref = (v or {}).get('model', '')
        mpath = mref.split(':', 1)[-1]
        mdl = A.read_json(self.jar, f'assets/{self.ns}/models/{mpath}.json') or {}
        tex = mdl.get('textures') or {}

        def tk(ref):
            if not isinstance(ref, str):
                return None
            tns, _, tp = ref.partition(':') if ':' in ref else ('minecraft', '', ref)
            if tns != self.ns:
                return None
            f = f'assets/{self.ns}/textures/{tp}.png'
            return self._add_texture(f, 'terrain', tp.split('/')[-1]) if f in self.jar else None
        faces = None
        if 'all' in tex:
            faces = tk(tex['all'])
        elif 'side' in tex:
            faces = {'up': tk(tex.get('top', tex.get('end', tex['side']))), 'down': tk(tex.get('bottom', tex.get('end', tex['side']))),
                     'side': tk(tex['side'])}
            if 'front' in tex:
                faces['north'] = tk(tex['front'])
            if None in faces.values():
                faces = None
        if not faces:
            return False
        e['texture'] = faces
        if props & {'facing', 'horizontal_facing'}:
            e['fields']['placement'] = 'facing'
        elif 'axis' in props:
            e['fields']['placement'] = 'axis'
        self.rep.add('auto', f'block {e["key"]}: rotation-only blockstate ({", ".join(sorted(props))}) -> placement '
                             f'{e["fields"].get("placement", "fixed")}, faces from {mpath}')
        return True

    def _placeholder(self, e, atlas, what):
        stem = 'placeholder_' + e['key'].split(':', 1)[1].replace('/', '_')
        key = f'{self.lid}:{"t_" if atlas == "terrain" else "i_"}{stem}'
        dst = self.out / f'textures/{atlas}/{stem}.png'
        placeholder_png(dst, e['key'])
        self.textures[key] = {'file': f'textures/{atlas}/{stem}.png', 'atlas': atlas}
        e['texture'] = key
        self.rep.add('guessed', f'{what} {e["key"]}: no icon found in the jar (custom renderer or dynamic icon); a generated placeholder is used')

    def _names(self):
        lang = self.lang
        ns = self.ns
        for kind, entries in (('item', self.item_entries), ('block', self.block_entries)):
            for path, e in entries.items():
                r = e['reg']
                if e.get('variant') is not None:
                    u = e.get('unloc')
                    vc = [f'{u}.name', u] if u else []
                    name = next((lang[c] for c in vc if c and c in lang), None)
                    if name is None:
                        base = next((lang[c] for c in (f'item.{r.unloc}.name', f'tile.{r.unloc}.name') if r.unloc and c in lang), None)
                        sfx = path[len(r.path) + 1:] if path.startswith(r.path + '_') else str(e['variant']['meta'])
                        name = f'{base or r.name.replace("_", " ").title()} ({sfx.replace("_", " ")})'
                        self.rep.add('guessed', f'{kind} {e["key"]}: no en_US name for sub-item {u or e["variant"]["meta"]}; using "{name}"')
                    e['name'] = name[:127]
                    continue
                cands = [f'{kind}.{ns}.{path}', f'{kind}.{ns}.{r.name}' if r else None]
                if r and r.unloc:
                    u = r.unloc
                    cands += [f'item.{u}.name', f'tile.{u}.name', f'{u}.name', f'item.{ns}.{u}.name', f'tile.{ns}.{u}.name', f'item.{ns}:{u}.name',
                              f'tile.{ns}:{u}.name', u]
                if r:
                    cands += [f'item.{r.name}.name', f'tile.{r.name}.name', f'{kind}.{r.name}.name', f'block.{ns}.{r.name}', f'item.{ns}.{r.name}']
                name = next((lang[c] for c in cands if c and c in lang), None)
                if name is None and e.get('port_name') and not e['port_name'].replace(' ', '').lower() == path.replace('_', ''):
                    name = e['port_name']
                if name is None:
                    name = (r.unloc or r.name if r else path).replace('_', ' ')
                    name = re.sub(r'^(item|tile|block)\.', '', name)
                    name = re.sub(r'(?<=[a-z])(?=[A-Z])', ' ', name).replace('.', ' ').strip().title()
                    self.rep.add('guessed', f'{kind} {e["key"]}: no en_US name; using "{name}"')
                e['name'] = name[:127]

    # ---- recipes ----------------------------------------------------------------
    def _code_recipes(self):
        rr = self.rr
        n = 0
        seen_sigs = set()
        for r in self.facts.recipes:
            w = f'recipe in {r["where"]}'
            out_v, cnt, aux = rr.stack(r['out'])
            out = rr.key(r['out'], w + ' (result)')
            if not out:
                self.rep.add('manual', f'{w}: result not portable ({rr.last})')
                continue
            if aux not in (None, 0, -1, 32767) and out.startswith('minecraft:') and '@' not in out:
                out = f'{out}@{aux}'
            entry = {'key': None, 'result': out, 'count': max(1, min(64, cnt or 1)), 'craft_tab': self.hints.get('craft_tab', 'minecraft:misc')}
            if r['type'] == 'shaped':
                pat = r.get('pattern') or []
                if not r.get('ok') or not pat or len(pat) > 3 or any(len(x) > 3 for x in pat):
                    self.rep.add('manual', f'{w}: shaped recipe arguments not constant or larger than 3x3 ({pat})')
                    continue
                keys, ok = {}, True
                for ch, v in r['keys'].items():
                    if not any(ch in row for row in pat):
                        continue
                    k = rr.key(v, w)
                    if not k:
                        ok = False
                        break
                    keys[ch] = k
                if not ok:
                    self.rep.add('manual', f'{w}: unresolved ingredient ({rr.last}) -> recipe for {out} skipped')
                    continue
                missing = {c for row in pat for c in row if c != ' ' and c not in keys}
                if missing:
                    self.rep.add('manual', f'{w}: pattern symbols {sorted(missing)} have no key -> skipped')
                    continue
                entry.update(type='shaped', pattern=[row for row in pat], keys=keys)
            else:
                ings = []
                ok = True
                for v in r['ingredients']:
                    k = rr.key(v, w)
                    if not k:
                        ok = False
                        break
                    ings.append(k)
                if not ok or not 1 <= len(ings) <= 9:
                    self.rep.add('manual', f'{w}: unresolved/oversized shapeless recipe for {out} skipped ({rr.last if not ok else f"{len(ings)} ingredients"})')
                    continue
                entry.update(type='shapeless', ingredients=ings)
            sig = json.dumps({k: v for k, v in entry.items() if k != 'key'}, sort_keys=True)
            if sig in seen_sigs:
                continue  # the same recipe reached through another code path
            seen_sigs.add(sig)
            n += 1
            entry['key'] = f'{self.lid}:code_{D.normalize_name(out.split(":")[1].split("@")[0])}_{n}'
            self.recipes.append(entry)
            self.rep.add('auto', f'recipe {entry["key"]} ({entry["type"]} -> {out} x{entry["count"]}) from {r["where"]}')
        for s in self.facts.smelting:
            w = f'smelting in {s["where"]}'
            inp = rr.key(s['input'], w + ' (input)')
            _ov, cnt, aux = rr.stack(s['out'])
            res = rr.key(s['out'], w + ' (result)')
            if not inp or not res:
                self.rep.add('manual', f'{w}: unresolved input/result -> skipped')
                continue
            if aux not in (None, 0, -1, 32767) and res.startswith('minecraft:') and '@' not in res:
                res = f'{res}@{aux}'
            if any(x['input'] == inp for x in self.smelting):
                self.rep.add('manual', f'{w}: duplicate smelting input {inp} skipped')
                continue
            key = f'{self.lid}:smelt_{D.normalize_name(inp.split(":")[1].split("@")[0])}'
            if any(x['key'] == key for x in self.smelting):
                key += f'_{len(self.smelting)}'
            self.smelting.append({'key': key, 'input': inp, 'result': res, 'count': max(1, min(64, cnt or 1)),
                                  'xp': round(float(s['xp'] or 0), 3)})
            self.rep.add('auto', f'smelting {key}: {inp} -> {res} from {s["where"]}')
        for r in getattr(self, 'port_recipes', []):
            if not self._refs_ok(r):
                continue
            self.recipes.append(r)
        for s in getattr(self, 'port_smelting', []):
            if any(x['input'] == s['input'] for x in self.smelting):
                self.rep.add('manual', f'smelting {s["key"]}: duplicate input {s["input"]} skipped')
                continue
            self.smelting.append(s)

    def _refs_ok(self, r):
        return True

    def _json_recipes(self):
        """1.12 assets/<ns>/recipes JSON (data values, ore dictionary, _constants)."""
        if self.era != 'old':
            return
        recs, consts = A.old_json_recipes(self.jar, self.ns)
        rr = self.rr
        for path, r in recs:
            name = Path(path).stem
            w = f'recipe {name} ({path})'
            rtype = str(r.get('type', '')).split(':')[-1]
            res = r.get('result') or {}
            out = self._json_ing(res, w, consts)
            if not out:
                self.rep.add('manual', f'{w}: result unresolved -> skipped')
                continue
            cnt = int(res.get('count', 1)) if isinstance(res, dict) else 1
            entry = {'key': f'{self.lid}:{D.normalize_name(name)}', 'result': out, 'count': cnt, 'craft_tab': self.hints.get('craft_tab', 'minecraft:misc')}
            if rtype in ('crafting_shaped', 'ore_shaped', 'shaped'):
                keys, ok = {}, True
                for sym, spec in (r.get('key') or {}).items():
                    k = self._json_ing(spec, w, consts)
                    if not k:
                        ok = False
                        break
                    keys[sym] = k
                if not ok:
                    self.rep.add('manual', f'{w}: unresolved ingredient -> skipped')
                    continue
                entry.update(type='shaped', pattern=r.get('pattern') or [], keys=keys)
            elif rtype in ('crafting_shapeless', 'ore_shapeless', 'shapeless'):
                ings = [self._json_ing(s, w, consts) for s in r.get('ingredients') or []]
                if None in ings or not ings:
                    self.rep.add('manual', f'{w}: unresolved ingredient -> skipped')
                    continue
                entry.update(type='shapeless', ingredients=ings)
            else:
                self.rep.add('manual', f'{w}: recipe type {r.get("type")} not supported')
                continue
            if any(x['key'] == entry['key'] for x in self.recipes):
                entry['key'] += f'_{len(self.recipes)}'
            self.recipes.append(entry)
            self.rep.add('auto', f'recipe {entry["key"]} ({entry["type"]}) from {path}')

    def _json_ing(self, spec, w, consts):
        if isinstance(spec, list):
            if len(spec) > 1:
                self.rep.add('guessed', f'{w}: alternative ingredients; the first is used')
            spec = spec[0] if spec else None
        if isinstance(spec, str):
            spec = consts.get(spec, {'item': spec}) if spec.startswith('#') else {'item': spec}
        if not isinstance(spec, dict):
            return None
        if spec.get('item', '').startswith('#') and spec['item'] in consts:
            return self._json_ing(consts[spec['item']], w, consts)
        t = str(spec.get('type', '')).split(':')[-1]
        if t == 'ore_dict' or 'ore' in spec:
            ore = spec.get('ore')
            for v2 in self.rr.oredict.get(ore, []):
                k = self.rr.key(v2, w, allow_tag=False)
                if k:
                    self.rep.add('guessed', f'{w}: ore dictionary "{ore}" -> {k} (registered by this mod)')
                    return k
            tgt = VN.OREDICT.get(ore)
            if tgt:
                self.rep.add('guessed', f'{w}: ore dictionary "{ore}" -> {tgt}')
                return tgt
            self.rep.add('manual', f'{w}: ore dictionary "{ore}" has no 1.95 mapping')
            return None
        name = spec.get('item')
        if not name:
            return None
        aux = spec.get('data')
        ns, _, p = name.partition(':') if ':' in name else ('minecraft', '', name)
        if ns == 'minecraft':
            return rr_name(self.rr, f'minecraft:{p}', aux, w)
        if ns == self.ns:
            k = f'{self.lid}:{D.normalize_name(p)}'
            if any(e['key'] == k for e in list(self.item_entries.values()) + list(self.block_entries.values())):
                try:
                    aux = int(aux) if aux is not None else None
                except (TypeError, ValueError):
                    aux = None
                return self.rr.variant(k, aux, w)
            self.rep.add('manual', f'{w}: references {name}, which was not ported')
            return None
        self.rep.add('manual', f'{w}: references another mod ({name})')
        return None

    # ---- fuels / worldgen / entities ------------------------------------------------------
    def _fuels(self):
        rr = self.rr
        for fu in self.facts.fuels:
            k = rr.key(fu['ref'], f'fuel in {fu["where"]}')
            if not k:
                continue
            self._set_fuel(k, fu['ticks'], fu['where'])
        for item, (ticks, f) in A.data_map_fuels(self.jar).items():
            k = self._vanilla_or_mod(item, f) if ':' in item else None
            if k:
                self._set_fuel(k, ticks, f)

    def _set_fuel(self, key, ticks, where):
        for e in self.item_entries.values():
            if e['key'] == key:
                e['fields']['fuel_ticks'] = int(ticks)
                self.rep.add('auto', f'item {key}: fuel {ticks} ticks ({where})')
                return
        if not any(f['item'] == key for f in self.ext['fuels']):
            self.ext['fuels'].append({'item': key, 'burn_ticks': int(ticks)})
            self.rep.add('auto', f'fuel {key}: {ticks} ticks ({where})')

    def _worldgen(self):
        rr = self.rr
        n = 0
        for o in self.facts.ores:
            w = f'ore generator in {o["where"]}'
            k = rr.key(o['block'], w)
            if not k:
                self.rep.add('manual', f'{w}: ore block unresolved')
                continue
            n += 1
            e = {'key': f'{self.lid}:ore_{D.normalize_name(k.split(":")[1].split("@")[0])}_{n}', 'block': k.split('@')[0],
                 'size': int(o['size'] or 8), 'per_chunk': int(o['per_chunk'] or 8),
                 'min_y': int(max(0, o['min_y'])) if o['min_y'] is not None else 0, 'max_y': int(min(255, o['max_y'])) if o['max_y'] is not None else 64}
            if e['max_y'] < e['min_y']:
                e['max_y'] = e['min_y']
            guessed = [x for x, v in (('size', o['size']), ('per_chunk', o['per_chunk']), ('height', o['min_y'])) if v is None]
            self.rep.add('guessed' if guessed else 'auto', f'ore {e["key"]}: {k} size {e["size"]}, {e["per_chunk"]}/chunk, y {e["min_y"]}-{e["max_y"]} '
                                                           f'(WorldGenMinable in {o["where"]})' + (f'; defaults for {", ".join(guessed)}' if guessed else '')
                         + '; dimension assumed overworld')
            self.ext['ores'].append(e)
        for o in getattr(self, 'port_ores', []):
            self.ext['ores'].append(o)
        if self.facts.world_generators and not self.facts.ores:
            self.rep.add('manual', f'world generators {", ".join(c.split("/")[-1] for c in self.facts.world_generators)}: no constant WorldGenMinable found '
                                   '(structures/custom generation need features/structures by hand)')

    def _entities(self):
        prog = self.facts.prog
        found = []  # (cls, name, eggs, where, extra)
        for e in self.facts.entity_regs:
            found.append((e['cls'], e['name'], e['egg'], e['where'], {}))
        for r in self.regs_by_kind.get('entity_type', []):
            cls, cat, wd, ht = X.entity_type_info(prog, r.obj)
            if cls:
                found.append((cls, r.name, [], r.where, {'category': cat, 'width': wd, 'height': ht}))
            else:
                self.rep.add('manual', f'entity type {r.name} ({r.where}): entity class not resolved from EntityType.Builder')
        # spawn eggs (modern): SpawnEggItem(type, primary, secondary, props)
        eggs = {}
        for path, e in list(self.item_entries.items()):
            r = e['reg']
            if r is None:
                continue
            base, args = D._vanilla_init(prog, r.obj)
            if simple(base) in ('SpawnEggItem', 'DeferredSpawnEggItem', 'ForgeSpawnEggItem') and len(args) >= 3:
                t = unwrap(args[0], prog)
                ints = [num(a) for a in args[1:3]]
                eggs[id(t)] = ints
                self.rep.add('auto', f'item {e["key"]}: spawn egg -> entity egg colours (the item itself is dropped; 1.95 uses its own spawn egg)')
                del self.item_entries[path]
        seen = set()
        base_ok = VN.base_mobs_195()
        lt = A.loot_tables(self.jar, self.ns)
        spawns_json = A.biome_spawns(self.jar, self.ns)
        for cls, name, egg, where, extra in found:
            if cls in seen or not name:
                continue
            seen.add(cls)
            path = D.normalize_name(name.split(':')[-1])
            key = f'{self.lid}:{path}'
            chain = [simple(c) for c in prog.chain(cls)]
            base = next((VN.ENTITY_BASE[c] for c in chain if c in VN.ENTITY_BASE), None)
            cat = None
            gnote = None
            if base is None:
                g = next((VN.ENTITY_GENERIC[c] for c in chain if c in VN.ENTITY_GENERIC), None)
                if g:
                    base, cat = g
                    gnote = f'entity {key}: abstract base {next(c for c in chain if c in VN.ENTITY_GENERIC)} -> base_mob minecraft:{base} (guess)'
            if base is None:
                if cls in self.rr.projectiles:   # API 1.9: the projectile of a ported throwable item
                    self.rep.add('auto', f'entity {key} ({cls.split("/")[-1]} extends {chain[-1]}): ported as the projectile of throwable item '
                                         f'{self.rr.projectiles[cls]} (flies and renders as a snowball; no entity registration needed)')
                    continue
                if any(c in VN.NONLIVING_ENTITY for c in chain):
                    self.rep.add('impossible', f'entity {key} ({cls.split("/")[-1]} extends {chain[-1]}): non-living entities (projectiles, vehicles, '
                                               'effects) cannot be cloned on 1.95 (entities195 clones LivingEntity types only)')
                else:
                    self.rep.add('manual', f'entity {key} ({cls.split("/")[-1]}): base class {chain[-1]} has no 1.95 base mob')
                continue
            if base_ok is not None and base not in base_ok:
                self.rep.add('impossible', f'entity {key}: base mob {base} is not cloneable on 1.95 (modules/entities195/base_mobs195.inc)')
                continue
            if gnote:
                self.rep.add('guessed', gnote)
            ent = {'key': key, 'name': self._entity_name(name, path), 'base_mob': f'minecraft:{base}',
                   'category': extra.get('category') if extra.get('category') in ('creature', 'monster', 'water', 'ambient')
                   else cat or VN.ENTITY_CATEGORY.get(base, 'creature')}
            if extra.get('category') == 'water_creature':
                ent['category'] = 'water'
            attrs, goals, notes = self._entity_attrs(cls, key)
            ent.update(attrs)
            if goals:
                ent['goals'] = goals
            self.rep.extend(notes)
            e2 = eggs.get(id(next((r.obj for r in self.regs_by_kind.get('entity_type', []) if r.name == name), None)), None)
            egg = egg or e2 or []
            if len(egg) >= 2 and None not in egg[:2]:
                ent['egg_primary'] = '#%06x' % (int(egg[0]) & 0xffffff)
                ent['egg_secondary'] = '#%06x' % (int(egg[1]) & 0xffffff)
            if extra.get('width'):
                ent['width'] = float(extra['width'])
                ent['height'] = float(extra['height'] or 1.8)
            drops = self._entity_drops(cls, path, lt, key)
            if drops:
                ent['drops'] = drops
            for sp in self.facts.spawns:
                if sp['cls'] == cls:
                    if sp['weight'] is not None:
                        ent['spawn_weight'] = int(sp['weight'])
                    if sp['min'] is not None:
                        ent['min_group'] = int(sp['min'])
                    if sp['max'] is not None:
                        ent['max_group'] = int(sp['max'])
                    if sp['category'] in ('monster', 'creature', 'ambient', 'watercreature', 'water_creature'):
                        ent['category'] = 'water' if 'water' in sp['category'] else sp['category']
                    if sp['weight'] is None or sp['min'] is None or sp['max'] is None:
                        self.rep.add('guessed', f'entity {key}: spawn weight/group not constant (config?) in {sp["where"]}; base mob spawning used')
                    else:
                        self.rep.add('auto', f'entity {key}: spawn weight {sp["weight"]}, group {sp["min"]}-{sp["max"]} ({sp["where"]})')
                    if sp['biomes']:
                        self.rep.add('guessed', f'entity {key}: spawn biomes {sp["biomes"][:4]} are not constant 1.95 biome keys; left to the base mob biomes')
            for (etype, wgt, mn, mx, biomes, f) in spawns_json:
                if isinstance(etype, str) and etype.split(':')[-1] == name.split(':')[-1]:
                    if isinstance(wgt, int):
                        ent['spawn_weight'] = wgt
                    if isinstance(mn, int):
                        ent['min_group'] = mn
                    if isinstance(mx, int):
                        ent['max_group'] = mx
                    bl = self._biomes(biomes)
                    if bl:
                        ent['spawn_biomes'] = bl
                    self.rep.add('auto', f'entity {key}: spawns from {f}')
            tex = self._entity_texture(cls, path)
            if tex:
                ent['texture'] = tex
            else:
                self.rep.add('guessed', f'entity {key}: no matching 64x32/64x64 skin found; renders with the {base} skin')
            self.rep.add('auto', f'entity {key}: clone of minecraft:{base} ({cls.split("/")[-1]} extends {" -> ".join(chain[1:4])}; {where}); '
                                 'model/renderer are the base mob\'s')
            self.ext['entities'].append(ent)
            self.entity_classes = getattr(self, 'entity_classes', {})
            self.entity_classes[cls] = key

    def _entity_name(self, name, path):
        for k in (f'entity.{self.ns}.{path}', f'entity.{name}.name', f'entity.{self.ns}.{name}.name', f'entity.{self.ns}:{name}.name'):
            if k in self.lang:
                return self.lang[k][:127]
        return path.replace('_', ' ').title()

    def _biomes(self, b):
        vals = b if isinstance(b, list) else [b] if isinstance(b, str) else []
        out = []
        for v in vals:
            if isinstance(v, str) and not v.startswith('#') and v.startswith('minecraft:'):
                out.append(v)
            elif isinstance(v, str) and v.startswith('#'):
                self.rep.add('guessed', f'spawn biome tag {v}: not expanded; base mob biomes used')
        return out

    def _entity_attrs(self, cls, key):
        prog = self.facts.prog
        attrs, goals, notes = {}, [], []
        for c in prog.chain(cls):
            cf = prog.classes.get(c)
            if cf is None:
                break
            for m in cf.methods:
                if m.code is None:
                    continue
                if m.name in ('applyEntityAttributes', 'createAttributes', 'registerAttributes', 'createMobAttributes', 'setAttributes') or \
                        (m.name.startswith('create') and 'Attribute' in m.desc):
                    fr = prog.frames.get((cf.name, m.name, m.desc)) or prog.run_root(cf.name, m.name, m.desc)
                    for ev in (fr.events if fr else []):
                        if ev.name in ('setBaseValue', 'func_111128_a') and ev.args and isinstance(ev.recv, Ret):
                            fld = next((a for a in ev.recv.args if isinstance(a, SField)), None)
                            v = num(ev.args[0])
                            if fld is not None and v is not None:
                                a = VN.ATTRIBUTE_MAP.get(fld.name.lower())
                                if a and a not in attrs:
                                    attrs[a] = float(v)
                        elif ev.name == 'add' and len(ev.args) == 2 and isinstance(ev.args[0], SField):
                            a = VN.ATTRIBUTE_MAP.get(ev.args[0].name.lower())
                            v = num(ev.args[1])
                            if a and v is not None and a not in attrs:
                                attrs[a] = float(v)
                elif m.name in ('<init>', 'registerGoals', 'initEntityAI', 'func_184651_r'):
                    fr = prog.frames.get((cf.name, m.name, m.desc)) or prog.run_root(cf.name, m.name, m.desc)
                    for ev in (fr.events if fr else []):
                        if ev.name in ('addTask', 'addGoal', 'func_75776_a') and len(ev.args) == 2 and isinstance(ev.args[1], Obj):
                            g = ev.args[1]
                            gname = simple(g.cls)
                            goal = VN.AI_GOALS.get(gname) or VN.AI_GOALS.get(simple(prog.vanilla_base(g.cls)))
                            pr = num(ev.args[0])
                            if goal is None:
                                if gname not in VN.AI_IGNORABLE:
                                    notes.append(('manual', f'entity {key}: AI task {gname} has no 1.95 goal'))
                                continue
                            if goal == 'target_player' and not any(isinstance(a, ClassRef) and simple(a.name) in ('EntityPlayer', 'Player')
                                                                   for a in g.args or []):
                                notes.append(('manual', f'entity {key}: {gname} targets a non-player class; dropped'))
                                continue
                            entry = {'goal': goal, 'priority': int(pr) if pr is not None else len(goals)}
                            spd = next((num(a) for a in g.args or [] if isinstance(a, Const) and a.t == 'D'), None)
                            if spd is not None:
                                entry['speed'] = float(spd)
                            if goal == 'follow_tempt_item':
                                it = next((self.rr.key(a, f'entity {key} tempt') for a in g.args or [] if isinstance(a, (SField, Obj)) and not isinstance(a, Ret)
                                           and simple(getattr(a, 'cls', '')) not in ('Ingredient',)), None)
                                if it:
                                    entry['param'] = it
                                else:
                                    continue
                            goals.append(entry)
        if attrs:
            notes.append(('auto', f'entity {key}: attributes {attrs}'))
        if goals:
            notes.append(('auto', f'entity {key}: {len(goals)} AI goals mapped ({", ".join(g["goal"] for g in goals)})'))
        return attrs, goals, notes

    def _entity_drops(self, cls, path, lt, key):
        prog = self.facts.prog
        out = []
        t = lt['entities'].get(path)
        if t:
            for item, lo, hi, chance, rolls in A.loot_simple(t[0]):
                k = self._vanilla_or_mod(item, t[1]) if isinstance(item, str) else None
                if k:
                    out.append({'item': k, 'min': lo, 'max': hi, 'chance': chance})
            if out:
                self.rep.add('auto', f'entity {key}: drops from {t[1]}')
            return out
        v, w = D._method_return(prog, cls, ('getDropItem',), want_num=False)
        if w and isinstance(v, list) and len(v) == 1:
            k = self.rr.key(v[0], f'entity {key} drop ({w})')
            if k:
                out.append({'item': k, 'min': 0, 'max': 2, 'chance': 100, 'looting': 1})
                self.rep.add('guessed', f'entity {key}: drops {k} from {w}; vanilla 0-2 + looting count assumed')
        return out

    def _entity_texture(self, cls, path):
        cands = [path, path.replace('_', ''), cls.split('/')[-1].replace('Entity', '').lower()]
        f = None
        for c in cands:
            for k, v in self.texfiles['entity'].items():
                if k == c or k.replace('_', '') == c.replace('_', '') or k == 'entity_' + c:
                    f = v
                    break
            if f:
                break
        if not f:
            return None
        sz = A.image_size(self.jar, f)
        if not sz:
            return None
        w, h = sz
        if w == 2 * h:
            size = (64, 32)
        elif w == h:
            size = (64, 64)
        else:
            self.rep.add('manual', f'entity skin {f} is {w}x{h}; 1.95 needs 64x32 or 64x64 (custom model UVs)')
            return None
        key = f'{self.lid}:ent_{D.normalize_name(Path(f).stem)}'
        dst = f'textures/entity/{D.normalize_name(Path(f).stem)}.png'
        if key not in self.textures:
            if not save_png(self.jar.read(f), self.out / dst, size, self.rep, f):
                return None
            self.textures[key] = {'file': dst, 'atlas': 'entity'}
        self.rep.add('guessed', f'entity skin {f}: the mod\'s own model UVs may not match the base mob model')
        return key

    def _effects_potions_enchants(self):
        prog = self.facts.prog
        # effects: registered (1.12/modern) + allocated subclasses of Potion (1.7.10 Potion.potionTypes)
        objs = [(r.name, r.obj, r.where) for r in self.regs_by_kind.get('effect', [])]
        seen = {D.site_key(o) for _n, o, _w in objs}
        for fr in list(prog.frames.values()):
            for pc, o in fr.news:
                if D.site_key(o) in seen or not prog.is_mod(o.cls):
                    continue
                if classify(prog, o) == 'effect':
                    nm = None
                    a = o.call_args('setPotionName')
                    if a and str_val(a[0]):
                        nm = str_val(a[0]).split('.')[-1]
                    objs.append((nm or simple(o.cls), o, f'{fr.cls.split("/")[-1]}.{fr.method.name}'))
                    seen.add(D.site_key(o))
        for name, o, where in objs:
            path = D.normalize_name(name.split(':')[-1])
            key = f'{self.lid}:{path}'
            base, args = D._vanilla_init(prog, o)
            ints = [a for a in args if isinstance(a, Const) and a.t in ('I', 'Z')]
            e = {'key': key, 'name': self._lang_any([f'effect.{self.ns}.{path}', f'potion.{path}', f'potion.{name}', f'effect.{name}'],
                                                     path.replace('_', ' ').title())}
            bad = next((a for a in args if isinstance(a, Const) and a.t == 'Z'), None)
            col = [a for a in args if isinstance(a, Const) and a.t == 'I']
            cat = next((a for a in args if isinstance(a, SField) and simple(a.owner) in ('MobEffectCategory', 'EffectType')), None)
            if cat is not None:
                e['beneficial'] = 1 if cat.name.upper() == 'BENEFICIAL' else 0
            elif bad is not None:
                e['beneficial'] = 0 if bad.v else 1
            if col:
                e['color'] = '#%06x' % (int(col[-1].v) & 0xffffff)
            beh = [m.name for m in prog.classes[o.cls].methods if m.name in ('performEffect', 'applyEffectTick', 'onEffectStarted', 'isReady',
                                                                               'shouldApplyEffectTickThisTick')] if o.cls in prog.classes else []
            if simple(base) in ('InstantenousMobEffect', 'PotionHealth', 'InstantEffect', 'InstantenousEffect'):
                e['clone_of'] = 'minecraft:instant_damage' if e.get('beneficial') == 0 else 'minecraft:instant_health'
                e['instant'] = 1
                self.rep.add('guessed', f'effect {key}: instant effect class {simple(base)} -> clone_of {e["clone_of"]}')
            elif simple(base) in ('AttackDamageMobEffect', 'AttackDamageEffect', 'PotionAttackDamage'):
                e['clone_of'] = 'minecraft:weakness' if e.get('beneficial') == 0 else 'minecraft:strength'
                self.rep.add('guessed', f'effect {key}: attack damage effect class {simple(base)} -> clone_of {e["clone_of"]}')
            self.ext['effects'].append(e)
            self.rep.add('auto', f'effect {key} (colour {e.get("color")}, beneficial {e.get("beneficial")}) from {simple(o.cls)} ({where})')
            if beh:
                self.rep.add('manual', f'effect {key}: per-tick behaviour ({", ".join(beh)}) needs the generated EffectTick script '
                                       '(effects195 raises LCE_EV_EFFECT_TICK every 20 ticks for an effect without clone_of)')
                self.lua.add(f'effect {key} <- {o.cls.split("/")[-1]}.{beh[0]}', 'EFFECT_TICK', 'effect', key, None,
                             ['custom effect behaviour'], self._java(o.cls, beh[0]))
            else:
                if 'clone_of' not in e:
                    self.rep.add('manual', f'effect {key}: no behaviour and no vanilla clone_of; set "clone_of" by hand if it mirrors a vanilla effect '
                                           '(regeneration/poison/wither/hunger/speed/strength/... work; identity-checked ones like night_vision do not)')
        for r in self.regs_by_kind.get('potion', []):
            o = r.obj
            effs = []
            for a in (o.args or []):
                for x in (a.values() if hasattr(a, 'values') and not isinstance(a, dict) else [a]):
                    ei = D._effect_instance(x, self.rr, f'potion {r.name}')
                    if ei:
                        effs.append(ei)
            path = D.normalize_name(r.name)
            if not effs:
                self.rep.add('manual', f'potion {r.name} ({r.where}): effects not constant')
                continue
            self.ext['potions'].append({'key': f'{self.lid}:{path}', 'name': self._lang_any([f'item.minecraft.potion.effect.{path}'], path.replace('_', ' ').title()),
                                        'effects': [{k: v for k, v in x.items() if k != 'chance'} for x in effs]})
            self.rep.add('auto', f'potion {self.lid}:{path}: {effs} ({r.where})')
        for b in self.facts.brewing:
            w = f'brewing in {b["where"]}'
            inp = self._potion_ref(b['input'])
            out = self._potion_ref(b['output'])
            reag = self.rr.key(b['reagent'], w)
            if inp and out and reag:
                self.ext['brewing'].append({'key': f'{self.lid}:brew_{len(self.ext["brewing"])}', 'input': inp, 'reagent': reag, 'output': out})
                self.rep.add('auto', f'{w}: {inp} + {reag} -> {out}')
            else:
                self.rep.add('manual', f'{w}: brewing recipe not resolvable')
        # enchantments (1.7.10/1.12 classes; 1.21 JSON)
        ench = [(r.name, r.obj, r.where) for r in self.regs_by_kind.get('enchantment', [])]
        seen = {D.site_key(o) for _n, o, _w in ench}
        for fr in list(prog.frames.values()):
            for pc, o in fr.news:
                if D.site_key(o) not in seen and prog.is_mod(o.cls) and classify(prog, o) == 'enchantment':
                    a = o.call_args('setName')
                    ench.append(((str_val(a[0]) if a else None) or simple(o.cls), o, f'{fr.cls.split("/")[-1]}.{fr.method.name}'))
                    seen.add(D.site_key(o))
        for name, o, where in ench:
            path = D.normalize_name(name.split(':')[-1])
            base, args = D._vanilla_init(prog, o)
            ml, _mw = D._method_return(prog, o.cls, ('getMaxLevel',))
            ints = [num(a) for a in args if isinstance(a, Const) and a.t == 'I']
            typ = next((a.name.lower() for a in args if isinstance(a, SField) and simple(a.owner) in ('EnumEnchantmentType', 'EnchantmentCategory')), None)
            applies = {'weapon': ['sword'], 'digger': ['pickaxe', 'axe', 'shovel'], 'armor': ['helmet', 'chestplate', 'leggings', 'boots'],
                       'armor_head': ['helmet'], 'armor_chest': ['chestplate'], 'armor_torso': ['chestplate'], 'armor_legs': ['leggings'],
                       'armor_feet': ['boots'], 'breakable': ['sword', 'pickaxe', 'axe', 'shovel', 'hoe'], 'all': ['sword', 'pickaxe', 'axe', 'shovel', 'hoe']}.get(typ)
            e = {'key': f'{self.lid}:{path}', 'name': self._lang_any([f'enchantment.{name}', f'enchantment.{self.ns}.{path}'], path.replace('_', ' ').title()),
                 'max_level': int(ml) if isinstance(ml, int) else 1, 'weight': int(ints[1]) if len(ints) >= 2 else 5,
                 'applies_to': applies or ['sword']}
            e['weight'] = min(10, max(1, e['weight']))
            clone = self._enchant_clone(prog, o, base)
            if clone:
                e['clone_of'] = clone
            if typ in ('weapon', 'digger', 'armor', 'armor_feet', 'armor_head', 'armor_chest', 'armor_torso', 'armor_legs', 'breakable', 'all') and \
                    D._method_return(prog, o.cls, ('isTreasureEnchantment', 'isTreasureOnly'))[0] is True:
                e['treasure'] = 1
            self.ext['enchantments'].append(e)
            self.rep.add('guessed' if not applies or not isinstance(ml, int) else 'auto',
                         f'enchantment {e["key"]} (max {e["max_level"]}, weight {e["weight"]}, {typ}) from {simple(o.cls)} ({where})')
            if clone:
                self.rep.add('guessed', f'enchantment {e["key"]}: clone_of {clone} (from {simple(base)} / its damage-protection overrides): '
                                        'enchants195 reuses that vanilla behaviour on 1.95')
            else:
                self.rep.add('manual', f'enchantment {e["key"]}: its effect is code ({simple(o.cls)}); it is registered without built-in behaviour '
                                       '(name, levels, table weight, anvil) - script it or set "clone_of" (sharpness/smite/bane/impaling/protection family)')
        for path, (d, f) in A.enchant_json(self.jar, self.ns).items():
            sup = d.get('supported_items') or d.get('primary_items') or ''
            sup = sup if isinstance(sup, str) else ''
            applies = [k for k in ('sword', 'pickaxe', 'axe', 'shovel', 'hoe', 'helmet', 'chestplate', 'leggings', 'boots') if k in sup] or \
                (['helmet', 'chestplate', 'leggings', 'boots'] if 'armor' in sup else ['sword', 'pickaxe', 'axe', 'shovel'] if 'mining' in sup or 'weapon' in sup else ['sword'])
            p = D.normalize_name(path)
            e = {'key': f'{self.lid}:{p}', 'name': self._lang_any([f'enchantment.{self.ns}.{path}'], p.replace('_', ' ').title()),
                 'max_level': int(d.get('max_level', 1)), 'weight': int(d.get('weight', 5)), 'applies_to': applies}
            self.ext['enchantments'].append(e)
            self.rep.add('auto', f'enchantment {e["key"]} from {f} (supported {sup or "?"})')
            if d.get('effects'):
                self.rep.add('manual', f'enchantment {e["key"]}: data-driven effects {sorted(d["effects"])[:5]} are not representable; set clone_of or script it')

    # enchants195 behaviour clones (modules/enchants195: virtual-dispatch damage/protection classes only)
    ENCHANT_BASE_CLONE = {'EnchantmentDamage': 'minecraft:sharpness', 'DamageEnchantment': 'minecraft:sharpness',
                          'EnchantmentProtection': 'minecraft:protection', 'ProtectionEnchantment': 'minecraft:protection',
                          'TridentImpalerEnchantment': 'minecraft:impaling', 'ImpalingEnchantment': 'minecraft:impaling'}

    def _enchant_clone(self, prog, o, base):
        c = self.ENCHANT_BASE_CLONE.get(simple(base) if base else '')
        if c:
            return c
        meths = {m.name for m in prog.classes[o.cls].methods} if o.cls in prog.classes else set()
        if meths & {'calcDamageByCreature', 'getDamageBonus'}:
            return 'minecraft:sharpness'
        if meths & {'calcModifierDamage', 'getDamageProtection'}:
            return 'minecraft:protection'
        return None

    def _potion_ref(self, v):
        v = unwrap(v, self.facts.prog)
        if isinstance(v, SField) and simple(v.owner) in ('Potions', 'PotionTypes'):
            return f'minecraft:{v.name.lower()}'
        if isinstance(v, Obj):
            reg = self.regmap.get(D.site_key(v))
            if reg and reg.get('kind') == 'potion':
                return f'{self.lid}:{D.normalize_name(reg["name"])}'
        return None

    def _lang_any(self, keys, default):
        for k in keys:
            if k in self.lang:
                return self.lang[k][:127]
        return default

    def _sounds(self):
        evs, oggs = A.sounds(self.jar, self.ns)
        if self.ffmpeg and evs:
            self._convert_sounds(evs)
            return
        if oggs or evs:
            dec = [p for p in ('/usr/bin/ffmpeg', '/usr/bin/oggdec', '/usr/bin/sox') if Path(p).exists()]
            how = (f'a local decoder exists ({", ".join(dec)}) but the porter does not run external programs; convert by hand to 16-bit mono WAV'
                   if dec else 'no local Ogg Vorbis decoder (ffmpeg/oggdec/sox) is installed')
            self.rep.add('manual', f'{len(evs)} sound event(s), {len(oggs)} .ogg file(s): {how}, then add "sounds" entries '
                                   f'{{"key": "{self.lid}:<name>", "file": "sounds/<name>.wav"}} (assets/{self.ns}/sounds.json)')

    def _convert_sounds(self, evs):
        """Opt-in (--ffmpeg PATH): decode each event's first .ogg to 16-bit mono WAV with a local ffmpeg.
        The .ogg is copied into a fresh work directory under a generated name; ffmpeg never sees jar paths."""
        import subprocess
        import uuid
        d = self.work / f'snd-{uuid.uuid4().hex[:8]}'
        d.mkdir(parents=True, exist_ok=True)
        n = 0
        for ev, files in evs:
            present = [f for f, ok in files if ok]
            if not present:
                self.rep.add('manual', f'sound {ev}: no .ogg in the jar')
                continue
            src = d / f's{n}.ogg'
            src.write_bytes(self.jar.read(present[0]))
            name = D.normalize_name(ev.replace('.', '_'))
            dst = self.out / 'sounds' / f'{name}.wav'
            dst.parent.mkdir(parents=True, exist_ok=True)
            try:
                r = subprocess.run([self.ffmpeg, '-nostdin', '-v', 'error', '-y', '-i', str(src), '-ac', '1', '-ar', '22050', '-sample_fmt', 's16',
                                    '-t', '30', str(dst)], cwd=str(d), stdin=subprocess.DEVNULL, capture_output=True, timeout=60, check=False)
            except (OSError, subprocess.TimeoutExpired) as e:
                self.rep.add('manual', f'sound {ev}: ffmpeg failed ({e})')
                continue
            if r.returncode != 0 or not dst.exists():
                self.rep.add('manual', f'sound {ev}: ffmpeg exit {r.returncode}')
                continue
            n += 1
            self.ext['sounds'].append({'key': f'{self.lid}:snd_{name}', 'file': f'sounds/{name}.wav'})
            self.rep.add('auto', f'sound {self.lid}:snd_{name}: {present[0]} -> 16-bit mono WAV (ffmpeg, first variant of {len(files)})')
        shutil.rmtree(d, ignore_errors=True)

    def _loot(self):
        lt = A.loot_tables(self.jar, self.ns)
        for name, (t, f) in lt['blocks'].items():
            e = self.block_entries.get(D.normalize_name(name))
            if not e:
                continue
            ents = A.loot_simple(t)
            if len(ents) == 1:
                item, lo, hi, chance, rolls = ents[0]
                k = self._vanilla_or_mod(item, f) if isinstance(item, str) else None
                if k and k != e['key']:
                    e['fields']['drop'] = k
                    e['fields']['drops'] = [max(0, lo), min(64, hi)]
                    self.rep.add('auto', f'block {e["key"]}: drops {k} x{lo}-{hi} ({f})')
                elif k == e['key']:
                    e['fields'].pop('drop', None)
            elif len(ents) > 1:
                self.rep.add('guessed', f'block {e["key"]}: loot table {f} has {len(ents)} entries (silk touch / fortune / alternatives); '
                                        'the block drops itself')
        if lt['other']:
            self.rep.add('manual', f'{len(lt["other"])} other loot table(s) (chests/injections/gameplay): no 1.95 equivalent in mod.json '
                                   f'({", ".join(Path(x).name for x in lt["other"][:4])})')

    def _apply_user_hints(self):
        for kind, table in (('items', self.item_entries), ('blocks', self.block_entries)):
            for name, h in (self.hints.get(kind) or {}).items():
                e = table.get(D.normalize_name(name))
                if e is None:
                    continue
                for k, v in h.items():
                    if k in ('name', 'texture'):
                        e[k] = v
                    else:
                        e['fields'][k] = v
                self.rep.add('auto', f'{kind[:-1]} {e["key"]}: hints applied ({", ".join(sorted(h))})')
        for name, h in (self.hints.get('entities') or {}).items():
            for ent in self.ext['entities']:
                if ent['key'].split(':', 1)[1] == D.normalize_name(name):
                    ent.update(h)

    # ---- behaviour --------------------------------------------------------------------------
    def _java(self, cls, mname):
        if self.decompile:
            if not hasattr(self, 'dec'):
                rev = {}
                if self.facts.mapper is not None:
                    rev = dict(self.facts.mapper.used)
                self.dec = B.Decompiler(self.jar, self.work, rev)
                if not self.dec.ok:
                    self.rep.add('manual', f'--decompile: {self.dec.error}; bytecode listings are used instead')
            orig = cls
            if self.facts.mapper is not None and self.facts.mapper.classes:
                inv = {v: k for k, v in self.facts.mapper.classes.items()}
                orig = inv.get(cls, cls)
            src = self.dec.method(cls, mname, orig) if self.dec.ok else None
            if src:
                head = f'Java (CFR 0.152) {cls}.{mname}:'
                mp = self.facts.mapper
                if mp is not None and (mp.methods or mp.fields):
                    table = {**mp.methods, **mp.fields}
                    src = re.sub(r'\b((?:func|field|method|class)_\d+(?:_[A-Za-z_]+)?)\b', lambda x: table.get(x.group(1), x.group(1)), src)
                    head += f' (obfuscated names replaced from: {mp.source})'
                return self._ref_text(cls, mname, head, src.splitlines())
        cf = self.facts.prog.classes.get(cls)
        m = next((m for m in cf.methods if m.name == mname), None) if cf else None
        if m is None:
            return None
        return self._ref_text(cls, mname, f'bytecode {cls}.{mname}{m.desc} (re-run with --decompile for Java):', disasm_lines(cf, m, 2000))

    def _ref_text(self, cls, mname, head, lines, keep=60):
        """Short excerpt for the Lua comment; the full text goes to <out>/java/ (not packaged)."""
        if len(lines) <= keep:
            return head + '\n' + '\n'.join(lines)
        ref = self.out / 'java' / f'{cls.replace("/", ".")}.{mname}.txt'
        ref.parent.mkdir(parents=True, exist_ok=True)
        ref.write_text(head + '\n' + '\n'.join(lines) + '\n')
        return head + '\n' + '\n'.join(lines[:keep]) + f'\n... ({len(lines) - keep} more lines in java/{ref.name})'

    def _behaviour(self):
        prog = self.facts.prog
        targets = []
        for path, e in self.item_entries.items():
            if e['reg']:
                targets.append(('item', e['key'], e['reg'].obj.cls, VN.ITEM_BEHAVIOUR, VN.ITEM_DATA_METHODS))
        for path, e in self.block_entries.items():
            if e['reg']:
                targets.append(('block', e['key'], e['reg'].obj.cls, VN.BLOCK_BEHAVIOUR, VN.BLOCK_DATA_METHODS))
        for cls, key in getattr(self, 'entity_classes', {}).items():
            targets.append(('entity', key, cls, VN.ENTITY_BEHAVIOUR, VN.ENTITY_DATA_METHODS))
        done = set()
        for what, key, cls, table, data in targets:
            for cf, m, kind in B.overrides(prog, cls, table, data):
                tag = (key, cf.name, m.name, m.desc)
                if tag in done:
                    continue
                done.add(tag)
                title = f'{what} {key} <- {cf.name.split("/")[-1]}.{m.name}'
                if kind is None:
                    if re.match(r'^(func|method)_\d+', m.name):
                        self.rep.add('manual', f'{title}: override of an unmapped obfuscated method (behaviour unknown without mappings)')
                    else:
                        self.rep.add('manual', f'{title}: override with no lce_api 1.1 equivalent (listed for review)')
                    continue
                ev, filt, note = B.KIND_EVENT.get(kind, (None, None, 'no equivalent'))
                fr = prog.frames.get((cf.name, m.name, m.desc)) or prog.run_root(cf.name, m.name, m.desc)
                lines, reasons = B.translate(fr)
                if kind == 'item_finish' and what == 'item' and lines and not reasons:
                    effs = B.effects_only(fr)
                    ent = next((e for e in self.item_entries.values() if e['key'] == key), None)
                    if effs is not None and ent is not None and ent['fields'].get('kind') == 'food':
                        food = ent['fields']['food']
                        food.setdefault('effects', [])
                        for x in effs:
                            food['effects'].append({'effect': x['effect'], 'duration': x['duration_ticks'], 'amplifier': x['amplifier']})
                        self.rep.add('auto', f'{title}: constant potion effects on eating -> food.effects {[x["effect"] for x in effs]}')
                        continue
                if ev is None:
                    reasons = [note] + (reasons or [])
                java = self._java(cf.name, m.name)
                self.lua.add(title + f' [{kind}]', ev, filt, key, lines, reasons, java, note)
                if lines and not reasons:
                    self.rep.add('auto', f'{title}: translated to Lua ({len(lines)} call(s)){" - " + note if note else ""}')
                elif ev is None and kind == 'client_fx':
                    self.rep.add('impossible', f'{title}: {note}')
                else:
                    self.rep.add('manual', f'{title}: Lua stub ({"; ".join(reasons)[:300]})')
        for s in self.facts.subscribers:
            ecls = s['event']
            parts = ecls.split('/')[-1].split('$')
            evmap = None
            for p in reversed(parts):
                if p in VN.FORGE_EVENTS:
                    evmap = VN.FORGE_EVENTS[p]
                    break
            title = f'event subscriber {s["where"]}({"$".join(parts)})'
            if evmap is None:
                self.rep.add('manual', f'{title}: event type has no known lce_api mapping')
                ev, note = None, 'unknown event'
            else:
                ev, note = evmap
                if ev is None and note.startswith(('registration', 'mod lifecycle', 'creative tab', 'entity attributes', 'fuel (data)')):
                    continue
            fr = self.facts.prog.frames.get((s['cls'], s['method'], s['desc'])) or self.facts.prog.run_root(s['cls'], s['method'], s['desc'])
            lines, reasons = B.translate(fr)
            reasons = (reasons or []) + ['event handler logic (conditions on the event object) is not translated']
            if ev is None:
                reasons.insert(0, note)
            self.lua.add(title, ev, None, None, lines, reasons, self._java(s['cls'], s['method']), note)
            if ev is None and ('impossible' in note or 'rendering' in note or 'GUI' in note or 'tooltips' in note or 'HUD' in note):
                self.rep.add('impossible', f'{title}: {note}')
            else:
                self.rep.add('manual', f'{title}: Lua stub for {ev or "(no event)"}{" - " + note if note else ""}')
        if hasattr(self, 'dec'):
            self.dec.cleanup()

    # ---- manifest -------------------------------------------------------------------------
    def _manifest(self):
        info = self.info
        version = re.sub(r'[^0-9.]', '', str(info.version or '1.0.0')).strip('.')
        version = re.sub(r'\.+', '.', version)
        if not re.match(r'^[0-9]+(\.[0-9]+){0,3}$', version or ''):
            version = '1.0.0'
        items, blocks = [], []
        for path, e in sorted(self.block_entries.items()):
            if not e.get('texture'):
                self.rep.add('manual', f'block {e["key"]}: no texture -> skipped')
                continue
            b = {'key': e['key'], 'name': e.get('name') or path, 'texture': e['texture']}
            for k, v in e['fields'].items():
                if k in ('hardness', 'resistance', 'material', 'sound', 'light', 'harvest_tier', 'drop', 'drops', 'transparent', 'no_item',
                         'creative_tab', 'storage_slots', 'placement', 'harvest_tool', 'flammability', 'slipperiness', 'replaceable', 'no_collision',
                         'box', 'variants'):
                    b[k] = v
            b.setdefault('creative_tab', 'minecraft:building_blocks')
            if e['reg'] is not None and 'hardness' not in e['fields']:
                self.rep.add('guessed', f'block {e["key"]}: hardness not set in code; default 1.5')
            blocks.append(b)
        for path, e in sorted(self.item_entries.items()):
            if not e.get('texture'):
                self.rep.add('manual', f'item {e["key"]}: no texture -> skipped')
                continue
            it = {'key': e['key'], 'name': e.get('name') or path, 'texture': e['texture']}
            for k, v in e['fields'].items():
                if k in ('kind', 'max_stack', 'max_damage', 'tier', 'food', 'attack_bonus', 'handheld', 'foil', 'creative_tab', 'armor_points',
                         'armor_texture', 'repair_item', 'enchantability', 'mining_speed', 'fuel_ticks'):
                    it[k] = v
            kind = it.get('kind', 'generic')
            it.setdefault('creative_tab', {'food': 'minecraft:food', 'generic': 'minecraft:materials'}.get(kind,
                          'minecraft:armor' if kind in VN.ARMOR_KINDS else 'minecraft:tools'))
            if kind in VN.ARMOR_KINDS and 'armor_texture' not in it:
                at = self._armor_texture(path, kind)
                if at:
                    it['armor_texture'] = at
            items.append(it)
        keys = {x['key'] for x in items + blocks}
        lang = {x['key']: x['name'] for x in items + blocks}
        for sec in ('effects', 'enchantments', 'potions', 'entities'):
            for x in self.ext[sec]:
                if x.get('name'):
                    lang.setdefault(x['key'], x['name'])
        m = {'format': 1, 'id': self.lid, 'name': (info.name or self.lid)[:127], 'version': version, 'author': (info.authors or '')[:127],
             'description': re.sub(r'\s+', ' ', info.description or '')[:500], 'target': lce_modpack.TARGET,
             'ported_from': {'kind': 'java', 'loader': info.loader, 'minecraft': info.mc_version, 'namespace': self.ns, 'source': self.jar_path.name,
                             'tool': 'lce_autoport'},
             'textures': self.textures, 'items': items, 'blocks': blocks, 'recipes': self._dedupe_recipes(), 'smelting': self.smelting,
             'lang': {'en_US': lang}}
        for sec, lst in self.ext.items():
            if lst:
                m[sec] = lst
        if self.config:
            m['config'] = self.config
        if self.lua.blocks:
            sp = self.out / 'scripts' / 'main.lua'
            sp.parent.mkdir(parents=True, exist_ok=True)
            txt = self.lua.text()
            head = CFG.lua_header(self.config, self.config_fields)
            if head:   # after the generated header comment, before the handlers
                cut = txt.index('local MOD = ')
                txt = txt[:cut] + '\n'.join(head) + '\n' + txt[cut:]
            if len(txt.encode()) > 900_000:
                txt = B.shrink(txt, 900_000)
                self.rep.add('manual', 'scripts/main.lua exceeded the 1 MiB script limit; Java excerpts were shortened (full text in java/)')
            sp.write_text(txt)
            m['scripts'] = {'main.lua': 'scripts/main.lua'}
            if not B.lua_sanity(txt):
                self.rep.add('manual', 'scripts/main.lua failed the structural sanity check; review before use')
        return m

    def _configs(self):
        """Forge config specs -> mod.json "config" (API 1.8); values the packer would refuse are dropped and reported."""
        cfg, notes, fields = CFG.extract(self.facts.prog, getattr(self.facts, 'mapper', None))
        self.rep.extend(notes)
        bad = {}
        for key, spec in cfg.items():
            errs = []
            lce_modpack.validate_config({key: spec}, errs.append)
            if errs:
                bad[key] = errs[0]
        for key, why in bad.items():
            cfg.pop(key)
            self.rep.add('manual', f'config value {key} not ported: {why}')
        self.config, self.config_fields = cfg, {k: v for k, v in fields.items() if v in cfg}

    def _armor_texture(self, path, kind):
        layer = 2 if kind == 'leggings' else 1
        stem = path.rsplit('_', 1)[0]
        for k, f in self.texfiles['armor'].items():
            if k.endswith(f'_layer_{layer}') and (k.startswith(stem) or stem.startswith(k.rsplit('_layer', 1)[0])):
                key = f'{self.lid}:arm_{D.normalize_name(k)}'
                if key not in self.textures:
                    dst = f'textures/armor/{D.normalize_name(k)}.png'
                    if not save_png(self.jar.read(f), self.out / dst, (64, 32), self.rep, f):
                        return None
                    self.textures[key] = {'file': dst, 'atlas': 'armor'}
                return key
        return None

    def _dedupe_recipes(self):
        out, seen = [], set()
        for r in self.recipes:
            sig = json.dumps({k: v for k, v in r.items() if k != 'key'}, sort_keys=True)
            if sig in seen:
                continue
            seen.add(sig)
            k = r['key']
            i = 2
            while any(x['key'] == k for x in out):
                k = f'{r["key"]}_{i}'
                i += 1
            r = dict(r, key=k)
            out.append(r)
        return out

    def _shorten_keys(self, m):
        """Keys must be shorter than 64 bytes: long generated keys get a hashed suffix, renamed everywhere."""
        import hashlib
        ren = {}

        def short(k):
            if not isinstance(k, str) or len(k.encode()) < 64 or ':' not in k or not k.startswith(self.lid + ':'):
                return None
            h = hashlib.sha1(k.encode()).hexdigest()[:8]
            return k[:63 - 9] + '_' + h
        for k in list(m.get('textures', {})):
            n = short(k)
            if n:
                ren[k] = n
        for sec, lst in m.items():
            if isinstance(lst, list):
                for x in lst:
                    if isinstance(x, dict):
                        n = short(x.get('key'))
                        if n:
                            ren[x['key']] = n
        if not ren:
            return m

        def walk(o):
            if isinstance(o, str):
                b, at, aux = o.partition('@')
                return ren[b] + at + aux if b in ren else o
            if isinstance(o, list):
                return [walk(x) for x in o]
            if isinstance(o, dict):
                return {walk(k): walk(v) for k, v in o.items()}
            return o
        m = walk(m)
        for old, new in ren.items():
            self.rep.add('auto', f'key {old} shortened to {new} (64-byte key limit)')
        return m

    def _sanitize(self, m):
        """Clamp numeric fields to the ranges lce_modpack accepts (each change reported)."""
        m = self._shorten_keys(m)
        def clamp(obj, k, lo, hi, what, cast=float):
            if k not in obj or obj[k] is None:
                return
            try:
                v = cast(obj[k])
            except (TypeError, ValueError):
                obj.pop(k)
                return
            nv = min(hi, max(lo, v))
            if nv != v:
                self.rep.add('guessed', f'{what} {obj.get("key", obj.get("item"))}: {k} {v} clamped to {nv}')
            obj[k] = nv
        for b in m['blocks']:
            if isinstance(b.get('hardness'), (int, float)) and b['hardness'] < 0:
                b['hardness'] = 100000.0
                self.rep.add('guessed', f'block {b["key"]}: unbreakable (hardness < 0) -> hardness 100000')
            clamp(b, 'hardness', 0, 100000, 'block')
            clamp(b, 'resistance', 0, 100000, 'block')
            clamp(b, 'light', 0, 15, 'block', int)
            clamp(b, 'slipperiness', 0, 1.0, 'block')
            clamp(b, 'flammability', 0, 255, 'block', int)
        for it in m['items']:
            clamp(it, 'max_stack', 1, 64, 'item', int)
            clamp(it, 'max_damage', 0, 65535, 'item', int)
            clamp(it, 'armor_points', 1, 20, 'item', int)
            clamp(it, 'enchantability', 0, 65535, 'item', int)
            clamp(it, 'fuel_ticks', 0, 1 << 30, 'item', int)
            clamp(it, 'attack_bonus', -1000000, 1000000, 'item')
            clamp(it, 'mining_speed', 0, 1000000, 'item')
            f = it.get('food')
            if f:
                clamp(f, 'hunger', 1, 65535, 'food', int)
                clamp(f, 'saturation', 0, 60, 'food')
                for e in f.get('effects', []):
                    clamp(e, 'amplifier', 0, 255, 'food effect', int)
                    clamp(e, 'chance', 0, 100, 'food effect', int)
        for e in m.get('entities', []):
            for k in ('health', 'attack_damage', 'move_speed', 'width', 'height'):
                clamp(e, k, 0, 1000000, 'entity')
            for k in ('min_group', 'max_group'):
                clamp(e, k, 0, 255, 'entity', int)
            clamp(e, 'spawn_weight', 0, 1 << 30, 'entity', int)
        for o in m.get('ores', []):
            for k in ('size', 'per_chunk', 'min_y', 'max_y'):
                clamp(o, k, 0, 65535, 'ore', int)
        for e in m.get('enchantments', []):
            clamp(e, 'max_level', 1, 255, 'enchantment', int)
            clamp(e, 'weight', 0, 255, 'enchantment', int)
        for r in m['recipes'] + m['smelting']:
            clamp(r, 'count', 1, 64, 'recipe', int)
            clamp(r, 'xp', 0, 100, 'smelting')
        return m

    def _limits(self, m):
        """Enforce the per-mod limits of lce_modpack (LIMITS); overflow is dropped and reported (split the mod by hand)."""
        L = lce_modpack.LIMITS
        for sec, lim in (('items', L['items']), ('blocks', L['blocks']), ('recipes', L['recipes']), ('smelting', L['smelting']),
                         ('entities', L['entities']), ('ores', L['ores'])):
            lst = m.get(sec) or []
            if len(lst) > lim:
                drop = lst[lim:]
                m[sec] = lst[:lim]
                self.rep.add('manual', f'{len(drop)} {sec} over the per-mod limit of {lim} dropped ({", ".join(x.get("key", "?") for x in drop[:5])}...); '
                                       'split the mod into several packages with dependencies (PORTING.md section 5)')
        used = set()

        def refs(o):
            if isinstance(o, str):
                used.add(o.split('@')[0])
            elif isinstance(o, list):
                for x in o:
                    refs(x)
            elif isinstance(o, dict):
                for v in o.values():
                    refs(v)
        refs({k: v for k, v in m.items() if k not in ('textures',)})
        unused = [k for k in m['textures'] if k not in used]
        for k in unused:
            m['textures'].pop(k)
        if len(m['textures']) > L['textures']:
            keep = list(m['textures'])[:L['textures']]
            dropped = [k for k in m['textures'] if k not in keep]
            m['textures'] = {k: m['textures'][k] for k in keep}
            self.rep.add('manual', f'{len(dropped)} textures over the limit of {L["textures"]} dropped; entries using them are removed below')
        return m

    def _validate_fix(self, m):
        """Run the SDK validator; drop each entry it rejects (reported) until the package is valid."""
        for _ in range(8):
            try:
                lce_modpack.validate(m, self.out, resize=False)
                return m
            except lce_modpack.PackError as e:
                errs = str(e).split('\n')
            dropped = False
            for err in errs:
                mm = re.match(r'^([a-z]+) ([a-z0-9_]{2,31}:[a-z0-9_./#@-]+):', err)
                key = mm.group(2) if mm else None
                if key is None:
                    mm = re.match(r'^([a-z0-9_]+:[^ :]+): unknown creative tab', err)
                    if mm:
                        for x in m['items'] + m['blocks']:
                            if x['key'] == mm.group(1):
                                x['creative_tab'] = 'minecraft:misc'
                                dropped = True
                        self.rep.add('guessed', f'{mm.group(1)}: creative tab replaced by minecraft:misc ({err})')
                        continue
                    self.rep.add('manual', f'validator: {err}')
                    continue
                for sec in ('items', 'blocks', 'recipes', 'smelting', 'effects', 'potions', 'brewing', 'enchantments', 'fuels', 'entities', 'ores',
                            'features', 'sounds'):
                    lst = m.get(sec) or []
                    keep = [x for x in lst if x.get('key', x.get('item')) != key]
                    if len(keep) != len(lst):
                        m[sec] = keep
                        dropped = True
                        self.rep.add('manual', f'dropped {sec[:-1]} {key}: {err.split(":", 1)[1].strip() if ":" in err else err}')
                if mm and mm.group(1) == 'texture':
                    m['textures'].pop(key, None)
                    dropped = True
                    self.rep.add('manual', f'dropped texture {key}: {err}')
            for loc in m.get('lang', {}).values():
                for k in list(loc):
                    if not any(x.get('key') == k for sec in ('items', 'blocks', 'effects', 'enchantments', 'potions', 'entities') for x in m.get(sec) or []):
                        loc.pop(k)
            if not dropped:
                break
        try:
            lce_modpack.validate(m, self.out, resize=False)
        except lce_modpack.PackError as e:
            self.rep.add('manual', f'package still invalid after automatic fixes: {e}')
        return m

    def _counts(self, m):
        f = self.facts
        regs = [r for r in f.registrations]
        found_items = len([e for e in self.item_entries.values()]) + len([r for r in regs if r.kind == 'item' and
                                                                         not self.regmap.get(D.site_key(r.obj), {}).get('key')])
        recipe_files = [n for n in self.jar.names() if re.match(rf'^(data|assets)/{re.escape(self.ns)}/recipes?/.+\.json$', n) and '/_' not in n]
        crafting = 0
        for n in recipe_files[:5000]:
            r = A.read_json(self.jar, n)
            t = str((r or {}).get('type', '')).split(':')[-1] if isinstance(r, dict) else ''
            if t in ('crafting_shaped', 'crafting_shapeless', 'shaped', 'shapeless', 'ore_shaped', 'ore_shapeless', 'smelting', 'blasting', 'smoking'):
                crafting += 1
        return {
            'loader': self.info.loader, 'mc': self.info.mc_version, 'era': self.era, 'namespace': self.ns, 'id': self.lid, 'classes': f.classes,
            'items_found': found_items, 'items_ported': len(m.get('items', [])),
            'blocks_found': len(self.block_entries) + len([r for r in regs if r.kind == 'block' and not self.regmap.get(D.site_key(r.obj), {}).get('key')]),
            'blocks_ported': len(m.get('blocks', [])),
            'recipes_found': len(f.recipes) + len(f.smelting) + len(recipe_files), 'recipes_ported': len(m.get('recipes', [])) + len(m.get('smelting', [])),
            'recipes_crafting_found': len(f.recipes) + len(f.smelting) + crafting,
            'entities_found': len(f.entity_regs) + len(self.regs_by_kind.get('entity_type', [])), 'entities_ported': len(m.get('entities', [])),
            'effects': len(m.get('effects', [])), 'enchantments': len(m.get('enchantments', [])), 'ores': len(m.get('ores', [])),
            'lua_auto': self.lua.auto, 'lua_manual': self.lua.manual,
            'report': {k: len(v) for k, v in self.rep.lines.items()},
        }


def rr_name(rr, full, aux, where):
    return rr.name(full, aux, where)


REF_KEYS = ('key', 'ingredients', 'ingredient', 'result', 'base', 'addition', 'template', 'input', 'output')


def _json_refs(o, inside=False):
    """Item ids / tags referenced by a recipe JSON (never the recipe 'type')."""
    out = []
    if isinstance(o, dict):
        for k, v in o.items():
            if k in ('type', 'conditions', 'neoforge:conditions', 'fabric:load_conditions', 'group', 'category'):
                continue
            if k in ('item', 'id') and isinstance(v, str):
                out.append(v)
            elif k == 'tag' and isinstance(v, str):
                out.append('#' + v)
            elif k in REF_KEYS or inside:
                out += _json_refs(v, True)
    elif isinstance(o, list):
        for v in o:
            out += _json_refs(v, inside)
    elif inside and isinstance(o, str) and (o.startswith('#') or ':' in o):
        out.append(o)
    return out


def disasm_lines(cf, m, limit=250):
    from .classfile import disassemble
    return [l.strip() for l in disassemble(cf, m, limit)]


# ---- report / checks ------------------------------------------------------------------------
def run_check(src, profile):
    buf, ebuf = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(ebuf):
        try:
            rc = lce_modpack.main(['check', str(src), '--profile', profile])
        except SystemExit as e:  # argparse
            rc = e.code
    return rc, (buf.getvalue() + ebuf.getvalue()).strip()


def write_report(porter, checks, build_result):
    rep, info = porter.rep, porter.info
    s = porter.stats
    out = [f'# Port report: {info.name or porter.lid} ({porter.jar_path.name}) -> `{porter.lid}`', '',
           f'Generated by `tools/lce_autoport.py` (static analysis only; no mod code was executed).', '',
           '| | found | ported |', '|---|---|---|',
           f'| items | {s["items_found"]} | {s["items_ported"]} |', f'| blocks | {s["blocks_found"]} | {s["blocks_ported"]} |',
           f'| recipes + smelting | {s["recipes_found"]} | {s["recipes_ported"]} |', f'| entities | {s["entities_found"]} | {s["entities_ported"]} |',
           f'| Lua behaviour stubs | {s["lua_auto"] + s["lua_manual"]} | {s["lua_auto"]} auto |', '',
           f'Loader **{info.loader}**, Minecraft **{info.mc_version or "?"}**, era **{porter.era}**, {s["classes"]} classes analysed in {s["seconds"]} s.', '']
    for k in CATS:
        out.append(f'## {CAT_TITLES[k]} ({len(rep.lines[k])})')
        out += [f'- {m}' for m in rep.lines[k]] or ['- none']
        out.append('')
    out += port_compat.report_section(port_compat.java_features(porter.jar.names()))
    out.append('## lce_modpack.py check')
    for prof, (rc, text) in checks.items():
        out.append(f'### `--profile {prof}` (exit {rc})')
        out.append('```')
        out.append(text[:6000])
        out.append('```')
    if build_result:
        out.append('### build')
        out.append('```')
        out.append(build_result)
        out.append('```')
    (porter.out / 'PORT_REPORT.md').write_text('\n'.join(out) + '\n')
