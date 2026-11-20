"""Static pins for enchants195 against the analysis image."""
import hashlib, re, struct, sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / 'strings195/tests'))
from uc195 import IMAGE, IMAGE_SHA256, Checks

img = IMAGE.read_bytes()
ok = Checks('enchants195 pins')
ok(hashlib.sha256(img).hexdigest() == IMAGE_SHA256, 'analysis image SHA-256')
SEG = [(0, 0x4000, 0x10c0354), (0x1200000, 0x10c8000, 0x14d928), (0x1400000, 0x1218000, 0x8ac8)]
def v2o(v):
    for va, off, fs in SEG:
        if va <= v < va + fs: return off + v - va
def q(v): return struct.unpack_from('<Q', img, v2o(v))[0]
def d(v): return struct.unpack_from('<I', img, v2o(v))[0]
def at(v, n): o = v2o(v); return img[o:o + n]
def rel(v, size): return v + size + struct.unpack('<i', at(v + size - 4, 4))[0]
def cstr(v): o = v2o(v); return img[o:img.index(b'\0', o)].decode()
def rtti(vt): return cstr(q(q(vt - 8) + 8))
text = img[0x4000:0x4000 + 0x10c0354]

# class vtables (19 slots each) and RTTI
for vt, name in ((0x1245d68, '11Enchantment'), (0x1236350, '17DamageEnchantment'), (0x12ac170, '21ProtectionEnchantment'),
                 (0x12e6680, '25TridentImpalerEnchantment'), (0x12e0e90, '17ThornsEnchantment'), (0x1255398, None)):
    if name: ok(rtti(vt) == name, f'vtable {vt:#x} is {name}')
ok(rtti(0x1255398).startswith('14MappedRegistryI16ResourceLocationP11Enchantment'), 'registry vtable 0x1255398 = MappedRegistry<RL, Enchantment*>')
base = [q(0x1245d68 + 8 * i) for i in range(19)]
ok(base == [0x24ab30, 0x24ab40, 0x24ae30, 0x24ae40, 0x24ae50, 0x24ae60, 0x24ae70, 0x24ae80, 0x24ae90, 0x24aea0, 0x24aeb0, 0x24aec0,
            0x24b040, 0x24b070, 0x24b080, 0x24b090, 0x24b0a0, 0x2ba760, 0x2ba770], 'base Enchantment slot table')
ok(q(0x1245d68 + 19 * 8) == 0 or not (0x1000 <= q(0x1245d68 + 19 * 8) < 0x10c0354), 'base vtable has exactly 19 slots')
for vt in (0x1236350, 0x12ac170, 0x12e6680):
    ok(q(vt) == 0x24ab30 and q(vt + 0x58) == 0x24aec0 and q(vt + 0x48) == 0x24aea0, f'{rtti(vt)}: shared getRarity/getFullname/setDescriptionId')
ok(q(0x1236350 + 0x38) == 0x1f1ae0 and q(0x12ac170 + 0x30) == 0x58da00 and q(0x12e6680 + 0x38) == 0x6f6bb0, 'damage/protection/impaler virtuals at +0x38/+0x30/+0x38')
ok(q(0x1236350 + 0x50) == 0x1f1b70 and q(0x12ac170 + 0x50) == 0x58dae0, 'subclasses override getDescriptionId (+0x10 ignored) -> slot override needed')
# small retail functions the private vtables point at
ok(at(0x24aeb0, 4) == bytes.fromhex('8b4710c3'), '0x24aeb0: mov 0x10(%rdi),%eax; ret')
ok(at(0x24aea0, 7) == bytes.fromhex('4889f8897710c3'), '0x24aea0 setDescriptionId writes +0x10')
ok(at(0x24ab30, 5) == bytes.fromhex('488b4728c3'), 'getRarity returns +0x28')
ok(at(0x24b090, 3) == bytes.fromhex('31c0c3') and at(0x24b0a0, 3) == bytes.fromhex('31c0c3'), 'isTreasureOnly / isCurse: return 0')
for n, fn in enumerate([0, 0x24ae30, 0x48c0, 0x158c0, 0x3ca20, 0x1f1ad0, 0x12180, 0x5db00, 0x24d10, 0x58420, 0x319f0]):
    if n: ok(fn % 16 == 0 and at(fn, 6) == bytes([0xb8, n, 0, 0, 0, 0xc3]) and text[fn - 1] in (0x90, 0xcc, 0xc3), f'const fn {n} at {fn:#x}')
# 0x24b040 canEnchant uses the category at +0x08
ok(at(0x24b047, 4) == bytes.fromhex('488b5f08'), 'canEnchant reads category +0x08')
# ctors: vptr and fields
ok(at(0x24ab10, 3) == bytes.fromhex('488d05') and rel(0x24ab10, 7) == 0x1245d58, 'base ctor vptr 0x1245d68')
ok(at(0x24ab1e, 16) == bytes.fromhex('48895708488977284889 4f18448947 20'.replace(' ', '')), 'base ctor: +8 category, +0x28 rarity, +0x18 slots, +0x20 count')
ok(rel(0x1f1a47, 7) == 0x1415480 and rel(0x1f1a57, 5) == 0x24ab10 and at(0x1f1a6a, 4) == bytes.fromhex('44897330'), 'damage ctor: WEAPON category, base ctor, type at +0x30')
ok(rel(0x58d977, 7) == 0x142ed50 and rel(0x58d989, 7) == 0x14154c8 and rel(0x58d992, 7) == 0x14154c0 and at(0x58d9b2, 4) == bytes.fromhex('4c897330'), 'protection ctor: FEET if FALL else ARMOR, Type at +0x30')
ok(rel(0x6f6b36, 7) == 0x1415508 and at(0x6f6b57, 5) == bytes.fromhex('be2c65d52e'), 'impaler ctor: TRIDENT, name 0x2ed5652c')
ok(rel(0x6e0b46, 7) == 0x14154d8, 'thorns ctor category CHEST (thorns itself is refused)')
# registry slots and the stock bootstrap's use of them
rv = 0x1255398
ok([q(rv + o) for o in (0x20, 0x28, 0x38, 0x40)] == [0x2beb00, 0x2bec20, 0x2bef00, 0x2bf020], 'registry slots contains/register/getId/byId')
ok(at(0x2bf020, 4) == bytes.fromhex('85f6782b') or at(0x2bf020, 2) == bytes.fromhex('85f6'), 'byId rejects negative ids')
ok(at(0x2bf024, 7) == bytes.fromhex('488b87e8000000') and at(0x2bf02b, 7) == bytes.fromhex('488b8ff0000000'), 'byId bounds against the id vector (+0xe8/+0xf0)')
ok(at(0x24909c, 4) == bytes.fromhex('4c8b6828'), 'bootstrap calls register through slot +0x28')
ok(rel(0x478134, 5) == 0x248fa0 and rel(0x47812f, 5) == 0x562e30, 'Enchantments bootstrap 0x248fa0 at 0x478134, right after PotionBrewing')
ok(0x477cc8 < 0x478134, 'item window 0x477cc8 runs before the bootstrap -> publish later (title phase)')
ok(at(0x2d305b, 5) == bytes.fromhex('bf50010000') and rel(0x2d3072, 5) == 0x2be3d0 and rel(0x2d307e, 7) == 0x1415448, 'registry object 0x150 built by static init 0x2d3040')
# readers that see a registered enchantment live
ok(rel(0x24d8eb, 7) == 0x1415448 and at(0x24d8f9, 7) == bytes.fromhex('488b8008010000'), 'table 0x24d890 iterates reg+0x108')
ok(at(0x24d948, 4) != b'' and at(0x24d94b, 3) == bytes.fromhex('ff5078'), 'table checks isTreasureOnly (+0x78)')
ok(rel(0xa31da5, 7) == 0x1415448 and at(0xa31daf, 7) == bytes.fromhex('488b8708010000'), 'Creative book list iterates reg+0x108')
ok(at(0x248d13, 3) == bytes.fromhex('4885c0'), 'NBT apply null-checks byId (unknown ids skipped)')
# rarities: weight +0x30 read by 0x59d310
ok(at(0x59d310, 4) == bytes.fromhex('8b4730c3'), 'Rarity weight at +0x30')
# protection Types: {name, minCost, levelCost, span} constants copied by the static init
types = [struct.unpack_from('<iiii', img, v2o(0xea7c00 + 16 * i)) for i in range(5)]
ok([t[1:3] for t in types] == [(1, 11), (10, 8), (5, 6), (5, 8), (3, 6)], 'protection Type min/level costs (Java values)')
# level text: table of 11 ids at 0xe8bd80; 0x24afd0 indexes it up to 11 (index 11 is not a string id)
ok(at(0x24afda, 3) == bytes.fromhex('83fa0b') and rel(0x24afe2, 7) == 0xe8bd80, 'level text table indexed <= 11')
ok(d(0xe8bd80 + 44) == 4, 'entry 11 is not a string id -> max level 10')
# identity reads: Enchantments::X statics (0x250cba..) and their extra references
src = text.decode('latin1')
def refs(g):
    n = 0
    for mm in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', text):
        j = mm.start(); r = struct.unpack_from('<i', text, j + 3)[0]
        if j + 7 + r == g: n += 1
    return n
statics = {'protection': 0x1415628, 'feather_falling': 0x1415638, 'projectile_protection': 0x1415648, 'sharpness': 0x1415658,
           'smite': 0x1415660, 'bane_of_arthropods': 0x1415668, 'impaling': 0x14156b0,
           'fire_protection': 0x1415630, 'blast_protection': 0x1415640, 'thorns': 0x1415650, 'fire_aspect': 0x14155c8, 'mending': 0x14156a8}
counts = {k: refs(v) for k, v in statics.items()}
ok(all(counts[k] == 1 for k in ('protection', 'feather_falling', 'projectile_protection', 'sharpness', 'smite', 'bane_of_arthropods', 'impaling')),
   f'accepted clones: their static pointer is only stored, never read ({counts})')
ok(counts['thorns'] == 2 and rel(0x6e0ce8, 7) == 0x1415650, 'thorns doPostHurt reads Enchantments::THORNS (0x6e0ce8) -> clone refused')
ok(counts['fire_protection'] == 2 and counts['blast_protection'] == 2, 'fire/blast protection: one identity read each (burn time / knockback); protection part still virtual')
ok(counts['fire_aspect'] >= 2 and counts['mending'] >= 2, 'fire_aspect / mending identity-read -> refused')
sys.exit(ok.done())
