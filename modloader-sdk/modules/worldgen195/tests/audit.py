#!/usr/bin/env python3
"""Pin every RVA worldgen195 relies on against the 1.95 analysis image.

Run with a python that has capstone (the analysis venv). Prints PASS lines and
exits non-zero on the first mismatch.
"""
from pathlib import Path
import hashlib, re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
HERE = Path(__file__).resolve().parent
raw = IMAGE.read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f'
SEGS = [(0, 0x4000, 0x10c0354), (0x1200000, 0x10c8000, 0x14d928), (0x1400000, 0x1218000, 0x8ac8)]
mem = bytearray(0x1500000)
for va, off, n in SEGS:
    mem[va:va + n] = raw[off:off + n]
REL = {}
for off in range(0x12aaf70 + 0xb550, 0x12aaf70 + 0xb550 + 0x3aa2b0, 24):
    at, kind, add = struct.unpack_from('<QQq', raw, off)
    if kind & 0xffffffff == 8:
        REL[at] = add
        struct.pack_into('<Q', mem, at, add & 0xffffffffffffffff)
TEXT_END = 0x10c0354
md = Cs(CS_ARCH_X86, CS_MODE_64)
count = 0


def q(a): return struct.unpack_from('<Q', mem, a)[0]
def cstr(a): return mem[a:mem.index(b'\0', a)].decode('latin1')
def wstr(a):
    s = ''
    while True:
        c = struct.unpack_from('<H', mem, a)[0]
        if not c: return s
        s += chr(c); a += 2
def ins(a): return next(md.disasm(bytes(mem[a:a + 16]), a))
def check(cond, what):
    global count
    if not cond:
        print('FAIL', what); sys.exit(1)
    count += 1
def pin(a, mnem, ops):
    i = ins(a)
    check((i.mnemonic, i.op_str) == (mnem, ops), f'{a:#x}: want {mnem} {ops}, got {i.mnemonic} {i.op_str}')
def rip_target(a):
    i = ins(a)
    m = re.search(r'\[rip \+ (0x[0-9a-f]+)\]', i.op_str)
    return i.address + i.size + int(m.group(1), 16)
def rtti(vt):
    return cstr(q(q(vt - 8) + 8))
def lea_refs(target):
    blob = bytes(mem[0:TEXT_END]); out = []
    for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', blob):
        j = m.start(); rel = struct.unpack_from('<i', blob, j + 3)[0]
        if j + 7 + rel == target: out.append(j)
    return out
def fstart(a):
    return bytes(mem).rfind(b'\x55\x48\x89\xe5', 0, a + 1)
def calls_in(f, n):
    return [i for i in md.disasm(bytes(mem[f:f + n]), f) if i.mnemonic == 'call']

# ---- tables from the module source ---------------------------------------------
src = (HERE.parent / 'worldgen195.cpp').read_text()
classes = [(int(a, 16), int(b, 16), int(c, 16), int(d), e) for a, b, c, d, e in
           re.findall(r'\{(0x[0-9a-f]+)u, (0x[0-9a-f]+)u, (0x[0-9a-f]+)u, (\d+), "(\w+)"\}', src)]
names = [(int(a), b) for a, b in re.findall(r'\{(\d+), "([a-z_]+)"\}', src.split('lce_wg195_biome_names[]')[1].split('};')[0])]
hdr = (HERE.parent / 'worldgen195.h').read_text()
R = {k: int(v, 16) for k, v in re.findall(r'#define LCE_WG195_RVA_(\w+)\s+(0x[0-9a-f]+)u', hdr)}
check(len(classes) == 25 and len(names) == 72, 'table sizes')

# ---- Biome storage --------------------------------------------------------------
pin(0x83ed2, 'mov', 'dword ptr [r13 + 0x68], esi')                       # id
check(rip_target(0x840aa) == R['BIOME_ARRAY'], 'ctor array lea')
pin(0x840cd, 'mov', 'qword ptr [rcx + rax*8], r13')                      # biomes[id] = this
check(rip_target(0x7f850) == R['BIOME_ARRAY'], 'getBiome(id) 0x7f850')
check(R['BIOME_ARRAY'] >= 0x1400000 + 0x8ac8, 'biome array is BSS (beyond file data, RW)')
pin(0x8414d, 'mov', 'qword ptr [r13 + 0x60], rbx')                       # decorator at +0x60
check(ins(0x84143).op_str == '0x86c00', 'BiomeDecorator ctor 0x86c00')
check(rip_target(0x83e7b) + 0x10 == R['BIOME_BASE_VT'], 'Biome ctor writes base vtable')
# decorate adapter
for a, m, o in [(0x85430, 'mov', 'r8, rcx'), (0x85433, 'mov', 'rcx, rdi'), (0x85436, 'mov', 'rdi, qword ptr [rcx + 0x60]'),
                (0x8543a, 'mov', 'rax, qword ptr [rdi]'), (0x8543d, 'mov', 'rax, qword ptr [rax + 0x10]'), (0x85441, 'jmp', 'rax')]:
    pin(a, m, o)
check(q(R['BIOME_BASE_VT'] + 0x58) == R['BIOME_DECORATE'], 'Biome vtable +0x58')
print('PASS biome storage: array 0x1409fc0 (BSS), id +0x68, decorator +0x60, decorate 0x85430')

# ---- registrations (static init 0x7f900): id + name before every subclass ctor --
ctor_of = {}
for vt, ti, dec, dim, name in classes:
    check(rtti(vt) == f'{len(name)}{name}', f'rtti {name}')
    check(q(vt - 8) == ti and q(vt - 16) == 0, f'typeinfo {name}')
    check(all(0 < q(vt + 8 * i) < TEXT_END for i in range(27)) and q(vt + 27 * 8) == 0, f'27 slots {name}')
    check(q(vt + 0x58) == dec, f'decorate slot {name}')
    refs = lea_refs(vt - 0x10)
    check(len(refs) >= 1, f'ctor ref {name}')
    for r in refs:   # every reference is a vptr store (ctor or dtor), never a comparison
        i = list(md.disasm(bytes(mem[r:r + 0x40]), r))[:8]
        reg = i[0].op_str.split(',')[0]
        k = next((n for n, x in enumerate(i) if x.mnemonic == 'add' and x.op_str == reg + ', 0x10'), None)
        check(k is not None and any(x.mnemonic == 'mov' and re.match(r'qword ptr \[r\w+\], ' + reg + '$', x.op_str) for x in i[k + 1:]) and
              not any(x.mnemonic in ('cmp', 'test') and reg in x.op_str for x in i), f'{name} ref {r:#x} is a vptr store')
    ctor_of[fstart(refs[0])] = name
check(sorted(ctor_of.values()) == sorted(c[4] for c in classes), 'one ctor per class')
regs = []; esi = None; last_name = None
code = bytes(mem[0x7f900:0x83e00])
for i in md.disasm(code, 0x7f900):
    if i.mnemonic == 'lea' and i.op_str.startswith('rsi, [rip'):
        last_name = i.address + i.size + int(i.op_str.split('+ ')[1].rstrip(']'), 16)
    if i.mnemonic == 'mov' and i.op_str.startswith('esi, '):   # capstone prints imm < 10 in decimal
        try: esi = int(i.op_str.split(', ')[1], 0)
        except ValueError: pass
    if i.mnemonic == 'xor' and i.op_str == 'esi, esi': esi = 0
    if i.mnemonic == 'call':
        try: t = int(i.op_str, 16)
        except ValueError: continue
        if t in ctor_of or t in (0x4a21d0, 0x4a2240):
            regs.append((esi, wstr(last_name)))
check(regs == names, f'registrations differ: {regs[:5]}...')
print(f'PASS 25 Biome classes (RTTI, 27 slots, +0x58, vptr-store-only refs) and 72 registrations (ids+names)')

# ---- decorate callers and pos semantics ------------------------------------------
pin(0x5091ff, 'mov', 'rax, qword ptr [rax + 0x58]'); pin(0x509220, 'call', 'qword ptr [rbp - 0x98]')
pin(0x50920f, 'mov', 'rsi, r13'); pin(0x509219, 'mov', 'rdx, r15')
pin(0x508bde, 'shl', 'esi, 4'); pin(0x508be1, 'xor', 'edx, edx'); pin(0x508be3, 'shl', 'ecx, 4'); pin(0x508bfc, 'call', '0x7e010')
pin(0x508c49, 'lea', 'r15, [r13 + 0x30]'); pin(0x508cb8, 'call', '0x596c70')
pin(0x2e6fc8, 'call', 'qword ptr [rax + 0x58]'); pin(0x6ded84, 'call', 'qword ptr [rbx + 0x58]')
srcs = {'20OverworldLevelSource': (0x1299240, 0x508bb0), '15FlatLevelSource': (0x1257f00, 0x2e6840),
        '17TheEndLevelSource': (0x12e0940, 0x6dec40), '17NetherLevelSource': (0x128f388, 0x4b8090),
        '21NetherFlatLevelSource': (0x128f2f8, 0x4b5530)}
for n, (vt, pp) in srcs.items():
    check(rtti(vt) == n and q(vt + 0x18) == pp, f'{n} postProcess slot')
for n in ('17NetherLevelSource', '21NetherFlatLevelSource'):
    f = srcs[n][1]
    check(not any('+ 0x58]' in c.op_str or c.op_str == '0x85430' for c in calls_in(f, 0xbc0)), f'{n} never decorates')
check(rtti(R['SERVER_LEVEL_VT']) == '11ServerLevel', 'ServerLevel vtable')
print('PASS decorate callers 0x509220/0x2e6fc8/0x6ded84, pos=(cx*16,0,cz*16), Random=source+0x30; nether never decorates')

# ---- Random -----------------------------------------------------------------------
pin(0x596c75, 'movabs', 'rcx, 0x5deece66d'); pin(0x596c87, 'mov', 'qword ptr [rdi], rcx'); pin(0x596c8a, 'mov', 'byte ptr [rdi + 8], 0')
pin(0x593952, 'movabs', 'r9, 0xffffffffffff')
print('PASS Java Random: seed +0 (48-bit), flag +8; setSeed 0x596c70, nextInt 0x593950')

# ---- OreFeature -------------------------------------------------------------------
check(rtti(R['ORE_VT']) == '10OreFeature' and q(R['ORE_VT'] + 0x10) == R['ORE_PLACE'], 'OreFeature vtable/place')
pin(0x271a2b, 'mov', 'qword ptr [rdi], rax'); pin(0x271a2e, 'mov', 'byte ptr [rdi + 8], sil')
pin(0x503e1e, 'xor', 'esi, esi'); pin(0x503e26, 'call', '0x271a20')
check(rip_target(0x503e2b) + 0x10 == R['ORE_VT'], 'ctor vtable')
pin(0x503e8b, 'mov', 'qword ptr [rbx + 0xc], rcx'); pin(0x503e8f, 'mov', 'dword ptr [rbx + 0x14], r14d')
pin(0x503e93, 'mov', 'qword ptr [rbx + 0x18], rax'); check(rip_target(0x503e84) == R['STONE_PREDICATE'], 'predicate')
check(R['STONE_PREDICATE'] in REL and rtti(q(R['STONE_PREDICATE']) ) == 'N10OreFeature21NaturalStonePredicateE', 'const predicate')
pin(0x503fc9, 'add', 'eax, 8'); pin(0x50403e, 'add', 'eax, 8')
pin(0x503ff6, 'vmulss', 'xmm0, xmm0, dword ptr [rip + 0x99cde6]')
check(struct.unpack_from('<f', mem, 0xea0de4)[0] == 0.125, 'size/8')
check(struct.unpack_from('<d', mem, 0xea0608)[0] == 0.0625, 'size/16')
pin(0x5048ec, 'mov', 'rdi, qword ptr [rcx + 0x18]'); pin(0x5048f3, 'call', 'qword ptr [rax]')
pin(0x504918, 'mov', 'r8d, dword ptr [rax + 0xc]'); pin(0x50491c, 'mov', 'r9d, dword ptr [rax + 0x10]')
pin(0x504920, 'mov', 'dword ptr [rsp], 0x74'); pin(0x504927, 'call', '0x3efa80')
print('PASS OreFeature fields (+8,+0xc,+0x10,+0x14,+0x18), +8 origin shift, predicate call, write flags 0x74')

# ---- setTileAndData / heightmap ---------------------------------------------------
pin(0x3efaa4, 'cmp', 'r14d, 0xff'); pin(0x3efadf, 'test', 'al, 0x40')
pin(0x3efab8, 'mov', 'rdi, qword ptr [rbx + 0x1f8]'); pin(0x3efad6, 'call', 'qword ptr [rax + 0x58]')
pin(0x3f2106, 'call', 'qword ptr [rax + 0x148]'); pin(0x3f2124, 'call', 'qword ptr [rax + 0x58]')
pin(0x2a4977, 'mov', 'eax, 3'); pin(0x2a497c, 'sbb', 'eax, 0')     # Feature::setBlock flags 2|3
print('PASS Level::setTileAndData 0x3efa80 (y<=255, 0x40 raw path), getHeightmap 0x3f20c0, Feature::setBlock flags')

# ---- TreeFeature / LakeFeature / FlowerFeature ---------------------------------------
check(rtti(R['TREE_VT']) == '11TreeFeature' and q(R['TREE_VT'] + 0x10) == R['TREE_PLACE'], 'tree vt')
pin(0x6f5b39, 'call', '0x1c320'); check(rip_target(0x6f5b3e) + 0x10 == R['TREE_VT'], 'tree ctor vt')
for a, o in [(0x1c350, 'dword ptr [rbx + 0xc], r13d'), (0x1c354, 'byte ptr [rbx + 0x10], r15b'),
             (0x1c358, 'qword ptr [rbx + 0x18], r12'), (0x1c35c, 'qword ptr [rbx + 0x20], r14')]:
    pin(a, 'mov', o)
pin(0x1c33d, 'call', '0x271a20')
check(rtti(R['LAKE_VT']) == '11LakeFeature' and q(R['LAKE_VT'] + 0x10) == R['LAKE_PLACE'], 'lake vt')
pin(0x3db8f2, 'mov', 'qword ptr [rbx + 0x10], r14'); check(rip_target(0x3db8e4) + 0x10 == R['LAKE_VT'], 'lake ctor vt')
pin(0x3db933, 'mov', 'esi, 0xfffffff8'); pin(0x3db94d, 'mov', 'ecx, 0xfffffff8'); pin(0x3db959, 'call', '0x7ea10')
check(rtti(0x12589b8) == '13FlowerFeature' and rip_target(0x2ea35f) + 0x10 == 0x12589b8, 'flower ctor 0x2ea330')
pin(0x2ea382, 'call', 'qword ptr [rax + 0x4e8]')
print('PASS TreeFeature ctor 0x6f5b30/0x1c320 fields, LakeFeature ctor 0x3db8d0 (+0x10 Block*), lake -8 shift, FlowerFeature needs Block vt+0x4e8')
print(f'audit: {count} pins, 0 failures')
