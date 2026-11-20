#!/usr/bin/env python3
"""Automatic porter: NeoForge / Forge (1.7.10-1.21) / Fabric mod jars -> LCE SDK source packages.

  lce_autoport.py port   <mod.jar> <out-src-dir> [--hints h.json] [--decompile] [--build-dir D] [--mappings F ...]
  lce_autoport.py survey <dir-or-list> [--out build/autoport/survey] [--limit N] [--jobs N] [--keep]
  lce_autoport.py content-level            print the 1.95 content-level table (Java 1.13/1.14/1.15/1.16 names)

`port` reads the jar read-only (static analysis: a pure-Python class-file parser
and abstract interpreter; mod code is never loaded or executed), converts assets
and data through tools/lce_port.py, extracts registry facts from bytecode,
writes <out>/mod.json, textures/, scripts/main.lua and PORT_REPORT.md, then runs
`lce_modpack.py check --profile full` and `--profile retail195` and builds the
package into --build-dir (default build/autoport/pkg).  See docs/AUTOPORT.md.
"""
import argparse
import concurrent.futures as cf
import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from autoport import port as P  # noqa: E402
from autoport import vanilla as VN  # noqa: E402
from autoport import jar as J  # noqa: E402

SDK = HERE.parent
DEFAULT_BUILD = SDK / 'build' / 'autoport' / 'pkg'


def cmd_port(a):
    hints = json.loads(Path(a.hints).read_text()) if a.hints else None
    fallback = json.loads(Path(a.fallbacks).read_text()) if a.fallbacks else None
    work = Path(a.work) if a.work else Path(tempfile.mkdtemp(prefix='lce_autoport_'))
    try:
        porter = P.Porter(a.jar, a.out, hints=hints, decompile=a.decompile, work=work, mappings=a.mappings, fallback=fallback, ffmpeg=a.ffmpeg)
        porter.run()
    except J.JarError as e:
        print(f'error: {e}', file=sys.stderr)
        return 1
    checks = {}
    for prof in ('full', 'retail195'):
        checks[prof] = P.run_check(a.out, prof)
    build = None
    if not a.no_build:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
            rc = P.lce_modpack.main(['build', str(a.out), str(a.build_dir)])
        build = f'exit {rc}: ' + buf.getvalue().strip()
    P.write_report(porter, checks, build)
    s = porter.stats
    s['check_full'] = checks['full'][0]
    s['check_retail195'] = checks['retail195'][0]
    s['retail_problems'] = sum(1 for l in checks['retail195'][1].splitlines() if l.startswith('  - '))
    (Path(a.out) / 'autoport.json').write_text(json.dumps(s, indent=2) + '\n')
    if not a.work:
        import shutil
        shutil.rmtree(work, ignore_errors=True)
    if a.json:
        print(json.dumps(s))
    else:
        print(f'{porter.info.loader} {porter.info.mc_version or "?"} {porter.ns} -> {a.out}/mod.json: '
              f'items {s["items_ported"]}/{s["items_found"]}, blocks {s["blocks_ported"]}/{s["blocks_found"]}, '
              f'recipes {s["recipes_ported"]}/{s["recipes_found"]}, entities {s["entities_ported"]}/{s["entities_found"]}, '
              f'lua {s["lua_auto"]} auto + {s["lua_manual"]} manual; check full={checks["full"][0]} retail195={checks["retail195"][0]} '
              f'(see PORT_REPORT.md)')
    return 0 if checks['full'][0] == 0 else 1


def _jar_list(src):
    p = Path(src)
    if p.is_dir():
        return sorted(str(x) for x in p.rglob('*') if x.suffix.lower() in ('.jar', '.zip') and x.is_file())
    return [l.strip() for l in p.read_text().splitlines() if l.strip() and not l.startswith('#')]


def cmd_survey(a):
    jars = _jar_list(a.src)
    if a.limit:
        jars = jars[:a.limit]
    # identical files (backups, copies) are ported once
    import hashlib
    first, dups = {}, {}
    uniq = []
    for j in jars:
        try:
            h = hashlib.sha256()
            with open(j, 'rb') as fh:
                for chunk in iter(lambda: fh.read(1 << 20), b''):
                    h.update(chunk)
            d = h.hexdigest()
        except OSError:
            d = 'unreadable:' + j
        if d in first:
            dups.setdefault(first[d], []).append(j)
            continue
        first[d] = j
        uniq.append(j)
    if len(uniq) != len(jars):
        print(f'{len(jars)} files, {len(uniq)} distinct (SHA-256); duplicates are reported once', file=sys.stderr)
    jars = uniq
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    scratch = Path(a.work) if a.work else Path(tempfile.mkdtemp(prefix='lce_autoport_survey_'))
    scratch.mkdir(parents=True, exist_ok=True)
    rows = []

    def one(i_j):
        i, j = i_j
        dst = scratch / f'{i:05d}'
        cmd = [sys.executable, '-I', str(HERE / 'lce_autoport.py'), 'port', j, str(dst / 'src'), '--build-dir', str(dst / 'pkg'), '--json',
               '--work', str(dst / 'work')]
        t = time.time()
        try:
            r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=a.timeout, text=True, stdin=subprocess.DEVNULL)
            line = (r.stdout.strip().splitlines() or [''])[-1]
            try:
                s = json.loads(line)
            except ValueError:
                s = {'error': (r.stderr.strip().splitlines() or ['no output'])[-1][:200]}
        except subprocess.TimeoutExpired:
            s = {'error': f'timeout after {a.timeout}s'}
        s['jar'] = j
        s['duplicates'] = len(dups.get(j, []))
        s['wall'] = round(time.time() - t, 1)
        if not a.keep:
            import shutil
            shutil.rmtree(dst, ignore_errors=True)
        return s

    with cf.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for n, s in enumerate(ex.map(one, enumerate(jars)), 1):
            rows.append(s)
            if a.progress and n % 25 == 0:
                print(f'{n}/{len(jars)}', file=sys.stderr, flush=True)
    (out / 'survey.json').write_text(json.dumps(rows, indent=1) + '\n')
    md = survey_markdown(rows, a.src, sum(len(v) for v in dups.values()))
    (out / 'survey.md').write_text(md)
    ok = [r for r in rows if 'error' not in r]
    print(f'surveyed {len(rows)} jars ({len(ok)} ported, {len(rows) - len(ok)} failed); '
          f'check --profile full passed for {sum(1 for r in ok if r.get("check_full") == 0)}; table: {out / "survey.md"}')
    return 0


def survey_markdown(rows, src, ndup=0):
    ok = [r for r in rows if 'error' not in r]
    tot = {k: sum(r.get(k, 0) or 0 for r in ok) for k in ('items_found', 'items_ported', 'blocks_found', 'blocks_ported', 'recipes_found',
                                                            'recipes_ported', 'recipes_crafting_found', 'entities_found', 'entities_ported', 'lua_auto', 'lua_manual')}
    by_loader = {}
    for r in ok:
        by_loader.setdefault(r.get('loader'), []).append(r)
    lines = [f'# lce_autoport survey ({len(rows)} distinct jars from {src}' + (f'; {ndup} byte-identical duplicates skipped' if ndup else '') + ')', '',
             f'- ported: {len(ok)}; failed: {len(rows) - len(ok)}; `check --profile full` exit 0: {sum(1 for r in ok if r.get("check_full") == 0)}; '
             f'`--profile retail195` exit 0: {sum(1 for r in ok if r.get("check_retail195") == 0)}',
             f'- items {tot["items_ported"]}/{tot["items_found"]}, blocks {tot["blocks_ported"]}/{tot["blocks_found"]}, '
             f'recipes {tot["recipes_ported"]}/{tot["recipes_found"]} ({tot["recipes_crafting_found"]} of them crafting/smelting), entities {tot["entities_ported"]}/{tot["entities_found"]}, '
             f'Lua stubs {tot["lua_auto"]} auto / {tot["lua_manual"]} manual', '',
             '| loader | jars | items p/f | blocks p/f | recipes p/f | entities p/f |', '|---|---|---|---|---|---|']
    for ld, rs in sorted(by_loader.items(), key=lambda x: str(x[0])):
        f = lambda k: sum(r.get(k, 0) or 0 for r in rs)  # noqa: E731
        lines.append(f'| {ld} | {len(rs)} | {f("items_ported")}/{f("items_found")} | {f("blocks_ported")}/{f("blocks_found")} | '
                     f'{f("recipes_ported")}/{f("recipes_found")} | {f("entities_ported")}/{f("entities_found")} |')
    lines += ['', 'p/f = ported/found. recipes: ported/found(all types)/crafting+smelting found. full/retail = exit code of '
              '`lce_modpack.py check --profile full|retail195` (0 ok, 1 invalid, 2 valid but not loadable on today\'s retail backend).', '',
              '| jar | loader | MC | id | items p/f | blocks p/f | recipes p/f/c | entities p/f | lua a/m | full | retail | s |',
              '|---|---|---|---|---|---|---|---|---|---|---|---|']
    for r in rows:
        name = Path(r['jar']).name
        if 'error' in r:
            lines.append(f'| {name} | error: {r["error"][:80]} | | | | | | | | | | {r.get("wall")} |')
            continue
        lines.append(f'| {name} | {r.get("loader")} | {r.get("mc") or "?"} | {r.get("id")} | {r["items_ported"]}/{r["items_found"]} | '
                     f'{r["blocks_ported"]}/{r["blocks_found"]} | {r["recipes_ported"]}/{r["recipes_found"]}/{r.get("recipes_crafting_found", "?")} | {r["entities_ported"]}/{r["entities_found"]} | '
                     f'{r["lua_auto"]}/{r["lua_manual"]} | {r.get("check_full")} | {r.get("check_retail195")} | {r.get("wall")} |')
    return '\n'.join(lines) + '\n'


CONTENT_LEVEL = [
    # (java version, java name, expected 1.95 name or None)
    ('1.13', 'trident', 'trident'), ('1.13', 'conduit', 'conduit'), ('1.13', 'kelp', 'kelp'), ('1.13', 'dried_kelp_block', 'dried_kelp_block'),
    ('1.13', 'turtle_egg', 'turtle_egg'), ('1.13', 'turtle_helmet', 'turtle_helmet'), ('1.13', 'scute', 'turtle_shell_piece'),
    ('1.13', 'phantom_membrane', 'phantom_membrane'), ('1.13', 'heart_of_the_sea', 'nautilus_core'), ('1.13', 'nautilus_shell', 'nautilus'),
    ('1.13', 'sea_pickle', 'sea_pickle'), ('1.13', 'seagrass', 'sea_grass'), ('1.13', 'blue_ice', 'blue_ice'),
    ('1.13', 'tube_coral_block', 'coral_block'), ('1.13', 'tube_coral', 'coral'), ('1.13', 'tube_coral_fan', 'coral_fan'),
    ('1.13', 'cod_bucket', 'fish_bucket'), ('1.13', 'pufferfish_bucket', 'puffer_bucket'), ('1.13', 'stripped_oak_log', 'stripped_log_oak'),
    ('1.13', 'spruce_trapdoor', 'spruce_trapdoor'), ('1.13', 'prismarine_stairs', 'prismarine_stairs'), ('1.13', 'oak_wood', 'wood'),
    ('1.14', 'crossbow', 'crossbow'), ('1.14', 'lantern', 'lantern'), ('1.14', 'bell', 'bell'), ('1.14', 'campfire', 'campfire'),
    ('1.14', 'barrel', 'barrel'), ('1.14', 'scaffolding', 'scaffolding'), ('1.14', 'bamboo', 'bamboo'), ('1.14', 'sweet_berries', 'sweet_berries'),
    ('1.14', 'lectern', 'lectern'), ('1.14', 'loom', 'loom'), ('1.14', 'composter', 'composter'), ('1.14', 'smoker', 'smoker'),
    ('1.14', 'blast_furnace', 'blastfurnace'), ('1.14', 'grindstone', 'grindstone'), ('1.14', 'stonecutter', 'stonecutter2'),
    ('1.14', 'cartography_table', 'cartography_table'), ('1.14', 'fletching_table', 'fletching_table'), ('1.14', 'smithing_table', 'smithing_table'),
    ('1.14', 'creeper_banner_pattern', 'banner_pattern'), ('1.14', 'stone_stairs', 'stone_stairs'), ('1.14', 'andesite_stairs', 'andesite_stairs'),
    ('1.14', 'leather_horse_armor', 'leather_horse_armor'), ('1.14', 'spruce_sign', 'sign_spruce'), ('1.14', 'smooth_stone', 'smoothstone'),
    ('1.14', 'cornflower', None), ('1.14', 'lily_of_the_valley', None), ('1.14', 'wither_rose', None), ('1.14', 'suspicious_stew', None),
    ('1.14', 'black_dye', 'dye@0'), ('1.14', 'jigsaw', None), ('1.14', 'brick_wall', None), ('1.14', 'andesite_slab', None),
    ('1.15', 'bee_nest', None), ('1.15', 'beehive', None), ('1.15', 'honey_bottle', None), ('1.15', 'honeycomb', None), ('1.15', 'honey_block', None),
    ('1.16', 'netherite_ingot', None), ('1.16', 'ancient_debris', None), ('1.16', 'crimson_planks', None), ('1.16', 'warped_fungus', None),
    ('1.16', 'soul_lantern', None), ('1.16', 'blackstone', None), ('1.16', 'basalt', None), ('1.16', 'respawn_anchor', None),
    ('1.16', 'lodestone', None), ('1.16', 'target', None), ('1.16', 'chain', None), ('1.17', 'copper_ingot', None), ('1.17', 'amethyst_shard', None),
]


def content_table():
    """[(java version, name, 1.95 name or None, resolver result, ok)] for the documented content-level claims."""
    vanilla = VN.VANILLA
    res = VN.VanillaResolver('modern')
    rows = []
    for ver, name, expect in CONTENT_LEVEL:
        key, status, note = res.resolve('minecraft:' + name)
        base = expect.split('@')[0] if expect else None
        present = base is not None and base in vanilla
        ok = ((expect is None) == (key is None)) and (expect is None or (present and key == 'minecraft:' + expect or key.split('@')[0] == 'minecraft:' + base))
        rows.append((ver, name, f'{expect} (id {vanilla[base]})' if present else 'absent', key or status, ok))
    return rows


def cmd_content(a):
    rows = content_table()
    print('| Java | name | 1.95 | resolver |')
    print('|---|---|---|---|')
    for ver, name, have, got, ok in rows:
        print(f'| {ver} | {name} | {have} | {got}{"" if ok else "  <-- MISMATCH"} |')
    return 0 if all(r[4] for r in rows) else 1


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    po = sub.add_parser('port')
    po.add_argument('jar')
    po.add_argument('out')
    po.add_argument('--hints')
    po.add_argument('--decompile', action='store_true', help='include CFR-decompiled Java in Lua stubs (needs the local CFR jar)')
    po.add_argument('--build-dir', default=str(DEFAULT_BUILD))
    po.add_argument('--no-build', action='store_true')
    po.add_argument('--mappings', nargs='+', help='MCP fields.csv/methods.csv or Tiny v2 files')
    po.add_argument('--fallbacks', help='JSON {"minecraft:netherite_ingot": "minecraft:diamond"} for content newer than 1.95 (reported)')
    po.add_argument('--work', help='scratch directory (default: a fresh temp dir, removed afterwards)')
    po.add_argument('--ffmpeg', help='opt-in: path of a local ffmpeg used to convert .ogg sounds to 16-bit mono WAV (off by default)')
    po.add_argument('--json', action='store_true')
    su = sub.add_parser('survey')
    su.add_argument('src')
    su.add_argument('--out', default=str(SDK / 'build' / 'autoport' / 'survey'))
    su.add_argument('--limit', type=int)
    su.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    su.add_argument('--timeout', type=int, default=600)
    su.add_argument('--work', help='where per-jar ports go (default: a temp dir)')
    su.add_argument('--keep', action='store_true')
    su.add_argument('--progress', action='store_true')
    sub.add_parser('content-level')
    a = p.parse_args(argv)
    if a.cmd == 'port':
        return cmd_port(a)
    if a.cmd == 'survey':
        return cmd_survey(a)
    return cmd_content(a)


if __name__ == '__main__':
    sys.exit(main())
