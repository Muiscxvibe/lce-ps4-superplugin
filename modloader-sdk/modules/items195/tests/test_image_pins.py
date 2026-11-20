"""Pin every RVA/offset items195 relies on against the retail 1.95 image.

Static checks on the real bytes (capstone): RTTI class names behind each
vtable, constructor field stores, stock call sites, slot values and counts,
and the stock effect/potion name order the module hard-codes. Run with the
analysis venv python (capstone); the image is read-only.
"""
import hashlib, re, struct, sys
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
SHA = 'c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f'
HERE = Path(__file__).resolve().parent.parent
d = IMAGE.read_bytes()
fails = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        fails.append(what)
        print('FAIL', what)


check(hashlib.sha256(d).hexdigest() == SHA, 'image sha256')
phoff = struct.unpack_from('<Q', d, 0x20)[0]
phnum = struct.unpack_from('<H', d, 0x38)[0]
segs = []
for i in range(phnum):
    t, fl, off, va, pa, fs, ms, al = struct.unpack_from('<IIQQQQQQ', d, phoff + i * 56)
    if fs and t in (1, 0x61000010):
        segs.append((va, off, fs, fl))


def v2o(v):
    for va, off, fs, fl in segs:
        if va <= v < va + fs:
            return off + v - va
    raise ValueError(hex(v))


def rd(v, n):
    o = v2o(v)
    return d[o:o + n]


def q(v):
    return struct.unpack('<Q', rd(v, 8))[0]


md = Cs(CS_ARCH_X86, CS_MODE_64)


def dis(va, n):
    return [f'{i.mnemonic} {i.op_str}' for i in md.disasm(rd(va, n), va)]


def ins_at(va):
    i = next(md.disasm(rd(va, 16), va))
    return i


def rip_target(va):
    i = ins_at(va)
    m = re.search(r'\[rip \+ (0x[0-9a-f]+)\]', i.op_str)
    return va + i.size + int(m.group(1), 16)


def rtti(vt):
    ti = q(vt - 8)
    name = q(ti + 8)
    o = v2o(name)
    return d[o:d.index(b'\0', o)].decode()


def has(va, n, *needles):
    text = '\n'.join(dis(va, n))
    return all(x in text for x in needles)


# ---- vtables -> RTTI class names
for vt, name in [(0x126e638, '4Item'), (0x12bbed8, '12ShapedRecipe'), (0x12bbf58, '15ShapelessRecipe'), (0x12594d8, '8FoodItem'),
                 (0x12095f8, '9ArmorItem'), (0x12de818, 'SwordItem'), (0x129e568, 'PickaxeItem'), (0x120b688, 'AxeItem'),
                 (0x12be6c0, 'ShovelItem'), (0x1267b68, 'HoeItem'), (0x1287760, '9MobEffect')]:
    n = rtti(vt)
    check(n.endswith(name), f'RTTI {vt:#x} = {n} (want *{name})')
check('ItemInstancePredicate' in rtti(0x12a62a8), 'predicate vtable 0x12a62a8 RTTI')
# ---- slot values / counts
check(q(0x126e638 + 0x108) == 0x3af070, 'Item +0x108 name 0x3af070')
check(q(0x126e638 + 0x138) == 0x3af3c0, 'Item +0x138 enchant 0x3af3c0')
check(q(0x126e638 + 0x148) == 0x3af3e0, 'Item +0x148 repair 0x3af3e0')
check(q(0x126e638 + 56 * 8) == 0, 'Item vtable has 56 slots')
check(rtti(0x12c4c38) == '16SimpleFoiledItem' and q(0x12c4c38 + 0x110) == 0x6276d0,
      'SimpleFoiledItem isFoil slot')
check(rd(0x6276d0, 3) == bytes.fromhex('b001c3'), 'SimpleFoiledItem::isFoil returns true without touching its arguments')
for vt in (0x126e638, 0x129e568, 0x120b688, 0x12be6c0, 0x1267b68, 0x12095f8, 0x12de818, 0x12594d8, 0x1219f18, 0x12cae00):
    check(q(vt + 0x110) == 0x3af180, f'{vt:#x} keeps stock isFoil slot')
check(rd(0x3aef40, 4) == bytes.fromhex('8a4730c3'), 'Item::isHandEquipped reads item byte +0x30')
for vt in (0x126e638, 0x12095f8, 0x12594d8, 0x1219f18, 0x12cae00):
    check(q(vt + 0x88) == 0x3aef40, f'{vt:#x} reads the stock hand-equipped flag')
for vt in (0x129e568, 0x120b688, 0x12be6c0, 0x1267b68, 0x12de818):
    check(rd(q(vt + 0x88), 3) == bytes.fromhex('b001c3'), f'{vt:#x} is always hand-equipped')
for vt in (0x129e568, 0x120b688, 0x12be6c0, 0x1267b68, 0x12095f8, 0x12de818, 0x12594d8):
    check(q(vt + 0x108) == 0x3af070, f'{vt:#x} keeps stock name slot')
check(q(0x12de818 + 56 * 8) == 0x6cf030, 'Sword vtable slot 56 (57 slots)')
check(q(0x12594d8 + 61 * 8) != 0 and q(0x12594d8 + 0x1e0) == 0x2f5ff0, 'FoodItem 62 slots, +0x1e0 setEatEffect 0x2f5ff0')
check(q(0x129e568 + 0x138) == 0x1fd370 and q(0x129e568 + 0x148) == 0x1fd380, 'digger enchant/repair slots')
# ---- API 1.10 boat items: BoatItem = Item ctor + vptr + [+0x9c] type + stack 1; use reads the type
check(rtti(0x1219f18).endswith('8BoatItem'), 'RTTI 0x1219f18 = BoatItem')
check(q(0x1219f18 + 56 * 8) == 0 and q(0x1219f18 + 55 * 8) != 0, 'BoatItem vtable has 56 slots')
check(q(0x1219f18 + 0x108) == 0x3af070 and q(0x1219f18 + 0x40) == 0xd7c00, 'BoatItem keeps the stock name slot, +0x40 use 0xd7c00')
check(all(q(0x1219f18 + o) == q(0x126e638 + o) for o in range(0x48, 0x1c0, 8)), 'BoatItem slots +0x48.. equal Item (only class id/dtors/+0x38/use differ)')
check(has(0xd7640, 0x33, 'call 0x354d80', 'mov dword ptr [rbx + 0x9c], r14d', 'mov dword ptr [rbx + 0x18], 1') and rip_target(0xd7652) + 0x10 == 0x1219f18,
      'BoatItem ctor 0xd7640: Item ctor, vptr 0x1219f18, +0x9c type, stack 1')
check(any('[rbx + 0x9c]' in l for l in dis(0xd80f9, 6)) and ins_at(0xd8088).op_str == 'rbx, qword ptr [rbp - 0x148]' and ins_at(0xd7c1e).op_str == 'rbx, rsi'
      and ins_at(0xd7e14).op_str == 'qword ptr [rbp - 0x148], rbx', 'BoatItem::use reads this+0x9c (the boat type) at 0xd80f9')
for site, t in ((0x399422, 0), (0x3a1f44, 1), (0x3a207c, 2), (0x3a21b4, 3), (0x3a22ec, 4), (0x3a2424, 5)):
    check(ins_at(site).op_str == ('esi, esi' if t == 0 else f'esi, {t}') and has(site, 16, 'call 0xd7640'), f'Items::init boat type {t} at {site:#x}')
check(q(0x1267b68 + 0x138) == 0x3af3c0, 'hoe enchant slot is stock 0 (override needed)')
check(has(0x1fd370, 0x10, 'qword ptr [rdi + 0xc0]', 'jmp 0x3aece0'), 'digger enchant reads tier +0x10')
check(has(0x3aece0, 0x4, 'dword ptr [rdi + 0x10]'), 'Tier +0x10 enchantment')
check(has(0x35ee90, 0x4, 'dword ptr [rdi + 4]'), 'Tier +4 uses')
check(has(0x3aecc0, 0x6, 'dword ptr [rdi + 8]'), 'Tier +8 speed')
check(has(0x35eea0, 0x6, 'dword ptr [rdi + 0xc]'), 'Tier +0xc damage')
check(has(0x1fd380, 0x40, 'qword ptr [r14 + 0xc0]', 'call 0x3aecf0'), 'digger repair uses Tier::getRepairItem')
# ---- item ctors
check(has(0x6cefd0, 0x60, 'call 0x354d80', 'qword ptr [rbx + 0xa0], r14', 'dword ptr [rbx + 0x18], 1', 'call 0x35ecd0', 'dword ptr [rbx + 0x9c], xmm0'), 'sword ctor fields')
check(has(0x1fcfd0, 0xb0, 'qword ptr [rbx + 0xc0], r12', 'dword ptr [rbx + 0x18], 1', 'call 0x35ecd0', 'dword ptr [rbx + 0xb0], xmm0'), 'DiggerItem ctor fields')
for c in (0x52bd50, 0x60b060):
    check(has(c, 0x40, 'call 0x1fcfd0'), f'{c:#x} -> DiggerItem')
check(has(0x3e3c0, 0x40, 'call 0x1fd080'), 'axe ctor')
check(has(0x35ecd0, 0x30, 'dword ptr [r12 + 0x1c], esi'), 'setMaxDamage writes +0x1c')
check(has(0x32cd0, 0xc0, 'call 0x35ecd0'), 'armour ctor sets durability')
armour = '\n'.join(dis(0x32cd0, 0xc0))
for off in ('0xa0', '0xa8', '0xac', '0xb0', '0xb8'):
    check(f'+ {off}]' in armour, f'armour ctor stores +{off}')
food = '\n'.join(dis(0x2d69d0, 0x70))
for off in ('0x9c', '0xa0', '0xa4'):
    check(f'+ {off}]' in food, f'food ctor stores +{off}')
check(has(0x2f5cf0, 0x20, '+ 0xa6]'), 'getUseDuration reads +0xa6')
check(has(0x2f5ff0, 0x40, '+ 0xa8]', '+ 0xb0]'), 'setEatEffect writes +0xa8/+0xb0')
check(rip_target(0x3944d7 + 0x0) != 0, 'items init reachable')
# ---- recipes
check(has(0x6013d0, 0x99, 'dword ptr [rdi + 0x1c], esi', 'dword ptr [rdi + 0x20], edx', 'qword ptr [rdi + 0x28], rcx', 'qword ptr [rdi + 0x30], r8', 'dword ptr [rdi + 0x24], r9d'), 'ShapedRecipe ctor layout')
check(rip_target(0x6013d0) + 0x10 == 0x12bbed8, 'ShapedRecipe ctor vtable')
check(has(0x602e10, 0x35, 'dword ptr [rdi + 0x1c], ecx', 'qword ptr [rdi + 0x20], rsi', 'qword ptr [rdi + 0x28], rdx'), 'ShapelessRecipe ctor layout')
check(rip_target(0x602e10) + 0x10 == 0x12bbf58, 'ShapelessRecipe ctor vtable')
check(has(0x3b86f0, 0x30, 'call 0x385940', 'shl eax, 0x10', 'or eax, dword ptr [rbx + 0x3c]'), 'output key (id<<16)|aux')
check(has(0x5ab77a, 0x60, 'mov edi, 0x30', 'call 0x602e10', 'dword ptr [r12 + 0xc], eax', 'dword ptr [r12 + 0x10], eax', 'call 0x3b86f0', 'call 0x5ab110'), 'stock shapeless add sequence')
check(has(0x5aaf13, 0x60, 'call 0x5d3150', 'add qword ptr [rbx + 0x10], 8'), 'stock shaped add appends to bucket unconditionally')
check(has(0x5ab59d, 0x10, 'call 0x5d4130'), 'shapeless ingredient vector grow 0x5d4130')
check(has(0x5ab5c8, 0x30, 'mov ecx, 0xffffffff', 'call 0x3b7680'), 'stock tile ingredients use aux -1')
check(has(0x601bb4, 0x30, 'cmp eax, -1'), 'shaped aux -1 wildcard')
check(has(0x6032e1, 0x30, 'cmp eax, -1'), 'shapeless aux -1 wildcard')
check(has(0x5abe1a, 0x20, '+ 0x58]', '+ 0x60]'), 'getItemFor iterates the list vector')
check(has(0x5ac900, 0x10, 'shl rax, 5', '+ 0x50]'), 'list vector owner+0x50+i*0x20')
check(has(0x3b7720, 0x20, 'jmp 0x3b7380'), 'ItemInstance(Item*,count,aux)')
check(ins_at(0x5a9ef1).mnemonic == 'call', 'pre-cache call 0x5a9ef1')
# ---- furnace
check(rd(0x325fb6, 5) == b'\xe8\x15\x00\x00\x00', 'furnace ctor call 0x325fb6')
check(rip_target(0x325fbb) == 0x1416a80 or rip_target(0x325fc2) == 0x1416a80, 'furnace global 0x1416a80')
check(rd(0x320f20, 8) == bytes.fromhex('554889e541574156'), 'getBurnDuration prologue')
for m in (0x329640, 0x329820, 0x329a00):
    check(dis(m, 4)[0].startswith('push rbp'), f'furnace map helper {m:#x}')
# ---- effects / potions / brewing
check(rip_target(0x490d60) == 0x14195c8 and has(0x490d60, 0x18, '+ 0x40]'), 'MobEffect::byId -> registry vt+0x40')
check(rip_target(0x55e560) == 0x142e938 and has(0x55e560, 0x18, '+ 0x40]'), 'Potion::byId -> registry vt+0x40')
check(rip_target(0x563416) == 0x142e948, 'potion mix vector 0x142e948')
check(has(0x563448, 0x10, 'call 0x583020'), 'mix vector grow 0x583020')
check(rip_target(0x562e63) + 0x10 == 0x12a62a8, 'ItemInstancePredicate vtable address point')
check(has(0x562e79, 0x18, 'qword ptr [rax + 8], rcx', 'dword ptr [rax + 0x10], 0xffffffff'), 'predicate {vt, Item*, aux -1}')


def names(start, length):
    out, last = [], None
    for i in md.disasm(rd(start, length), start):
        m = re.search(r'\[rip \+ (0x[0-9a-f]+)\]', i.op_str)
        if i.mnemonic == 'lea' and i.op_str.startswith('rsi') and m:
            last = i.address + i.size + int(m.group(1), 16)
        if i.mnemonic == 'call' and i.op_str == '0x67840' and last:
            o = v2o(last)
            s = ''
            for k in range(40):
                c = struct.unpack_from('<H', d, o + 2 * k)[0]
                if not c:
                    break
                s += chr(c)
            if s and s != 'minecraft':
                out.append(s)
            last = None
        if i.mnemonic == 'ret':
            break
    return out


src = (HERE / 'effects.cpp').read_text()
def table(name):
    body = src[src.index(f'const char *const {name}[] = {{'):]
    body = body[:body.index('};')]
    return re.findall(r'"([a-z_]+)"', body)


check(names(0x55e6d0, 0x9000) == table('kPotions'), 'potion registration order == kPotions')
eff = names(0x4781e0, 0x9000)
check(eff[:31] == table('kEffects'), f'effect registration order == kEffects ({eff[:4]}...)')

print(f'test_image_pins: {checks} checks, {len(fails)} failures')
sys.exit(1 if fails else 0)
