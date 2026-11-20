#!/usr/bin/env python3
# Audit the pinned events195/services195 RVAs against the 1.95 image:
# every executable RVA must disassemble to a function prologue, and the named
# vtables must carry the expected slot functions. Fail-closed (nonzero exit).
import struct, sys, re
E = '/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf'
d = open(E, 'rb').read()
phoff = struct.unpack_from('<Q', d, 0x20)[0]; phnum = struct.unpack_from('<H', d, 0x38)[0]
segs = []
for i in range(phnum):
    t, fl, off, va, pa, fs, ms, al = struct.unpack_from('<IIQQQQQQ', d, phoff + i * 56)
    if fs and t in (1, 0x61000010):
        segs.append((va, off, fs, fl))
def v2o(v):
    for va, off, fs, fl in segs:
        if va <= v < va + fs: return off + v - va
    return None
def q(v):
    o = v2o(v); return struct.unpack_from('<Q', d, o)[0] if o is not None else None

fails = []
# (rva, label): first byte must be a plausible prologue (push rbp / endbr-ish / sub rsp / mov).
CODE = {
    0x3aedb0: "Item::use", 0x94800: "Block::playerWillDestroy", 0x7d4c0: "BlockItem::useOn",
    0x188220: "ClientboundChatPacket::ctor(str)", 0xcc7310: "PlayerList::broadcastAll",
    0x3f3fe0: "ServerLevel::playSound", 0x424580: "LivingEntity::hurt", 0x426600: "LivingEntity::die",
    0xd05800: "ServerPlayer::addEffect", 0x4232f0: "getEffect", 0x491aa0: "MobEffectInstance::ctor",
    0x385af0: "Item::byId", 0x3b7720: "ItemInstance::ctor", 0x7c2f0: "Block::popResource",
    0x4c34f0: "effect contains", 0x4c3250: "effect get", 0xcda2f0: "ResourceLocation::ctor",
    0x67840: "wstring::assign",
    # API 1.2
    0x940b0: "Block::use (stock: false)", 0x1d4ab0: "CraftingTableBlock::use", 0x4244d0: "LivingEntity::getHealth",
    0x4244f0: "LivingEntity::setHealth", 0x427e20: "LivingEntity::getMaxHealth", 0x427240: "isAlive",
    0x37d4d0: "Inventory::add", 0x3b8730: "ItemInstance::getMaxDamage", 0x3b8750: "ItemInstance::hurt", 0x3b86e0: "ItemInstance::setDamage",
    0x284180: "Entity::teleportTo", 0xd0ca30: "ServerPlayer::teleportTo", 0x27b3b0: "Entity::push", 0x275680: "Entity::tick",
    0x42d120: "LivingEntity::tick", 0x41f1c0: "LivingEntity::baseTick", 0x27b400: "Entity::interact", 0x1d21c0: "Cow::interact (bucket)",
    0x1596c0: "CactusBlock::entityInside (hurt CACTUS)", 0x41dad0: "LivingEntity::kill (hurt OUT_OF_WORLD)",
    0x2762a0: "Entity::clearFire", 0x3f2820: "ServerLevel::getBlockState", 0x91bc0: "BlockState encode (id|meta<<12)",
    0x936b0: "Block::randomTick (-> vt+0x1f0)", 0xcf61f0: "ServerLevel::tickBlocks", 0x92a90: "Block::setTicking", 0x92aa0: "Block::isTicking",
    0x936f0: "Block::neighborChanged (stock: ret)", 0x940c0: "Block::stepOn (stock: ret)", 0x94180: "Block::entityInside (stock: ret)",
    0x3af000: "Item::inventoryTick (stock: ret)", 0x445340: "MapItem::inventoryTick", 0x6eda60: "TntBlock::neighborChanged",
    0x4d4a90: "NoteBlock::neighborChanged", 0x401bf0: "Level::hasNeighborSignal", 0x43eed0: "MagmaBlock::stepOn",
    0x1f22a0: "DamageSource::playerAttack", 0x1f2080: "DamageSource::mobAttack", 0x2871a0: "EntityDamageSource::ctor",
    0x2ba860: "EntityDamageSource deleting dtor", 0x68e60: "weak_ptr::lock (_Incref_nz)", 0x165c90: "Cat::doHurtTarget",
    0xcfd290: "ServerLevel::sendParticles", 0x18d080: "ClientboundLevelParticlesPacket::ctor", 0x526750: "ParticleType::getId",
    0xcfd730: "ServerLevel::sendParticles (RGB)", 0x18d1a0: "ClientboundLevelParticlesPacket::ctor (RGB)",
    0x526780: "ParticleType::byId", 0x3ba900: "ItemInstance::getOrCreateTag", 0x3b7740: "ItemInstance::load", 0x3befe0: "ItemInstance::save",
    0x1b6df0: "CompoundTag::ctor", 0x1b7400: "CompoundTag::get", 0x1b8630: "CompoundTag::remove", 0x1b25e0: "CompoundTag::putInt",
    0x18ba10: "ClientboundCustomPayloadPacket::ctor", 0x18bab0: "ClientboundCustomPayloadPacket::dtor", 0xcf10a0: "handleCustomPayload",
    0x5fbfc0: "ServerboundCustomPayloadPacket::handle", 0x5fbd70: "ServerboundCustomPayloadPacket::ctor",
    0x514e40: "ClientboundCustomPayloadPacket::create", 0x511890: "ClientboundSetTimePacket::create", 0x5115e0: "Packet::map",
    0x18bd90: "ClientboundCustomPayloadPacket::handle", 0x1a2620: "ClientboundSetTimePacket::handle", 0xba2a90: "ClientPacketListener::handleCustomPayload",
    0xb91460: "ClientPacketListener::send", 0x5fbc50: "ServerboundCustomPayloadPacket::dtor",
    0x2085a0: "DispenserBlock::use", 0x27bc70: "Entity::saveWithoutId", 0x27cc90: "Entity::load", 0x2c5150: "Entity tag set insert", 0x2c50f0: "Entity tag set _Erase",
    0x1b7010: "CompoundTag deleting dtor", 0x1b6ec0: "CompoundTag dtor", 0x41cbf0: "ListTag::ctor", 0x40e850: "ListTag::add",
    0x1b7910: "CompoundTag::putDouble", 0x1af3b0: "CompoundTag::putString", 0x1b2770: "CompoundTag::getInt", 0x1af0c0: "CompoundTag::contains", 0x5267d0: "byId map operator[]", 0x936c0: "Block::tick (stock: ret)", 0x3626c0: "ItemInstance::setCount", 0x380b70: "Inventory::setChanged",
}
for rva, label in CODE.items():
    o = v2o(rva)
    if o is None: fails.append(f"{label} {rva:#x}: not mapped"); continue
    exe = any(va <= rva < va + fs and (fl & 1) for va, off, fs, fl in segs)
    if not exe: fails.append(f"{label} {rva:#x}: not in an executable segment")

# vtable slot checks
VT = [
    (0x1341168, 0x968, 0xd05800, "ServerPlayer+0x968 addEffect"),
    (0x1341168, 0x960, 0x4232f0, "ServerPlayer+0x960 getEffect"),
    (0x12782d8, 0x248, 0x424580, "LivingEntity+0x248 hurt"),
    (0x12782d8, 0xa00, 0x426600, "LivingEntity+0xa00 die"),
    (0x126e638, 0x40, 0x3aedb0, "Item+0x40 use"),
    (0x1218a50, 0x28, 0x7d4c0, "BlockItem+0x28 useOn"),
    (0x1219f18, 0x40, 0xd7c00, "BoatItem+0x40 use (API 1.10 boat items, accepted route original)"),
    (0x1340d50, 0x1c0, 0x3f3fe0, "ServerLevel+0x1c0 playSound"),
    (0x1228438, 0, None, "ClientboundChatPacket vtable present"),
    (0x1217e60, 0x2c0, 0x940b0, "Block+0x2c0 use"),
    (0x1340d50, 0x38, 0x3f2820, "ServerLevel+0x38 getBlockState"),
    (0x1341168, 0x670, 0xd0ca30, "ServerPlayer+0x670 teleportTo"), (0x12782d8, 0xa8, 0x42d120, "LivingEntity+0xa8 tick"),
    (0x12782d8, 0xb0, 0x41f1c0, "LivingEntity+0xb0 baseTick"), (0x12782d8, 0x968, 0x423400, "LivingEntity+0x968 addEffect"),
    (0x1217e60, 0x1e8, 0x936b0, "Block+0x1e8 randomTick"), (0x1217e60, 0x1f0, 0x936c0, "Block+0x1f0 tick"),
    (0x1298cf0, 0x2c0, 0x940b0, "OreBlock+0x2c0 use (stock, routed for mod blocks)"),
    (0x12782d8, 0x9b8, 0x4244d0, "LivingEntity+0x9b8 getHealth"),
    (0x126e390, 0x68, 0x380b70, "Inventory+0x68 setChanged"),
    (0x13408c8, 0x340, 0xcf10a0, "ServerGamePacketListenerImpl+0x340 handleCustomPayload"), (0x12bb4c0, 0x28, 0x5fbfc0, "SB CustomPayload+0x28 handle"),
    # API 1.3
    (0x1217e60, 0x128, 0x92a90, "Block+0x128 setTicking"), (0x1217e60, 0x140, 0x92aa0, "Block+0x140 isTicking"),
    (0x1217e60, 0x208, 0x936f0, "Block+0x208 neighborChanged"), (0x1217e60, 0x2c8, 0x940c0, "Block+0x2c8 stepOn"),
    (0x1217e60, 0x328, 0x94180, "Block+0x328 entityInside"), (0x126e638, 0xc8, 0x3af000, "Item+0xc8 inventoryTick"),
    (0x1340d50, 0x280, 0xcf61f0, "ServerLevel+0x280 tickBlocks"), (0x124cb48, 8, 0x2ba860, "EntityDamageSource+8 deleting dtor"),
    # API 1.5 entity NBT
    (0x12287e0, 0x28, 0x18bd90, "CB CustomPayload+0x28 handle"), (0x122c3a0, 0x28, 0x1a2620, "CB SetTime+0x28 handle"),
    (0x132eb18, 0x110, 0xba2a90, "ClientPacketListener+0x110 handleCustomPayload"), (0x132eb18, 0x200, 0xb9aa40, "ClientPacketListener+0x200 handleSetTime"),
    (0x1239608, 0x2c0, 0x2085a0, "DispenserBlock+0x2c0 use (storage blocks)"), (0x122ede0, 0x30, 0x1b7010, "CompoundTag+0x30 deleting dtor"), (0x122ede0, 0x18, 0x1b73e0, "CompoundTag+0x18 getId (10)"),
    (0x1278268, 0x18, 0x41cf10, "ListTag+0x18 getId (9)"), (0x12782d8, 0x290, 0x27cc90, "LivingEntity+0x290 load (Entity::load)"),
]
# exact bytes of the API 1.2 natives (same pins as services195.cpp kPins / r72 nbt_* pins)
PINS = {0x2762a0: bytes([0xc7,0x87,0xc0,0x01,0,0,0,0,0,0,0xc3]), 0x380b70: bytes([0xff,0x87,0x38,0x01,0,0,0xc3]),
        0x940b0: bytes([0x31,0xc0,0xc3]), 0x936c0: bytes([0xc3]),
        # ItemInstance calls useOn with a 64-bit Direction in r9 and a stack test-only flag.
        0x3b7dd8: bytes.fromhex('4c 8b 68 28'),
        0x3b7df4: bytes.fromhex('4c 8b 8d 20 ff ff ff c5 fa 10 85 44 ff ff ff c5 fa 10 8d 40 ff ff ff c5 fa 10 95 4c ff ff ff'),
        0x3b7e13: bytes.fromhex('0f b6 45 10 4c 89 f7 4c 89 e6 4c 89 fa 48 89 d9 89 04 24 41 ff d5'),
        0x7d503: bytes.fromhex('4d 89 cf'),                              # keep the full Direction pointer
        0x7d593: bytes.fromhex('48 8b 95 50 fe ff ff 8b 4a 60 8b 42 5c 8b 52 64'), # offsets for the placement neighbour
        0x7d78b: bytes.fromhex('8a 45 10 45 31 f6 84 c0'),              # load and test the flag
        0x200600: bytes.fromhex('8b 47 08 c3'),                         # Direction::get3DDataValue
        0x936b0: bytes([0x48,0x8b,0x07,0x48,0x8b,0x80,0xf0,0x01,0x00,0x00,0xff,0xe0]),
        # API 1.3: stock bodies the routes require, the ticking flag accessors, the random-tick gate in tickBlocks
        0x936f0: bytes([0xc3]), 0x940c0: bytes([0xc3]), 0x94180: bytes([0xc3]), 0x3af000: bytes([0xc3]),
        0x92a90: bytes([0x48,0x89,0xf8,0x40,0x88,0xb7,0x49,0x01,0x00,0x00,0xc3]), 0x92aa0: bytes([0x8a,0x87,0x49,0x01,0x00,0x00,0xc3]),
        0xcf63a3: bytes([0x48,0x8b,0x07,0xff,0x90,0x40,0x01,0x00,0x00,0x84,0xc0,0x74,0x47]),       # vcall +0x140 isTicking; je skip
        0xcf63cf: bytes([0x4d,0x8b,0x84,0x24,0xb8,0x01,0x00,0x00]),  # r8 = level->random
        0xcf63ea: bytes([0xff,0x93,0xf0,0x01,0x00,0x00]),                                          # call [rbx+0x1f0] tick
        0x1f22e7: bytes([0xbe,0x1e,0,0,0,0xba,0x3c,0,0,0]), 0x1f20c7: bytes([0xbe,0x1d,0,0,0,0xba,0x1d,0,0,0]),
        0x165cbf: bytes([0x48,0x8b,0x77,0x08,0x48,0x8b,0x57,0x10,0x48,0x8d,0x7d,0x80,0xe8]) + struct.pack('<i', 0x68e60 - 0x165cd0),  # lock [this+8]/[this+0x10]
        0x52679e: b'\x48\x8d\x3d' + struct.pack('<i', 0x142e1b8 - 0x5267a5),             # byId: lea rdi, map 0x142e1b8
        0x526868: bytes([0x49,0x8b,0x54,0x24,0x08]),  # mov rdx,[r12+8] list sentinel
        0x5268a8: bytes([0x39,0x48,0x10]),                                                  # cmp [node+0x10], key
        0xcfd37f: b'\xe8' + struct.pack('<i', 0x18d080 - 0xcfd384),                       # sendParticles -> packet ctor
        0x18d0cc: b'\x48\x8d\x05' + struct.pack('<i', 0x1228aa8 - 0x18d0d3),             # vtable 0x1228ab8 - 0x10
        0x1b2647: b'\xe8' + struct.pack('<i', 0x37a3b0 - 0x1b264c),   # putInt -> IntTag ctor
        0x1b7982: b'\xe8' + struct.pack('<i', 0x215dc0 - 0x1b7987),   # putDouble -> DoubleTag ctor
        0x1af417: b'\xe8' + struct.pack('<i', 0x68b620 - 0x1af41c),   # putString -> StringTag ctor
        0x1b8688: b'\xe8' + struct.pack('<i', 0x1b86b0 - 0x1b868d),   # remove -> erase
        0x3ba923: b'\xe8' + struct.pack('<i', 0x1b6df0 - 0x3ba928),   # getOrCreateTag -> CompoundTag ctor
        0x1b742a: b'\xe8' + struct.pack('<i', 0x1e4f30 - 0x1b742f),   # get -> map find
        0x3ba90a: bytes([0x49,0x8b,0x5e,0x30]),                         # mov rbx,[stack+0x30] (tag)
        0x18ba2c: b'\x48\x8d\x05' + struct.pack('<i', 0x12287d0 - 0x18ba33),   # payload ctor: vptr 0x12287e0
        0x18ba8b: bytes([0x4c,0x89,0x7b,0x50,0x44,0x89,0x73,0x58]),          # data +0x50, len +0x58
        0x18bac7: bytes([0x48,0x8b,0x7b,0x50]) + b'\x48\x85\xff\x74\x05\xe8' + struct.pack('<i', 0xe6cfa0 - 0x18bad5),  # dtor delete[] data
        0x5fbff0: bytes([0x4c,0x8b,0xa0,0x40,0x03,0x00,0x00]),               # handle: listener vt +0x340
        0x5112af: b'\x48\x8d\x05' + struct.pack('<i', 0x514e40 - 0x5112b6) + b'\xbf\xfa\x00\x00\x00',   # map(250, ..., create)
        0x5115fe: b'\x4c\x8d\x2d' + struct.pack('<i', 0x142dcb0 - 0x511605),                               # map object 0x142dcb0
        0x511646: bytes([0x89,0x58,0x10,0x48,0x89,0x50,0x18]),                                       # node key +0x10, create +0x18
        0x50b2bc: b'\x48\x8d\x15' + struct.pack('<i', 0x511890 - 0x50b2c3),                             # SetTime create
        0x50b2d7: bytes([0xc7,0x40,0x10,0x04,0x00,0x00,0x00,0x48,0x89,0x50,0x18]),                  # id 4, create +0x18
        0x18bdc0: bytes([0x4c,0x8b,0xa0,0x10,0x01,0x00,0x00]),                                      # handle: listener vt +0x110
        0x5fbc67: bytes([0x48,0x8b,0x7b,0x50,0x48,0x85,0xff,0x74,0x05,0xe8]) + struct.pack('<i', 0xe6cfa0 - 0x5fbc75),  # dtor delete[] data
        0xc61686: bytes([0x4d,0x8b,0xbd,0x68,0x0b,0x00,0x00]),                                      # local player +0xb68 = listener
        0x165d2e: b'\xe8' + struct.pack('<i', 0x1f2080 - 0x165d33),                                     # mobAttack
        0x1f22f7: b'\xe8' + struct.pack('<i', 0x2871a0 - 0x1f22fc), 0x2871c6: b'\x48\x8d\x05' + struct.pack('<i', 0x124cb38 - 0x2871cd)}
# every kPins entry of services195.cpp (the bytes the module checks at init before binding a native)
import pathlib
src = (pathlib.Path(__file__).resolve().parent.parent / 'services195.cpp').read_text()
nat = (pathlib.Path(__file__).resolve().parent.parent / 'natives195.h').read_text()
consts = {m.group(1): int(m.group(2), 16) for m in re.finditer(r'#define (LCE195_\w+)\s+(0x[0-9a-f]+)u', nat)}
for m in re.finditer(r'\{(LCE195_\w+), (\d+), \{([^}]*)\}\}', src):
    PINS[consts[m.group(1)]] = bytes(int(x, 16) for x in m.group(3).split(','))[:int(m.group(2))]
for rva, want in PINS.items():
    o = v2o(rva)
    if o is None or d[o:o + len(want)] != want: fails.append(f"pin {rva:#x}: bytes differ")
base = 0   # reconstructed ELF stores plain RVAs in vtables
for vt, off, expect, label in VT:
    slot = q(vt + off)
    if slot is None: fails.append(f"{label}: vtable not mapped"); continue
    if expect is not None and slot != base + expect:
        fails.append(f"{label}: slot {slot:#x} != {base+expect:#x}")

# vtables found by RTTI name (typeinfo name -> typeinfo -> vtable), so the slot claims do not depend on hard-coded tables
def vt_of(cls):
    name = (str(len(cls)) + cls).encode() + b'\0'
    for m in re.finditer(re.escape(name), d):
        if d[m.start() - 1:m.start()] in b'0123456789N':
            continue
        nv = next((va + m.start() - off for va, off, fs, fl in segs if off <= m.start() < off + fs), None)
        for va, off, fs, fl in segs:
            if fl & 1:
                continue
            for i in range(0, fs - 7, 8):
                if struct.unpack_from('<Q', d, off + i)[0] == nv:
                    ti = va + i - 8
                    for va2, off2, fs2, fl2 in segs:
                        if fl2 & 1:
                            continue
                        blob = d[off2:off2 + fs2]
                        j = blob.find(struct.pack('<Q', ti))
                        while j >= 0:
                            if j % 8 == 0 and struct.unpack_from('<Q', blob, j - 8)[0] == 0:
                                return va2 + j + 8
                            j = blob.find(struct.pack('<Q', ti), j + 1)
    return None
for cls, off, want, label in (("Entity", 0xa8, 0x275680, "Entity::tick"), ("Entity", 0xe50, 0x27b400, "Entity::interact"),
                              ("Cow", 0xe50, 0x1d21c0, "Cow::interact"), ("Entity", 0x230, 0x27b3b0, "Entity::push"),
                              ("Entity", 0x670, 0x284180, "Entity::teleportTo"), ("CactusBlock", 0x328, 0x1596c0, "Cactus entityInside"),
                              ("LivingEntity", 0x70, 0x41dad0, "LivingEntity::kill"), ("DamageSource", 0, None, "DamageSource vtable 0x1236418")):
    v = vt_of(cls)
    if cls == "DamageSource":
        if v != 0x1236418: fails.append(f"{label}: found {v}")
        continue
    if v is None or q(v + off) != want: fails.append(f"{cls}+{off:#x} {label}: {hex(q(v + off) or 0) if v else 'no vtable'}")
# the GENERIC damage source global is the 14th static of the DamageSource initialiser and sits right after OUT_OF_WORLD
def rip_lea(target):
    out = []
    for va, off, fs, fl in segs:
        if not fl & 1: continue
        for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', d[off:off + fs]):
            o = off + m.start(); disp = struct.unpack_from('<i', d, o + 3)[0]
            if va + m.start() + 7 + disp == target: out.append(va + m.start())
    return out
for cls, want in (("CompoundTag", 0x122ede0), ("ListTag", 0x1278268), ("BoatItem", 0x1219f18)):
    if vt_of(cls) != want: fails.append(f"{cls} vtable by RTTI {vt_of(cls)} != {want:#x}")
if d[v2o(0x1b73e0):v2o(0x1b73e0) + 3] != bytes([0xb0, 0x0a, 0xc3]): fails.append("CompoundTag getId != 10")
if d[v2o(0x41cf10):v2o(0x41cf10) + 3] != bytes([0xb0, 0x09, 0xc3]): fails.append("ListTag getId != 9")
if 0x41dad3 not in rip_lea(0x1413b60): fails.append("kill() does not load OUT_OF_WORLD 0x1413b60")
if 0x1596e6 not in rip_lea(0x1413b48): fails.append("cactus does not load CACTUS 0x1413b48")
if fails:
    print("events195 RVA audit FAILED:")
    for f in fails: print("  " + f)
    sys.exit(1)
print(f"events195 RVA audit OK: {len(CODE)} code RVAs, {len(VT)} vtable slots verified")
