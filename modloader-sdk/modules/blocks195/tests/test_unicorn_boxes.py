"""API 1.11 multi-box blocks: run the real retail 1.95 consumers of Block vt +0x190 in Unicorn against a fake block whose
private vtable returns a module-style box list {AABB **data; uint32 count} (what b195_box_list_dispatch returns).

- StairBlock +0x1b0 0x6765c0 (collision): must call +0x1b8 addAABB once per non-null box, in order, with that AABB.
- The render shape 10 tesselator 0xb3b160: must hand every non-null box to 0xb5be10 with the renderer shape floats
  (+0x84 x0, +0x88 x1, +0x8c y0, +0x90 y1, +0x94 z0, +0x98 z1) set from that box.
- Block +0x188 0x92c90 (face culling): a face no box reaches is rendered (returns 1) without asking the neighbour.
Only the callees outside the path are stubbed (listed per run). Code paths and layouts only, not gameplay.
"""
import struct, sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
img = IMAGE.read_bytes()
STACK, DONE, OBJ, FAKE = 0x4000000, 0x5000000, 0x6000000, 0x7000000
checks = fails = 0


def check(c, what):
    global checks, fails
    checks += 1
    if not c:
        fails += 1
        print('FAIL', what)


def machine():
    u = Uc(UC_ARCH_X86, UC_MODE_64)
    u.mem_map(0, 0x1500000)
    u.mem_write(0, img[0x4000:0x4000 + 0x10c0354])
    u.mem_write(0x1200000, img[0x10c8000:0x10c8000 + 0x14d928])
    u.mem_write(0x1400000, img[0x1218000:0x1218000 + 0x8ac8])
    for a in (STACK, DONE, OBJ, FAKE):
        u.mem_map(a, 0x100000)
    return u


def q(u, a): return struct.unpack('<Q', u.mem_read(a, 8))[0]
def f32(u, a): return struct.unpack('<f', u.mem_read(a, 4))[0]
def wq(u, a, v): u.mem_write(a, struct.pack('<Q', v))


def call(u, fn, args=(), stack_args=(), stubs=None, limit=400000):
    stubs = stubs or {}
    for r, v in zip([UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RCX, UC_X86_REG_R8, UC_X86_REG_R9], args):
        u.reg_write(r, v & 0xffffffffffffffff)
    sp = STACK + 0xf000
    wq(u, sp, DONE)
    for i, v in enumerate(stack_args):
        wq(u, sp + 8 + 8 * i, v)
    u.reg_write(UC_X86_REG_RSP, sp)
    u.reg_write(UC_X86_REG_FS_BASE, STACK + 0x80000)

    def hook(m, a, size, _):
        if a == DONE:
            m.emu_stop(); return
        if a in stubs:
            rv = stubs[a](m)
            m.reg_write(UC_X86_REG_RAX, rv or 0)
            s = m.reg_read(UC_X86_REG_RSP)
            m.reg_write(UC_X86_REG_RIP, q(m, s)); m.reg_write(UC_X86_REG_RSP, s + 8)
    h = u.hook_add(UC_HOOK_CODE, hook)
    u.emu_start(fn, DONE + 0x10, count=limit)
    u.hook_del(h)
    return u.reg_read(UC_X86_REG_RAX)


# Fake block: vtable at FAKE (slots are stub addresses in FAKE+0x8000.., each a `ret` the hook intercepts).
STUB = FAKE + 0x8000
def fake_block(u, boxes, list_null_at=None):
    vt, block, lst, data = FAKE + 0x100, OBJ, OBJ + 0x800, OBJ + 0x900
    for i in range(0x4e0 // 8):
        wq(u, vt + 8 * i, STUB + 8 * i)
    u.mem_write(STUB, b'\xc3' * 0x4e0)
    wq(u, block, vt)
    aabbs = []
    for k, b in enumerate(boxes):
        a = OBJ + 0x1000 + 0x40 * k
        u.mem_write(a, struct.pack('<6d', *[c / 16 for c in b]))
        aabbs.append(a)
    ptrs = [0 if k == list_null_at else a for k, a in enumerate(aabbs)]
    u.mem_write(data, struct.pack(f'<{len(ptrs)}Q', *ptrs))
    u.mem_write(lst, struct.pack('<QII', data, len(ptrs), 0))
    return block, vt, lst, aabbs


def slot(o): return STUB + o

STAIRS = [(0, 0, 0, 16, 8, 16), (0, 8, 8, 16, 16, 16), (6, 0, 6, 10, 16, 10)]

# ---- collision: StairBlock +0x1b0 over our list (one entry null: skipped)
u = machine()
block, vt, lst, aabbs = fake_block(u, STAIRS, list_null_at=2)
added, seen_state = [], []
STATE, LEVEL, POS, MASK, LIST, ENTITY = 0x11110, 0x22220, 0x33330, 0x44440, 0x55550, 0x66660
def box_list(m):
    seen_state.append((m.reg_read(UC_X86_REG_RSI), m.reg_read(UC_X86_REG_RDX), m.reg_read(UC_X86_REG_RCX))); return lst
def add_aabb(m):
    added.append((m.reg_read(UC_X86_REG_RSI), m.reg_read(UC_X86_REG_RDX), m.reg_read(UC_X86_REG_RCX), m.reg_read(UC_X86_REG_R8)))
call(u, 0x6765c0, (block, STATE, LEVEL, POS, MASK, LIST), stack_args=(ENTITY, 1),
     stubs={slot(0x190): box_list, slot(0x1b8): add_aabb, slot(0x60): lambda m: STATE})
check(seen_state == [(STATE, LEVEL, POS)], f'0x6765c0 asks +0x190 once with (state, level, pos): {seen_state}')
check([a[3] for a in added] == aabbs[:2], f'0x6765c0 adds each non-null box via +0x1b8 (r8 = AABB): {added}')
check(all(a[:3] == (POS, MASK, LIST) for a in added), '0x6765c0 +0x1b8 args (pos, mask, list)')
u = machine()
block, vt, lst, aabbs = fake_block(u, STAIRS)
added.clear(); seen_state.clear()
call(u, 0x6765c0, (block, STATE, LEVEL, POS, MASK, LIST), stack_args=(ENTITY, 0),
     stubs={slot(0x190): box_list, slot(0x1b8): add_aabb, slot(0x60): lambda m: 0x77770})
check(seen_state and seen_state[0][0] == 0x77770, 'flag 0: the state comes from +0x60 (actual state) first')
check([a[3] for a in added] == aabbs, 'all three boxes added')

# ---- rendering: shape 10 tesselator 0xb3b160 (renderer at OBJ+0x4000, level at +8)
u = machine()
block, vt, lst, aabbs = fake_block(u, STAIRS, list_null_at=1)
rend = OBJ + 0x4000
u.mem_write(rend, bytes(0x200)); wq(u, rend + 8, LEVEL)
state = OBJ + 0x6000
svt = FAKE + 0x4000
for i in range(0x60): wq(u, svt + 8 * i, STUB + 0x800 + 8 * i)
u.mem_write(STUB + 0x800, b'\xc3' * 0x300)
wq(u, state, svt)
drawn = []
def tess(m):
    r = m.reg_read(UC_X86_REG_RDI)
    drawn.append(tuple(round(f32(m, r + o) * 16) for o in (0x84, 0x8c, 0x94, 0x88, 0x90, 0x98)) + (m.reg_read(UC_X86_REG_RSI) == state,))
asked = []
r = call(u, 0xb3b160, (rend, state, POS), stubs={STUB + 0x800 + 0x170: lambda m: block,
                                                 slot(0x190): lambda m: (asked.append((m.reg_read(UC_X86_REG_RDI), m.reg_read(UC_X86_REG_RSI), m.reg_read(UC_X86_REG_RDX), m.reg_read(UC_X86_REG_RCX))), lst)[1],
                                                 0xb5be10: tess})
check(asked == [(block, state, LEVEL, POS)], f'0xb3b160: state +0x170 -> block, block +0x190(state, level, pos): {asked}')
check(drawn == [STAIRS[0] + (True,), STAIRS[2] + (True,)], f'0xb3b160 draws every non-null box with its shape floats: {drawn}')
check(r & 0xff == 1, '0xb3b160 returns true')

# ---- face culling: Block +0x188 0x92c90; the down face (D) against a box starting at y = 8 is rendered without the neighbour
u = machine()
block, vt, lst, aabbs = fake_block(u, [(0, 8, 0, 16, 16, 16)])
face = OBJ + 0x7000
u.mem_write(face, struct.pack('<QIIIII', 0, 0, 0, 0, 0, 0))   # Direction: +8 data value 0 (down), +0x5c.. step
wq(u, 0x134bae8, STACK + 0x90000)   # stack-guard pointer slot (as in items195's unicorn test)
neighbour = []
pos = OBJ + 0x7100; u.mem_write(pos, struct.pack('<3i', 1, 2, 3))
lv = OBJ + 0x7200; lvt = FAKE + 0x6000
for i in range(0x40): wq(u, lvt + 8 * i, STUB + 0xc00 + 8 * i)
u.mem_write(STUB + 0xc00, b'\xc3' * 0x200); wq(u, lv, lvt)
r = call(u, 0x92c90, (block, state, lv, pos, face), stubs={slot(0x190): lambda m: lst, STUB + 0xc00 + 0xa0: lambda m: (neighbour.append(1), 1)[1]})
check(r & 0xff == 1 and not neighbour, f'0x92c90: a face no box reaches renders (1) without the neighbour query (r={r & 0xff}, n={neighbour})')

print(f'test_unicorn_boxes: {checks} checks, {fails} failures')
sys.exit(1 if fails else 0)
