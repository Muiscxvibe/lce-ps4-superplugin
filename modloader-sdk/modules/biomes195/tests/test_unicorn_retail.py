"""Run the real retail 1.95 biome code that biomes195 relies on, in Unicorn.

Real code executed (from the pinned image bytes):
  0x53d140 PlainsBiome ctor -> 0x83e60 Biome base ctor (fields, biomes[id] store, spawn lists, decorator 0x86c00)
  0x67840  wstring assign (props name), 0x71a60 vector grow
  0x3e4850 Layer base ctor, 0x3e48c0 Layer::init(seed), 0x89ec0 BiomeInitLayer ctor (the four climate arrays)
  0x8a290  BiomeInitLayer::getArea (+ 0x3e4940 initRandom, 0x3e4670 nextRandom, 0x3e45d0 isOcean), after
           the biomes195 array rewrite
  0x859e0  Biome::getBiome(id) (RegionHills mutation lookup), 0x85990 Biome temperature category
  0x85450 / 0x854b0 base grass / foliage getters on the mod biome (colormap path), the mod biome's private
           colour slots resolved through its vtable
Stubbed (out of scope, each listed where it is used): operator new/new[]/delete (0xe6cfd0/0xe6cfc0/0xe6cfb0/0xe6cfa0),
libc wmemcpy 0xe6d110 and memmove/memcpy 0xe6d0a0/0xe6d090, Mth::clamp 0x49e800 (vblendvps is not emulated by Unicorn; reimplemented exactly),
the FlowerFeature ctor 0x2ea330 and BiomeDecorator ctor 0x86c00 (they need live Block objects),
the profiler marker 0xbe80b0 (a bare ret in the image, kept real), IntCache::allocate 0x376860 (TLS), the
biome name map insert 0x141560/0x141750 (hash map on BSS 0x140a7c0), the parent layer's getArea, the block
registry/BlockState virtuals the base ctor queries for grass/dirt ids, and GrassColor/FoliageColor colormaps
(0x337040/0x2eef40: their pixel buffer is runtime data).
This tests code paths and layouts only. It is not a gameplay test.
"""
import struct, sys
from pathlib import Path
from unicorn import UcError, Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
img = IMAGE.read_bytes()
HEAP, STACK, DONE, OBJ, STUB = 0x8000000, 0x4000000, 0x5000000, 0x6000000, 0x7000000
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
    for a in (STACK, DONE, OBJ, STUB):
        u.mem_map(a, 0x100000)
    u.mem_map(HEAP, 0x400000)
    wq(u, 0x134bae8, OBJ + 0xff000)        # stack-guard pointer slot (__stack_chk_guard)
    return u


def q(u, a): return struct.unpack('<Q', u.mem_read(a, 8))[0]
def d(u, a): return struct.unpack('<I', u.mem_read(a, 4))[0]
def f(u, a): return struct.unpack('<f', u.mem_read(a, 4))[0]
def wq(u, a, v): u.mem_write(a, struct.pack('<Q', v & 0xffffffffffffffff))
def wd(u, a, v): u.mem_write(a, struct.pack('<I', v & 0xffffffff))


heap = [HEAP]
def alloc(m):
    n = m.reg_read(UC_X86_REG_RDI); p = heap[0]; heap[0] += (n + 15) & ~15
    m.mem_write(p, bytes(n)); return p


YMM_ZERO_STORES = {}
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_REG
_cs = Cs(CS_ARCH_X86, CS_MODE_64); _cs.detail = True


def ymm_store(u, rip):
    """Unicorn lacks 256-bit AVX. Emulate only `vmov{u,a}p{s,d}/vmovdqu %ymmN, mem` when xmmN == 0
    (every such site in the code run here zeroes ymmN with a VEX vxorps first); anything else fails."""
    ins = next(_cs.disasm(bytes(u.mem_read(rip, 15)), rip), None)
    if not ins or ins.mnemonic not in ('vmovups', 'vmovaps', 'vmovdqu', 'vmovdqa') or len(ins.operands) != 2:
        return None
    dst, src = ins.operands
    if dst.type != X86_OP_MEM or src.type != X86_OP_REG or not ins.reg_name(src.reg).startswith('ymm'):
        return None
    n = int(ins.reg_name(src.reg)[3:])
    if u.reg_read(UC_X86_REG_XMM0 + n) != 0:
        return None
    ea = dst.mem.disp
    if dst.mem.base: ea += u.reg_read(_uc_reg(ins.reg_name(dst.mem.base)))
    if dst.mem.index: ea += u.reg_read(_uc_reg(ins.reg_name(dst.mem.index))) * dst.mem.scale
    u.mem_write(ea & 0xffffffffffffffff, bytes(32))
    return rip + ins.size


def _uc_reg(name):
    import unicorn.x86_const as c
    return getattr(c, 'UC_X86_REG_' + name.upper())

def xf(m, i): return struct.unpack('<f', struct.pack('<I', m.reg_read(UC_X86_REG_XMM0 + i) & 0xffffffff))[0]
def clamp(m):           # Mth::clamp 0x49e800 uses vblendvps (absent in Unicorn): same semantics in Python
    v, lo, hi = xf(m, 0), xf(m, 1), xf(m, 2)
    return {'xmm0': lo if v < lo else min(v, hi)}
def wmemcpy(m):         # libc PLT 0xe6d110 (wmemcpy, 2-byte wchar_t): used by wstring assign 0x67840
    dst, src, n = (m.reg_read(r) for r in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX))
    m.mem_write(dst, bytes(m.mem_read(src, 2 * n))); return dst
def memmove(m):         # libc PLT 0xe6d0a0 (memmove/memcpy, bytes): used by vector grow 0x71a60
    dst, src, n = (m.reg_read(r) for r in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX))
    if n: m.mem_write(dst, bytes(m.mem_read(src, n)))
    return dst
BASE_STUBS = {0x49e800: clamp, 0xe6d110: wmemcpy, 0xe6d0a0: memmove, 0xe6d090: memmove, 0xe6cfd0: alloc, 0xe6cfc0: alloc, 0xe6cfb0: lambda m: 0, 0xe6cfa0: lambda m: 0}


def call(u, fn, args=(), xmm=(), stubs=None, limit=2000000):
    st = dict(BASE_STUBS); st.update(stubs or {})
    regs = [UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RCX, UC_X86_REG_R8, UC_X86_REG_R9]
    for r, v in zip(regs, args):
        u.reg_write(r, v & 0xffffffffffffffff)
    for i, v in enumerate(xmm):
        u.reg_write(UC_X86_REG_XMM0 + i, struct.unpack('<Q', struct.pack('<d', v))[0])
    sp = STACK + 0xf000
    wq(u, sp, DONE)
    u.reg_write(UC_X86_REG_RSP, sp)
    seen = []

    def hook(m, a, size, _):
        if a == DONE:
            m.emu_stop(); return
        if a in YMM_ZERO_STORES:              # Unicorn has no 256-bit AVX: emulate `vmovups %ymm0,disp(%reg)`
            reg, disp, ln = YMM_ZERO_STORES[a]  # (ymm0 is always freshly zeroed by vxorps at these sites)
            m.mem_write(m.reg_read(reg) + disp, bytes(32))
            m.reg_write(UC_X86_REG_RIP, a + ln); return
        if a in st:
            seen.append(a)
            rv = st[a](m)
            if isinstance(rv, dict) and 'jump' in rv:      # tail call: continue at another address
                m.reg_write(UC_X86_REG_RIP, rv['jump']); return
            if isinstance(rv, dict):
                for k, v in rv.items():
                    m.reg_write(UC_X86_REG_XMM0, struct.unpack('<I', struct.pack('<f', v))[0])
            elif isinstance(rv, tuple):
                m.reg_write(UC_X86_REG_RAX, rv[0]); m.reg_write(UC_X86_REG_RDX, rv[1])
            else:
                m.reg_write(UC_X86_REG_RAX, rv or 0)
            s = m.reg_read(UC_X86_REG_RSP)
            m.reg_write(UC_X86_REG_RIP, q(m, s)); m.reg_write(UC_X86_REG_RSP, s + 8)
    h = u.hook_add(UC_HOOK_CODE, hook)
    start = fn
    while True:
        try:
            u.emu_start(start, DONE + 0x10, count=limit)
            if u.reg_read(UC_X86_REG_RIP) != DONE:
                raise RuntimeError(f'emulation of {fn:#x} stopped early at rip={u.reg_read(UC_X86_REG_RIP):#x} ret={q(u, u.reg_read(UC_X86_REG_RSP)):#x}')
            break
        except UcError as e:
            rip = u.reg_read(UC_X86_REG_RIP)
            nxt = ymm_store(u, rip)
            if nxt is None:
                raise RuntimeError(f'emulation of {fn:#x} failed at rip={rip:#x}: {e}')
            start = nxt
    u.hook_del(h)
    return u.reg_read(UC_X86_REG_RAX), seen


# Biome* globals written by the biome static init 0x7f900 (global RVA -> biome id), pinned statically.
BIOME_GLOBALS = {0x140a810: 1, 0x140a818: 2, 0x140a820: 3, 0x140a828: 4, 0x140a830: 5, 0x140a838: 6,
                 0x140a868: 12, 0x140a8b0: 21, 0x140a8d8: 27, 0x140a8e8: 29, 0x140a8f0: 30, 0x140a900: 32,
                 0x140a918: 35, 0x140a930: 38, 0x140a938: 39, 0x140a940: 0}
ARR = {1: 0x38, 2: 0x48, 3: 0x58, 4: 0x68}      # BiomeInitLayer climate -> {Biome** +0, int len +8}
MOD_ID = 170


def fake_biomes(u):
    objs = {}
    for g, bid in BIOME_GLOBALS.items():
        o = OBJ + 0x40000 + bid * 0x200
        wd(u, o + 0x68, bid)
        wq(u, g, o)
        objs[bid] = o
    return objs


def make_bil(u, seed):
    """Construct a BiomeInitLayer with the real ctor; normal level type (not 1_1, not customized)."""
    wq(u, 0x1418a18, 0x1111)      # LevelType::lvl_normal_1_1 global (BSS) -> some other object
    wq(u, 0x14185a0, 0x2222)      # LevelType::customized
    parent_ref = OBJ + 0x3000     # shared_ptr<Layer> {obj, ctrl}
    parent = OBJ + 0x3100
    wq(u, parent, STUB + 0x800 - 0x20 * 0)    # parent vptr: slot +0x20 (getArea) -> STUB+0x820
    wq(u, STUB + 0x800 + 0x20, STUB + 0x20)    # parent getArea (stubbed per call)
    wq(u, STUB + 0x800 + 0x10, STUB + 0x10)    # parent init(seed): stub
    wq(u, parent_ref, parent); wq(u, parent_ref + 8, 0)
    bil = OBJ + 0x2000
    call(u, 0x89ec0, (bil, 7, parent_ref, 0x3333, 0))
    call(u, 0x3e48c0, (bil, seed), stubs={STUB + 0x10: lambda m: 0})  # Layer::init(worldSeed), real
    return bil


def read_array(u, bil, climate):
    p, n = q(u, bil + ARR[climate]), d(u, bil + ARR[climate] + 8)
    return [d(u, q(u, p + 8 * i) + 0x68) for i in range(n)]


def rewrite(u, bil, climate, mod_obj, copies):
    """The biomes195 array rewrite (b195_layer_patch): new[] array = stock entries, then the mod biome
    `copies` times; store the pointer first, then the length (readers load len, then ptr)."""
    old_p, old_n = q(u, bil + ARR[climate]), d(u, bil + ARR[climate] + 8)
    new = heap[0]; heap[0] += ((old_n + copies) * 8 + 15) & ~15
    for i in range(old_n): wq(u, new + 8 * i, q(u, old_p + 8 * i))
    for i in range(copies): wq(u, new + 8 * (old_n + i), mod_obj)
    wq(u, bil + ARR[climate], new); wd(u, bil + ARR[climate] + 8, old_n + copies)


def get_area(u, bil, climates, w, h):
    src, dst = OBJ + 0x8000, OBJ + 0xc000
    u.mem_write(src, struct.pack(f'<{w*h}i', *climates))
    stubs = {STUB + 0x20: lambda m: (src, w * h),           # parent->getArea
             0x376860: lambda m: (dst, m.reg_read(UC_X86_REG_RDI))}   # IntCache::allocate
    r, seen = call(u, 0x8a290, (bil, 0, 0, w, h), stubs=stubs)
    return list(struct.unpack(f'<{w*h}i', u.mem_read(dst, 4 * w * h))), r


# 1. BiomeInitLayer: the stock climate arrays are per-instance heap data (ctor 0x89ec0)
u = machine(); heap[0] = HEAP
objs = fake_biomes(u)
bil = make_bil(u, 12345)
check(q(u, bil) == 0x12163b8, 'BiomeInitLayer vptr 0x12163b8')
check(read_array(u, bil, 1) == [2, 2, 2, 35, 35, 1], 'warm array desert x3, savanna x2, plains')
check(read_array(u, bil, 2) == [4, 29, 3, 1, 27, 6], 'medium array forest, roofed, hills, plains, birch, swamp')
check(read_array(u, bil, 3) == [4, 3, 5, 1], 'cold array forest, hills, taiga, plains')
check(read_array(u, bil, 4) == [12, 12, 12, 30], 'icy array ice plains x3, cold taiga')
check(q(u, bil + 0x78) == 0, 'no customized settings (+0x78 null)')
check(q(u, 0x1409fc0 + 8 * MOD_ID) == 0, 'biome id 170 is free before registration')

W = H = 32
stock, _ = get_area(u, bil, [2] * (W * H), W, H)
check(set(stock) <= {4, 29, 3, 1, 27, 6} and len(set(stock)) == 6, 'stock getArea picks only medium biomes')
check(MOD_ID not in stock, 'stock output has no mod biome')

# 2. After the rewrite the REAL getArea selects the mod biome (medium, weight 2 slots of 8)
mod_fake = OBJ + 0x50000; wd(u, mod_fake + 0x68, MOD_ID)
rewrite(u, bil, 2, mod_fake, 2)
check(read_array(u, bil, 2) == [4, 29, 3, 1, 27, 6, MOD_ID, MOD_ID], 'medium array after rewrite')
out, _ = get_area(u, bil, [2] * (W * H), W, H)
frac = out.count(MOD_ID) / len(out)
check(0.18 < frac < 0.32, f'mod biome share ~2/8 of medium cells (got {frac:.3f})')
same, _ = get_area(u, bil, [2] * (W * H), W, H)
check(same == out, 'deterministic: same seed + area -> same ids')
mixed, _ = get_area(u, bil, [1, 2, 3, 4] * (W * H // 4), W, H)
check(all(v == MOD_ID or v in (4, 29, 3, 1, 27, 6) for v in mixed[1::4]), 'only medium cells can become the mod biome')
check(MOD_ID not in mixed[0::4] + mixed[2::4] + mixed[3::4], 'warm/cold/icy cells untouched')
oc, _ = get_area(u, bil, [0] * 64, 8, 8)
check(set(oc) == {0}, 'ocean cells (isOcean 0x3e45d0) pass through')
# a different world seed still selects it, at a different layout
bil2 = make_bil(u, 999)
rewrite(u, bil2, 2, mod_fake, 2)
out2, _ = get_area(u, bil2, [2] * (W * H), W, H)
check(out2 != out and MOD_ID in out2, 'other seed: different layout, mod biome present')

# 3. Biome::getBiome and the RegionHills mutation lookup for a mod id
wq(u, 0x1409fc0 + 8 * MOD_ID, mod_fake)
r, _ = call(u, 0x859e0, (MOD_ID,)); check(r == mod_fake, 'getBiome(170) = biomes[170]')
r, _ = call(u, 0x859e0, (300,)); check(r == q(u, 0x140a940), 'getBiome(>256) = ocean')


# 4. A mod biome built with the REAL PlainsBiome ctor 0x53d140 at the free id 170 (what b195_construct does)
u = machine(); heap[0] = HEAP
objs = fake_biomes(u)
REG, GRASS, DIRT = OBJ + 0x70000, OBJ + 0x71000, OBJ + 0x72000
wq(u, 0x140ad68, REG); wq(u, REG, STUB + 0x900); wq(u, STUB + 0x900 + 0x38, STUB + 0x38)   # block registry getId
for b in (GRASS, DIRT):
    wq(u, b, STUB + 0xa00); wq(u, b + 0x190, b + 0x100)
wq(u, STUB + 0xa00 + 0x50, STUB + 0x50)                                                  # Block::getData(state)
wq(u, 0x1409fb0, GRASS); wq(u, 0x1409fa8, DIRT)
name_nodes = []
def map_find(m):        # 0x141560(out, map, key): returns an iterator; we hand back a fresh node (insert done)
    node = heap[0]; heap[0] += 0x40; m.mem_write(node, bytes(0x40)); name_nodes.append(node)
    out = m.reg_read(UC_X86_REG_RDI); m.mem_write(out, struct.pack('<QQQ', 0, 0, node)); return out
ctor_stubs = {STUB + 0x38: lambda m: 2 if m.reg_read(UC_X86_REG_RSI) == GRASS else 3,
              STUB + 0x50: lambda m: 0, 0x141560: map_find, 0x141750: lambda m: 0,
              0x2ea330: lambda m: 0,   # FlowerFeature ctor (needs live flower Block objects): stubbed
              0x86c00: lambda m: 0}    # BiomeDecorator ctor (builds features from live Block/BlockState objects): stubbed
# props (0x48): wstring name +0 (+8 buf, +0x18 size, +0x20 res), +0x28 depth, +0x2c scale, +0x30 temp,
# +0x34 rain, +0x38 water rgb, +0x3c snow, +0x3d rain flag, +0x40 parent id (-1)
props = heap[0]; heap[0] += 0x50
u.mem_write(props, bytes(0x50)); wq(u, props + 0x20, 7)
key = 'gem:crystal_fields'
src = OBJ + 0x90000; u.mem_write(src, key.encode('utf-16-le') + b'\0\0')
call(u, 0x67840, (props, src, len(key)))
u.mem_write(props + 0x28, struct.pack('<ffffIBBxxi', 0.125, 0.05, 0.8, 0.4, 0xffffff, 0, 1, -1))
mod = heap[0]; heap[0] += 0x1e0; u.mem_write(mod, bytes(0x1e0))
r, seen = call(u, 0x53d140, (mod, MOD_ID, 0, props), stubs=ctor_stubs)
check(q(u, mod) == 0x12a2bb0, 'mod biome vptr = PlainsBiome 0x12a2bb0')
check(d(u, mod + 0x68) == MOD_ID, 'id at +0x68')
check(q(u, 0x1409fc0 + 8 * MOD_ID) == mod, 'ctor stored biomes[170] = mod (0x840cd)')
check(bytes(u.mem_read(mod + 0x10, 16)) == struct.pack('<ffff', 0.125, 0.05, 0.8, 0.4),
      'depth/scale/temperature/rainfall at +0x10..+0x1c')
check(u.mem_read(mod + 0x20, 2) == b'\x00\x01' and d(u, mod + 0x24) == 0xffffffff, 'snow 0, rain 1, parent -1')
nlen = q(u, mod + 0x28 + 0x18); nres = q(u, mod + 0x28 + 0x20)
nptr = q(u, mod + 0x28 + 8) if nres >= 8 else mod + 0x28 + 8
check(nlen == len(key) and u.mem_read(nptr, 2 * nlen).decode('utf-16-le') == key, 'name wstring at +0x28')
check(d(u, mod + 0x50) == 2 and d(u, mod + 0x58) == 3, 'top/filler block ids at +0x50/+0x58 (grass, dirt)')
check(q(u, mod + 0x60) != 0 and 0x86c00 in seen, 'decorator allocated and its ctor called (+0x60, 0x86c00 stubbed)')
check(name_nodes and q(u, name_nodes[0] + 0x38) == mod, 'registered in the name map (node+0x38)')
check(q(u, mod + 0xa0) > q(u, mod + 0x98), 'spawn list vector filled')
# write the descriptor's top/filler blocks the way b195 does after the ctor
wd(u, mod + 0x50, 12); wd(u, mod + 0x54, 0); wd(u, mod + 0x58, 24); wd(u, mod + 0x5c, 0)
r, _ = call(u, 0x85990, (mod,)); check(r & 0xffffffff == 2, 'temperature category (0x85990) of t=0.8 -> 2 (medium)')
r, _ = call(u, 0x859e0, (MOD_ID,)); check(r == mod, 'getBiome(170) returns the mod biome')
# the real getArea, arrays rewritten with the real object
bil = make_bil(u, 4242); rewrite(u, bil, 2, mod, 2)
out, _ = get_area(u, bil, [2] * 1024, 32, 32)
check(MOD_ID in out, 'real getArea -> real mod biome object id')

# 5. Colours: stock grass/foliage getters run on the mod biome; the private slots override them
cm = []
col_stubs = {0x337040: lambda m: cm.append('grass') or 0xff112233, 0x2eef40: lambda m: cm.append('fol') or 0xff445566}
temp_vt = q(u, 0x12a2bb0 + 0x50)       # getTemperature(pos) slot of PlainsBiome
r, _ = call(u, q(u, 0x12a2bb0 + 0x60), (mod, OBJ + 0x95000), stubs=col_stubs)
check(cm[-1:] == ['grass'] and r & 0xffffffff == 0xff112233, 'PlainsBiome grass slot +0x60 -> GrassColor colormap')
r, _ = call(u, q(u, 0x12a2bb0 + 0x68), (mod, OBJ + 0x95000), stubs=col_stubs)
check(cm[-1:] == ['fol'] and r & 0xffffffff == 0xff445566, 'PlainsBiome foliage slot +0x68 -> FoliageColor colormap')


# 6. The getArea vtable slot path, driven by a REAL consumer: ZoomLayer::getArea 0x7acfc0 calls its parent
#    through vt+0x20. The BiomeInitLayer slot 0x12163d8 (RELRO word) is CASed to the biomes195 hook (here a
#    Python model of lce_b195_get_area: patch the layer once, then tail-call the stock 0x8a290).
u = machine(); heap[0] = HEAP
objs = fake_biomes(u)
mod_fake = OBJ + 0x50000; wd(u, mod_fake + 0x68, MOD_ID)
bil = make_bil(u, 777)
check(q(u, 0x12163d8) == 0x8a290, 'RELRO slot holds the stock getArea before install')
HOOK = STUB + 0x40
wq(u, 0x12163d8, HOOK)                    # what lce_b195_select's patch_ro8 does
patched = []
def hook_get_area(m):
    me = m.reg_read(UC_X86_REG_RDI)
    last = q(m, q(m, me + 0x48) + 8 * (d(m, me + 0x50) - 1))
    if last != mod_fake:                  # lce_b195_patch_layer: once per layer object
        rewrite(m, me, 2, mod_fake, 2); patched.append(me)
    return {'jump': 0x8a290}
zoom = OBJ + 0x1000
wq(u, zoom, 0x12fdbe8); wq(u, zoom + 0x10, bil); wq(u, zoom + 0x18, 0)
call(u, 0x3e48c0, (zoom, 777), stubs={STUB + 0x10: lambda m: 0})  # Layer::init on the zoom layer also inits its parent (via slot +0x10)
check(q(u, 0x12fdbe8 + 0x20) == 0x7acfc0, 'ZoomLayer getArea 0x7acfc0')
bufs = [OBJ + 0xa0000 + i * 0x4000 for i in range(8)]
def intcache(m):
    b = bufs.pop(0); return (b, m.reg_read(UC_X86_REG_RDI))
src = OBJ + 0x8000
u.mem_write(src, struct.pack('<1024i', *([2] * 1024)))
zstubs = {STUB + 0x20: lambda m: (src, 1024), 0x376860: intcache, HOOK: hook_get_area}
r, seen = call(u, 0x7acfc0, (zoom, 0, 0, 16, 16), stubs=zstubs)
res = list(struct.unpack('<256i', u.mem_read(r, 1024)))
check(patched == [bil] and HOOK in seen, 'consumer reached the hook through the BiomeInitLayer vtable slot; layer patched once')
check(MOD_ID in res and set(res) <= {4, 29, 3, 1, 27, 6, MOD_ID}, 'zoomed real output contains the mod biome')
bufs[:] = [OBJ + 0xa0000 + i * 0x4000 for i in range(8)]
r2, _ = call(u, 0x7acfc0, (zoom, 0, 0, 16, 16), stubs=zstubs)
check(patched == [bil] and list(struct.unpack('<256i', u.mem_read(r2, 1024))) == res, 'second call: no re-patch, same output')

print(f'test_unicorn_retail: {checks} checks, {fails} failures')
sys.exit(1 if fails else 0)
