"""strings195 against the REAL retail 1.95 Localizer code, in Unicorn.

Real code that runs here (nothing of it is reimplemented):
  0xd59ed0  language-node construction (outer map node + its empty string map, 8 buckets)
  0x67840   wstring assign;  0xd5a9e0 list emplace_front;  0x7ed670 hash _Insert (+ the real
            rehash in 0x7ed830 once the load factor is exceeded)
  0xd39880  Localizer::get;  0xaac9b0  the global string getter every name path calls
Only PLT imports are stubbed (operator new/delete, wmemcpy, wcslen, atomics).

The module itself (strings195.cpp, host-compiled into libs195bridge.so) drives
the real insert functions through the bridge; its reads/writes hit emulated memory.
"""
import ctypes, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from uc195 import Machine, Bridge, Checks, new_localizer, stock_insert, list_walk as walk

LIB = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / 'build/host/libs195bridge.so'
ok = Checks('strings195 unicorn')
MISSING = 0xf0fdc0
LOCALIZER = 0x1440530


class Report(ctypes.Structure):
    _fields_ = [('localizer', ctypes.c_uint64), ('languages', ctypes.c_uint32), ('inserted', ctypes.c_uint32),
                ('present', ctypes.c_uint32), ('failed', ctypes.c_uint32)]


def get(m, L, sid):
    p = m.call(0xd39880, (L, sid))
    return None if p == MISSING else m.wstr_read(p)


def get_global(m, sid):
    p = m.call(0xaac9b0, (sid,))
    return None if p == MISSING else m.wstr_read(p)


m = Machine()
b = Bridge(m, LIB)
lib = b.lib
lib.bridge_s195_ensure.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_char_p),
                                   ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(Report)]
lib.bridge_s195_find.argtypes = [ctypes.c_uint64, ctypes.c_uint32]; lib.bridge_s195_find.restype = ctypes.c_uint64


def ensure(entries, report=None):
    n = len(entries)
    sids = (ctypes.c_uint32 * n)(*[e[0] for e in entries])
    en = (ctypes.c_char_p * n)(*[e[1].encode() for e in entries])
    de = (ctypes.c_char_p * n)(*[(e[2].encode() if len(e) > 2 and e[2] else None) for e in entries])
    r = report or Report()
    return lib.bridge_s195_ensure(n, sids, en, de, ctypes.byref(r)), r


# 1. Real language nodes and the stock insert path; layout facts the module relies on
L, nodes = new_localizer(m, ['en-EN', 'de-DE'])
en_map, de_map = nodes['en-EN'] + 0x38, nodes['de-DE'] + 0x38
ok(m.wstring_value(nodes['de-DE'] + 0x10) == 'de-DE', 'lang node +0x10 is the language wstring')
ok(m.q(en_map + 0x38) == 7 and m.q(en_map + 0x40) == 8 and abs(m.f(en_map + 0x48) - 1.0) < 1e-6, 'fresh string map: mask 7, maxidx 8, max_load_factor 1.0')
ok(m.q(m.q(en_map + 8)) == m.q(en_map + 8) and m.q(en_map + 0x10) == 0, 'fresh string map: empty list, size 0')
ok(get(m, L, 0x12345678) is None, 'absent id -> MISSING sentinel 0xf0fdc0')
ok(m.call(0xaac9b0, (0x12345678,)) == 0xeeeda2 and m.wstr_read(0xeeeda2) == '', 'no Localizer global -> 0xaac9b0 returns its empty literal')
stock = {0xd8a6b805: 'Play Game', 0xdbb08587: 'Minecraft Store', 0x88507d2e: 'Thorns'}
for i in range(40): stock[0x91995 + 0x01000193 * i & 0xffffffff] = f'stock string {i}'
for k, v in stock.items():
    ok(stock_insert(m, en_map, k, v) == 1, f'stock insert {k:#x}')
ok(m.q(en_map + 0x10) == len(stock), 'size counts stock inserts')
ok(m.q(en_map + 0x40) > 8, f'the real rehash ran (maxidx now {m.q(en_map + 0x40)})')
ok(all(get(m, L, k) == v for k, v in stock.items()), 'real Localizer::get finds every stock entry after rehash')
ok(stock_insert(m, en_map, 0xd8a6b805, 'dup') == 0 and get(m, L, 0xd8a6b805) == 'Play Game', 'duplicate id: _Insert keeps the old node, frees the new one')
for k in list(stock)[:3]: stock_insert(m, de_map, k, 'DE ' + stock[k])

# 2. The module through the bridge: not ready before the Localizer exists
rc, r = ensure([(0x4C430000, 'Gem Sharpness')])
ok(rc == 0, 'ensure before [0x1440530] is set -> 0 (not ready, no write)')
m.wq(LOCALIZER, L)
lib.bridge_held(0)
rc, r = ensure([(0x4C430000, 'Gem Sharpness')])
ok(rc == 0 and r.inserted == 0, 'not held -> refused')
lib.bridge_held(1)

entries = [(0x4C430000, 'Gem Sharpness', 'Edelstein-Schärfe'),
           (0x4C430001, 'Potion of Gemmy Luck (long name > 8 units: heap wstring)'),
           (0x4C430002, 'Ü€𝄞 surrogate'),
           (0x4C430003, 'Gemland')]
rc, r = ensure(entries)
ok(rc == 1, f'ensure -> 1 (rc {rc}, errors {b.errors[:2]})')
ok(r.localizer == L and r.languages == 2 and r.inserted == 8 and r.failed == 0, f'report: 2 languages, 8 inserts ({r.languages}, {r.inserted}, {r.failed})')
ok(get(m, L, 0x4C430000) == 'Gem Sharpness', 'real Localizer::get resolves the mod id (en)')
ok(get(m, L, 0x4C430001) == entries[1][1], 'real get: long text (heap wstring)')
ok(get(m, L, 0x4C430002) == entries[2][1], 'real get: non-ASCII + surrogate pair')
ok(get_global(m, 0x4C430003) == 'Gemland', 'real 0xaac9b0 (the path every name uses) resolves the mod id')
ok(all(get(m, L, k) == v for k, v in stock.items()), 'stock entries unchanged')
ok(m.q(en_map + 0x10) == len(stock) + 4, 'map size grew by 4')
ok(b.errors == [], f'no emulation errors {b.errors[:2]}')
ok(0x7ed670 in b.native_calls and 0xd5a9e0 in b.native_calls and 0x67840 in b.native_calls, 'module used the game insert functions')
# German node picked the de text; others fall back to texts[0]
m.wq(L + 0xa0, de_map)
ok(get(m, L, 0x4C430000) == 'Edelstein-Schärfe', 'de-DE map got the de_DE text')
ok(get(m, L, 0x4C430003) == 'Gemland', 'de-DE map falls back to the first text')
ok(get(m, L, list(stock)[0]) == 'DE Play Game', 'de stock entry intact')
m.wq(L + 0xa0, en_map)

# 3. Idempotent: second call inserts nothing; partial presence completes
calls_before = len(b.native_calls)
rc, r = ensure(entries)
ok(rc == 1 and r.inserted == 0 and r.present == 4, 'second ensure: fast path, nothing inserted')
ok(len(b.native_calls) == calls_before, 'fast path makes no native call')
more = entries + [(0x4C430010 + i, f'mod string {i}') for i in range(60)]
rc, r = ensure(more)
ok(rc == 1 and r.inserted == 120 and r.present == 8, f'60 new ids in 2 languages (inserted {r.inserted}, present {r.present})')
ok(all(get(m, L, 0x4C430010 + i) == f'mod string {i}' for i in range(60)), 'all 60 resolve after further real rehashes')
ok(all(get(m, L, k) == v for k, v in stock.items()), 'stock entries still intact after mod-triggered rehash')
ok(lib.bridge_s195_find(en_map, 0x4C430005) == 0 and lib.bridge_s195_find(en_map, 0x4C430010) != 0, 'module find mirrors the real lookup')
mism = [k for k in list(stock) + [0x4C430000 + i for i in range(0x60)] if (lib.bridge_s195_find(en_map, k) != 0) != (get(m, L, k) is not None)]
ok(not mism, f'module find == real get for 136 ids {mism[:3]}')

# 4. Localizer rebuilt (language change 0x9d985f -> 0xafc510): ensure re-inserts
L2, nodes2 = new_localizer(m, ['fr-FR'])
m.wq(LOCALIZER, L2)
ok(get_global(m, 0x4C430000) is None, 'fresh Localizer has no mod strings')
rc, r = ensure(entries)
ok(rc == 1 and r.localizer == L2 and r.inserted == 4, 're-ensure on the new Localizer inserts again')
ok(get_global(m, 0x4C430000) == 'Gem Sharpness', 'fr-FR without a fr text falls back to texts[0]')

# 5. Refusals
rc, r = ensure([(0x12345678, 'outside range')])
ok(rc == 0 and r.inserted == 0, 'id outside the reserved range refused')
rc, r = ensure([(0x4C430000, 'a'), (0x4C430000, 'b')])
ok(rc == 0, 'duplicate ids refused')

sys.exit(ok.done())
