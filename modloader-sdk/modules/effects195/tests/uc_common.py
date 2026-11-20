"""Shared Unicorn machine for effects195: real 1.95 image bytes, bump heap.
Only operator new/new[]/delete/delete[] (0xe6cfd0/0xe6cfc0/0xe6cfb0/0xe6cfa0), the stack-check
failure (0xe6d030), libc wcslen (0xe6d0e0) and wmemcpy (0xe6d110, 2-byte
wchar_t) memmove (0xe6d0a0) wmemcmp (0xe6d100), wmemmove (0xe6d120), wmemchr (0xe6d190), __cxa_atexit (0xe6cfe0, no-op) are
stubbed, and the
AVX2 wstring hash 0x68b350 is reimplemented from its scalar tail (Unicorn has
no ymm support); any other import raises."""
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
img = IMAGE.read_bytes()
HEAP, STACK, DONE, OBJ, GUARD = 0x8000000, 0x4000000, 0x5000000, 0x6000000, 0x7000000
HEAP_SIZE = 0x2000000


def machine():
    u = Uc(UC_ARCH_X86, UC_MODE_64)
    u.mem_map(0, 0x1500000)
    u.mem_write(0, img[0x4000:0x4000 + 0x10c0354])
    u.mem_write(0x1200000, img[0x10c8000:0x10c8000 + 0x14d928])
    u.mem_write(0x1400000, img[0x1218000:0x1218000 + 0x8ac8])
    for a in (STACK, DONE, OBJ, GUARD):
        u.mem_map(a, 0x100000)
    u.mem_map(HEAP, HEAP_SIZE)
    wq(u, 0x134bae8, GUARD)          # __stack_chk_guard pointer slot
    u.heap = HEAP
    u.frees = []
    return u


def q(u, a): return struct.unpack('<Q', u.mem_read(a, 8))[0]
def d(u, a): return struct.unpack('<I', u.mem_read(a, 4))[0]
def b(u, a): return u.mem_read(a, 1)[0]
def dbl(u, a): return struct.unpack('<d', u.mem_read(a, 8))[0]
def wq(u, a, v): u.mem_write(a, struct.pack('<Q', v & 0xffffffffffffffff))
def wd(u, a, v): u.mem_write(a, struct.pack('<I', v & 0xffffffff))


def alloc(u, n):
    p = u.heap; u.heap += (n + 15) & ~15
    u.mem_write(p, b'\xcd' * n)      # operator new does not zero: poison like the console heap could be
    return p


def _alloc_stub(m):
    return alloc(m, m.reg_read(UC_X86_REG_RDI))


def _free_stub(m):
    m.frees.append(m.reg_read(UC_X86_REG_RDI)); return 0


def _wcslen(m):
    p = m.reg_read(UC_X86_REG_RDI); n = 0
    while struct.unpack('<H', m.mem_read(p + 2 * n, 2))[0]: n += 1
    return n


def _wmemcpy(m):
    dst, src, n = (m.reg_read(r) for r in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX))
    if n: m.mem_write(dst, bytes(m.mem_read(src, 2 * n)))
    return dst


def _memmove(m):
    dst, src, n = (m.reg_read(r) for r in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX))
    if n: m.mem_write(dst, bytes(m.mem_read(src, n)))
    return dst


def _wstr_hash(m):
    # 0x68b350 wstring hash: AVX2 (ymm) vectorised, which Unicorn cannot execute.
    # Same loop as its scalar tail 0x68b4d8..0x68b4ff, reimplemented exactly.
    w = m.reg_read(UC_X86_REG_RDI)
    n = q(m, w + 0x18) & 0xffffffff
    if not n: return 0
    p = q(m, w + 8) if q(m, w + 0x20) > 7 else w + 8
    s = struct.unpack(f'<{n}H', m.mem_read(p, 2 * n))
    h = 0
    for i, c in enumerate(s):
        v = (((c << 5) - c) ^ (n - 1 - i)) & 0xffffffff if i < n - 1 else c
        h = (h + v) & 0xffffffff
    return h


def _wmemcmp(m):
    a, c, n = (m.reg_read(r) for r in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX))
    x = struct.unpack(f'<{n}H', m.mem_read(a, 2 * n)); y = struct.unpack(f'<{n}H', m.mem_read(c, 2 * n))
    return 0 if x == y else (-1 if x < y else 1)


def _wmemchr(m):
    p, c, n = (m.reg_read(r) for r in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX))
    for i in range(n):
        if struct.unpack('<H', m.mem_read(p + 2 * i, 2))[0] == (c & 0xffff): return p + 2 * i
    return 0


def _fail(m):
    raise RuntimeError('stack check failure stub reached')


BASE_STUBS = {0xe6cfd0: _alloc_stub, 0xe6cfc0: _alloc_stub, 0xe6cfb0: _free_stub, 0xe6cfa0: _free_stub, 0xe6d030: _fail, 0xe6d0e0: _wcslen, 0xe6d110: _wmemcpy, 0xe6d0a0: _memmove, 0x68b350: _wstr_hash, 0xe6d100: _wmemcmp, 0xe6d120: _wmemcpy, 0xe6d190: _wmemchr, 0xe6cfe0: lambda m: 0}
PLT = (0xe6c000, 0xe6e000)
REGS = [UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RCX, UC_X86_REG_R8, UC_X86_REG_R9]


def call(u, fn, args=(), xmm0=None, stubs=None, limit=2_000_000):
    st = dict(BASE_STUBS); st.update(stubs or {})
    for r, v in zip(REGS, args):
        u.reg_write(r, v & 0xffffffffffffffff)
    if xmm0 is not None:
        u.reg_write(UC_X86_REG_XMM0, struct.unpack('<Q', struct.pack('<d', xmm0))[0])
    sp = STACK + 0xf0000
    wq(u, sp, DONE)
    u.reg_write(UC_X86_REG_RSP, sp)
    seen = []

    def hook(m, a, size, _):
        if a == DONE:
            m.emu_stop(); return
        if a not in st and PLT[0] <= a < PLT[1]:
            raise RuntimeError(f'unstubbed import {a:#x} from {q(m, m.reg_read(UC_X86_REG_RSP)):#x}')
        if a in st:
            seen.append(a)
            rv = st[a](m)
            m.reg_write(UC_X86_REG_RAX, (rv or 0) & 0xffffffffffffffff)
            s = m.reg_read(UC_X86_REG_RSP)
            m.reg_write(UC_X86_REG_RIP, q(m, s)); m.reg_write(UC_X86_REG_RSP, s + 8)
    h = u.hook_add(UC_HOOK_CODE, hook)
    try:
        u.emu_start(fn, DONE + 0x10, count=limit)
    finally:
        u.hook_del(h)
    if u.reg_read(UC_X86_REG_RIP) != DONE:
        raise RuntimeError(f'{fn:#x} did not return (rip {u.reg_read(UC_X86_REG_RIP):#x})')
    return u.reg_read(UC_X86_REG_RAX), seen


def new_registry(u, ctor, size, glob):
    reg = alloc(u, size)
    call(u, ctor, (reg,))
    wq(u, glob, reg)
    return reg


def wstr(u, s):
    """Build a wstring object (0x28) exactly as the inline code does, via the real ctor 0x67840."""
    obj = alloc(u, 0x40)
    u.mem_write(obj, bytes(0x40)); wq(u, obj + 0x20, 7)
    src = alloc(u, 2 * len(s) + 2); u.mem_write(src, s.encode('utf-16le') + b'\0\0')
    call(u, 0x67840, (obj, src, len(s)))
    return obj


def run_range(u, start, stop, stubs=None, limit=2_000_000):
    """Execute [start, stop) of a retail function body with a scratch frame (rbp)."""
    st = dict(BASE_STUBS); st.update(stubs or {})
    frame = STACK + 0x80000
    u.reg_write(UC_X86_REG_RBP, frame); u.reg_write(UC_X86_REG_RSP, frame - 0x400)

    def hook(m, a, size, _):
        if a not in st and PLT[0] <= a < PLT[1]:
            raise RuntimeError(f'unstubbed import {a:#x}')
        if a in st:
            rv = st[a](m)
            m.reg_write(UC_X86_REG_RAX, (rv or 0) & 0xffffffffffffffff)
            s = m.reg_read(UC_X86_REG_RSP)
            m.reg_write(UC_X86_REG_RIP, q(m, s)); m.reg_write(UC_X86_REG_RSP, s + 8)
    h = u.hook_add(UC_HOOK_CODE, hook)
    try:
        u.emu_start(start, stop, count=limit)
    finally:
        u.hook_del(h)


def boot_effects_and_potions(u):
    """Real 1.95 startup order for the two registries: placeholder attribute statics, MobEffect registry ctor 0x4c2dc0
    (as 0x4d3bb4), effect initializer 0x4781e0, statics 0x47b780, Potion registry
    construction 0x588f5f..0x589032 (DefaultedMappedRegistry, default key "empty"),
    potion initializer 0x55e6d0."""
    # SharedMonsterAttributes statics are set by the DT_INIT_ARRAY entry 0x61d300 (0x1408958), which uses
    # 256-bit AVX stores Unicorn cannot run. The effect initializer only stores these pointers as map keys,
    # so distinct placeholder Attribute objects stand in for the five it reads.
    for g in (0x142ff40, 0x142ff58, 0x142ff68, 0x142ff70, 0x142ff88):
        a = alloc(u, 0x40); u.mem_write(a, bytes(0x40)); wq(u, g, a)
    new_registry(u, 0x4c2dc0, 0x150, 0x14195c8)
    call(u, 0x4781e0, (), limit=20_000_000)
    call(u, 0x47b780, (), limit=5_000_000)
    run_range(u, 0x588f5f, 0x589032)
    wq(u, 0x142e938, u.reg_read(UC_X86_REG_RBX))
    call(u, 0x55e6d0, (), limit=20_000_000)
