"""Tests for tools/lce_modpack.py and tools/lce_port.py (run: python3 -I tests/test_tools.py)."""
import contextlib
import io
import json
import shutil
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import lce_modpack  # noqa: E402
import lce_port  # noqa: E402


class PackTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def port_ruby(self):
        out = self.tmp / 'src'
        hints = json.loads((ROOT / 'examples/java-rubymod/hints.json').read_text())
        m, rep = lce_port.convert_java(ROOT / 'examples/java-rubymod/src', out, hints=hints)
        return out, m, rep

    def test_java_port_contents(self):
        out, m, rep = self.port_ruby()
        self.assertEqual(rep.lines['errors'], [])
        self.assertEqual(m['id'], 'rubymod')
        self.assertEqual(m['version'], '1.2.0')
        self.assertEqual({i['key'] for i in m['items']}, {'rubymod:ruby', 'rubymod:ruby_pickaxe', 'rubymod:ruby_apple'})
        pick = next(i for i in m['items'] if i['key'] == 'rubymod:ruby_pickaxe')
        self.assertEqual((pick['kind'], pick['max_damage'], pick['max_stack']), ('pickaxe', 900, 1))
        ore = next(b for b in m['blocks'] if b['key'] == 'rubymod:ruby_ore')
        self.assertEqual((ore['drop'], ore['drops']), ('rubymod:ruby', [1, 2]))
        wool = next(r for r in m['recipes'] if r['key'] == 'rubymod:red_wool')
        self.assertEqual(wool['result'], 'minecraft:wool@14')
        self.assertIn('minecraft:wool@0', wool['ingredients'])
        pickr = next(r for r in m['recipes'] if r['key'] == 'rubymod:ruby_pickaxe')
        self.assertEqual(pickr['keys']['S'], 'minecraft:stick')
        self.assertEqual(len(m['smelting']), 1)  # blasting duplicate skipped
        ore = m['ores'][0]
        self.assertEqual((ore['key'], ore['block'], ore['size'], ore['per_chunk'], ore['min_y'], ore['max_y']),
                         ('rubymod:ruby_ore_gen', 'rubymod:ruby_ore', 7, 5, 0, 48))
        self.assertTrue(any('worldgen ore' in s for s in rep.lines['converted']))
        self.assertTrue((out / 'PORT_REPORT.md').exists())

    def test_shapeless_as_shaped(self):
        out = self.tmp / 'src2'
        m, rep = lce_port.convert_java(ROOT / 'examples/java-rubymod/src', out,
                                       hints=json.loads((ROOT / 'examples/java-rubymod/hints.json').read_text()), shapeless_as_shaped=True)
        self.assertTrue(all(r['type'] == 'shaped' for r in m['recipes']))
        self.assertTrue(any('shapeless emitted' in g for g in rep.lines['guessed']))

    def test_java_feature_report_and_profile_check(self):
        archive = self.tmp / 'rubymod.jar'
        example = ROOT / 'examples/java-rubymod'
        with zipfile.ZipFile(archive, 'w') as jar:
            for file in (example / 'src').rglob('*'):
                if file.is_file():
                    jar.write(file, file.relative_to(example / 'src').as_posix())
            jar.writestr('rubymod/Behaviour.class', b'illustrative unreadable bytecode')
            jar.writestr('assets/minecraft/shaders/core/new_pass.fsh', b'shader')
            jar.writestr('assets/rubymod/items/compass.json', b'{}')
            jar.writestr('data/rubymod/dimension/planet.json', b'{}')
            jar.writestr('data/rubymod/function/reset.mcfunction', b'function example:unported')
            jar.writestr('docs/shaders/example.txt', b'unrelated')

        out = self.tmp / 'src'
        hints = json.loads((example / 'hints.json').read_text())
        manifest, report = lce_port.convert_java(archive, out, hints=hints)
        body = (out / 'PORT_REPORT.md').read_text()
        for evidence in ('**Java code**', '**Shaders**', '**Item definition assets**', '**Dimensions and dimension types**',
                         '**Data pack functions**', 'planet.json', 'new_pass.fsh', 'reset.mcfunction'):
            self.assertIn(evidence, body)
        self.assertNotIn('docs/shaders/example.txt', body)
        self.assertEqual(report.target_exit, 2)
        self.assertEqual(report.target_problems, lce_modpack.profile_report(lce_modpack.validate(manifest, out), 'retail195'))
        self.assertIn('harvest tiers above wood', body)

    def test_port_cli_profile_exit_codes(self):
        example = ROOT / 'examples/java-rubymod'
        out = self.tmp / 'src'
        args = ['java', str(example / 'src'), str(out), '--hints', str(example / 'hints.json')]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(lce_port.main(args), 0)
            self.assertEqual(lce_port.main([*args, '--require-profile']), 2)
            self.assertEqual(lce_port.main([*args, '--profile', 'full', '--require-profile']), 0)
        self.assertIn('Package passed the SDK profile check.', (out / 'PORT_REPORT.md').read_text())

        invalid = self.tmp / 'invalid.json'
        invalid.write_text(json.dumps({'lce_id': 'x'}))
        args[-1] = str(invalid)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(lce_port.main([*args, '--require-profile']), 1)
        body = (out / 'PORT_REPORT.md').read_text()
        self.assertIn('Package schema validation failed', body)
        self.assertIn("invalid mod id 'x'", body)

    def test_java_mixed_case_texture_paths_get_distinct_valid_keys(self):
        example = ROOT / 'examples/java-rubymod'
        source = self.tmp / 'source'
        shutil.copytree(example / 'src', source)
        assets = source / 'assets/rubymod'
        for folder, item in (('first', 'ruby'), ('second', 'ruby_apple')):
            icon_path = f'item/{folder}/SameIcon'
            texture = assets / f'textures/{icon_path}.png'
            texture.parent.mkdir()
            shutil.copyfile(assets / f'textures/item/{item}.png', texture)
            model = assets / f'models/item/{item}.json'
            definition = json.loads(model.read_text())
            definition['textures']['layer0'] = f'rubymod:{icon_path}'
            model.write_text(json.dumps(definition))
        hints = json.loads((example / 'hints.json').read_text())
        out = self.tmp / 'src'
        m, report = lce_port.convert_java(source, out, hints=hints, target_profile='full')
        self.assertEqual(report.target_exit, 0, report.target_problems)
        items = {item['key']: item['texture'] for item in m['items']}
        ruby, apple = (m['textures'][items[key]]['file'] for key in ('rubymod:ruby', 'rubymod:ruby_apple'))
        self.assertNotEqual(ruby, apple)
        self.assertNotEqual((out / ruby).read_bytes(), (out / apple).read_bytes())
        self.assertTrue(any('SameIcon.png uses texture key' in line for line in report.lines['converted']))

    def test_java_corrupt_texture_does_not_stop_unrelated_assets(self):
        example = ROOT / 'examples/java-rubymod'
        source = self.tmp / 'source'
        shutil.copytree(example / 'src', source)
        assets = source / 'assets/rubymod'
        bad_texture = assets / 'textures/item/bad/ruby.png'
        bad_texture.parent.mkdir()
        bad_texture.write_bytes(b'invalid image')
        model = assets / 'models/item/ruby_apple.json'
        definition = json.loads(model.read_text())
        definition['textures']['layer0'] = 'rubymod:item/bad/ruby'
        model.write_text(json.dumps(definition))
        hints = json.loads((example / 'hints.json').read_text())

        out = self.tmp / 'src'
        m, report = lce_port.convert_java(source, out, hints=hints, target_profile='full')
        self.assertEqual({item['key'] for item in m['items']}, {'rubymod:ruby', 'rubymod:ruby_pickaxe'})
        self.assertTrue(any('item/bad/ruby.png: could not decode texture image' in error
                            for error in report.lines['errors']))
        self.assertTrue((out / 'textures/items/ruby.png').is_file())

    def test_build_roundtrip_deterministic(self):
        out, m, _ = self.port_ruby()
        _, a = lce_modpack.build(out, self.tmp / 'o1')
        _, b = lce_modpack.build(out, self.tmp / 'o2')
        self.assertEqual(a, b)
        d = lce_modpack.decode(a)
        self.assertEqual(d['meta']['target'], 'CUSA00265-1.95')
        self.assertEqual(len(d['items']), 3)
        self.assertEqual(len(d['recipes']), 5)
        r = next(r for r in d['recipes'] if r['key'] == 'rubymod:ruby_block')
        self.assertEqual(r['size'], [3, 3])
        self.assertEqual(r['cells'], ['rubymod:ruby'] * 9)

    def test_config_section(self):
        errs = []
        ok = lce_modpack.validate_config({'a.b': {'default': 3, 'min': 1, 'max': 5, 'comment': 'x'}, 'flag': True, 's': 'v'}, errs.append)
        self.assertEqual(errs, [])
        self.assertEqual([k for k, _ in ok], ['a.b', 'flag', 's'])
        text = lce_modpack.render_config(ok, 'm')
        self.assertIn('flag = true\ns = "v"', text)
        self.assertIn('[a]\n# x\n# Range: 1 ~ 5\nb = 3', text)
        errs = []
        lce_modpack.validate_config({'bad key': 1, 'r': {'default': 9, 'max': 5}, 'm': {'default': 'x', 'min': 0},
                                     'n': {'default': None}, 'f': {'default': float('nan')}, 'u': {'default': 1, 'what': 2},
                                     'l': {'default': [{'x': 1}]}, 'o': {'default': 1, 'min': 3, 'max': 2},
                                     'z': {'default': 'a\x01b'}}, errs.append)
        msg = '\n'.join(errs)
        for needle in ("invalid key 'bad key'", 'default 9 outside', 'min/max only apply', 'config n: default must be',
                       'must be finite', "unknown field(s) ['what']", 'list defaults', 'min > max', 'without control'):
            self.assertIn(needle, msg)
        errs = []
        lce_modpack.validate_config([1], errs.append)
        self.assertIn('config must be an object', errs[0])
        # build writes the file next to mod.lcm, and removes a stale one when the section is gone
        out, m, _ = self.port_ruby()
        m['config'] = {'general.rate': 4}
        (out / 'mod.json').write_text(json.dumps(m))
        dst, _ = lce_modpack.build(out, self.tmp / 'pkg')
        self.assertIn('[general]\nrate = 4\n', (dst / 'config').read_text())
        del m['config']
        (out / 'mod.json').write_text(json.dumps(m))
        dst, _ = lce_modpack.build(out, self.tmp / 'pkg')
        self.assertFalse((dst / 'config').exists())

    def test_validation_errors(self):
        out, m, _ = self.port_ruby()
        bad = json.loads((out / 'mod.json').read_text())
        bad['items'].append({'key': 'other:thing', 'name': 'X', 'texture': 'rubymod:i_ruby'})
        bad['items'].append({'key': 'rubymod:sword2', 'name': 'S', 'texture': 'rubymod:i_ruby', 'kind': 'sword'})  # no durability
        bad['recipes'].append({'key': 'rubymod:bad', 'type': 'shaped', 'pattern': ['X'], 'keys': {'X': 'minecraft:not_an_item'}, 'result': 'rubymod:ruby'})
        bad['entities'] = [{'key': 'rubymod:golem'}]
        with self.assertRaises(lce_modpack.PackError) as cm:
            lce_modpack.validate(bad, out)
        msg = str(cm.exception)
        for needle in ('must start with "rubymod:"', 'tools need max_stack 1', 'unknown ingredient', 'base_mob is required'):
            self.assertIn(needle, msg)

    def test_texture_size_enforced(self):
        from PIL import Image
        src = self.tmp / 'm'
        (src / 't').mkdir(parents=True)
        Image.new('RGBA', (32, 32)).save(src / 't' / 'x.png')
        m = {'id': 'sizemod', 'name': 'S', 'version': '1', 'textures': {'sizemod:x': 't/x.png'},
             'items': [{'key': 'sizemod:x', 'name': 'X', 'texture': 'sizemod:x'}]}
        (src / 'mod.json').write_text(json.dumps(m))
        with self.assertRaises(lce_modpack.PackError):
            lce_modpack.validate(m, src)
        self.assertEqual(len(lce_modpack.validate(m, src, resize=True)['textures']), 1)

    def test_reserved_ids(self):
        for bad in ('packs', 'minecraft', 'reservations', 'A', 'x'):
            with self.assertRaises(lce_modpack.PackError):
                lce_modpack.validate({'id': bad, 'name': 'n', 'version': '1'}, self.tmp)

    def test_bedrock_port(self):
        out = self.tmp / 'bed'
        m, rep = lce_port.convert_bedrock(ROOT / 'examples/bedrock-sapphire/BP', ROOT / 'examples/bedrock-sapphire/RP', out)
        self.assertEqual(rep.lines['errors'], [])
        self.assertEqual(m['id'], 'gem')
        self.assertEqual(m['version'], '1.0.3')
        blk = m['blocks'][0]
        self.assertEqual((blk['key'], blk['light'], blk['hardness']), ('gem:sapphire_block', 3, 4.0))
        self.assertTrue(any('base_mob' in s for s in rep.lines['skipped']))
        out2 = self.tmp / 'bed2'
        m2, rep2 = lce_port.convert_bedrock(ROOT / 'examples/bedrock-sapphire/BP', ROOT / 'examples/bedrock-sapphire/RP', out2,
                                            {'entities': {'sapphire_golem': {'base_mob': 'minecraft:iron_golem'}}})
        self.assertEqual(m2['entities'][0]['base_mob'], 'minecraft:iron_golem')
        lce_modpack.build(out2, self.tmp / 'bo2')
        lce_modpack.build(out, self.tmp / 'bo')

    def test_bedrock_feature_report(self):
        example = ROOT / 'examples/bedrock-sapphire'
        bp, rp = self.tmp / 'BP', self.tmp / 'RP'
        shutil.copytree(example / 'BP', bp)
        shutil.copytree(example / 'RP', rp)
        manifest_file = bp / 'manifest.json'
        manifest = json.loads(manifest_file.read_text())
        manifest['modules'].append({'type': 'script', 'entry': 'custom/start.js'})
        manifest_file.write_text(json.dumps(manifest))
        for file in (bp / 'spawn_rules/golem.json', bp / 'loot_tables/blocks/sapphire.json',
                     rp / 'entity/sapphire.entity.json', rp / 'ui/settings.json'):
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('{}')

        out = self.tmp / 'bed'
        _, report = lce_port.convert_bedrock(bp, rp, out, target_profile='full')
        body = (out / 'PORT_REPORT.md').read_text()
        for evidence in ('BP/manifest.json (script module)', 'BP/spawn_rules/golem.json', 'spawn_weight',
                         'BP/loot_tables/blocks/sapphire.json', 'RP/entity/sapphire.entity.json', 'RP/ui/settings.json'):
            self.assertIn(evidence, body)
        self.assertEqual(report.target_exit, 0)

    def test_scaffold_is_retail_clean(self):
        d = lce_modpack.scaffold('starter_mod', 'Starter', self.tmp)
        n = lce_modpack.validate(json.loads((d / 'mod.json').read_text()), d)
        self.assertEqual(lce_modpack.profile_report(n, 'retail195'), [])
        _, data = lce_modpack.build(d, self.tmp / 'out')
        self.assertEqual(lce_modpack.decode(data)['meta']['id'], 'starter_mod')
        with self.assertRaises(lce_modpack.PackError):
            lce_modpack.scaffold('starter_mod', None, self.tmp)  # never overwrites

    def test_profile_flags_unsupported(self):
        out, _, _ = self.port_ruby()
        n = lce_modpack.validate(json.loads((out / 'mod.json').read_text()), out)
        msgs = '\n'.join(lce_modpack.profile_report(n, 'retail195'))
        # composite retail195: tools, smelting, shapeless and counts publish; harvest tiers above wood do not
        self.assertIn('rubymod:ruby_ore: harvest tiers above wood', msgs)
        for absent in ("kind 'pickaxe'", 'shapeless', 'result count', 'smelting'):
            self.assertNotIn(absent, msgs)
        self.assertEqual(lce_modpack.profile_report(n, 'full'), [])

    def test_retail_block_physics_limits(self):
        import subprocess

        src = self.tmp / 'src'
        src.mkdir()
        shutil.copyfile(ROOT / 'tests/fixtures/retailall/textures/ore.png', src / 'stone.png')
        texture = 'retailphysics:stone'
        m = {'id': 'retailphysics', 'name': 'Retail physics', 'version': '1.0.0',
             'textures': {texture: {'file': 'stone.png', 'atlas': 'terrain'}},
             'blocks': [
                 {'key': 'retailphysics:limit', 'texture': texture, 'hardness': 1000, 'resistance': 10000},
                 {'key': 'retailphysics:hard', 'texture': texture, 'hardness': 1000.001},
                 {'key': 'retailphysics:resistant', 'texture': texture, 'resistance': 10000.001},
             ]}
        (src / 'mod.json').write_text(json.dumps(m))
        n = lce_modpack.validate(m, src)
        self.assertEqual(lce_modpack.profile_report(n, 'full'), [])
        why = 'hardness/resistance out of range (hardness up to 1000, resistance up to 10000)'
        self.assertEqual(lce_modpack.retail195_verdicts(n), [
            ('block', 'retailphysics:limit', None),
            ('block', 'retailphysics:hard', why),
            ('block', 'retailphysics:resistant', why),
        ])
        exe = ROOT / 'build/host/test_composite195'
        if exe.exists():
            dst, _ = lce_modpack.build(src, self.tmp / 'pkg')
            run = subprocess.run([str(exe), '--verdicts', str(dst / 'mod.lcm')],
                                 capture_output=True, text=True, check=True)
            verdicts = {}
            for line in run.stdout.splitlines():
                if line.startswith('VERDICT block '):
                    _, _, key, verdict, *_ = line.split(maxsplit=4)
                    verdicts[key] = verdict
            self.assertEqual(verdicts, {'retailphysics:limit': 'OK', 'retailphysics:hard': 'REFUSED',
                                        'retailphysics:resistant': 'REFUSED'})

    def test_duplicate_registration_keys(self):
        out, m, _ = self.port_ruby()
        m['blocks'][1]['no_item'] = True
        m['blocks'].append(dict(m['blocks'][0]))
        m['items'].extend([dict(m['items'][0]), dict(m['items'][0], key=m['blocks'][1]['key'])])
        m['tabs'].append(dict(m['tabs'][0]))
        m['smelting'].append(dict(m['smelting'][0], input='minecraft:iron_ore'))
        m['features'] = [dict(m['ores'][0], kind='ore')]
        m['fuels'] = [{'item': 'rubymod:ruby', 'burn_ticks': ticks} for ticks in (100, 150)]

        with self.assertRaises(lce_modpack.PackError) as cm:
            lce_modpack.validate(m, out)
        message = str(cm.exception)
        for diagnostic in ('duplicate block key rubymod:ruby_block', 'duplicate item key rubymod:ruby',
                           'item rubymod:ruby_ore: key already used by a block', 'duplicate tab key rubymod:rubies',
                           'duplicate smelting key rubymod:ruby_from_ore',
                           'feature rubymod:ruby_ore_gen: duplicate key', 'fuel rubymod:ruby: duplicate fuel item'):
            self.assertIn(diagnostic, message)

    def test_profile_matches_composite_backend(self):
        """PROFILES['retail195'] must refuse exactly the keys the C composite backend refuses."""
        import subprocess
        exe = ROOT / 'build/host/test_composite195'
        if not exe.exists():
            self.skipTest('build/host/test_composite195 not built (make test builds it first)')
        for fixture in ('allcontent', 'retailall', 'newretail', 'throwables', 'boats', 'shapes'):
            lcm = ROOT / 'build/pkg' / fixture / 'mod.lcm'
            if not lcm.exists():
                lce_modpack.build(ROOT / 'tests/fixtures' / fixture, ROOT / 'build/pkg')
            res = subprocess.run([str(exe), '--verdicts', str(lcm)], capture_output=True, text=True, check=True)
            c = {}
            for line in res.stdout.splitlines():
                if line.startswith('VERDICT '):
                    _, kind, key, verdict = line.split(' ', 4)[:4]
                    c[key] = verdict
            d = ROOT / 'tests/fixtures' / fixture
            n = lce_modpack.validate(json.loads((d / 'mod.json').read_text()), d)
            py = {key: ('REFUSED' if why else 'OK') for kind, key, why in lce_modpack.retail195_verdicts(n) if kind != 'script'}
            self.assertEqual(set(c), set(py), fixture)
            self.assertEqual(c, py, fixture)
            if fixture != 'allcontent':
                self.assertEqual(sorted(k for k, v in c.items() if v == 'REFUSED'), [], fixture + ' must be fully publishable')

    def test_worldgen_biome_filters_match_composite_backend(self):
        import subprocess
        ore = {'kind': 'ore', 'block': 'minecraft:gold_ore', 'size': 6, 'per_chunk': 1}
        manifest = {
            'id': 'profileworld', 'name': 'Worldgen profile', 'version': '1',
            'biomes': [{'key': 'profileworld:grove', 'name': 'Grove'}],
            'features': [
                dict(ore, key='profileworld:terrain', dimension='overworld', biomes=['profileworld:grove']),
                dict(ore, key='profileworld:end_grove', dimension='end', biomes=['profileworld:grove']),
                dict(ore, key='profileworld:end_stock', dimension='end', biomes=['minecraft:the_end']),
            ],
        }
        normalized = lce_modpack.validate(manifest, self.tmp)
        expected = {'profileworld:grove': 'OK', 'profileworld:terrain': 'OK',
                    'profileworld:end_grove': 'REFUSED', 'profileworld:end_stock': 'OK'}
        py = {key: ('REFUSED' if why else 'OK') for _, key, why in lce_modpack.retail195_verdicts(normalized)}
        self.assertEqual(py, expected)

        exe = ROOT / 'build/host/test_composite195'
        if exe.exists():
            (self.tmp / 'mod.json').write_text(json.dumps(manifest))
            dst, _ = lce_modpack.build(self.tmp, self.tmp / 'pkg')
            res = subprocess.run([str(exe), '--verdicts', str(dst / 'mod.lcm')], capture_output=True, text=True, check=True)
            c = {line.split()[2]: line.split()[3] for line in res.stdout.splitlines() if line.startswith('VERDICT ')}
            self.assertEqual(c, expected)

    def test_all_content_roundtrip(self):
        d = ROOT / 'tests/fixtures/allcontent'
        n = lce_modpack.validate(json.loads((d / 'mod.json').read_text()), d)
        secs = sorted({sec for _t, sec, _v in n['ext']})
        self.assertEqual(secs, ['biomes', 'brewing', 'effects', 'enchantments', 'entities', 'features', 'fuels', 'particles', 'potions',
                                'sounds', 'structures'])
        _, data = lce_modpack.build(d, self.tmp / 'ac')
        dump = lce_modpack.decode(data)
        self.assertEqual(dump['entities'], ['allcontent:gem_golem'])
        self.assertEqual(dump['scripts'][0]['name'], 'main.lua')
        self.assertEqual({t['atlas'] for t in dump['textures']}, {'items', 'terrain', 'icon', 'armor', 'entity'})

    def test_schema_mirror_matches_c(self):
        """tools/lce_ext_schema.py must list the same field kinds/order as loader/ext.cpp."""
        import re as _re
        src = (ROOT / 'loader/ext.cpp').read_text()
        cmap = {'KEY': 'key', 'TEXT': 'text', 'REF': 'ref', 'REF_REQ': 'ref_req', 'U8': 'u8', 'U16': 'u16', 'U32': 'u32',
                'ARR_EFFECT': 'arr_effect', 'ARR_GOAL': 'arr_goal', 'ARR_DROP': 'arr_drop', 'ARR_REF': 'arr_ref',
                'ARR_U16_XYZ': 'u16xyz', 'BLOB': 'blob'}
        py_slot = {'milli': 'u32', 'rgb': 'u32', 'kinds': 'u16', 'category': 'u16', 'feature_kind': 'u16', 'dimension': 'u8', 'milli16': 'u16'}
        tables = dict(_re.findall(r'const lce_ext_field (k\w+)\[\] = \{(.*?)\n\};', src, _re.S))
        names = {'effects': 'kEffect', 'potions': 'kPotion', 'brewing': 'kBrew', 'enchantments': 'kEnchant', 'fuels': 'kFuel',
                 'entities': 'kEntity', 'features': 'kFeature', 'biomes': 'kBiome', 'structures': 'kStructure', 'sounds': 'kSound',
                 'particles': 'kParticle'}
        for sec, (tid, fields) in lce_modpack.XS.SCHEMAS.items():
            c_fields = _re.findall(r'[FA]\(LCE_F_(\w+),[^"]*"(\w+)"\)', tables[names[sec]])
            self.assertEqual([n for _k, n in c_fields], [n for n, _k in fields], sec)
            for (ck, cn), (pn, pk) in zip(c_fields, fields):
                self.assertEqual(cmap[ck], py_slot.get(pk, pk), f'{sec}.{cn}')

    def test_vanilla_table(self):
        self.assertEqual(lce_modpack.VANILLA_ITEMS['stick'], 280)
        self.assertEqual(lce_modpack.VANILLA_ITEMS['bed'], 355)  # separate item beats block-item 26
        self.assertEqual(lce_modpack.VANILLA_BLOCKS['bed'], 26)
        self.assertEqual(lce_modpack.VANILLA_ITEMS['conduit'], 1024)


if __name__ == '__main__':
    unittest.main(verbosity=1)
