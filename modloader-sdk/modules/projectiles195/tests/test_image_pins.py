"""Pin every 1.95 RVA, layout and instruction projectiles195 relies on against the analysis image."""
import re, struct, sys
from pathlib import Path
import hashlib
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OPT_SYNTAX_ATT

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
img = IMAGE.read_bytes()
checks = fails = 0


def check(c, what):
    global checks, fails
    checks += 1
    if not c:
        fails += 1
        print('FAIL', what)


phoff = struct.unpack_from('<Q', img, 0x20)[0]
segs = []
for i in range(struct.unpack_from('<H', img, 0x38)[0]):
    t, fl, off, va, pa, fs, ms, al = struct.unpack_from('<IIQQQQQQ', img, phoff + i * 56)
    segs.append((t, fl, off, va, fs, ms))
LOADS = [s for s in segs if s[4] and s[0] in (1, 0x61000010)]


def v2o(v):
    for t, fl, off, va, fs, ms in LOADS:
        if va <= v < va + fs: return off + v - va
    raise KeyError(hex(v))


def q(v): return struct.unpack_from('<Q', img, v2o(v))[0]


cs = Cs(CS_ARCH_X86, CS_MODE_64); cs.syntax = CS_OPT_SYNTAX_ATT


def ins(v):
    i = next(cs.disasm(img[v2o(v):v2o(v) + 16], v))
    return (i.mnemonic + ' ' + i.op_str).replace(' ', '')


def pin(v, text):
    got = ins(v)
    check(got == text.replace(' ', ''), f'{v:#x}: expected {text!r}, got {got!r}')


def rtti(vt):
    ti = q(vt - 8); nm = q(ti + 8); o = v2o(nm)
    return img[o:img.index(b'\0', o)].decode()

check(hashlib.sha256(img).hexdigest() == 'c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f', 'image hash')

ITEM_VT, SNOWITEM_VT, BALL_VT = 0x126e638, 0x12cae00, 0x12ca530
# vtables live in the read-only data segment (RELRO), not segment 0: patch_ro8 territory, no .text write
seg1 = [s for s in LOADS if s[3] == 0x1200000][0]
for vt in (SNOWITEM_VT, BALL_VT):
    check(seg1[3] <= vt < seg1[3] + seg1[4], f'{vt:#x} in the RELRO segment')
check(rtti(SNOWITEM_VT) == '12SnowballItem', 'SnowballItem RTTI')
check(rtti(BALL_VT) == '8Snowball', 'Snowball RTTI')
check(rtti(ITEM_VT) == '4Item', 'Item RTTI')
# SnowballItem = Item + vtable: the 57 slots differ only in class id (+0x10), destructors (+0x18/+0x20) and use (+0x40)
diff = [i * 8 for i in range(57) if q(SNOWITEM_VT + 8 * i) != q(ITEM_VT + 8 * i)]
check(diff == [0x10, 0x18, 0x20, 0x40], f'SnowballItem slot diff {[hex(d) for d in diff]}')
check(q(ITEM_VT + 0x40) == 0x3aedb0 and q(SNOWITEM_VT + 0x40) == 0x637970, 'use slots')
check(q(SNOWITEM_VT - 16) == 0 and q(BALL_VT - 16) == 0, 'offset-to-top 0')
pin(0x68df96, 'leaq 0xbe068b(%rip), %rax')          # SnowballItem dtor resets the Item vptr: no extra fields
check(0x68df9d + 0xbe068b + 0x10 == ITEM_VT, 'SnowballItem dtor -> Item vtable')
# Snowball primary vtable
check(q(BALL_VT + 0x860) == 0x6e15a0, 'shoot slot')
check(q(BALL_VT + 0x878) == 0x6e36c0, 'getOwner slot')
check(q(BALL_VT + 0x880) == 0x637490, 'onHit slot')
check(struct.unpack_from('<q', img, v2o(BALL_VT + 0x888))[0] == -0x440, 'secondary vtable offset-to-top after +0x880')
# SnowballItem::use 0x637970: builds the Snowball and shoots it through vt +0x860
pin(0x637b46, 'callq 0x6371a0')                      # Snowball(level, shared_ptr<LivingEntity>)
pin(0x637bef, 'movq 0x860(%rax), %r13')
pin(0x637c20, 'vxorps %xmm2, %xmm2, %xmm2')
pin(0x637c24, 'movq %rbx, %rdi')
pin(0x637c27, 'movq %r15, %rsi')
pin(0x637c2a, 'vmovss 0x1a0(%rax), %xmm1')
pin(0x637c32, 'vmovss 0x1a4(%rax), %xmm0')
pin(0x637c3a, 'callq *%r13')
for at, val in ((0x637c10 + 8 + 0x8764ec, 1.5), (0x637c18 + 8 + 0x8764e8, 1.0)):
    check(struct.unpack_from('<f', img, v2o(at))[0] == val, f'shoot constant {val}')
pin(0x637c10, 'vmovss 0x8764ec(%rip), %xmm3')
pin(0x637c18, 'vmovss 0x8764e8(%rip), %xmm4')
# onHit 0x637490: (this, HitResult*), hit entity at +0x20, level +0x150, isClientSide +0x248, position +0x170
pin(0x6374ab, 'movq %rdi, %r13')
pin(0x6374ae, 'movq %rsi, %r14')
pin(0x6374c5, 'cmpq $0, 0x20(%r14)')
pin(0x6374d0, 'movq 0x150(%r13), %rax')
pin(0x6374d7, 'cmpb $0, 0x248(%rax)')
pin(0x637823, 'vmovsd 0x170(%r13), %xmm0')
pin(0x637854, 'movb $1, %al')

print(f'projectiles195 image pins: {checks} checks, {fails} failures')
sys.exit(1 if fails else 0)
