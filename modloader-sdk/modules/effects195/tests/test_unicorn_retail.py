"""effects195 against the real 1.95 code in Unicorn.

The module's own C publishers (built as build/host/libeffects195_uc.so with
tests/uc_shim.cpp) run on the host; every native they call executes the pinned
retail image in Unicorn, and their memory accessor reads/writes Unicorn memory.
The real startup order is reproduced first: MobEffect registry ctor 0x4c2dc0,
effect initializer 0x4781e0 (31 effects), statics 0x47b780, Potion registry
construction 0x588f5f..0x589032, potion initializer 0x55e6d0 (43 potions).

Stubs (out of scope, see uc_common.py): operator new/new[]/delete/delete[],
stack-check failure, libc wcslen/wmemcpy/wmemmove/memmove/wmemcmp/wmemchr, the
AVX2 wstring hash 0x68b350 (reimplemented from its scalar tail), and in the
name test the Localizer lookup 0xaac9b0 (records the string id). The fake
LivingEntity's virtuals getHealth/getMaxHealth/heal are Python markers.
This proves code paths and layouts, not gameplay.
"""
import ctypes, struct, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from uc_common import *

HERE = Path(__file__).resolve().parent
SO = HERE.parent / 'build' / 'host' / 'libeffects195_uc.so'
SO.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(['g++', '-std=c++17', '-O1', '-g', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror', '-DLCE195E_HOST',
                str(HERE.parent / 'effects195.cpp'), str(HERE / 'uc_shim.cpp'), '-o', str(SO)], check=True)
lib = ctypes.CDLL(str(SO))

checks = fails = 0
def check(c, what):
    global checks, fails
    checks += 1
    if not c:
        fails += 1; print('FAIL', what)

u = machine()
boot_effects_and_potions(u)
check(d(u, 0x14195d0) == 31 and d(u, 0x142e940) == 43, 'stock initializers ran: 31 effects, 43 potions')

# stock tables compiled into the module equal what the real initializers produced
lib.lce195e_stock_icon.restype = ctypes.c_uint32; lib.lce195e_stock_colour.restype = ctypes.c_uint32
lib.lce195e_stock_effect_id.restype = ctypes.c_int32; lib.lce195e_stock_potion_id.restype = ctypes.c_int32
tab_ok = True
for i in range(1, 32):
    o, _ = call(u, 0x490d60, (i,))
    tab_ok &= d(u, o + 0x5c) == lib.lce195e_stock_icon(i) and d(u, o + 0x74) == lib.lce195e_stock_colour(i)
check(tab_ok, 'C icon/colour tables == stock effect fields +0x5c/+0x74 for ids 1..31')

def key_map(regp, voff):
    out = {}; h = q(u, regp + 0x18); n = q(u, h)
    def wt(w):
        k = q(u, w + 0x18); p = q(u, w + 8) if q(u, w + 0x20) > 7 else w + 8
        return bytes(u.mem_read(p, 2 * k)).decode('utf-16le')
    while n != h:
        out[wt(n + 0x10 + 0x40) + ':' + wt(n + 0x10 + 0x18)] = q(u, n + voff); n = q(u, n)
    return out
ek = key_map(q(u, 0x14195c8), 0x80)
check(len(ek) == 31 and all(call(u, 0x490d60, (lib.lce195e_stock_effect_id(k.encode()),))[0] == v for k, v in ek.items()),
      'C effect name->id table matches the registry keys of the real initializer')
pk = key_map(q(u, 0x142e938), 0x80)
check(len(pk) == 43 and all(call(u, 0x55e560, (lib.lce195e_stock_potion_id(k.encode()),))[0] == v for k, v in pk.items()),
      'C potion name->id table matches the registry keys of the real initializer')

# ---- host scratch buffers (wstring / ResourceLocation) mirrored into Unicorn at stable addresses
mirror = {}
def m_addr(h, size=0x40):   # exact C buffer sizes (Rl: ns/path 0x40, rl 0x80): mirrors must not overlap
    if h not in mirror: mirror[h] = (alloc(u, size), size)
    return mirror[h][0]
def m_in(h):
    a, n = mirror[h]; u.mem_write(a, ctypes.string_at(h, n))
def m_out(h):
    a, n = mirror[h]; ctypes.memmove(h, bytes(u.mem_read(a, n)), n)
def uc(fn, *args, xmm0=None):
    r, _ = call(u, fn, args, xmm0=xmm0, limit=5_000_000); return r

V = ctypes.c_void_p; U64 = ctypes.c_uint64; I = ctypes.c_int; SZ = ctypes.c_size_t
F_alloc = ctypes.CFUNCTYPE(U64, SZ)(lambda n: alloc(u, n))
F_release = ctypes.CFUNCTYPE(None, U64)(lambda p: u.frees.append(p))
def wstring(dst, units, n):
    m_addr(dst); m_in(dst)
    src = alloc(u, 2 * n + 2); u.mem_write(src, ctypes.string_at(units, 2 * n) + b'\0\0')
    uc(0x67840, mirror[dst][0], src, n); m_out(dst)
F_wstring = ctypes.CFUNCTYPE(None, U64, U64, U64)(wstring)
def rl_ctor(rl, ns, path):
    m_addr(rl, 0x80); m_addr(ns); m_addr(path)
    for h in (rl, ns, path): m_in(h)
    uc(0xcda930, mirror[rl][0], mirror[ns][0], mirror[path][0])
    for h in (rl, ns, path): m_out(h)
F_rl_ctor = ctypes.CFUNCTYPE(None, U64, U64, U64)(rl_ctor)
rl_dtors = []
def rl_dtor(rl):
    m_addr(rl, 0x80); m_in(rl); uc(0xcdab00, mirror[rl][0]); m_out(rl); rl_dtors.append(rl)
F_rl_dtor = ctypes.CFUNCTYPE(None, U64)(rl_dtor)
F_effect_ctor = ctypes.CFUNCTYPE(None, U64, I, I)(lambda o, h, c: uc(0x490db0, o, h, c))
F_add_mod = ctypes.CFUNCTYPE(U64, U64, U64, I, I, ctypes.c_double)(lambda o, a, i, op, amt: uc(0x4917d0, o, a, i, op, xmm0=amt))
F_effect_by_id = ctypes.CFUNCTYPE(U64, I)(lambda i: uc(0x490d60, i))
F_mei = ctypes.CFUNCTYPE(None, U64, U64, I, I)(lambda m, e, dur, amp: uc(0x491aa0, m, e, dur, amp))
F_potion_by_id = ctypes.CFUNCTYPE(U64, I)(lambda i: uc(0x55e560, i))
F_vec_grow = ctypes.CFUNCTYPE(None, U64, I)(lambda v, n: uc(0x6fd00, v, n))
def reg_register(reg, i, rl, v):
    m_addr(rl, 0x80); m_in(rl); uc(q(u, q(u, reg) + 0x28), reg, i, mirror[rl][0], v); m_out(rl)
F_reg_register = ctypes.CFUNCTYPE(None, U64, I, U64, U64)(reg_register)
def reg_get(reg, rl):
    m_addr(rl, 0x80); m_in(rl); r = uc(q(u, q(u, reg) + 0x00), reg, mirror[rl][0]); m_out(rl); return r
F_reg_get = ctypes.CFUNCTYPE(U64, U64, U64)(reg_get)
def mread(ctx, a, buf, n):
    try: ctypes.memmove(buf, bytes(u.mem_read(a, n)), n); return 1
    except Exception: return 0
def mwrite(ctx, a, buf, n):
    try: u.mem_write(a, ctypes.string_at(buf, n)); return 1
    except Exception: return 0
F_read = ctypes.CFUNCTYPE(I, V, U64, U64, SZ)(mread)
F_write = ctypes.CFUNCTYPE(I, V, U64, U64, SZ)(mwrite)
F_held = ctypes.CFUNCTYPE(I, V)(lambda c: 1)
fns = (V * 13)(*[ctypes.cast(f, V) for f in (F_alloc, F_release, F_wstring, F_rl_ctor, F_rl_dtor, F_effect_ctor, F_add_mod,
                                            F_effect_by_id, F_mei, F_potion_by_id, F_vec_grow, F_reg_register, F_reg_get)])
mem = (V * 3)(*[ctypes.cast(f, V) for f in (F_read, F_write, F_held)])
THUNK = [DONE + 0x1000, DONE + 0x1100, DONE + 0x1200]      # marker addresses written into the private vtables
lib.shim_set_env(fns, mem, (U64 * 3)(*THUNK))

# ---- 1. effects: regeneration clone, speed clone (attribute), strength clone (AttackDamage), custom
keys = [b'gem:regen2', b'gem:fast', b'gem:strong', b'gem:instant', b'gem:mark']
ids = [32, 33, 34, 35, 64]
sids = [0x4c430020, 0x4c430021, 0x4c430022, 0x4c430023, 0x4c430040]
clones = [10, 1, 5, 6, 0]
colours = [0, 0, 0, 0, 0x104]
icons = [0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 6]
ben = [1, 1, 1, 1, 0]
intervals = [0, 0, 0, 0, 20]
lib.shim_effect_record.restype = U64; lib.shim_potion_record.restype = U64
lib.lce195e_forward_target.restype = U64; lib.lce195e_forward_target.argtypes = [U64, ctypes.c_uint, ctypes.POINTER(U64)]
rc = lib.shim_publish_effects(len(keys), (ctypes.c_char_p * 5)(*keys), (ctypes.c_int32 * 5)(*ids), (ctypes.c_uint32 * 5)(*sids),
                              (ctypes.c_int32 * 5)(*clones), (ctypes.c_uint32 * 5)(*colours), (ctypes.c_uint32 * 5)(*icons),
                              (ctypes.c_uint8 * 5)(*ben), (ctypes.c_uint32 * 5)(*intervals))
check(rc == 1, f'effects publish rc=1 (got {rc})')
objs = [lib.shim_effect_record(i, 0) for i in range(5)]
for i, o in enumerate(objs):
    check(uc(0x490d60, ids[i]) == o, f'real MobEffect::byId({ids[i]}) returns the mod effect')
    check(d(u, o + 0x58) == sids[i] and uc(0x491590, o) & 0xffffffff == sids[i], 'name sid via real getDescriptionId 0x491590')
    vt = q(u, o)
    check(vt == lib.shim_effect_record(i, 1) + 16 and q(u, vt) == THUNK[0] and q(u, vt + 8) == THUNK[1] and q(u, vt + 16) == THUNK[2], 'private vtable, slots 0/1/2 = thunks')
check(d(u, 0x14195d0) == 64, 'count global = highest id (HUD array 0xa00abb covers id 64)')
check(all(uc(0x490d60, i) for i in range(1, 32)) and uc(0x490d60, 36) == 0 and uc(0x490d60, 63) == 0, 'stock 1..31 intact, gaps stay null')
# fields through the REAL getters/virtuals of the private vtable
fast, strong, mark = objs[1], objs[2], objs[4]
check(uc(q(u, q(u, fast) + 0x38), fast) & 0xffffffff == 0xfd, 'getColor (slot 7) on the speed clone = Effect_MovementSpeed')
check(uc(0x4915b0, objs[0]) & 0xffffffff == 11 and uc(0x4915a0, objs[0]) & 0xff == 1, 'icon frame of the regeneration clone via 0x4915b0/0x4915a0')
check(uc(0x4915b0, mark) & 0xffffffff == 6 and uc(q(u, q(u, mark) + 0x38), mark) & 0xffffffff == 0x104 and b(u, mark + 0x60) == 1, 'custom effect icon/colour/harmful')
check(q(u, fast + 0x18) == 1 and lib.shim_effect_record(1, 3) == 1, 'speed modifier copied through the real addAttributeModifier 0x4917d0')
node = q(u, q(u, fast + 0x10)); mod = q(u, node + 0x18); sp_stock = uc(0x490d60, 1); snode = q(u, q(u, sp_stock + 0x10))
check(q(u, node + 0x10) == q(u, snode + 0x10) and dbl(u, mod) == dbl(u, q(u, snode + 0x18)) and d(u, mod + 8) == d(u, q(u, snode + 0x18) + 8)
      and d(u, mod + 0x38) == d(u, q(u, snode + 0x18) + 0x38), 'modifier attribute/amount/op/id equal the stock speed modifier')
check(q(u, strong) == lib.shim_effect_record(2, 1) + 16 and q(u, lib.shim_effect_record(2, 1) + 8) == q(u, 0x120b560 - 8), 'strength clone copies the AttackDamageMobEffect class (RTTI)')
check(dbl(u, strong + 0x78) == dbl(u, uc(0x490d60, 5) + 0x78), 'AttackDamage +0x78 multiplier copied')
# getAttributeModifierValue (slot 12) is the AttackDamage class function, run on the clone
smod = q(u, q(u, q(u, uc(0x490d60, 5) + 0x10)) + 0x18)       # the stock strength modifier template
call(u, q(u, q(u, strong) + 0x60), (strong, 1, smod), limit=100000); x1 = u.reg_read(UC_X86_REG_XMM0) & 0xffffffffffffffff
call(u, q(u, q(u, uc(0x490d60, 5)) + 0x60), (uc(0x490d60, 5), 1, smod), limit=100000); x2 = u.reg_read(UC_X86_REG_XMM0) & 0xffffffffffffffff
check(x1 == x2, 'getAttributeModifierValue (slot 12, AttackDamage class) gives the same value on the clone as on stock strength')
inst = objs[3]
r, _ = call(u, q(u, q(u, inst) + 0x18), (inst,)); check(r & 0xff == 1, 'instant_health clone: isInstantenous (slot 3, InstantenousMobEffect class) = 1')
r, _ = call(u, q(u, q(u, fast) + 0x18), (fast,)); check(r & 0xff == 0, 'speed clone: isInstantenous = 0')

# ---- 2. forwarded ticks: REAL MobEffectInstance::tick 0x491bc0 on the regeneration clone and a fake LivingEntity
regen = objs[0]
mei = alloc(u, 0x18); uc(0x491aa0, mei, regen, 100, 0)
ENT_VT = alloc(u, 0x1000); ent = alloc(u, 0x400); wq(u, ent, ENT_VT); u.mem_write(ent + 8, bytes(0x3f8))
SP = alloc(u, 16); wq(u, SP, ent); wq(u, SP + 8, 0)
M_HEALTH, M_MAXH, M_HEAL = DONE + 0x2000, DONE + 0x2100, DONE + 0x2200
wq(u, ENT_VT + 0x9b8, M_HEALTH); wq(u, ENT_VT + 0xa90, M_MAXH); wq(u, ENT_VT + 0x9b0, M_HEAL)
heals = []; fwd = []
def fl(x): return struct.unpack('<I', struct.pack('<f', x))[0]
def ret(m):
    s = m.reg_read(UC_X86_REG_RSP); m.reg_write(UC_X86_REG_RIP, q(m, s)); m.reg_write(UC_X86_REG_RSP, s + 8)
def thunk(slot):
    def h(m):
        so = U64(0)
        tgt = lib.lce195e_forward_target(m.reg_read(UC_X86_REG_RDI), slot, ctypes.byref(so))
        fwd.append((slot, m.reg_read(UC_X86_REG_RDI), so.value, tgt))
        if not tgt:
            # no vanilla behaviour: slot 2 is answered by the C thunk itself (tick_interval); slot 0 would call on_tick
            rv = lib.lce195e_thunk_ready(m.reg_read(UC_X86_REG_RDI), m.reg_read(UC_X86_REG_RSI) & 0xffffffff, m.reg_read(UC_X86_REG_RDX) & 0xffffffff) if slot == 2 else 0
            m.reg_write(UC_X86_REG_RAX, rv); ret(m); return
        m.reg_write(UC_X86_REG_RDI, so.value); m.reg_write(UC_X86_REG_RIP, tgt)    # tail call, `this` = stock effect
    return h
def health(m): m.reg_write(UC_X86_REG_XMM0, fl(10.0)); ret(m)
def maxh(m): m.reg_write(UC_X86_REG_XMM0, fl(20.0)); ret(m)
def heal(m): heals.append(struct.unpack('<f', struct.pack('<I', m.reg_read(UC_X86_REG_XMM0) & 0xffffffff))[0]); ret(m)
markers = {THUNK[0]: thunk(0), THUNK[1]: thunk(1), THUNK[2]: thunk(2), M_HEALTH: health, M_MAXH: maxh, M_HEAL: heal}
def mhook(m, a, size, _):
    if a in markers: markers[a](m)
for a in markers: pass
hm = u.hook_add(UC_HOOK_CODE, mhook, begin=DONE + 0x1000, end=DONE + 0x3000)
results = []
for dur in (100, 99, 51, 50):
    wd(u, mei + 8, dur); d0 = dur
    r, _ = call(u, 0x491bc0, (mei, SP), limit=200000)
    results.append((d0, r & 0xff, len(heals)))
check(d(u, mei + 8) == 49, 'MEI::tick decremented the duration')
check([x[2] for x in results] == [1, 1, 1, 2], f'regeneration ticks at duration 100 and 50 only (50 >> amp 0): heals {results}')
check(heals and all(abs(h - 1.0) < 1e-6 for h in heals), 'heal(1.0) from the REAL applyEffectTick 0x490ed0 with this = stock regeneration')
stock_regen = uc(0x490d60, 10)
check(any(s == 2 and so == stock_regen and t == q(u, q(u, stock_regen) + 0x10) for s, _, so, t in fwd), 'slot 2 forwarded to the regeneration class function with the stock pointer')
check(any(s == 0 and so == stock_regen and t == 0x490ed0 for s, _, so, t in fwd), 'slot 0 forwarded to 0x490ed0 with the stock pointer')
# custom effect (no clone): slot 2/0 thunks resolve to "no forward"
lib.lce195e_thunk_ready.argtypes = [U64, ctypes.c_int32, ctypes.c_int32]
fwd.clear(); mei2 = alloc(u, 0x18); uc(0x491aa0, mei2, mark, 40, 0)
call(u, 0x491bc0, (mei2, SP), limit=200000)
check([f[0] for f in fwd] == [2, 0] and fwd[0][3] == 0 and fwd[1][3] == 0 and len(heals) == 2, 'custom effect at duration 40: ready (20-tick interval, C thunk) -> slot 0 thunk, no vanilla forward')
fwd.clear(); call(u, 0x491bc0, (mei2, SP), limit=200000)
check([f[0] for f in fwd] == [2], 'custom effect at duration 39: not ready, slot 0 not called')
u.hook_del(hm)

# ---- 3. NBT round trip of id 32 through the REAL MobEffectInstance::save 0x491c60 / load 0x491e80
tag = alloc(u, 0x58); uc(0x1b6df0, tag)
mei3 = alloc(u, 0x18); uc(0x491aa0, mei3, regen, 200, 2)
call(u, 0x491c60, (mei3, tag), limit=2_000_000)
r, _ = call(u, 0x491e80, (tag,), limit=2_000_000)
check(r and q(u, r) == regen and d(u, r + 8) == 200 and d(u, r + 0xc) == 2, 'save -> load restores the mod effect (Id byte 32), duration and amplifier')
mei4 = alloc(u, 0x18); uc(0x491aa0, mei4, mark, 5, 0); tag2 = alloc(u, 0x58); uc(0x1b6df0, tag2)
call(u, 0x491c60, (mei4, tag2), limit=2_000_000); r, _ = call(u, 0x491e80, (tag2,), limit=2_000_000)
check(r and q(u, r) == mark, 'save -> load of id 64')

# ---- 4. registry key lookup (the /effect and NBT-by-key path): get("gem:fast")
def rl_of(ns, path):
    out = alloc(u, 0x80); uc(0xcda930, out, wstr(u, ns), wstr(u, path)); return out
reg = q(u, 0x14195c8)
check(uc(q(u, q(u, reg)), reg, rl_of('minecraft', 'speed')) == sp_stock, 'stock key still resolves')
check(uc(q(u, q(u, reg)), reg, rl_of('gem', 'fast')) == fast, 'effect registry get(gem:fast) -> mod effect')

# ---- 5. potions: one with [stock speed II 3600, mod regen2 200], one base potion
pkeys = [b'gem:glow_potion', b'gem:base']
psids = [0x4c430100, 0x4c430101, 0x4c430102, 0x4c430103, 0x4c430104, 0x2453270c,
         0x4c430110, 0x4c430111, 0x4c430112, 0x4c430113, 0x4c430114, 0x2453270c]
effs = [sp_stock, regen] + [0] * 6 + [0] * 8
durs = [3600, 200] + [0] * 14; amps = [1, 0] + [0] * 14
rc = lib.shim_publish_potions(2, (ctypes.c_char_p * 2)(*pkeys), (ctypes.c_uint32 * 12)(*psids), (U64 * 16)(*effs),
                              (ctypes.c_int32 * 16)(*durs), (ctypes.c_int32 * 16)(*amps), (ctypes.c_uint32 * 2)(2, 0))
check(rc == 1, f'potions publish rc=1 (got {rc})')
p0, p1 = lib.shim_potion_record(0, 1), lib.shim_potion_record(1, 1)
check(lib.shim_potion_record(0, 0) == 43 and lib.shim_potion_record(1, 0) == 44 and d(u, 0x142e940) == 45, 'potion ids 43/44, counter 45')
check(uc(0x55e560, 43) == p0 and uc(0x55e560, 44) == p1, 'real Potion::byId')
preg = q(u, 0x142e938)
check(uc(q(u, q(u, preg)), preg, rl_of('gem', 'glow_potion')) == p0, 'potion registry get(gem:glow_potion): what PotionUtils::getPotion 0x562670 does with the NBT "Potion" string')
check(uc(q(u, q(u, preg)), preg, rl_of('gem', 'missing')) == uc(0x55e560, 1), 'unknown key -> default (water)')
vb, ve = q(u, p0 + 0x20), q(u, p0 + 0x28)
check(ve - vb == 16 and q(u, q(u, vb)) == sp_stock and d(u, q(u, vb) + 8) == 3600 and d(u, q(u, vb) + 0xc) == 1 and q(u, q(u, vb + 8)) == regen,
      'potion MEI vector built through the real grow 0x6fd00 + MEI ctor')
check(d(u, q(u, vb) + 0x14) == 0xffffffff, 'MEI +0x14 = -1 (no stat source: MEI::tick skips 0x8e7830)')
# names through the REAL Potion::getName 0x55e620 for the four item kinds
seen_sid = []
TXT = alloc(u, 8); u.mem_write(TXT, 'X\0'.encode('utf-16le'))
def loc(m): seen_sid.append(m.reg_read(UC_X86_REG_RDI) & 0xffffffff); return TXT
for kind in range(4):
    out = alloc(u, 0x40); u.mem_write(out, bytes(0x40))
    call(u, 0x55e620, (out, p0, kind), stubs={0xaac9b0: loc}, limit=500000)
check(seen_sid == psids[:4], f'getName kinds 0..3 (potion/splash/lingering/arrow) read the mod sids {list(map(hex, seen_sid))}')
# key -> registry -> getKey round trip (setPotion 0x562cd0 writes this key into NBT)
out = alloc(u, 0x80); u.mem_write(out, bytes(0x80))
call(u, q(u, q(u, preg) + 0x30), (out, preg, p0), limit=500000)
def wtext(w):
    n = q(u, w + 0x18); p = q(u, w + 8) if q(u, w + 0x20) > 7 else w + 8
    return bytes(u.mem_read(p, 2 * n)).decode('utf-16le')
check(wtext(out + 0x40) == 'gem' and wtext(out + 0x18) == 'glow_potion', 'registry getKey(potion) = gem:glow_potion (RL: path +0x18, namespace +0x40)')

# ---- 6. refusal on the live image: id already taken / potion key already registered
rc = lib.shim_publish_effects(1, (ctypes.c_char_p * 1)(b'gem:dupe'), (ctypes.c_int32 * 1)(70), (ctypes.c_uint32 * 1)(1),
                              (ctypes.c_int32 * 1)(0), (ctypes.c_uint32 * 1)(0), (ctypes.c_uint32 * 1)(0xffffffff),
                              (ctypes.c_uint8 * 1)(1), (ctypes.c_uint32 * 1)(0))
check(rc == 0, 'second effect batch refused')

print(f'test_unicorn_retail: {checks} checks, {fails} failures')
sys.exit(1 if fails else 0)
