"""enchants195 against the REAL retail 1.95 enchantment code, in Unicorn.

Real code that runs here:
  0x2be3d0  MappedRegistry<ResourceLocation, Enchantment*> ctor (the object [0x1415448] holds)
  0x248fa0  Enchantments::bootstrap: all 36 stock enchantments, stock ctors and register
  module -> 0x24ab10 / 0x1f1a40 / 0x58d970 / 0x6f6b30 ctors, 0xcda2f0 ResourceLocation,
            registry virtual register/containsKey/byId/getId
  then, on the published objects: every overridden virtual (retail functions), the
  damage/protection virtuals, getAvailableEnchantmentResults 0x24d890 (enchanting
  table candidate list) and getFullname 0x24aec0 through a Localizer filled by strings195.
Stubbed: PLT imports, and the leaf hash<wstring> 0x68b350 (VEX.256 for >= 8 units;
its model is checked against the real function on short keys below).
Process-global setup the static initialisers would do (not part of the module):
rarity/category/protection-type/equipment-slot objects; categories use their real vtables.
"""
import ctypes, struct, sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / 'strings195/tests'))
from uc195 import Machine, Bridge, Checks, WSTRING_HASH, new_localizer, stock_insert

LIB = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / 'build/host/libe195bridge.so'
ok = Checks('enchants195 unicorn')
REG = 0x1415448
CATS = {0x14154b8: 0x1254ee8, 0x14154c0: 0x1254f18, 0x14154c8: 0x1254f48, 0x14154d0: 0x1254f78, 0x14154d8: 0x1254fa8,
        0x14154e0: 0x1254fd8, 0x1415480: 0x1255008, 0x1415488: 0x1255038, 0x1415490: 0x1255068, 0x14154e8: 0x1255098,
        0x14154f0: 0x12550c8, 0x14154f8: 0x12550f8, 0x1415500: 0x1255128, 0x1415508: 0x1255158}


class Spec(ctypes.Structure):
    _fields_ = [('key', ctypes.c_char_p), ('id', ctypes.c_int32), ('clone', ctypes.c_uint32), ('category', ctypes.c_uint32),
                ('max_level', ctypes.c_uint8), ('weight', ctypes.c_uint8), ('treasure', ctypes.c_uint8), ('curse', ctypes.c_uint8),
                ('name_sid', ctypes.c_uint32)]


class Record(ctypes.Structure):
    _fields_ = [('state', ctypes.c_int), ('id', ctypes.c_int32), ('clone', ctypes.c_uint32), ('object', ctypes.c_uint64), ('vtable', ctypes.c_uint64)]


m = Machine()
# hash<wstring> model == real function on its scalar path (sizes 1..7)
saved = m.replace.pop(WSTRING_HASH)
for s in ('a', 'lure', 'thorns', 'flame12', 'zz'):
    w = m.std_wstring(s)
    ok(m.call(WSTRING_HASH, (w,)) & 0xffffffff == m.wstring_hash_model([ord(c) for c in s]), f'hash<wstring> model == real 0x68b350 for "{s}"')
m.replace[WSTRING_HASH] = saved

# static-init state
reg = m.alloc(0x150); m.call(0x2be3d0, (reg,)); m.wq(REG, reg)
for i, w in enumerate((10, 5, 2, 1)):
    r = m.alloc(0x40); m.wd(r + 0x30, w); m.wq(0x142f250 + 8 * i, r)
for i in range(5):   # ProtectionEnchantment::Type {id, minCost, levelCost}: the static init 0x5e4437.. copies 0xea7c00 + 0x10*i
    t = m.alloc(0x10); m.u.mem_write(t, bytes(m.u.mem_read(0xea7c00 + 0x10 * i, 0x10))); m.wq(0x142ed40 + 8 * i, t)
for g, vt in CATS.items():
    c = m.alloc(8); m.wq(c, vt); m.wq(g, c)
for i in range(6):
    s_ = m.alloc(0x20); m.wd(s_ + 8, i); m.wq(0x1415450 + 8 * i, s_)
m.call(0x248fa0, ())
regvt = m.q(reg)
def vcall(obj, slot, args=(), xmm=()):
    return m.call(m.q(m.q(obj) + slot), (obj,) + tuple(args), xmm=xmm)
def rcall(slot, *args): return m.call(m.q(regvt + slot), (reg,) + args)
stock = {i: rcall(0x40, i) for i in range(0, 93)}
ok(regvt == 0x1255398, 'registry vtable 0x1255398')
ok(sum(1 for v in stock.values() if v) == 36, f'real bootstrap registered 36 stock enchantments ({sum(1 for v in stock.values() if v)})')
ok(m.q(stock[16]) == 0x1236350 and m.q(stock[0]) == 0x12ac170 and m.q(stock[80]) == 0x12e6680 and m.q(stock[7]) == 0x12e0e90, 'stock classes at 16/0/80/7')
ok(rcall(0x40, 100) == 0 and rcall(0x40, 5000) == 0 and rcall(0x40, -1) == 0, 'free ids read as null (byId bounds-checked)')
# stock name ids come from the class (DamageEnchantment ignores +0x10)
ok(vcall(stock[16], 0x50) & 0xffffffff == m.d(0xe859a8) and vcall(stock[7], 0x50) & 0xffffffff == 0x88507d2e, 'stock getDescriptionId: damage table / thorns 0x88507d2e')

b = Bridge(m, LIB)
lib = b.lib
lib.bridge_e195_publish.argtypes = [ctypes.POINTER(Spec), ctypes.c_uint, ctypes.POINTER(Record)]
lib.bridge_e195_verify.argtypes = [ctypes.POINTER(Record)]
lib.bridge_e195_clone.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]

SHARP, IMPALING, PROT, FEATHER = 1, 4, 5, 7
CAT_CHEST, CAT_BREAKABLE = 5, 10
specs = [(b'gemmod:gem_edge', 100, SHARP, 0, 7, 2, 0, 0, 0x4C430100),
         (b'gemmod:warding', 101, PROT, CAT_CHEST, 4, 5, 0, 0, 0x4C430101),
         (b'gemmod:soulbound', 102, 0, CAT_BREAKABLE, 1, 1, 1, 1, 0x4C430102),
         (b'gemmod:tidal', 103, IMPALING, 0, 10, 10, 0, 0, 0x4C430103),
         (b'gemmod:cushion', 104, FEATHER, 0, 3, 5, 0, 0, 0x4C430104)]
arr = (Spec * len(specs))(*[Spec(*s) for s in specs])
recs = (Record * len(specs))()
rc = lib.bridge_e195_publish(arr, len(specs), recs)
ok(rc == 1, f'publish -> 1 (rc {rc}, errors {b.errors[:2]})')
ok(b.errors == [], 'no emulation errors')
objs = [r.object for r in recs]
ok(all(r.state == 1 for r in recs), 'all records published')
for (key, id_, clone, cat, lvl, wgt, tre, cur, sid), r in zip(specs, recs):
    o = r.object
    ok(rcall(0x40, id_) == o and rcall(0x38, o) & 0xffffffff == id_, f'{key.decode()}: byId({id_}) / getId')
    ok(m.q(o) == r.vtable + 16 and m.q(r.vtable) == 0, f'{key.decode()}: private vtable (offset-to-top 0)')
    ok(m.q(r.vtable + 8) == m.q({SHARP: 0x1236350, PROT: 0x12ac170, 0: 0x1245d68, IMPALING: 0x12e6680, FEATHER: 0x12ac170}[clone] - 8), f'{key.decode()}: RTTI pointer kept')
    ok(vcall(o, 0x18) & 0xffffffff == lvl, f'{key.decode()}: real getMaxLevel -> {lvl}')
    ok(vcall(o, 0x10) & 0xffffffff == 1, f'{key.decode()}: getMinLevel 1')
    ok(m.d(vcall(o, 0x00) + 0x30) == {10: 10, 5: 5, 2: 2, 1: 1}[wgt], f'{key.decode()}: getRarity weight {wgt}')
    ok(vcall(o, 0x50) & 0xffffffff == sid, f'{key.decode()}: getDescriptionId -> mod string id')
    ok(vcall(o, 0x78) & 0xff == tre and vcall(o, 0x80) & 0xff == cur, f'{key.decode()}: treasure/curse')
gem, ward, soul, tidal, cush = objs
# behaviour inherited from the stock class: same results as the stock object at the same level
null_mob = m.alloc(8)
for lv in (1, 3, 5):
    m.call(m.q(m.q(stock[16]) + 0x38), (stock[16], lv, null_mob)); want = m.u.reg_read(__import__('unicorn').x86_const.UC_X86_REG_XMM0) & 0xffffffff
    m.call(m.q(m.q(gem) + 0x38), (gem, lv, null_mob)); got = m.u.reg_read(__import__('unicorn').x86_const.UC_X86_REG_XMM0) & 0xffffffff
    ok(got == want and want != 0, f'gem_edge getDamageBonus(level {lv}) == stock sharpness ({struct.unpack("<f", struct.pack("<I", got))[0]})')
    ok(vcall(gem, 0x20, (lv,)) == vcall(stock[16], 0x20, (lv,)) and vcall(gem, 0x28, (lv,)) == vcall(stock[16], 0x28, (lv,)), f'gem_edge min/max cost(level {lv}) == sharpness')
ok(m.q(ward + 0x30) == m.q(0x142ed40) and m.q(cush + 0x30) == m.q(0x142ed50), 'protection clones carry the stock Type (ALL / FALL)')
ok(m.q(ward + 0x08) == m.q(0x14154d8) and m.q(m.q(ward) + 0x60) == 0x24b040, 'warding: CHEST category + category canEnchant')
ok(m.q(cush + 0x08) == m.q(0x14154c8), 'feather clone: FEET category from the real ctor')
ok(m.q(gem + 0x08) == m.q(0x1415480) and m.q(m.q(gem) + 0x60) == 0x1f1bb0, 'gem_edge keeps WEAPON + the damage canEnchant (axes)')
ok(m.q(m.q(gem) + 0x40) == 0x1f1b80 and vcall(gem, 0x40, (stock[16],)) & 0xff == 0 and vcall(gem, 0x40, (stock[0],)) & 0xff == 1,
   'gem_edge incompatible with sharpness (DamageEnchantment class check), compatible with protection')
ok(m.q(soul + 0x08) == m.q(0x14154e8) and m.q(m.q(soul)) == 0x24ab30, 'soulbound: base class, BREAKABLE')
ok(m.q(m.q(tidal) + 0x38) == 0x6f6bb0, 'tidal: impaler getDamageBonus')
# the slots array is the stock one (never freed, shared)
ok(m.q(gem + 0x18) == m.q(stock[16] + 0x18) and m.q(ward + 0x18) == m.q(stock[0] + 0x18), 'slot arrays shared with the stock objects')

# enchanting table: getAvailableEnchantmentResults(cost, sp<ItemInstance>*, allowTreasure), book path
book = m.alloc(0xa0); m.wq(book, 0x126e638); m.wq(0x1417b88, book)   # an Item (base vtable) standing for Items::book
ii = m.alloc(0xa8); m.wq(ii + 0x28, book); m.u.mem_write(ii + 0x38, b'\0')
sp = m.alloc(0x10); m.wq(sp, ii)
def table(cost, treasure):
    v = m.call(0x24d890, (cost, sp, treasure))
    beg, end = m.q(v + 8), m.q(v + 0x10)
    out = {}
    for p in range(beg, end, 8):
        inst = m.q(p); out[m.q(inst + 0x10)] = (m.d(inst + 0x18), m.d(inst + 8))
    return out
t = table(30, 0)
t20 = table(20, 0)
ok(gem in t and ward in t and tidal in t and cush in t20, 'table candidates include the mod enchantments (cushion = FALL Type: cost 20)')
ok(all(stock[i] in t for i in (0, 1, 3)) and all(stock[i] in t20 for i in (2, 4)) and cush not in t, 'stock protection family listed by the real Type cost windows; cushion follows FALL costs')
ok(soul not in t, 'treasure enchantment excluded when allowTreasure = 0')
ok(t.get(gem, (0, 0))[1] == 2 and t.get(tidal, (0, 0))[1] == 10, 'EnchantmentInstance weight from the Rarity (2, 10)')
lv_gem = t.get(gem, (0,))[0]
ok(1 <= lv_gem <= 7 and vcall(gem, 0x20, (lv_gem,)) <= 30 <= vcall(gem, 0x28, (lv_gem,)), f'gem_edge level {lv_gem} chosen by the real cost window')
ok(stock[16] in t, 'stock sharpness still listed')
t2 = table(12, 1)
ok(soul in t2 and soul not in table(12, 0), 'treasure enchantment listed only when allowTreasure = 1 (cost 12 in its 11..16 window)')
t3 = table(1, 0)
ok(gem in t3 and t3[gem][0] == 1, 'cost 1 -> level 1')

# getFullname 0x24aec0 through the real Localizer, mod name inserted by strings195
from unicorn.x86_const import UC_X86_REG_RAX  # noqa: E402
L, nodes = new_localizer(m, ['en-EN'])
smap = nodes['en-EN'] + 0x38
for i, roman in enumerate(['', '', 'II', 'III', 'IV', 'V', 'VI', 'VII', 'VIII', 'IX', 'X']):
    lid = m.d(0xe8bd80 + 4 * i)
    if i >= 2: stock_insert(m, smap, lid, roman)
m.wq(0x1440530, L)
lib.bridge_s195_ensure.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_char_p), ctypes.c_void_p]
sids = (ctypes.c_uint32 * 2)(0x4C430100, 0x4C430102)
en = (ctypes.c_char_p * 2)(b'Gem Edge', b'Soulbound')
ok(lib.bridge_s195_ensure(2, sids, en, None, None) == 1, 'strings195 inserted the names')
def fullname(o, lv):
    out = m.alloc(0x40)
    try:
        m.call(0x24aec0, (out, o, lv))
    except RuntimeError as e:
        return 'ERR ' + str(e)[:120]
    return m.wstring_value(out)
fn7 = fullname(gem, 7)
ok('Gem Edge' in fn7 and 'VII' in fn7, f'real getFullname(gem_edge, 7) = {fn7!r}')
fn1 = fullname(soul, 1)
print(f'getFullname: {fn7!r} {fn1!r}')
ok('Soulbound' in fn1, f'real getFullname(soulbound, 1) = {fn1!r} (curse colour path)')

# refusals: nothing written
err = ctypes.create_string_buffer(300)
ok(lib.bridge_e195_clone(b'minecraft:thorns', err, 300) == -1 and b'identity' in err.value, 'thorns clone refused (identity-checked)')
ok(lib.bridge_e195_clone(b'minecraft:mending', err, 300) == -1, 'mending clone refused')
ok(lib.bridge_e195_clone(b'minecraft:smite', err, 300) == 2 and lib.bridge_e195_clone(None, err, 300) == 0, 'smite accepted, none = 0')
brk = m.brk
dup = (Spec * 1)(Spec(b'gemmod:other', 16, SHARP, 0, 3, 5, 0, 0, 0x4C430110))
ok(lib.bridge_e195_publish(dup, 1, (Record * 1)()) == 0, 'stock id 16 taken -> refused')
ok(m.brk == brk, 'refused before any allocation (heap unchanged)')
again = (Spec * 1)(Spec(b'gemmod:gem_edge', 120, SHARP, 0, 3, 5, 0, 0, 0x4C430111))
ok(lib.bridge_e195_publish(again, 1, (Record * 1)()) == 0, 'existing key -> refused')
ok(rcall(0x40, 120) == 0, 'nothing registered at 120')
ok(lib.bridge_e195_verify(ctypes.byref(recs[0])) == 1, 'verify a published record')
sys.exit(ok.done())
