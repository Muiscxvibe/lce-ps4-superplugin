"""Execute real retail 1.95 functions that items195 relies on, in Unicorn.

Each function runs from the pinned image bytes. Only out-of-scope callees
(allocator, free, dynamic_cast) are stubbed, and each stub is listed where it
is used. This tests code paths and layouts only. It is not a gameplay test.
"""
import struct, sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
img = IMAGE.read_bytes()
HEAP, STACK, DONE, OBJ = 0x8000000, 0x4000000, 0x5000000, 0x6000000
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
    for a in (HEAP, STACK, DONE, OBJ):
        u.mem_map(a, 0x100000)
    return u


def q(u, a): return struct.unpack('<Q', u.mem_read(a, 8))[0]
def d(u, a): return struct.unpack('<I', u.mem_read(a, 4))[0]
def f(u, a): return struct.unpack('<f', u.mem_read(a, 4))[0]
def wq(u, a, v): u.mem_write(a, struct.pack('<Q', v))


def call(u, fn, args=(), xmm0=None, stubs=None, limit=200000):
    stubs = stubs or {}
    regs = [UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RCX, UC_X86_REG_R8, UC_X86_REG_R9]
    for r, v in zip(regs, args):
        u.reg_write(r, v & 0xffffffffffffffff)
    if xmm0 is not None:
        u.reg_write(UC_X86_REG_XMM0, struct.unpack('<I', struct.pack('<f', xmm0))[0])
    sp = STACK + 0xf000
    wq(u, sp, DONE)
    u.reg_write(UC_X86_REG_RSP, sp)
    seen = []

    def hook(m, a, size, _):
        if a == DONE:
            m.emu_stop(); return
        if a in stubs:
            seen.append(a)
            rv = stubs[a](m)
            m.reg_write(UC_X86_REG_RAX, rv or 0)
            s = m.reg_read(UC_X86_REG_RSP)
            m.reg_write(UC_X86_REG_RIP, q(m, s)); m.reg_write(UC_X86_REG_RSP, s + 8)
    h = u.hook_add(UC_HOOK_CODE, hook)
    u.emu_start(fn, DONE + 0x10, count=limit)
    u.hook_del(h)
    return u.reg_read(UC_X86_REG_RAX), seen


heap = [HEAP]
def alloc(m):
    n = m.reg_read(UC_X86_REG_RDI); p = heap[0]; heap[0] += (n + 15) & ~15
    m.mem_write(p, bytes(n)); return p


# 1. ShapedRecipe ctor 0x6013d0: layout + null cells replaced by the empty stack global
u = machine()
cells = OBJ + 0x1000
u.mem_write(cells, struct.pack('<QQQQ', 0xaaa0, 0, 0xbbb0, 0))
wq(u, 0x1416d98, 0xe0e0)                            # the global the ctor reads for empty cells
call(u, 0x6013d0, (OBJ, 2, 2, cells, 0xccc0, 3))
check(q(u, OBJ) == 0x12bbed8, 'shaped vptr')
check(d(u, OBJ + 0x1c) == 2 and d(u, OBJ + 0x20) == 2 and d(u, OBJ + 0x24) == 3, 'shaped w/h/group')
check(q(u, OBJ + 0x28) == cells and q(u, OBJ + 0x30) == 0xccc0, 'shaped cells/result')
check(q(u, cells + 8) == 0xe0e0 and q(u, cells) == 0xaaa0, 'shaped null cells -> empty stack')
check(d(u, OBJ + 0x10) == 0xffffffff, 'shaped +0x10 default -1')

# 2. ShapelessRecipe ctor 0x602e10
u = machine()
call(u, 0x602e10, (OBJ, 0xccc0, 0xddd0, 5))
check(q(u, OBJ) == 0x12bbf58, 'shapeless vptr')
check(d(u, OBJ + 0x1c) == 5 and q(u, OBJ + 0x20) == 0xccc0 and q(u, OBJ + 0x28) == 0xddd0, 'shapeless group/result/ingredients')
# its group virtual (+0x40) and result virtual (+0x28) read the same fields
r, _ = call(u, q(u, 0x12bbf58 + 0x40), (OBJ,)); check(r & 0xffffffff == 5, 'shapeless getGroup')
r, _ = call(u, q(u, 0x12bbf58 + 0x28), (OBJ,)); check(r == 0xccc0, 'shapeless getResultItem')

# 3. Tier reads by the real digger virtuals: enchantability from a mod Tier
u = machine()
tier = OBJ + 0x200
u.mem_write(tier, struct.pack('<iiffi', 3, 2000, 9.5, 3.0, 18))
wq(u, OBJ + 0xc0, tier)
r, _ = call(u, 0x1fd370, (OBJ,)); check(r & 0xffffffff == 18, 'digger getEnchantmentValue reads Tier+0x10')
r, _ = call(u, 0x3aecf0, (tier,)); check(r == 0, 'Tier::getRepairItem(mod tier) = null -> repair override needed')
r, _ = call(u, 0x3af3c0, (OBJ,)); check(r & 0xffffffff == 0, 'base enchantability 0 (hoe/generic need override)')

# 4. FoodItem: getUseDuration 32/16 by +0xa6, setEatEffect stores +0xa8/+0xb0
u = machine()
r, _ = call(u, 0x2f5cf0, (OBJ,)); check(r & 0xffffffff == 32, 'eat duration 32')
u.mem_write(OBJ + 0xa6, b'\x01')
r, _ = call(u, 0x2f5cf0, (OBJ,)); check(r & 0xffffffff == 16, 'fast food 16')
freed = []
call(u, 0x2f5ff0, (OBJ, 0x1230), xmm0=0.6, stubs={0xe6cfb0: lambda m: freed.append(m.reg_read(UC_X86_REG_RDI))})
check(q(u, OBJ + 0xa8) == 0x1230 and abs(f(u, OBJ + 0xb0) - 0.6) < 1e-6, 'setEatEffect fields')

# 5. FurnaceRecipes operator[] 0x329640 on a fresh map: inserts front, zero value, returns &value
u = machine()
heap[0] = HEAP
m = OBJ + 0x400
head = HEAP + 0x80000
u.mem_write(head, struct.pack('<QQ', head, head))
wq(u, 0x134bae8, HEAP + 0xf0000)                    # stack-guard pointer slot read at 0x329651
buckets = HEAP + 0x81000                            # 8 buckets x 2 iterator slots of 0x18 (index math 0x329704..0x329725)
u.mem_write(buckets, struct.pack('<QQQ', 0, 0, head) * 16)
u.mem_write(m, struct.pack('<QQQQQQQQQf', 0, head, 0, 0, buckets, buckets + 0x18 * 16, buckets + 0x18 * 16, 7, 8, 1.0))
key = OBJ + 0x600
wq(u, key, 0x7777000)
r, seen = call(u, 0x329640, (m, key), stubs={0xe6cfd0: alloc, 0xe6cfc0: alloc, 0xe6cfb0: lambda m_: 0})
node = q(u, head)
check(q(u, m + 0x10) == 1, 'map size 1 after insert')
check(node != head and q(u, node + 0x10) == 0x7777000 and r == node + 0x18 and q(u, r) == 0, 'node key/value, zero value, &value returned')

# API 1.10 boat items: the real BoatItem ctor 0xd7640 with the Item ctor 0x354d80 stubbed (it reaches unresolved imports here;
# the stub fills the 0xa0 object with a pattern + the Item vptr). The ctor must then write exactly the vptr, [+0x9c] = type
# and [+0x18] = 1 -- the three stores items195 makes after its own Item ctor call.
def item_ctor_stub(m):
    o = m.reg_read(UC_X86_REG_RDI)
    m.mem_write(o, bytes((0x40 + i) & 0xff for i in range(0xa0))); wq(m, o, 0x126e638)
for t in (0, 3, 5):
    u = machine()
    a = OBJ + 0x800
    _, seen = call(u, 0xd7640, (a, t), stubs={0x354d80: item_ctor_stub})
    want = bytearray((0x40 + i) & 0xff for i in range(0xa0))
    want[0:8] = struct.pack('<Q', 0x1219f18); want[0x9c:0xa0] = struct.pack('<I', t); want[0x18:0x1c] = struct.pack('<I', 1)
    check(seen == [0x354d80], f'BoatItem ctor calls only the Item ctor t={t}')
    check(bytes(u.mem_read(a, 0xa0)) == bytes(want), f'BoatItem ctor = Item ctor + vptr 0x1219f18, +0x9c = {t}, +0x18 = 1 (0xa0 bytes)')

print(f'test_unicorn_retail: {checks} checks, {fails} failures')
sys.exit(1 if fails else 0)
