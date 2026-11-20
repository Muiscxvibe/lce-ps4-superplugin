#!/usr/bin/env python3
"""Unicorn checks of the REAL 1.95 natives behind the API 1.2 events195 services and the BLOCK_USE route.

The pinned analysis ELF (sha256 c675e6ce...029f) is mapped at base 0. Each test calls one retail function
on emulated objects and checks the documented behaviour/ABI that services195.cpp and ev195.cpp rely on:
  0x940b0  Block::use (stock)          -> false
  0x936b0  Block::randomTick           -> tail-calls vtable +0x1f0 (tick) with the arguments unchanged
  0x1d4ab0 CraftingTableBlock::use     -> rsi = Level (+0x248 client flag), stack word 1 = simulate flag
  0x2762a0 Entity::clearFire           -> +0x1c0 = 0
  0x380b70 Inventory::setChanged       -> ++[+0x138]
  0x3626c0 ItemInstance::setCount      -> +0x10 = n, +0x38 = (n <= 0)
  0x4244d0 LivingEntity::getHealth     -> float from the synched-data entry (+0x260 table, global index)
Run with the analysis venv: python -I test_unicorn_events195.py
"""
import hashlib
import struct
import sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_PROT_ALL, UcError
from unicorn.x86_const import (UC_X86_REG_RAX, UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RCX, UC_X86_REG_R8,
                               UC_X86_REG_R9, UC_X86_REG_R15, UC_X86_REG_RSP, UC_X86_REG_RIP, UC_X86_REG_XMM0, UC_CPU_X86_HASWELL)

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
SHA = 'c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f'
HEAP, STACK, DONE = 0x10000000, 0x4000000, 0x5000000
checks = 0


def check(c, msg):
    global checks
    checks += 1
    if not c:
        print('FAIL', msg)
        sys.exit(1)


class M:
    def __init__(self):
        img = IMAGE.read_bytes()
        check(hashlib.sha256(img).hexdigest() == SHA, 'image hash')
        u = Uc(UC_ARCH_X86, UC_MODE_64)
        u.ctl_set_cpu_model(UC_CPU_X86_HASWELL)
        phoff, = struct.unpack_from('<Q', img, 0x20)
        phnum, = struct.unpack_from('<H', img, 0x38)
        u.mem_map(0, 0x1500000, UC_PROT_ALL)
        for i in range(phnum):
            t, fl, off, va, pa, fs, ms, al = struct.unpack_from('<IIQQQQQQ', img, phoff + i * 56)
            if t in (1, 0x61000010) and fs and va + fs <= 0x1500000:
                u.mem_write(va, img[off:off + fs])
        u.mem_map(HEAP, 0x1000000, UC_PROT_ALL)
        u.mem_map(STACK, 0x100000, UC_PROT_ALL)
        u.mem_map(DONE, 0x1000, UC_PROT_ALL)
        u.mem_write(DONE, b'\xf4')   # hlt
        self.u, self.brk = u, HEAP

    def alloc(self, n):
        a = self.brk
        self.brk += (n + 15) & ~15
        self.u.mem_write(a, b'\0' * n)
        return a

    def wq(self, a, v): self.u.mem_write(a, struct.pack('<Q', v))
    def wd(self, a, v): self.u.mem_write(a, struct.pack('<i', v))
    def rd(self, a): return struct.unpack('<i', self.u.mem_read(a, 4))[0]
    def rb(self, a): return self.u.mem_read(a, 1)[0]

    def call(self, fn, args=(), stack=(), steps=20000):
        u = self.u
        regs = [UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RCX, UC_X86_REG_R8, UC_X86_REG_R9]
        for r, v in zip(regs, args):
            u.reg_write(r, v)
        sp = STACK + 0x80000
        frame = [DONE] + list(stack) + [0] * 4
        sp -= 8 * len(frame)
        for i, v in enumerate(frame):
            self.wq(sp + 8 * i, v)
        u.reg_write(UC_X86_REG_RSP, sp)
        try:
            u.emu_start(fn, DONE, count=steps)
        except UcError as e:
            check(False, f'{fn:#x}: {e} at rip {u.reg_read(UC_X86_REG_RIP):#x}')
        return u.reg_read(UC_X86_REG_RAX)


m = M()
# Block::use stock returns false
check(m.call(0x940b0, (m.alloc(64), 1, 2, 3, 4, 5)) & 0xff == 0, 'Block::use stock false')

# Block::randomTick forwards (block, level, pos, state, random) to vtable +0x1f0
stub = m.alloc(16)
m.u.mem_write(stub, bytes([0x48, 0x89, 0xc8, 0xc3]))                # mov rax, rcx ; ret -> returns the 4th argument
vt = m.alloc(0x400)
m.wq(vt + 0x1f0, stub)
blk = m.alloc(64)
m.wq(blk, vt)
check(m.call(0x936b0, (blk, 0x111, 0x222, 0x3333, 0x444)) == 0x3333, 'randomTick -> tick with state unchanged')

# CraftingTableBlock::use: simulate flag (stack word 1) -> false without touching the player
level, pos, player = m.alloc(0x300), m.alloc(16), m.alloc(16)
check(m.call(0x1d4ab0, (blk, level, pos, 0, player, 0), stack=(0, 1)) & 0xff == 0, 'simulate -> false')
# client-side level (+0x248 = 1) -> true (handled) without opening the menu
m.u.mem_write(level + 0x248, b'\x01')
check(m.call(0x1d4ab0, (blk, level, pos, 0, player, 0), stack=(0, 0)) & 0xff == 1, 'client level -> true, rsi is Level')

# Entity::clearFire
ent = m.alloc(0x800)
m.wd(ent + 0x1c0, 77)
m.wd(ent + 0x1c4, 0x5a5a5a5a)
m.call(0x2762a0, (ent,))
check(m.rd(ent + 0x1c0) == 0 and m.rd(ent + 0x1c4) == 0x5a5a5a5a, 'clearFire zeroes only +0x1c0')

# Inventory::setChanged
inv = m.alloc(0x200)
m.wd(inv + 0x138, 41)
m.call(0x380b70, (inv,))
check(m.rd(inv + 0x138) == 42, 'setChanged increments +0x138')

# ItemInstance::setCount
st = m.alloc(0xa8)
m.wq(st + 0x28, m.alloc(64))      # a non-null Item that is not the air global
m.call(0x3626c0, (st, 3))
check(m.rd(st + 0x10) == 3 and m.rb(st + 0x38) == 0, 'setCount 3')
m.call(0x3626c0, (st, 0))
check(m.rd(st + 0x10) == 0 and m.rb(st + 0x38) == 1, 'setCount 0 marks empty')

# LivingEntity::getHealth: [[rip+X]] is the synched-data id, entry = [[ent+0x260] + id*8 + 8], value float at +0x18
code = bytes(m.u.mem_read(0x4244d0, 7))
disp, = struct.unpack_from('<i', code, 3)
gidx = 0x4244d0 + 7 + disp
idx_ptr = struct.unpack('<Q', m.u.mem_read(gidx, 8))[0]
if idx_ptr == 0:                 # static storage not initialised in the image: provide it
    idx_ptr = m.alloc(8)
    m.wq(gidx, idx_ptr)
m.wd(idx_ptr, 6)
table, entry = m.alloc(0x100), m.alloc(0x40)
m.wq(table + 6 * 8 + 8, entry)
m.u.mem_write(entry + 0x18, struct.pack('<f', 13.5))
m.wq(ent + 0x260, table)
m.call(0x4244d0, (ent,))
hp = struct.unpack('<f', m.u.reg_read(UC_X86_REG_XMM0).to_bytes(16, 'little')[:4])[0]
check(hp == 13.5, f'getHealth reads the synched entry ({hp})')

# ItemInstance::getMaxDamage = Item+0x1c, setDamage clamps at 0
item = m.alloc(0x80)
m.wd(item + 0x1c, 250)
st2 = m.alloc(0xa8)
m.wq(st2 + 0x28, item)
check(m.call(0x3b8730, (st2,)) & 0xffffffff == 250, 'getMaxDamage reads Item+0x1c')
m.call(0x3b86e0, (st2, (-5) & 0xffffffff))
check(m.rd(st2 + 0x3c) == 0, 'setDamage clamps negative to 0')
m.call(0x3b86e0, (st2, 17))
check(m.rd(st2 + 0x3c) == 17, 'setDamage 17')

from unicorn.x86_const import UC_X86_REG_XMM1, UC_X86_REG_XMM2
def call_xmm(fn, rdi, xs):
    for r, v in zip((UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2), xs):
        m.u.reg_write(r, int.from_bytes(struct.pack('<d', v) + b'\0' * 8, 'little'))
    return m.call(fn, (rdi,))
# Entity::push adds to deltaMovement (+0x188) and sets hasImpulse (+0x2a9)
e2 = m.alloc(0x800)
m.u.mem_write(e2 + 0x188, struct.pack('<3d', 0.5, 0.0, -1.0))
call_xmm(0x27b3b0, e2, (0.25, 1.0, 0.5))
got = struct.unpack('<3d', m.u.mem_read(e2 + 0x188, 24))
# z goes through a scalar VEX vaddsd that this Unicorn build mis-emulates (reads the destination); x/y and the flag are exact
check(got[:2] == (0.75, 1.0) and m.rb(e2 + 0x2a9) == 1, f'push adds + hasImpulse {got}')

# LivingEntity::kill and CactusBlock::entityInside call hurt(+0x248) with the OUT_OF_WORLD / CACTUS globals
rec = m.alloc(16)
hurt = m.alloc(32)   # mov [rec], rsi ; ret
m.u.mem_write(hurt, bytes([0x48, 0xb8]) + struct.pack('<Q', rec) + bytes([0x48, 0x89, 0x30, 0xc3]))
lvt = m.alloc(0x1000)
m.wq(lvt + 0x248, hurt)
mob = m.alloc(0x800)
m.wq(mob, lvt)
m.wq(0x1413b60, 0x7700)   # stand-in DamageSource* values for the (runtime-initialised) statics
m.wq(0x1413b48, 0x7748)
m.call(0x41dad0, (mob,))
check(struct.unpack('<Q', m.u.mem_read(rec, 8))[0] == 0x7700, 'kill() passes OUT_OF_WORLD 0x1413b60')
pp = m.alloc(16)
m.wq(pp, mob)
m.u.mem_write(mob + 0x178, struct.pack('<d', 64.0))          # entity y
posb = m.alloc(16)
m.u.mem_write(posb, struct.pack('<3i', 0, 64, 0))
m.call(0x1596c0, (blk, level, posb, 0, pp))
check(struct.unpack('<Q', m.u.mem_read(rec, 8))[0] == 0x7748, 'cactus passes CACTUS 0x1413b48')

# Entity::tick (+0xa8) calls baseTick through vtable +0xb0 on a server Level
calls = m.alloc(16)
inc = m.alloc(32)    # inc qword [calls] ; ret
m.u.mem_write(inc, bytes([0x48, 0xb8]) + struct.pack('<Q', calls) + bytes([0x48, 0xff, 0x00, 0xc3]))
evt = m.alloc(0x1000)
for off in range(0, 0x1000, 8):
    m.wq(evt + off, inc)
ent2 = m.alloc(0x800)
m.wq(ent2, evt)
lvl2 = m.alloc(0x300)
m.wq(ent2 + 0x150, lvl2)
m.call(0x275680, (ent2,))
check(struct.unpack('<Q', m.u.mem_read(calls, 8))[0] >= 1, 'Entity::tick dispatches through its vtable (baseTick +0xb0)')

# ---- API 1.3 ----
# Block::setTicking (+0x128) writes byte +0x149 and returns this; isTicking (+0x140) reads it (tickBlocks' random-tick gate)
b3 = m.alloc(0x200)
m.u.mem_write(b3 + 0x148, bytes([0x77, 0x00, 0x66]))
check(m.call(0x92a90, (b3, 1)) == b3 and bytes(m.u.mem_read(b3 + 0x148, 3)) == bytes([0x77, 0x01, 0x66]), 'setTicking(true) -> +0x149')
check(m.call(0x92aa0, (b3,)) & 0xff == 1, 'isTicking reads +0x149')
m.call(0x92a90, (b3, 0))
check(m.call(0x92aa0, (b3,)) & 0xff == 0, 'setTicking(false)')

# MapItem::inventoryTick (+0xc8): rdx is the Level (client levels return before touching stack/entity)
lvl3 = m.alloc(0x300)
m.u.mem_write(lvl3 + 0x248, b'\x01')
m.call(0x445340, (m.alloc(64), 0x7f000000, lvl3, 0x7f000000, 3, 1))   # unmapped stack/entity pairs: faults unless rdx is checked first
check(True, 'MapItem::inventoryTick returns early on a client Level (rdx)')

# DamageSource::playerAttack (0x1f22a0): operator new(0x40) + EntityDamageSource ctor holding a strong copy of the pair
from unicorn import UC_HOOK_CODE
def plt(u, addr, size, _):
    rsp = u.reg_read(UC_X86_REG_RSP)
    ret = struct.unpack('<Q', u.mem_read(rsp, 8))[0]
    a, v = u.reg_read(UC_X86_REG_RDI), u.reg_read(UC_X86_REG_RSI) & 0xffffffff
    if addr == 0xe6cfd0:                                   # operator new
        u.reg_write(UC_X86_REG_RAX, m.alloc(a))
    else:                                                  # 0xe6ce10 fetch_add / 0xe6ce20 fetch_sub (int32, returns old)
        old = struct.unpack('<i', u.mem_read(a, 4))[0]
        u.mem_write(a, struct.pack('<i', old + (v if addr == 0xe6ce10 else -v)))
        u.reg_write(UC_X86_REG_RAX, old & 0xffffffff)
    u.reg_write(UC_X86_REG_RSP, rsp + 8)
    u.reg_write(UC_X86_REG_RIP, ret)
for a in (0xe6cfd0, 0xe6ce10, 0xe6ce20):
    m.u.hook_add(UC_HOOK_CODE, plt, begin=a, end=a)
pl, ctl = m.alloc(0x800), m.alloc(32)
m.wd(ctl + 8, 1)
m.wd(ctl + 12, 1)
pair = m.alloc(16)
m.wq(pair, pl)
m.wq(pair + 8, ctl)
src = m.call(0x1f22a0, (pair,), steps=200000)
q = lambda a: struct.unpack('<Q', m.u.mem_read(a, 8))[0]
check(q(src) == 0x124cb48, f'playerAttack returns an EntityDamageSource ({q(src):#x})')
check(m.rd(src + 8) == 0x1e and m.rd(src + 0xc) == 0x3c and q(src + 0x20) == pl and q(src + 0x28) == ctl, 'ids 0x1e/0x3c, entity copy at +0x20/+0x28')
check(m.rd(ctl + 8) == 2 and m.rb(src + 0x38) == 0, 'one strong ref held by the source (factory temp released), not thorns')

# weak_ptr::lock 0x68e60(out, obj, ctrl, throw=0): live -> out = pair, uses+1; expired -> out untouched, no throw
from unicorn import UC_HOOK_CODE as _HC
def plt2(u, addr, size, _):
    rsp = u.reg_read(UC_X86_REG_RSP)
    ret = struct.unpack('<Q', u.mem_read(rsp, 8))[0]
    a = u.reg_read(UC_X86_REG_RDI)
    cur = struct.unpack('<i', u.mem_read(a, 4))[0]
    if addr == 0xe6ce30:                                   # atomic load
        u.reg_write(UC_X86_REG_RAX, cur & 0xffffffff)
    else:                                                  # 0xe6ce00 CAS(ptr, expected*, desired, ...)
        expp, want = u.reg_read(UC_X86_REG_RSI), u.reg_read(UC_X86_REG_RDX) & 0xffffffff
        exp = struct.unpack('<i', u.mem_read(expp, 4))[0]
        ok = cur == exp
        if ok: u.mem_write(a, struct.pack('<I', want))
        else: u.mem_write(expp, struct.pack('<i', cur))
        u.reg_write(UC_X86_REG_RAX, 1 if ok else 0)
    u.reg_write(UC_X86_REG_RSP, rsp + 8)
    u.reg_write(UC_X86_REG_RIP, ret)
for a in (0xe6ce30, 0xe6ce00):
    m.u.hook_add(_HC, plt2, begin=a, end=a)
mob3, mctl = m.alloc(0x800), m.alloc(32)
m.wd(mctl + 8, 1); m.wd(mctl + 12, 1)
out = m.alloc(16)
m.call(0x68e60, (out, mob3, mctl, 0), steps=200000)
check(q(out) == mob3 and q(out + 8) == mctl and m.rd(mctl + 8) == 2, 'weak lock: live -> strong copy')
out2 = m.alloc(16)
m.wd(mctl + 8, 0)
m.call(0x68e60, (out2, mob3, mctl, 0), steps=200000)
check(q(out2) == 0 and q(out2 + 8) == 0 and m.rd(mctl + 8) == 0, 'weak lock: expired -> empty, no throw')
# DamageSource::mobAttack 0x1f2080 -> EntityDamageSource id 0x1d holding the mob
m.wd(mctl + 8, 2)
pr = m.alloc(16); m.wq(pr, mob3); m.wq(pr + 8, mctl)
src2 = m.call(0x1f2080, (pr,), steps=200000)
check(q(src2) == 0x124cb48 and m.rd(src2 + 8) == 0x1d and q(src2 + 0x20) == mob3 and m.rd(mctl + 8) == 3, 'mobAttack source holds the mob')

# ---- item NBT on the REAL CompoundTag code: getOrCreateTag, putInt/putDouble/putString, get, remove ----
import unicorn
unknown = []
def plt3(u, addr, size, _):
    rsp = u.reg_read(UC_X86_REG_RSP)
    ret = struct.unpack('<Q', u.mem_read(rsp, 8))[0]
    a, b, c = u.reg_read(UC_X86_REG_RDI), u.reg_read(UC_X86_REG_RSI), u.reg_read(UC_X86_REG_RDX)
    if addr in (0xe6cfd0, 0xe6ce10, 0xe6ce20, 0xe6ce30, 0xe6ce00):   # handled by the earlier hooks
        return
    if addr == 0xe6cfc0:                                   # operator new[]
        u.reg_write(UC_X86_REG_RAX, m.alloc(max(a, 16)))
    elif addr in (0xe6d490, 0xe6d480):                     # process time counter frequency / counter (Packet ctor timestamp 0x6c24e0)
        u.reg_write(UC_X86_REG_RAX, 1000000 if addr == 0xe6d490 else 123456)
    elif addr == 0xe6cfb0:                                 # operator delete
        pass
    elif addr in (0xe6d090, 0xe6d0a0):                     # memcpy / memmove(dst, src, n) (vector growth 0x1e58db)
        u.mem_write(a, bytes(u.mem_read(b, c))); u.reg_write(UC_X86_REG_RAX, a)
    elif addr in (0xe6d110, 0xe6d120):                     # wmemmove / wmemcpy(dst, src, n) of 16-bit units (wstring assign 0x6790d, copy 0x3b05)
        u.mem_write(a, bytes(u.mem_read(b, 2 * c))); u.reg_write(UC_X86_REG_RAX, a)
    elif addr == 0xe6d100:                                 # wmemcmp(a, b, n) of 16-bit units (map key compare 0x1e5040)
        x, y = struct.unpack(f'<{c}H', u.mem_read(a, 2 * c)), struct.unpack(f'<{c}H', u.mem_read(b, 2 * c))
        u.reg_write(UC_X86_REG_RAX, 0 if x == y else (1 if x > y else 0xffffffff))
    elif addr == 0xe6d0e0:                                 # wcslen (16-bit units)
        n = 0
        while struct.unpack('<H', u.mem_read(a + 2 * n, 2))[0]: n += 1
        u.reg_write(UC_X86_REG_RAX, n)
    else:
        unknown.append((addr, ret)); u.emu_stop(); return
    u.reg_write(UC_X86_REG_RSP, rsp + 8)
    u.reg_write(UC_X86_REG_RIP, ret)
m.u.hook_add(UC_HOOK_CODE, plt3, begin=0xe6c000, end=0xe6e000)
def call_plt(fn, args, steps=400000, *, stack=()):
    unknown.clear()
    r = m.call(fn, args, stack=stack, steps=steps)
    check(not unknown, f'{fn:#x}: unhandled import {[(hex(a), "from", hex(r)) for a, r in unknown]}')
    return r
def wstr(text):   # Dinkumware wstring (40 bytes): +8 inline buffer / pointer, +0x18 size, +0x20 cap
    w = m.alloc(0x28)
    units = text.encode('utf-16-le')
    if len(text) < 8:
        m.u.mem_write(w + 8, units + b'\0\0')
        cap = 7
    else:
        buf = m.alloc(len(units) + 2); m.u.mem_write(buf, units + b'\0\0'); m.wq(w + 8, buf); cap = len(text)
    m.wq(w + 0x18, len(text)); m.wq(w + 0x20, cap)
    return w
stk = m.alloc(0xa8)
tag = call_plt(0x3ba900, (stk,))
check(q(tag) == 0x122ede0 and q(stk + 0x30) == tag and call_plt(0x3ba900, (stk,)) == tag, 'getOrCreateTag: CompoundTag at stack +0x30, reused')
k1, k2, k3 = wstr('mana'), wstr('lce:speedfactor'), wstr('owner')
call_plt(0x1b25e0, (tag, k1, 42))
t = call_plt(0x1b7400, (tag, k1))
check(t and q(t) == 0x126e2c8 and m.rd(t + 8) == 42, 'putInt -> IntTag (value +8), get(key) returns it')
call_plt(0x1b25e0, (tag, k1, 7))
check(m.rd(call_plt(0x1b7400, (tag, k1)) + 8) == 7, 'putInt replaces')
m.u.reg_write(UC_X86_REG_XMM0, int.from_bytes(struct.pack('<d', 2.5) + b'\0' * 8, 'little'))
call_plt(0x1b7910, (tag, k2))
t2 = call_plt(0x1b7400, (tag, k2))
check(q(t2) == 0x123cfd0 and struct.unpack('<d', m.u.mem_read(t2 + 8, 8))[0] == 2.5, 'putDouble (long key) -> DoubleTag')
call_plt(0x1af3b0, (tag, k3, wstr('Steve')))
t3 = call_plt(0x1b7400, (tag, k3))
check(q(t3) == 0x12d83a8 and q(t3 + 0x20) == 5 and bytes(m.u.mem_read(t3 + 0x10, 10)).decode('utf-16-le') == 'Steve', 'putString -> StringTag wstring at +8')
check(call_plt(0x1b7400, (tag, wstr('none'))) == 0, 'get(missing) -> NULL')
call_plt(0x1b8630, (tag, k1))
check(call_plt(0x1b7400, (tag, k1)) == 0 and call_plt(0x1b7400, (tag, k3)) == t3, 'remove erases only that key')


# ---- API 1.5 entity NBT on the REAL code: CompoundTag ctor, Entity::saveWithoutId, the Entity tag set's insert/_Erase, ----
# ---- Entity::load's head reset, and the CompoundTag deleting destructor (vt +0x30) ----
def stub(code):
    a = m.alloc(len(code) + 16); m.u.mem_write(a, bytes(code)); return a
ret0 = stub([0x31, 0xc0, 0xc3])                                                    # xor eax,eax; ret
sret_ws = stub([0x48, 0x31, 0xc0, 0x48, 0x89, 0x07, 0x48, 0x89, 0x47, 0x08, 0x48, 0x89, 0x47, 0x10, 0x48, 0x89, 0x47, 0x18,
                0x48, 0xc7, 0x47, 0x20, 0x07, 0x00, 0x00, 0x00, 0x48, 0x89, 0xf8, 0xc3])   # *rdi = empty wstring (cap 7); return rdi
evt5 = m.alloc(0x1000)
for off in range(0, 0x1000, 8):
    m.wq(evt5 + off, ret0)
m.wq(evt5 + 0x618, sret_ws); m.wq(evt5 + 0x650, sret_ws)                          # getStringUUID / getCustomName (sret wstring)
ent5 = m.alloc(0x800)
m.wq(ent5, evt5)
m.u.mem_write(ent5 + 0x170, struct.pack('<6d', 1.5, 70.0, -2.5, 0.0, -0.08, 0.0))   # Pos, Motion
m.u.mem_write(ent5 + 0x1c0, struct.pack('<h', 13))                                   # Fire (short)
sd = m.alloc(0x400)                                                                   # synched data table (+0x260): entries with value +0x18
for i in range(64):
    m.wq(sd + 8 + 8 * i, m.alloc(0x40))
m.wq(ent5 + 0x260, sd)
zint = m.alloc(16)   # EntityDataAccessor id pointers (.bss, filled by static init on the console): point them at id 0
for a in (0x1415a38, 0x1415a40, 0x1415a78, 0x1415a80, 0x1415a88):
    if q(a) == 0: m.wq(a, zint)
head = m.alloc(0x48)                                                                  # tag set head (nil, black), set object at +0x360
for o in (0, 8, 0x10): m.wq(head + o, head)
m.u.mem_write(head + 0x18, b'\x01\x01')
m.wq(ent5 + 0x368, head)
tset = ent5 + 0x360
def tags_inorder():
    out, n = [], q(head)
    while n != head:
        ln, cap = q(n + 0x38), q(n + 0x40)
        buf = q(n + 0x28) if cap >= 8 else n + 0x28
        out.append(bytes(m.u.mem_read(buf, 2 * ln)).decode('utf-16-le'))
        r = q(n + 0x10)
        if m.rb(r + 0x19) == 0:
            n = r
            while m.rb(q(n) + 0x19) == 0: n = q(n)
        else:
            p = q(n + 8)
            while m.rb(p + 0x19) == 0 and q(p + 0x10) == n: n, p = p, q(p + 8)
            n = p
    return out
names = ['bossy', 'lce|mymod:mana=i42', 'lce|mymod:speed=d3fb999999999999a', 'a', 'zz']
for t in names:
    call_plt(0x2c5150, (m.alloc(0x40), tset, 0, wstr(t)))
call_plt(0x2c5150, (m.alloc(0x40), tset, 0, wstr('bossy')))                          # duplicate: ignored
check(q(tset + 0x10) == 5 and tags_inorder() == sorted(names), f'native tag insert keeps a sorted set {tags_inorder()}')
# Entity::load's rebuild, as the module does it: _Erase(set, root), head links = head, size = 0, insert the kept entries
keep = [t for t in tags_inorder() if not t.startswith('lce|mymod:mana=')] + ['lce|mymod:mana=i7']
call_plt(0x2c50f0, (tset, q(head + 8)))
for o in (8, 0, 0x10): m.wq(head + o, head)
m.wq(tset + 0x10, 0)
check(tags_inorder() == [], 'erase + head reset -> empty set')
for t in keep:
    call_plt(0x2c5150, (m.alloc(0x40), tset, 0, wstr(t)))
check(q(tset + 0x10) == 5 and tags_inorder() == sorted(keep), 'rebuilt set holds the replacement')
# saveWithoutId into a fresh CompoundTag (new 0x58 + ctor), exactly as entity_nbt_get does
ctag = m.alloc(0x58)
call_plt(0x1b6df0, (ctag,))
check(q(ctag) == 0x122ede0, 'CompoundTag ctor stamps vtable 0x122ede0')
check(call_plt(0x27bc70, (ent5, ctag), steps=2000000) == ctag, 'saveWithoutId(entity, tag) returns the tag')
fire = call_plt(0x1b7400, (ctag, wstr('Fire')))
check(fire and q(fire) == 0x12bd5c8 and struct.unpack('<h', m.u.mem_read(fire + 8, 2))[0] == 13, 'Fire -> ShortTag from +0x1c0')
pos = call_plt(0x1b7400, (ctag, wstr('Pos')))
check(pos and q(pos) == 0x1278268 and (q(pos + 0x18) - q(pos + 0x10)) == 24, 'Pos -> ListTag (vtable 0x1278268, 3 elements at +0x10..+0x18)')
y = q(q(pos + 0x10) + 8)
check(q(y) == 0x123cfd0 and struct.unpack('<d', m.u.mem_read(y + 8, 8))[0] == 70.0, 'Pos[1] -> DoubleTag y')
mot = call_plt(0x1b7400, (ctag, wstr('Motion')))
check(struct.unpack('<d', m.u.mem_read(q(q(mot + 0x10) + 8) + 8, 8))[0] == -0.08, 'Motion[1] from +0x190')
tl = call_plt(0x1b7400, (ctag, wstr('Tags')))
check(tl and q(tl) == 0x1278268 and (q(tl + 0x18) - q(tl + 0x10)) // 8 == 5, 'Tags list saved from the set')
saved = []
for i in range(5):
    st5 = q(q(tl + 0x10) + 8 * i)
    check(q(st5) == 0x12d83a8, 'Tags element is a StringTag')
    ln, cap = q(st5 + 0x20), q(st5 + 0x28)
    buf = q(st5 + 0x10) if cap >= 8 else st5 + 0x10
    saved.append(bytes(m.u.mem_read(buf, 2 * ln)).decode('utf-16-le'))
check(saved == sorted(keep) and 'lce|mymod:mana=i7' in saved, f'mod data is written by the game save path {saved}')
check(q(0x122ede0 + 0x30) == 0x1b7010, 'CompoundTag vt +0x30 = deleting dtor')
call_plt(0x1b7010, (ctag,), steps=2000000)                                           # delete the temporary tag (frees every child)
check(True, 'CompoundTag deleting dtor ran on the saved tree with only operator delete')

# ---- API 1.6 client networking on the REAL code: factory create 0x514e40 -> handle 0x18bd90 -> listener vt +0x110 ----
out = m.alloc(16)
check(call_plt(0x514e40, (out,), steps=400000) == out, 'payload create returns its sret shared_ptr')
pkt, ctl = q(out), q(out + 8)
check(q(pkt) == 0x12287e0 and q(ctl) != 0, 'create builds a ClientboundCustomPayloadPacket (vtable 0x12287e0) owned by a _Ref_count')
check(q(pkt + 0x18) == pkt and q(pkt + 0x20) == ctl, 'enable_shared_from_this weak pair at +0x18/+0x20 (what handle locks)')
check(q(pkt + 0x50) == 0 and m.rd(pkt + 0x58) == 0 and q(pkt + 0x48) == 7, 'empty channel wstring +0x28, data +0x50, len +0x58')
rec = m.alloc(32)
sret_rec = stub([0x48, 0xb8] + list(struct.pack('<Q', rec)) + [0x48, 0x89, 0x38, 0x48, 0x89, 0x70, 0x08, 0x48, 0x8b, 0x0e, 0x48, 0x89, 0x48, 0x10, 0xc3])
# mov rax, rec; mov [rax], rdi; mov [rax+8], rsi; mov rcx, [rsi]; mov [rax+0x10], rcx; ret
lvt = m.alloc(0x400)
for off in range(0, 0x400, 8): m.wq(lvt + off, ret0)
m.wq(lvt + 0x110, sret_rec)
lis = m.alloc(0x100); m.wq(lis, lvt)
call_plt(0x18bd90, (pkt, lis), steps=400000)
check(q(rec) == lis and q(rec + 0x10) == pkt, 'handle(packet, listener) calls listener vt +0x110 (listener, shared_ptr<packet>*)')
# the same for SetTime (id 4): create 0x511890 -> handle 0x1a2620 -> listener vt +0x200
m.wq(lvt + 0x110, ret0); m.wq(lvt + 0x200, sret_rec); m.wq(rec, 0); m.wq(rec + 0x10, 0)
out2 = m.alloc(16)
call_plt(0x511890, (out2,), steps=400000)
st = q(out2)
check(q(st) == 0x122c3a0, 'SetTime create builds vtable 0x122c3a0')
call_plt(0x1a2620, (st, lis), steps=400000)
check(q(rec) == lis and q(rec + 0x10) == st, 'SetTime handle calls listener vt +0x200')
# ServerboundCustomPayloadPacket ctor 0x5fbd70(pkt, channel, data, len): the layout the client send fills
sb = m.alloc(0x60); dat = m.alloc(16)
call_plt(0x5fbd70, (sb, wstr('ruby:click'), dat, 5), steps=400000)
check(q(sb) == 0x12bb4c0 and q(sb + 0x50) == dat and m.rd(sb + 0x58) == 5 and q(sb + 0x40) == 10, 'SB payload ctor: vtable, channel wstring +0x28 (size +0x40), data +0x50, len +0x58')

# Exercise the SDK's native call ABI through the real color packet constructor.
# Stop after construction, before any connection is used.
tint_level, tint_type = m.alloc(0x300), m.alloc(0x40)
tint_packets = []
def capture_tint_packet(u, address, size, user):
    tint_packets.append(u.reg_read(UC_X86_REG_R15))
    u.emu_stop()
tint_hook = m.u.hook_add(UC_HOOK_CODE, capture_tint_packet, begin=0xcfd849, end=0xcfd849)
def double_bits(number):
    return struct.unpack('<Q', struct.pack('<d', number))[0]
for reg, number in enumerate((10.5, 65, -3.5, 1.25, 1.25, 1.25, 0, 18 / 255), UC_X86_REG_XMM0):
    m.u.reg_write(reg, double_bits(number))
call_plt(0xcfd730, (tint_level, tint_type, 7, 0, 0), stack=(double_bits(128 / 255), double_bits(1)))
m.u.hook_del(tint_hook)
check(len(tint_packets) == 1, 'native sender constructs one RGB particle packet')
tint_packet = tint_packets[0]
check(q(tint_packet) == 0x1228ab8 and q(tint_packet + 0x28) == tint_type and m.rb(tint_packet + 0x50) == 0,
      'RGB packet uses the normal particle type and stock range limiter')
check(struct.unpack('<fff', m.u.mem_read(tint_packet + 0x30, 12)) == (10.5, 65, -3.5) and m.rd(tint_packet + 0x4c) == 7,
      'RGB packet retains position and count')
check(m.rb(tint_packet + 0x68) == 1 and
      m.u.mem_read(tint_packet + 0x6c, 12) == struct.pack('<fff', 18 / 255, 128 / 255, 1),
      'RGB flag and separate red, green, blue floats survive the native call')

# Follow the same packet through the stock client listener. Stub its Gaussian
# random source and capture parameters at the level's particle spawn call.
tint_listener, tint_pair = m.alloc(0x100), m.alloc(16)
m.wq(tint_listener + 0x60, tint_level)
m.wq(tint_pair, tint_packet)
m.u.mem_write(0x596cb0, bytes.fromhex('66 0f ef c0 c3'))  # pxor xmm0,xmm0; ret
tint_spawns = []
def capture_tint_spawn(u, address, size, user):
    registers = tuple(u.reg_read(reg) for reg in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX))
    values = tuple(u.reg_read(reg) & 0xffffffffffffffff for reg in range(UC_X86_REG_XMM0, UC_X86_REG_XMM0 + 6))
    tint_spawns.append((registers, values))
    sp = u.reg_read(UC_X86_REG_RSP)
    u.reg_write(UC_X86_REG_RSP, sp + 8)
    u.reg_write(UC_X86_REG_RIP, q(sp))
spawn_hook = m.u.hook_add(UC_HOOK_CODE, capture_tint_spawn, begin=0x3f42c0, end=0x3f42c0)
call_plt(0xba4030, (tint_listener, tint_pair))
m.u.hook_del(spawn_hook)
packet_rgb = struct.unpack('<fff', m.u.mem_read(tint_packet + 0x6c, 12))
expected_spawn = ((tint_level, tint_type, 0), tuple(double_bits(v) for v in (10.5, 65, -3.5, *packet_rgb)))
check(tint_spawns == [expected_spawn] * 7, 'stock client forwards count, position and RGB to the level particle renderer')
print(f'events195 unicorn: ALL {checks} CHECKS PASSED (real 1.95 code)')
