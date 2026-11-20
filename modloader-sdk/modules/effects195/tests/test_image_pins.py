"""Static pins for effects195 against the 1.95 image (bytes, RTTI, call sites).
Everything effects195.h relies on that is not exercised by test_unicorn_retail.py."""
import hashlib, struct, sys
from pathlib import Path

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
d = IMAGE.read_bytes()
checks = fails = 0
def check(c, what):
    global checks, fails
    checks += 1
    if not c:
        fails += 1; print('FAIL', what)

check(hashlib.sha256(d).hexdigest() == 'c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f', 'image sha256')
phoff = struct.unpack_from('<Q', d, 0x20)[0]; phnum = struct.unpack_from('<H', d, 0x38)[0]
segs = []
for i in range(phnum):
    t, fl, off, va, pa, fs, ms, al = struct.unpack_from('<IIQQQQQQ', d, phoff + i * 56)
    if fs and t in (1, 0x61000010): segs.append((va, off, fs))
def v2o(v):
    for va, off, fs in segs:
        if va <= v < va + fs: return off + v - va
    raise KeyError(hex(v))
def rd(v, n): o = v2o(v); return d[o:o + n]
def q(v): return struct.unpack('<Q', rd(v, 8))[0]
def cstr(v): o = v2o(v); return d[o:d.index(b'\0', o)].decode()
def rtti(vt): return cstr(q(q(vt - 8) + 8))
def rel_target(at, oplen=5): return at + oplen + struct.unpack('<i', rd(at + 1, 4))[0]

# vtables and their RTTI
check(rtti(0x1287760) == '9MobEffect', 'MobEffect vtable 0x1287760')
check(rtti(0x120b560) == '21AttackDamageMobEffect', 'AttackDamageMobEffect vtable 0x120b560')
check(rtti(0x126e070) == '21InstantenousMobEffect', 'InstantenousMobEffect vtable 0x126e070')
check(rtti(0x12678e8) == '20HealthBoostMobEffect', 'HealthBoostMobEffect vtable 0x12678e8')
check(rtti(0x1294038).startswith('14MappedRegistryI16ResourceLocationP9MobEffect'), 'MobEffect registry vtable 0x1294038')
check(rtti(0x12aa668).startswith('23DefaultedMappedRegistryI16ResourceLocationP6Potion'), 'Potion registry vtable 0x12aa668')
# 15 slots: slot 15 is the next vtable's offset-to-top (0), 16 its typeinfo
check(all(q(0x1287760 + 8 * i) for i in range(15)) and q(0x1287760 + 8 * 15) == 0, 'MobEffect has exactly 15 slots')
for vt in (0x120b560, 0x126e070, 0x12678e8):
    check(all(q(vt + 8 * i) for i in range(15)) and q(vt + 8 * 15) == 0, f'{rtti(vt)}: 15 slots')
slots = [q(0x1287760 + 8 * i) for i in range(15)]
check(slots[0] == 0x490ed0 and slots[1] == 0x491240 and slots[2] == 0x491510 and slots[3] == 0x491580, 'slots 0..3 = tick/instant/ready/isInstant')
check(slots[7] == 0x4917c0 and slots[8] == 0x4917d0, 'slot 7 getColor, slot 8 addAttributeModifier')
check(q(0x1294038 + 0x28) == 0x4c3610 and q(0x1294038 + 0x40) == 0x4c3a10 and q(0x1294038) == 0x4c3250, 'effect registry slots get/register/byId')
check(q(0x12aa668 + 0x28) == 0x57f8e0 and q(0x12aa668 + 0x40) == 0x57fae0 and q(0x12aa668 + 0x30) == 0x57f980, 'potion registry slots register/byId/getKey')

# identity comparisons that make forwarding necessary (applyEffectTick, isDurationEffectTick)
check(rd(0x490ee8, 7) == bytes.fromhex('488d0de986f800') and rd(0x490f00, 3) == bytes.fromhex('483919'), 'applyEffectTick compares this with [0x14195d8]')
check(rd(0x491510, 3) == bytes.fromhex('488d05') and rd(0x491517, 3) == bytes.fromhex('483938'), 'isDurationEffectTick compares this with [0x14195d8]')
check(rd(0x491542, 5) == bytes.fromhex('bf32000000'), 'regeneration interval 50 >> amp')
# MobEffect ctor field stores
check(rd(0x490e6d, 4) == bytes.fromhex('44887360') and rd(0x490e71, 4) == bytes.fromhex('44897b74'), 'ctor: +0x60 harmful, +0x74 colour')
check(rd(0x490e75, 4) == bytes.fromhex('48894358'), 'ctor: +0x58 sid/icon qword')
check(rd(0x491590, 4) == bytes.fromhex('8b4758c3') and rd(0x4915b0, 4) == bytes.fromhex('8b475cc3'), 'getDescriptionId +0x58, getIcon +0x5c')
# MEI ctor and the potion vector grow
check(rd(0x491aa0, 10) == bytes.fromhex('48b8000000' + '01ffffffff'), 'MEI ctor 0x491aa0 defaults (+0x13=1, +0x14=-1)')
check(rd(0x55ecea, 5)[0] == 0xe8 and rel_target(0x55ecea) == 0x6fd00, 'potion init grows the MEI vector with 0x6fd00')
# HUD sizes an int array by the effect count global (must cover mod ids)
check(rd(0xa00abb, 1)[0] == 0xe8 and rel_target(0xa00abb) == 0x490da0 and rd(0xa00ad7, 1)[0] == 0xe8 and rel_target(0xa00ad7) == 0xe6cfc0, 'HUD 0xa00900: new int[count()+1]')
check(rd(0x490da0, 10) == bytes.fromhex('488d052988f8008b00c3'), 'count() 0x490da0 returns [0x14195d0]')
# one-byte ids: NBT save/load and packets zero-extend
check(rd(0x491ccc, 3) == bytes.fromhex('0fb6d0'), 'MEI::save putByte(Id, movzbl getId)')
check(rd(0x491ee5, 3) == bytes.fromhex('0fb6d8'), 'MEI::load movzbl getByte -> byId')
check(rd(0x19e3a2, 3) == bytes.fromhex('0fb6f8') and rel_target(0x19e3a5) == 0x490d60, 'RemoveMobEffect packet: movzbl -> byId')
check(rd(0xba15f3, 3) == bytes.fromhex('0fb6f8') and rel_target(0xba15f6) == 0x490d60, 'UpdateMobEffect handler: movzbl -> byId')
check(rd(0x1a4b50, 4) == bytes.fromhex('8a472cc3'), 'UpdateMobEffect id byte at +0x2c')
# init order: effects 0x4781e0 and statics 0x47b780 run before Items::init 0x386070; potions after
order = [(0x477cb3, 0x4781e0), (0x477cb8, 0x47b780), (0x477ccc, 0x386070), (0x477ce0, 0x55e6d0)]
check(all(rd(a, 1)[0] == 0xe8 and rel_target(a) == t for a, t in order), 'static chain order effects -> statics -> items -> potions')
check(rd(0x47823b - 6, 6) == bytes.fromhex('c70101000000'), 'effect initializer resets the count to 1 (so publishing must follow it)')
# potions: name kinds and the NBT key
check(rd(0x55e62a, 3) == bytes.fromhex('83fa03') and rd(0x55e639, 4) == bytes.fromhex('4883c604'), 'Potion::getName kind -> +0/+4/+8/+0xc')
check(rel_target(0x4196bd) == 0x55e620 and rd(0x4196b2, 5) == bytes.fromhex('ba02000000'), 'LingeringPotionItem uses kind 2')
check(rel_target(0x67140d) == 0x55e620 and rd(0x671402, 5) == bytes.fromhex('ba01000000'), 'SplashPotionItem uses kind 1')
check(rel_target(0x6eba5d) == 0x55e620 and rd(0x6eba52, 5) == bytes.fromhex('ba03000000'), 'TippedArrowItem uses kind 3')
check(rd(0x565e65, 3) == bytes.fromhex('488d15') and rel_target(0x565e79) == 0x1af2a0, 'PotionItem name: getString(tag, [0x142eba8]) -> registry get')
check(rd(0x562ceb, 3) == bytes.fromhex('488d0d') and rel_target(0x562dad) == 0x1af3b0, 'setPotion: putString("Potion", registry getKey)')
check(cstr(0xe9df69) == 'invalid unordered_map<K, T> key' and rel_target(0x4c3372) == 0xe6cf80, 'MappedRegistry::get throws on unknown keys (0x4c336b): callers must use known keys')

print(f'test_image_pins: {checks} checks, {fails} failures')
sys.exit(1 if fails else 0)
