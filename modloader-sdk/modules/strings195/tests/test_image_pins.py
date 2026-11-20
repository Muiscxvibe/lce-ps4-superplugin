"""Static pins for strings195: every byte/offset fact the module relies on,
checked against the analysis image and the stock MediaOrbis.arc."""
import hashlib, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from uc195 import IMAGE, IMAGE_SHA256, Checks

ARC = Path('/home/codey/minecraft-ps4-research/MediaOrbis.arc')
ARC_SHA256 = 'c49fc19428b267bbdf1179a1f0415be049056f65b4df146eb235fde3e46ceeae'
img = IMAGE.read_bytes()
ok = Checks('strings195 pins')
ok(hashlib.sha256(img).hexdigest() == IMAGE_SHA256, 'analysis image SHA-256')
text = img[0x4000:0x4000 + 0x10c0354]


def at(rva, n): return text[rva:rva + n]
def rel32_target(rva, size):  # call/jmp/lea rel32 at the END of an instruction of `size`
    return rva + size + struct.unpack('<i', text[rva + size - 4:rva + size])[0]


# 0xaac9b0: lea [0x1440530] ; ... ; jmp 0xd39880 ; null -> lea 0xeeeda2
ok(at(0xaac9b0, 3) == b'\x48\x8d\x0d' and rel32_target(0xaac9b0, 7) == 0x1440530, '0xaac9b0 reads the Localizer global 0x1440530')
ok(at(0xaac9c3, 1) == b'\xe9' and rel32_target(0xaac9c3, 5) == 0xd39880, '0xaac9b0 tail-jumps to Localizer::get 0xd39880')
ok(rel32_target(0xaac9c8, 7) == 0xeeeda2, 'null Localizer literal 0xeeeda2')
# 0xd39880: sentinel, +0xa0 map, FNV constants, mask/maxidx/vec/head offsets
ok(rel32_target(0xd39882, 7) == 0xf0fdc0, 'missing sentinel 0xf0fdc0')
ok(at(0xd39889, 3) == b'\x83\xf9\xfd' and at(0xd3988c, 2) == b'\x0f\x87', 'ids above 0xfffffffd are rejected')
ok(at(0xd39893, 7) == bytes.fromhex('4c8b8fa0000000'), 'map pointer = [Localizer+0xa0]')
ok(at(0xd3989d, 10) == bytes.fromhex('48ba25232284e49cf2cb'), 'FNV-1a offset basis')
ok(at(0xd398af, 10) == bytes.fromhex('48bfb30100000001000000'[:20]), 'FNV prime 0x100000001b3')
ok(at(0xd398e9, 4) == bytes.fromhex('498b7138'), 'mask at map+0x38')
ok(at(0xd398f0, 4) == bytes.fromhex('49397940'), 'maxidx at map+0x40')
ok(at(0xd39900, 4) == bytes.fromhex('498b7120'), 'bucket vector at map+0x20')
ok(at(0xd39908, 4) == bytes.fromhex('4d8b4108'), 'list head at map+0x08')
ok(at(0xd39948, 3) == bytes.fromhex('394a10'), 'node key at +0x10')
ok(at(0xd39952, 5) == bytes.fromhex('48837a3808') and at(0xd39957, 4) == bytes.fromhex('488d4220'), 'node wstring: cap +0x38 (>=8 heap), data +0x20')
# stock loader uses exactly emplace_front + _Insert on node+0x38
ok(at(0xd38f3f, 1) == b'\xe8' and rel32_target(0xd38f3f, 5) == 0xd5a9e0, 'loader 0xd38f3f calls list emplace 0xd5a9e0')
ok(at(0xd38f77, 1) == b'\xe8' and rel32_target(0xd38f77, 5) == 0x7ed670, 'loader 0xd38f77 calls hash _Insert 0x7ed670')
ok(at(0xd38dd6, 5) == bytes.fromhex('498d442438'), 'loader map = lang node + 0x38')
ok(at(0xd5a9f1, 5) == bytes.fromhex('bf40000000') and rel32_target(0xd5aa05, 5) == 0xe6cfd0, 'emplace allocates a 0x40 node with operator new 0xe6cfd0')
ok(at(0x7ed691, 10) == bytes.fromhex('48ba25232284e49cf2cb'), '_Insert hashes with the same FNV basis')
ok(at(0x7ed924, 5) == bytes.fromhex('c5f82e4348'), '_Insert compares the load factor with map+0x48')
# Localizer ctor wiring: [+0xa0] = node+0x38, [+0x98] = node+0x88
ok(at(0xd37d51, 4) == bytes.fromhex('488d4838') and at(0xd37d55, 6) == bytes.fromhex('480588000000'), 'ctor: node+0x38 / node+0x88')
ok(at(0xd37d5b, 7) == bytes.fromhex('48898398000000') and at(0xd37d62, 7) == bytes.fromhex('48898ba0000000'), 'ctor stores +0x98 / +0xa0')
# rebuild 0xafc510: delete old, new 0xd8, ctor 0xd37a40, store global; called from 0x9d985f and 0xaff71e
ok(rel32_target(0xafc543, 5) == 0xd37da0 and rel32_target(0xafc54b, 5) == 0xe6cfb0, 'rebuild deletes the old Localizer')
ok(at(0xafc5f2, 5) == bytes.fromhex('bfd8000000') and rel32_target(0xafc613, 5) == 0xd37a40, 'rebuild: new 0xd8 + ctor 0xd37a40')
ok(rel32_target(0xafc528, 7) == 0x1440530 and at(0xafc61b, 4) == bytes.fromhex('49895d00'), 'rebuild stores the new Localizer')
ok(rel32_target(0x9d985f, 5) == 0xafc510 and rel32_target(0xaff71e, 5) == 0xafc510, 'rebuild callers: language change 0x9d985f, startup 0xaff71e')
calls = [i for i in range(len(text) - 5) if text[i] == 0xe8 and (i + 5 + struct.unpack('<i', text[i + 1:i + 5])[0]) == 0xafc510]
ok(sorted(calls) == [0x9d985f, 0xaff71e], f'exactly two rebuild call sites {list(map(hex, calls))}')
# wstring assign 0x67840(this, src, n): buffer at +8, size +0x18, cap +0x20
ok(at(0x6785c, 4) == bytes.fromhex('498b4720') and at(0x67920, 4) == bytes.fromhex('4d896718'), 'wstring cap +0x20 / size +0x18')

# languages.loc ids vs the reserved range
if ARC.exists():
    a = ARC.read_bytes()
    ok(hashlib.sha256(a).hexdigest() == ARC_SHA256, 'MediaOrbis.arc SHA-256')
    cnt, p, ent = struct.unpack_from('>I', a, 0)[0], 4, {}
    for _ in range(cnt):
        n = struct.unpack_from('>H', a, p)[0]; name = a[p + 2:p + 2 + n]; p += 2 + n
        off, sz = struct.unpack_from('>II', a, p); p += 8; ent[name] = (off, sz)
    off, sz = ent[b'languages.loc']; loc = a[off:off + sz]
    c = struct.unpack_from('>I', loc, 9)[0]
    ids = struct.unpack_from(f'>{c}I', loc, 13)
    ok(c == 4888, 'stock languages.loc has 4888 ids')
    ok(not any(0x4C430000 <= i <= 0x4C43FFFF for i in ids), 'no stock id in the mod range 0x4C430000..0x4C43FFFF')
    ok(not any(0xfde30000 <= i <= 0xfde30004 for i in ids), 'plugin dialog ids 0xfde30000.. are not stock either')
    ok(0xd8a6b805 in ids and 0x88507d2e in ids, 'ids used by the image (title 0xd8a6b805, thorns 0x88507d2e) are stock')
else:
    print('MediaOrbis.arc absent: skipping loc id checks')
sys.exit(ok.done())
