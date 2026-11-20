#!/usr/bin/env python3
"""Static audit of every RVA pinned by entities195 against the 1.95 analysis image.

Run with the capstone venv:
  .../venv-analysis/bin/python tests/audit.py           check base_mobs195.inc + pins
  .../venv-analysis/bin/python tests/audit.py --emit    regenerate base_mobs195.inc

Read-only: never touches the console or the image file.
"""
import hashlib, json, re, struct, sys
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86_const import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_RIP, X86_REG_RBP

HERE = Path(__file__).resolve().parent
MOD = HERE.parent
IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
p = IMAGE.read_bytes()
assert hashlib.sha256(p).hexdigest() == 'c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f'
SEGS = [(0, 0x4000, 0x10c0354), (0x1200000, 0x10c8000, 0x14d928), (0x1400000, 0x1218000, 0x8ac8)]
rel = {}
for i in range(0x12aaf70 + 0xb550, 0x12aaf70 + 0xb550 + 0x3aa2b0, 24):
    a, k, v = struct.unpack_from('<QQq', p, i)
    if k & 0xffffffff == 8:
        rel[a] = v
cs = Cs(CS_ARCH_X86, CS_MODE_64); cs.detail = True
fails = []; passes = 0


def v2o(v):
    for va, off, n in SEGS:
        if va <= v < va + n:
            return off + v - va
    return None


def q(v):
    if v in rel:
        return rel[v]
    o = v2o(v)
    return struct.unpack_from('<Q', p, o)[0] if o is not None else None


def dis(a, n):
    o = v2o(a)
    return list(cs.disasm(p[o:o + n], a))


def ripref(i):
    for op in i.operands:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            return i.address + i.size + op.mem.disp
    return None


def wstr(v, maxn=80):
    o = v2o(v); s = ''
    for k in range(maxn):
        c = struct.unpack_from('<H', p, o + 2 * k)[0]
        if not c:
            break
        s += chr(c)
    return s


def cstr(v):
    o = v2o(v); return p[o:p.index(b'\0', o)].decode('latin1')


def rtti(vt):
    try:
        return cstr(q(q(vt - 8) + 8))
    except Exception:
        return '?'


def vtlen(vt):
    n = 0
    while vt + 8 * n in rel and 0 < rel[vt + 8 * n] < 0x10c0354:
        n += 1
    return n


def check(cond, what):
    global passes
    if cond:
        passes += 1
    else:
        fails.append(what)


R64 = {'eax': 'rax', 'ebx': 'rbx', 'ecx': 'rcx', 'edx': 'rdx', 'esi': 'rsi', 'edi': 'rdi'}


def full(i, reg):
    n = i.reg_name(reg)
    if n in R64:
        return R64[n]
    if n.startswith('r') and n[-1] in 'dwb' and n[1:-1].isdigit():
        return n[:-1]
    return n


def registrations():
    """Every call 0x28d9c0(factory, id, &name, mask, &oldName) in the initializer 0x28dc10."""
    st = {}; strs = []; out = []
    for i in dis(0x28dc10, 0x8000):
        if i.mnemonic == 'ret':
            break
        ops = i.operands
        if i.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == X86_OP_REG:
            d = full(i, ops[0].reg)
            if ops[1].type == X86_OP_IMM: st[d] = ops[1].imm & 0xffffffffffffffff
            elif ops[1].type == X86_OP_REG: st[d] = st.get(full(i, ops[1].reg))
            else: st.pop(d, None)
        elif i.mnemonic == 'lea' and ops[0].type == X86_OP_REG:
            d = full(i, ops[0].reg); m = ops[1].mem
            if m.base == X86_REG_RIP: st[d] = i.address + i.size + m.disp
            elif m.index == 0 and st.get(full(i, m.base)) is not None: st[d] = (st[full(i, m.base)] + m.disp) & 0xffffffffffffffff
            else: st.pop(d, None)
        elif i.mnemonic == 'call':
            t = ops[0].imm if ops[0].type == X86_OP_IMM else None
            if t == 0x67840:
                strs.append(st.get('rsi'))
            elif t == 0x28d9c0:
                out.append(dict(id=st['rsi'] & 0xffffffff, mask=st.get('rcx'), factory=st.get('rdi'),
                                name=wstr(strs[-2]) if len(strs) >= 2 else wstr(strs[-1]), old=wstr(strs[-1])))
                strs = []
            for r in ('rax', 'rcx', 'rdx', 'rsi', 'rdi', 'r8', 'r9', 'r10', 'r11'):
                st.pop(r, None)
    return out


def gettype(vt):
    f = q(vt + 0x60)
    if not f or v2o(f) is None:
        return None
    ins = dis(f, 16)
    if len(ins) >= 2 and ins[1].mnemonic == 'ret' and ins[0].mnemonic in ('mov', 'movabs') and ins[0].op_str.split(',')[0] in ('eax', 'rax'):
        return ins[0].operands[1].imm
    return None


def candidates(a, n):
    out = []
    for i in dis(a, n):
        if i.mnemonic == 'ret':
            break
        if i.mnemonic == 'lea' and ripref(i):
            for d in (0x10, 0):
                if ripref(i) + d in rel and rtti(ripref(i) + d) != '?':
                    out.append(ripref(i) + d)
    return out


def product(r):
    ctor = None; size = None
    for i in dis(r['factory'], 0x40):
        if size is None and i.mnemonic == 'mov' and i.op_str.startswith('edi, '): size = i.operands[1].imm
        if i.mnemonic == 'call' and i.operands[0].type == X86_OP_IMM and i.operands[0].imm != 0xe6cfd0:
            ctor = i.operands[0].imm; break
    good = [vt for vt in candidates(r['factory'], 0x100) + (candidates(ctor, 0x600) if ctor else []) if gettype(vt) == r['mask']]
    return ctor, size, (good[0] if good else None)


def eggs():
    st = {}; last = None; out = {}
    for i in dis(0x291940, 0x2000):
        ops = i.operands
        if i.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == X86_OP_REG:
            d = full(i, ops[0].reg)
            if ops[1].type == X86_OP_IMM: st[d] = ops[1].imm & 0xffffffff
            else: st.pop(d, None)
        if i.mnemonic == 'xor' and ops[0].type == X86_OP_REG and ops[0].reg == ops[1].reg: st[full(i, ops[0].reg)] = 0
        if i.mnemonic == 'lea' and ripref(i): st[full(i, ops[0].reg)] = ripref(i)
        if i.mnemonic == 'call' and ops[0].type == X86_OP_IMM:
            if ops[0].imm == 0x67840: last = wstr(st.get('rsi'))
            elif ops[0].imm == 0x28cba0: out[last] = (st.get('rsi'), st.get('rcx'), st.get('r8'), st.get('r9'))
            elif ops[0].imm == 0x2cf150: break
    return out


def fnv1_ids(name):
    h = 0x811c9dc5
    for c in ('IDS_' + name.upper()).encode('utf-16-le'):
        h = ((h ^ c) * 0x01000193) & 0xffffffff
    return h


EXCLUDED = {'ender_dragon', 'wither', 'giant', 'endermite', 'pillager', 'ravager', 'armor_stand'}


def base_rows():
    regs = registrations(); eg = eggs(); rows = []
    check(len(regs) == 96, 'registry initializer has 96 registrations (got %d)' % len(regs))
    for r in regs:
        if r['mask'] is None or r['name'] in EXCLUDED:
            continue
        ctor, size, vt = product(r)
        if not vt or vtlen(vt) < 470:
            continue
        e = eg.get(r['name'])
        rows.append(dict(r, vtable=vt, slots=vtlen(vt), size=size, egg=e))
    return regs, rows


def emit(rows):
    lines = ['/* GENERATED by tests/audit.py --emit from image c675e6ce...029f. One row per vanilla LivingEntity',
             ' * that entities195 can clone: EntityIO registration in 0x28dc10 (call 0x28d9c0: factory, id, name,',
             ' * old name, GetType mask), the factory\'s final primary vtable (its GetType slot +0x60 returns exactly',
             ' * the registered mask), primary slot count, allocation size, and the spawn-egg row from the same',
             ' * initializer (call 0x28cba0: IDS_ name hash, colour enums). Do not edit by hand. */']
    for r in rows:
        e = r['egg'] or (0, 0, 0, 0)
        lines.append('    {"%s", "%s", %du, 0x%xull, 0x%xu, 0x%xu, %du, 0x%xu, 0x%xu, %du, %du},' % (
            r['name'], r['old'], r['id'], r['mask'], r['factory'], r['vtable'], r['slots'], r['size'], e[0], e[1], e[2]))
    return '\n'.join(lines) + '\n'


def code_pins():
    def first(a, n=1):
        return ['%s %s' % (i.mnemonic, i.op_str) for i in dis(a, 0x40)[:n]]
    # Registry function prologue and its global users
    check(any(ripref(i) == 0x1416148 for i in dis(0x28d9c0, 0x40)), '0x28d9c0 uses class->factory map 0x1416148')
    check(any(ripref(i) == 0x1415fa8 for i in dis(0x28d9c0, 0x80)), '0x28d9c0 uses EntityIO registry 0x1415fa8')
    check(any(i.mnemonic == 'call' and i.op_str == '0xba480' for i in dis(0x28d9c0, 0xc0)), '0x28d9c0 calls registry register 0xba480')
    check(any(ripref(i) == 0x1416198 for i in dis(0x28d9c0, 0x100)), '0x28d9c0 grows the old-name vector 0x1416198')
    # 0xba480: reg+0x90 map[mask]=id, reg+0xe8 vector id->mask, then put via vt[1]
    L = dis(0xba480, 0x180)
    check(any(i.op_str == 'rdi, [r14 + 0x90]' for i in L), '0xba480 writes mask->id map at +0x90')
    check(any(i.op_str == 'rax, qword ptr [r14 + 0xe8]' for i in L), '0xba480 id->mask vector at +0xe8')
    # create-by-mask requires bit 27 and uses factory map node +0x18
    L = dis(0x283770, 0x80)
    check(any(i.mnemonic == 'bt' and i.op_str == 'esi, 0x1b' for i in L), '0x283770 requires mask bit 27')
    check(any(i.op_str == 'rcx, qword ptr [rax + 0x18]' for i in L), '0x283770 factory at node+0x18')
    # getEncodeId (save id) is GetType-driven
    L = dis(0x27bb60, 0x60)
    check(any(i.op_str == 'qword ptr [rax + 0x60]' for i in L) and any(i.op_str.endswith('[rbx + 0x100]') for i in L),
          'save id 0x27bb60 = registry+0x100 lookup of GetType()')
    # newById uses registry +0xe8 vector
    check(any(i.op_str == 'rax, qword ptr [rcx + 0xe8]' for i in dis(0x28d1b0, 0x30)), 'newById 0x28d1b0 reads +0xe8')
    # factory ABI Entity*(Level*)
    L = dis(0x2943c0, 0x30)
    check(['%s %s' % (i.mnemonic, i.op_str) for i in L[4:7]] == ['mov r14, rdi', 'mov edi, 0xb38', 'call 0xe6cfd0'], 'creeper factory = alloc 0xb38 + ctor(level)')
    # Mob::_init tail-calls registerGoals +0xcd0 after the final vptr is written
    check(any(i.mnemonic == 'jmp' and i.op_str == 'qword ptr [rax + 0xcd0]' for i in dis(0x486b80, 0x50)), '0x486b80 tail-jumps registerGoals +0xcd0')
    check(q(0x1234ce8 + 0xcd0) == 0x1e9120, 'Creeper registerGoals 0x1e9120 at +0xcd0')
    # LivingEntity::_init: registerAttributes +0x860, setHealth +0x9c0(getMaxHealth +0xa90)
    L = ['%s %s' % (i.mnemonic, i.op_str) for i in dis(0x41e290, 0x40)]
    check('call qword ptr [rax + 0x860]' in L and 'mov r14, qword ptr [rax + 0x9c0]' in L and 'call qword ptr [rax + 0xa90]' in L,
          '0x41e290 setHealth(+0x9c0) of getMaxHealth(+0xa90)')
    L = ['%s %s' % (i.mnemonic, i.op_str) for i in dis(0x427e20, 0x30)]
    check('call qword ptr [rax + 0xab8]' in L and 'call qword ptr [rcx + 0x28]' in L, 'getMaxHealth: getAttribute +0xab8 -> AttributeInstance +0x28')
    check(q(0x1234ce8 + 0xa90) == 0x427e20, 'getMaxHealth at +0xa90')
    # AttributeInstance (ModifiableAttributeInstance 0x1287fd0): +0x18 base getter, +0x20 setBaseValue
    check(first(q(0x1287fd0 + 0x18)) == ['vmovsd xmm0, qword ptr [rdi + 0x158]'], 'AttributeInstance +0x18 getBaseValue')
    check(any(i.op_str == 'qword ptr [rbp - 0x10], xmm0' for i in dis(q(0x1287fd0 + 0x20), 0x20)), 'AttributeInstance +0x20 setBaseValue(double)')
    check(rtti(0x1287fd0) == '27ModifiableAttributeInstance', 'AttributeInstance vtable RTTI')
    # attribute keys: ids from SharedMonsterAttributes init 0x61d8c3
    keys = {}
    L = dis(0x61d8c3, 0x260); pending = None
    for i in L:
        if i.mnemonic == 'mov' and i.op_str.startswith('edx, '): pending = i.operands[1].imm
        if i.mnemonic == 'lea' and ripref(i) and 0x142ff40 <= ripref(i) <= 0x142ff80: keys[ripref(i)] = pending
    check(keys.get(0x142ff48) == 1 and keys.get(0x142ff58) == 3 and keys.get(0x142ff68) == 4, 'attribute keys +0x48 id1, +0x58 id3 (speed), +0x68 id4 (attack)')
    # addGoal + selectors
    L = ['%s %s' % (i.mnemonic, i.op_str) for i in dis(0x1e9120, 0x30)]
    check('lea r14, [r12 + 0x800]' in L, 'goalSelector at Mob+0x800')
    check(any(i.op_str == 'r14, [r12 + 0x850]' for i in dis(0x1e92cf, 0x10)), 'targetSelector at Mob+0x850')
    L = ['%s %s' % (i.mnemonic, i.op_str) for i in dis(0x2d6260, 0xa0)]
    check('mov edi, 0x10' in L and 'mov byte ptr [rax + 0xd], cl' in L and 'mov qword ptr [rax], rbx' in L, 'addGoal 0x2d6260 WrappedGoal{goal,prio,running,canDelete}')
    for ctor, vt in ((0x2e9180, 0x1258318), (0x77f550, 0x12f2af0), (0x43a0c0, 0x127b280), (0x5985d0, 0x12af780), (0x462f20, 0x127ffb0),
                     (0x4a7970, 0x128d728), (0x370ab0, 0x126b7e8), (0x3d290, 0x120b618), (0xe2950, 0x121b4e8)):
        check(any(ripref(i) == vt - 0x10 for i in dis(ctor, 0x80)), 'goal ctor 0x%x writes %s' % (ctor, rtti(vt)))
    check(cstr(q(0x12a5440 + 8)) == '6Player', 'typeinfo Player 0x12a5440')
    # sounds / loot
    check(q(0x12fab20 + 0xd70) == 0x7a7b50 and q(0x12fab20 + 0xa18) == 0x7a7b60 and q(0x12fab20 + 0xa20) == 0x7a7b70, 'zombie ambient/hurt/death slots')
    check(['%s' % i.mnemonic for i in dis(0x7a7b50, 12)][:3] == ['lea', 'mov', 'ret'], 'sound getter = lea/mov/ret (no this)')
    # renderer dispatcher
    L = ['%s %s' % (i.mnemonic, i.op_str) for i in dis(0xc72ae9, 0x30)]
    check('mov edi, 0x140' in L and 'call 0xbd36c0' in L and 'mov qword ptr [r15 + 0x220], rbx' in L, 'dispatcher 0x140 bytes, ctor 0xbd36c0, Minecraft+0x220')
    L = ['%s %s' % (i.mnemonic, i.op_str) for i in dis(0xbd54c0, 0x60)]
    check('test esi, 0xc800000' in L and 'call 0xbe1310' in L and 'mov rax, qword ptr [rax + 0x18]' in L, 'getRenderer 0xbd54c0: unchecked map find')
    check(any(i.op_str == '0xbd5220' for i in dis(0xbd3c9f, 8)), 'renderer map operator[] 0xbd5220')
    # spawn lists
    check(q(0x1214758 - 8) == 0x1214780 and cstr(q(0x1214780 + 8)) == 'N5Biome14MobSpawnerDataE', 'MobSpawnerData vtable')
    L = ['%s %s' % (i.mnemonic, i.op_str) for i in dis(0x83d63, 0x80)]
    check('mov edi, 0x48' in L and 'mov qword ptr [rax + 0x10], 0x2d680005' in L and 'mov rcx, qword ptr [rbx + 0xa0]' in L, 'MobSpawnerData 0x48, mask +0x10, creature list end +0xa0')
    L = ['%s %s' % (i.mnemonic, i.op_str) for i in dis(0x844d6, 0x30)]
    check(any(x.startswith('mov qword ptr [rax + 0x10], 0x5d560040') for x in L) and 'mov rcx, qword ptr [r13 + 0x80]' in L, 'monster list (spider) end +0x80')
    check(any(i.op_str == 'rcx, qword ptr [r15 + 0x120]' for i in dis(0x4d65b3, 0x20)), 'water list end +0x120')
    check(any(i.op_str.endswith('+ 0x180]') for i in dis(0x84bdf, 0x20)), 'ambient list end +0x180')
    # egg name code = FNV-1 of UTF-16LE "IDS_<NAME>"
    eg = eggs()
    check(len(eg) == 56 and sum(v[0] == fnv1_ids(n) for n, v in eg.items()) == 49, 'egg code = FNV-1a(UTF-16LE "IDS_"+NAME) string id (49/56 by registry name; 7 use other IDS_ names)')
    # mask high bits in use (only 32/33)
    hi = set()
    code = p[0x4000:0x4000 + 0x10c0354]
    for m in re.finditer(rb'\x48\xb8(.{8})\xc3', code, re.S):
        imm = struct.unpack('<Q', m.group(1))[0]
        if imm >> 32 and imm & 0x08000000 and imm >> 32 < 0x10: hi.add(imm >> 32)
    check(hi <= {1, 2}, 'only bits 32/33 used above 32 in constant GetType masks: %s' % sorted(hi))


def main():
    regs, rows = base_rows()
    for r in rows:
        check(rtti(r['vtable']) != '?', 'RTTI for %s' % r['name'])
    code_pins()
    text = emit(rows)
    inc = MOD / 'base_mobs195.inc'
    if '--emit' in sys.argv:
        inc.write_text(text); print('wrote', inc, len(rows), 'rows')
    else:
        check(inc.read_text() == text, 'base_mobs195.inc matches the image (%d rows)' % len(rows))
    used = sorted(r['id'] for r in regs)
    check(all(i not in used for i in range(150, 200)), 'ids 150..199 unused by vanilla')
    for f in fails: print('FAIL', f)
    print('entities195 audit: %d pins PASS, %d FAIL, %d base mobs' % (passes, len(fails), len(rows)))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
