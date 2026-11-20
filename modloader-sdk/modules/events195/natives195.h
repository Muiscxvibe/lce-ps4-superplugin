/* Pinned 1.95 RVAs used by events195/services195 (image c675e6ce...029f).
 * Evidence column: C = console-proven by the r72 stack, S = static (this
 * module's RE, see STATUS.md), L = layout copied from a console-proven r72
 * helper. Never mix with 2.72 addresses. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ev195.h"

/* runtime */
#define LCE195_RVA_NEW              0xe6cfd0u  /* operator new                    C */
#define LCE195_RVA_DELETE           0xe6cfb0u  /* operator delete                 C */
#define LCE195_RVA_ATOMIC_SUB       0xe6ce20u  /* (int*,1,5) -> old; shared_ptr release C */
#define LCE195_RVA_WSTRING_ASSIGN   0x67840u   /* (ws*, const u16*, n)            C */
#define LCE195_RVA_TEXT_END         0x10c0354u /* end of RX segment               C */
/* server */
#define LCE195_RVA_SERVER           0x14648d0u /* MinecraftServer* singleton      C */
#define LCE195_OFF_SERVER_PLAYERS   0x28u      /* server->getPlayers() 0xc842b0   S */
#define LCE195_RVA_SERVERPLAYER_VT  0x1341168u /* ServerPlayer primary vtable     C */
#define LCE195_RVA_SERVERLEVEL_VT   0x1340d50u /* ServerLevel primary vtable      C */
#define LCE195_OFF_ENTITY_LEVEL     0x150u     /*                                 C */
#define LCE195_OFF_LEVEL_CLIENT     0x248u     /* bool isClientSide               C */
#define LCE195_OFF_ENTITY_ID        0x18u      /*                                 C */
#define LCE195_OFF_ENTITY_POS       0x170u     /* double x,y,z                    C */
#define LCE195_OFF_PLAYER_NAME      0x9a8u     /* wstring (getName +0x640 0x54f150) C */
#define LCE195_OFF_PLAYER_CONN      0xb60u     /* ServerGamePacketListenerImpl*   S */
#define LCE195_RVA_PLAYER_GETNAME   0x54f150u
/* chat (ChatPacket = ClientboundChatPacket id 3, 0xe0 bytes) */
#define LCE195_RVA_CHAT_CTOR        0x188220u  /* (pkt, const ws*, int type, int custom) S */
#define LCE195_RVA_CHAT_VT          0x1228438u /* ClientboundChatPacket vtable    S */
#define LCE195_CHAT_SIZE            0xe0u
#define LCE195_CHAT_TYPE_CUSTOM     0          /* client handler 0xb943c0 default case S */
#define LCE195_RVA_PACKET_REFCOUNT_VT 0x1220858u /* _Ref_count<Packet> vtable (+0x10) S */
#define LCE195_RVA_ENABLE_SHARED    0x14fb90u  /* (pair*, obj, ctrl)              S */
#define LCE195_RVA_BROADCAST_ALL    0xcc7310u  /* PlayerList::broadcastAll(pl, sp*) S */
#define LCE195_RVA_CONN_SEND        0xcbb710u  /* connection->send(conn, sp*)     S */
#define LCE195_RVA_LISTENER_VT      0x13408c8u /* ServerGamePacketListenerImpl    S */
#define LCE195_OFF_LISTENER_CHAT    0x300u     /* handleChat slot, stock 0x5ff9b0 S */
#define LCE195_RVA_LISTENER_CHAT    0x5ff9b0u
/* item stacks / drops (console-proven in debug_give_195 + lucky item.cpp) */
#define LCE195_RVA_ITEM_BY_ID       0x385af0u  /* Item* (unsigned id)             C */
#define LCE195_RVA_STACK_CTOR       0x3b7720u  /* (stack[0xa8], Item*, count, aux) C */
#define LCE195_RVA_STACK_DTOR       0x3b7be0u  /*                                  C */
#define LCE195_RVA_STACK_REFCOUNT_VT 0x1214808u /* _Ref_count<ItemInstance>        C */
#define LCE195_RVA_STACK_BIND       0x68d30u   /* (pair*, obj, ctrl)               C */
#define LCE195_RVA_POP_RESOURCE     0x7c2f0u   /* Block::popResource(level,pos,sp*) C */
#define LCE195_STACK_SIZE           0xa8u
/* sounds */
#define LCE195_RVA_SOUND_REGISTRY   0x1430570u /* +0xe8/+0xf0 SoundEvent* vector  S */
#define LCE195_OFF_LEVEL_PLAYSOUND  0x1c0u     /* -> 0x3f3fe0 on ServerLevel      C(XP) */
#define LCE195_RVA_LEVEL_PLAYSOUND  0x3f3fe0u
/* effects */
#define LCE195_RVA_EFFECT_REGISTRY  0x14195c8u /* MobEffect registry global       L */
#define LCE195_RVA_EFFECT_REG_VT    0x1294038u
#define LCE195_RVA_EFFECT_CONTAINS  0x4c34f0u  /* vt+0x20 */
#define LCE195_RVA_EFFECT_GET       0x4c3250u  /* vt+0x00 */
#define LCE195_RVA_EFFECT_ID        0x4c38f0u  /* vt+0x38 */
#define LCE195_RVA_EFFECT_BYID      0x4c3a10u  /* vt+0x40 */
#define LCE195_RVA_KEY_CTOR         0xcda2f0u  /* ResourceLocation(key, ws*)       L */
#define LCE195_RVA_KEY_DTOR         0xcdab00u
#define LCE195_RVA_EFFECT_INST_CTOR 0x491aa0u  /* (inst[24], effect, ticks, amp)   L */
#define LCE195_OFF_ADD_EFFECT       0x968u     /* ServerPlayer -> 0xd05800         L */
#define LCE195_RVA_ADD_EFFECT       0xd05800u
#define LCE195_RVA_LIVING_ADD_EFFECT 0x423400u /* LivingEntity::addEffect (LivingEntity/Mob/Cow/Zombie +0x968)  S */
#define LCE195_OFF_GET_EFFECT       0x960u     /* -> 0x4232f0                      L */
#define LCE195_RVA_GET_EFFECT       0x4232f0u
/* living entity virtuals (LivingEntity vtable 0x12782d8) */
#define LCE195_OFF_HURT             0x248u     /* LivingEntity::hurt 0x424580     S */
#define LCE195_OFF_DIE              0xa00u     /* LivingEntity::die 0x426600      S */
#define LCE195_OFF_GET_HEALTH       0x9b8u     /* 0x4244d0                        S */
/* items / blocks */
#define LCE195_OFF_ITEM_USE         0x40u      /* Item::use 0x3aedb0              C */
#define LCE195_RVA_ITEM_USE         0x3aedb0u
#define LCE195_RVA_BOATITEM_USE     0xd7c00u    /* API 1.10 boat items: BoatItem::use, same (sret, this, level, sp<Player>*, hand) slot +0x40 */
#define LCE195_OFF_BLOCK_WILL_DESTROY 0x3e0u   /* Block::playerWillDestroy 0x94800 C(Lucky) */
#define LCE195_RVA_BLOCK_WILL_DESTROY 0x94800u
#define LCE195_OFF_BLOCKITEM_USEON  0x28u      /* BlockItem::useOn 0x7d4c0        S */
#define LCE195_RVA_BLOCKITEM_USEON  0x7d4c0u
#define LCE195_OFF_DIRECTION_INDEX 0x08u      /* Direction::get3DDataValue 0x200600 S */
#define LCE195_OFF_LEVEL_GETSTATE   0x38u      /* -> 0x3f2820                      C */
#define LCE195_OFF_STATE_GETBLOCK   0x170u     /*                                  S */
/* API 1.2: block use (Block vtable 0x1217e60 +0x2c0 = stock 0x940b0 `xor eax,eax; ret`; overridden by all 25
 * interactive blocks checked (CraftingTable 0x1d4ab0, Lever, Note, Door, Cake, Chest...) and by none of 8 plain ones;
 * ABI from CraftingTableBlock::use: rdi block, rsi Level* (+0x248 isClientSide), rdx BlockPos* (x,y,z int32),
 * rcx BlockState*, r8 shared_ptr<Player>* (vcall +0xd80 openMenu), r9d int, stack[1] bool simulate -> false) S */
#define LCE195_OFF_BLOCK_USE        0x2c0u
#define LCE195_RVA_BLOCK_USE        0x940b0u
/* storage blocks (blocks195 block_entity_slots = 9) carry DispenserBlock::use 0x2085a0 in +0x2c0 (opens the 3x3 screen of the
 * DispenserBlockEntity at the position; type mask 0x3c00200, player vt +0xd70). The route accepts it as the original too. S */
#define LCE195_RVA_DISPENSER_USE    0x2085a0u
/* entity tick: Entity vtable +0xa8 = Entity::tick 0x275680 (calls baseTick +0xb0); LivingEntity +0xa8 0x42d120 calls
 * 0x275680 directly (super) and the aiStep path; LivingEntity +0xb0 0x41f1c0 = baseTick (references IN_WALL 0x1413b28
 * at 0x41f278 and DROWN 0x1413b38 at 0x41fa0a: suffocation/air are LivingEntity.baseTick in Java)      S */
#define LCE195_OFF_TICK             0xa8u
/* damage sources: static DamageSource* globals in Java 1.14 declaration order (static init 0x245f00):
 * 0x1413b48 CACTUS (CactusBlock::entityInside 0x1596c0 passes it to hurt +0x248 with 1.0f),
 * 0x1413b60 OUT_OF_WORLD (LivingEntity::kill +0x70 0x41dad0: hurt(it, FLT_MAX)), 0x1413b68 GENERIC (next) S */
#define LCE195_RVA_DMG_GENERIC      0x1413b68u
#define LCE195_RVA_DAMAGESOURCE_VT  0x1236418u
/* attacker damage sources (API 1.3): EntityDamageSource RTTI vtable 0x124cb48 (ctor 0x2871a0(this, id, msg, shared_ptr<Entity>*)
 * stores a strong copy at +0x20/+0x28, +0x38 thorns). Factories take shared_ptr<Entity>* (caller keeps ownership) and
 * return an operator-new'd source that the caller deletes through vt slot +8 after hurt (Cat 0x165d2e..0x165e38):
 * 0x1f22a0 playerAttack (id 0x1e; Player +0xd98 attack 0x54a1b0, Parrot cookie 0x52499e hurt(FLT_MAX)),
 * 0x1f2080 mobAttack (id 0x1d; Cat, Parrot, ~12 mob doHurtTarget callers)                                   S */
#define LCE195_RVA_DMG_PLAYER_ATTACK 0x1f22a0u
#define LCE195_RVA_DMG_PLAYER_ATTACK_IDS 0x1f22e7u  /* mov esi,0x1e; mov edx,0x3c before the ctor call 0x1f22f7 */
#define LCE195_RVA_DMG_MOB_ATTACK    0x1f2080u
#define LCE195_RVA_ENTITY_DAMAGESOURCE_VT 0x124cb48u
#define LCE195_RVA_DMG_MOB_ATTACK_IDS 0x1f20c7u     /* mov esi,0x1d; mov edx,0x1d before the ctor call 0x1f20d7 */
/* enable_shared_from_this: every Entity keeps weak_ptr<this> at +0x8 (object) / +0x10 (control); Cat::doHurtTarget
 * 0x165cbf..0x165ccb locks it with 0x68e60(out, obj, ctrl, throw=1) before mobAttack. 0x68e60 = weak_ptr::lock
 * (_Incref_nz CAS loop 0xe6ce30/0xe6ce00; leaves out untouched when expired and throw = 0, branch 0x68f75)      S */
#define LCE195_OFF_ENTITY_WEAK_THIS 0x8u
#define LCE195_RVA_WEAK_LOCK        0x68e60u
#define LCE195_RVA_WEAK_LOCK_BODY   0x68e97u
/* block / item callbacks (API 1.3). Block vtable 0x1217e60; every stock body is a bare `ret` (pinned by audit.py).
 * tick +0x1f0 (0x936c0): ServerLevel::tickBlocks (+0x280 0xcf61f0, profiler "Tick blocks" 0xcf556c) walks the queued
 *   positions, resolves the block through Level +0x98 and calls tick(block, level, pos, state, level->random +0x1b8)
 *   only when Block +0x140 isTicking (0x92aa0 `mov al,[rdi+0x149]`) is set (0xcf63a6 -> 0xcf63ea); scheduled ticks call the
 *   same slot (0xcf7479, 0xcf82eb). randomTick +0x1e8 0x936b0 forwards to it. setTicking +0x128 0x92a90 writes +0x149.
 * neighborChanged +0x208 (0x936f0): (block, state, level, pos, Block* from, BlockPos* fromPos); TntBlock 0x6eda60 and
 *   NoteBlock 0x4d4a90 begin with Level::hasNeighborSignal 0x401bf0(level=rdx, pos=rcx); DoorBlock 0x210250 too.
 * stepOn +0x2c8 (0x940c0): (block, level, pos, shared_ptr<Entity>*); overridden by Magma 0x43eed0, Slime, Campfire, TurtleEgg.
 * entityInside +0x328 (0x94180): (block, state, level, pos, shared_ptr<Entity>*); Cactus 0x1596c0, Web, SweetBerryBush,
 *   pressure plates, Campfire, SoulSand, Lily, Liquid, Tnt.
 * Item +0xc8 inventoryTick (0x3af000): (item, shared_ptr<ItemInstance>*, level, shared_ptr<Entity>*, int slot, bool
 *   selected); MapItem 0x445340 checks level +0x248 isClientSide first and keeps r9d (selected).                 S */
#define LCE195_OFF_BLOCK_TICK        0x1f0u
#define LCE195_RVA_BLOCK_TICK        0x936c0u
#define LCE195_OFF_BLOCK_IS_TICKING  0x140u
#define LCE195_RVA_BLOCK_IS_TICKING  0x92aa0u
#define LCE195_OFF_BLOCK_TICKING     0x149u     /* bool isTicking (word +0x148 byte 1) */
#define LCE195_OFF_BLOCK_NEIGHBOR    0x208u
#define LCE195_RVA_BLOCK_NEIGHBOR    0x936f0u
#define LCE195_OFF_BLOCK_STEP_ON     0x2c8u
#define LCE195_RVA_BLOCK_STEP_ON     0x940c0u
#define LCE195_OFF_BLOCK_INSIDE      0x328u
#define LCE195_RVA_BLOCK_INSIDE      0x94180u
#define LCE195_OFF_ITEM_INV_TICK     0xc8u
#define LCE195_RVA_ITEM_INV_TICK     0x3af000u
/* particles (API 1.3). ServerLevel::sendParticles 0xcfd290(level, ParticleType*, bool override, int count, int* data,
 * int ndata, double x, y, z, dx, dy, dz, speed): new(0x78) ClientboundLevelParticlesPacket ctor 0x18d080 (vtable 0x1228ab8;
 * type +0x28, override +0x50, x..speed floats +0x30..+0x48, count +0x4c), then sent to every player in range
 * ([level+0x100..0x108]). Caller pattern: BlockRemover 0x87dd10 -> colour variant 0xcfd730.
 * ParticleType has no RTTI: int id at +0x28 (getId 0x526750, 125 callers); byId 0x526780 uses unordered_map<int,
 * ParticleType*> 0x142e1b8 via operator[] (inserts on a miss), so the module walks that map's node list read-only:
 * list sentinel [map+8], node next +0 / key int +0x10 / value +0x18 (lookup 0x5267d0)                           S */
#define LCE195_RVA_SEND_PARTICLES   0xcfd290u
#define LCE195_RVA_SEND_PARTICLES_BODY 0xcfd2f1u   /* mov r15,rdi; mov edi,0x78; mov r13d,ecx; mov r14d,edx ... */
#define LCE195_RVA_PARTICLE_MAP     0x142e1b8u
#define LCE195_RVA_PARTICLE_GET_ID  0x526750u
#define LCE195_OFF_PARTICLE_ID      0x28u
/* The stock BlockRemover sends normalized RGB in a separate particle packet variant. */
#define LCE195_RVA_SEND_TINTED_PARTICLES 0xcfd730u
#define LCE195_RVA_TINTED_PARTICLES_PACKET_CALL 0xcfd839u
#define LCE195_RVA_TINTED_PARTICLES_PACKET_COLOR 0x18d2a6u
#define LCE195_RVA_TINTED_PARTICLE_TYPE_REF 0x87dd5au
#define LCE195_RVA_TINTED_PARTICLE_TYPE 0x142e060u
/* item NBT (API 1.3). ItemInstance: CompoundTag* tag at +0x30 (load 0x3b7740 replaces it via vt +0x30 delete, copies via vt
 * +0x40; save 0x3befe0). getOrCreateTag 0x3ba900 (new(0x58) + CompoundTag ctor 0x1b6df0, vtable 0x122ede0).
 * CompoundTag = std::map<wstring, Tag*> at +8 (find 0x1e4f30, end [tag+0x10], value node+0x38). Keys: std::wstring*.
 *   get(key) -> Tag* or NULL 0x1b7400   contains(key) 0x1af0c0   remove(key) 0x1b8630 (erase 0x1b86b0)
 *   putByte 0x1b7460 putShort 0x1b7550 putInt 0x1b25e0 putLong 0x1b7640 putFloat 0x1b7820 putDouble 0x1b7910
 *   putString 0x1af3b0 (StringTag ctor 0x68b620: wstring at +8) putByteArray 0x1b7a00 putIntArray 0x1b7b00 put(key,Tag*) 0x1af4a0
 *   getByte 0x1b7c90 getShort 0x1b7d70 getInt 0x1b2770 getLong 0x1b7e50 getFloat 0x1b8010 getDouble 0x1b8100
 *   getString 0x1af2a0 getCompound 0x1af120 getList 0x1b8460 getByteArray 0x1b81f0 getIntArray 0x1b82d0 contains(key,type) 0x1b7bf0
 * Tag vt +0x18 getId (Byte 1 Short 2 Int 3 Long 4 Float 5 Double 6 String 8 Compound 10); NumericTag +0x60 asLong,
 * +0x68 asInt, +0x70 asShort, +0x78 asByte, +0x80 asDouble, +0x88 asFloat; value at +8.                          S */
#define LCE195_OFF_STACK_TAG        0x30u
#define LCE195_RVA_TAG_GET_OR_CREATE 0x3ba900u
#define LCE195_RVA_TAG_GET          0x1b7400u
#define LCE195_RVA_TAG_REMOVE       0x1b8630u
#define LCE195_RVA_TAG_PUT_INT      0x1b25e0u
#define LCE195_RVA_TAG_PUT_DOUBLE   0x1b7910u
#define LCE195_RVA_TAG_PUT_STRING   0x1af3b0u
#define LCE195_RVA_COMPOUNDTAG_VT   0x122ede0u
#define LCE195_RVA_INTTAG_VT        0x126e2c8u
#define LCE195_RVA_BYTETAG_VT       0x12209a8u
#define LCE195_RVA_SHORTTAG_VT      0x12bd5c8u
#define LCE195_RVA_LONGTAG_VT       0x127b1e0u
#define LCE195_RVA_FLOATTAG_VT      0x1258388u
#define LCE195_RVA_DOUBLETAG_VT     0x123cfd0u
#define LCE195_RVA_STRINGTAG_VT     0x12d83a8u
/* mod networking (API 1.4). Custom payload packets (Java "plugin channels"):
 * ClientboundCustomPayloadPacket vtable 0x12287e0, ctor 0x18ba10(pkt, const wstring* channel, uint8_t* data, int len):
 *   channel wstring +0x28, data +0x50 (owned: dtor 0x18bab0 frees it with delete[] 0xe6cfa0), len +0x58; size 0x68.
 *   ServerPlayer +0xd68 0xd08bf0 builds one (new 0x68, data from toByteArray 0x13b270, channel global 0x14100e8),
 *   wraps it in a _Ref_count whose destroy is a virtual delete (0x573910, same body as _Ref_count<Packet> 0x14fc60)
 *   and sends it with connection->send 0xcbb710 ([player+0xb60]).
 * ServerboundCustomPayloadPacket vtable 0x12bb4c0 (ctor 0x5fbd70, same layout; handle 0x5fbfc0 -> listener vt +0x340).
 * ServerGamePacketListenerImpl +0x340 handleCustomPayload = 0xcf10a0 (vanilla MC| channels).                     S */
#define LCE195_RVA_PAYLOAD_CTOR     0x18ba10u
#define LCE195_RVA_PAYLOAD_VT       0x12287e0u
#define LCE195_PAYLOAD_SIZE         0x68u
#define LCE195_RVA_NEW_ARRAY        0xe6cfc0u
#define LCE195_OFF_LISTENER_PAYLOAD 0x340u
#define LCE195_RVA_LISTENER_PAYLOAD 0xcf10a0u
#define LCE195_RVA_SB_PAYLOAD_VT    0x12bb4c0u
/* entity NBT (API 1.5). Entity::saveWithoutId 0x27bc70(entity, CompoundTag*) -> the tag: writes "Pos"/"Motion"/"Rotation"
 * (ListTag), "FallDistance", "Fire", "Air", "OnGround", "Dimension", "Invulnerable", "PortalCooldown", "UUID", "CustomName",
 * ..., "Tags", then vt +0x2b8 addAdditionalSaveData (LivingEntity "Health", mobs, players) and the passengers. Read-only:
 * the module saves into a temporary CompoundTag (new 0x58 + ctor 0x1b6df0, vtable 0x122ede0) and deletes it through vt +0x30
 * (deleting dtor 0x1b7010 = dtor 0x1b6ec0 + delete, the same call saveWithoutId 0x27c7e6 uses).
 * ListTag vtable 0x1278268 (ctor 0x41cbf0: vector<Tag*> +0x10 begin / +0x18 end, element type byte +0x28; getId 9).
 * Entity scoreboard tags: std::set<wstring> object at +0x360 (head node +0x368, size +0x370). Node: left +0, parent +8,
 * right +0x10, color +0x18, isnil +0x19, wstring value +0x20 (buffer +0x28, size +0x38, cap +0x40). Saved as "Tags" (StringTag
 * per node, 0x27c605..0x27c6f9) and loaded back by Entity::load (at most 1024, 0x27dac5). Load rebuilds the set with exactly two
 * natives (0x27da18..0x27db13): _Erase(set, root=[head+8]) 0x2c50f0, reset head left/parent/right = head and size = 0, then
 * insert(out[0x20], set, false, const wstring*) 0x2c5150 per entry (copies the string). The module edits mod data the same way. S */
#define LCE195_RVA_ENTITY_SAVE      0x27bc70u
#define LCE195_RVA_ENTITY_SAVE_TAGS 0x27c605u   /* mov rax,[r12+0x368] (tag set head) */
#define LCE195_RVA_COMPOUND_CTOR    0x1b6df0u
#define LCE195_COMPOUND_SIZE        0x58u
#define LCE195_RVA_LISTTAG_VT       0x1278268u
#define LCE195_RVA_TAGSET_INSERT    0x2c5150u
#define LCE195_RVA_TAGSET_ERASE     0x2c50f0u
#define LCE195_RVA_TAGSET_ERASE_BODY 0x2c5100u  /* cmp byte [rbx+0x19],0 (isnil) ... */
#define LCE195_RVA_TAGSET_ERASE_CAP 0x2c511fu   /* cmp qword [rbx+0x40],8 (wstring cap of the node value) */
#define LCE195_RVA_LOAD_TAGS_CLEAR  0x27da26u   /* lea r15,[rbx+0x360]; mov rdi,r15; mov rsi,[rax+8] */
#define LCE195_RVA_LOAD_TAGS_ERASE_CALL 0x27da34u  /* call 0x2c50f0 */
#define LCE195_RVA_LOAD_TAGS_INSERT_CALL 0x27daf9u /* call 0x2c5150 */
#define LCE195_OFF_ENTITY_TAGS      0x360u
#define LCE195_MAX_ENTITY_TAGS      1024u

/* client-side mod networking (API 1.6). LCE keeps the TU packet ids: Packet::map 0x5115e0(id, client, server, any, ?, create)
 * fills unordered_map<int, shared_ptr<Packet>(*)()> 0x142dcb0 at static init (list sentinel [map+8]; node next +0, key int +0x10,
 * create +0x18; the same Dinkumware shape as the ParticleType map). id 250 = ClientboundCustomPayloadPacket, create 0x514e40
 * (new 0x68 + default ctor 0x18b9b0 + shared_ptr, registered at 0x5112af); id 4 = ClientboundSetTimePacket, create 0x511890
 * (node written at 0x50b2d7; the server sends it every second, so it pumps the client thread). Received packets are built by that
 * create function (remote clients deserialise every packet), then the client thread calls packet vt +0x28 handle(packet, listener):
 * payload 0x18bd90 -> ClientPacketListener (vtable 0x132eb18) +0x110 handleCustomPayload 0xba2a90; SetTime 0x1a2620 -> +0x200.
 * The module redirects the two create entries (data words) to wrappers that call the stock create and give the new packet a
 * private 11-slot vtable copy with +0x28 routed. Client -> server: ServerboundCustomPayloadPacket (new 0x60, ctor 0x5fbd70(pkt,
 * channel ws*, uint8_t* data, len): channel +0x28, data +0x50 owned (dtor 0x5fbc50 delete[]s it), len +0x58), shared like every
 * packet, sent by ClientPacketListener::send 0xb91460(listener, shared_ptr<Packet>*) (returns early when [listener+8] is set),
 * as the game's own client payload sender 0xc6e260 does (MultiplayerLocalPlayer +0xb68 holds the same listener).       S */
#define LCE195_RVA_PACKET_MAP        0x142dcb0u
#define LCE195_PACKET_ID_PAYLOAD     250
#define LCE195_PACKET_ID_SET_TIME    4
#define LCE195_RVA_PAYLOAD_CREATE    0x514e40u
#define LCE195_RVA_SET_TIME_CREATE   0x511890u
#define LCE195_RVA_SET_TIME_VT       0x122c3a0u
#define LCE195_RVA_PAYLOAD_HANDLE    0x18bd90u
#define LCE195_RVA_SET_TIME_HANDLE   0x1a2620u
#define LCE195_PACKET_VT_SLOTS       11u
#define LCE195_OFF_PACKET_HANDLE     0x28u
#define LCE195_RVA_CLIENT_LISTENER_VT 0x132eb18u
#define LCE195_RVA_CLIENT_LISTENER_PAYLOAD 0xba2a90u   /* +0x110 */
#define LCE195_RVA_CLIENT_SEND       0xb91460u
#define LCE195_RVA_SB_PAYLOAD_CTOR   0x5fbd70u
#define LCE195_SB_PAYLOAD_SIZE       0x60u

/* give / held item / movement (API 1.2) */
#define LCE195_RVA_INVENTORY_ADD    0x37d4d0u  /* bool Inventory::add(inv, shared_ptr<ItemInstance>*): GiveItemCommand 0x3335a0 calls it with
                                                * inv = [player+0x7d0] and a freshly built stack (0x3b7720 + bind 0x68d30)  S */
#define LCE195_RVA_STACK_MAX_DAMAGE 0x3b8730u  /* int getMaxDamage(stack): (empty ? EMPTY : stack+0x28)->+0x1c                S */
#define LCE195_RVA_STACK_HURT       0x3b8750u  /* bool hurt(stack, int amount, Random*): false when empty / maxDamage <= 0;
                                                * Random passed to the unbreaking check 0x1fcf60; true = broken                S */
#define LCE195_RVA_STACK_SET_DAMAGE 0x3b86e0u  /* setDamage(stack, max(0,v)) -> +0x3c (r72 nbt_item_damage pin)              C */
#define LCE195_OFF_LEVEL_RANDOM     0x1b8u     /* Level Random* (r72 native_owner random())                                    C */
#define LCE195_OFF_TELEPORT         0x670u     /* Entity::teleportTo(x,y,z) 0x284180 (+0x378 flag, moveTo +0x218); ServerPlayer
                                                * 0xd0ca30 forwards to connection(+0xb60)->teleport 0xcc8240 with yaw/pitch +0x1a0/+0x1a4 S */
#define LCE195_RVA_ENTITY_TELEPORT  0x284180u
#define LCE195_RVA_PLAYER_TELEPORT  0xd0ca30u
#define LCE195_OFF_PUSH             0x230u     /* Entity::push(dx,dy,dz) 0x27b3b0: deltaMovement +0x188..+0x198 += d; hasImpulse +0x2a9 = 1 S */
#define LCE195_RVA_ENTITY_PUSH      0x27b3b0u
#define LCE195_OFF_DELTA            0x188u
/* entity interact: Entity vtable +0xe50 (Entity 0x27b400, Mob 0x48e460, Animal 0x10ae0 breeding). Cow +0xe50 0x1d21c0
 * takes the player's held item (player vt +0xae0), compares it with the bucket global, and falls back to Animal's;
 * MushroomCow and Sheep override it again (bowl / shears). +0x348 is the interaction PROMPT query (returns a string hash). S */
#define LCE195_OFF_INTERACT         0xe50u

/* health (r72 nbt_health_player_195 pins, console-proven readers/writers) */
#define LCE195_RVA_HEALTH_GET       0x4244d0u  /* float getHealth(LivingEntity*)            C */
#define LCE195_RVA_HEALTH_SET       0x4244f0u  /* void setHealth(LivingEntity*, float)     C */
#define LCE195_RVA_HEALTH_MAX       0x427e20u  /* float getMaxHealth(LivingEntity*)        C */
#define LCE195_RVA_IS_ALIVE         0x427240u  /* bool isAlive: !removed(+0x1fa) && health>0 C */
#define LCE195_OFF_ENTITY_REMOVED   0x1fau
/* fire (r72 nbt_fire_player_195 pins): remaining fire ticks int32 at +0x1c0 */
#define LCE195_OFF_FIRE_TICKS       0x1c0u
#define LCE195_RVA_FIRE_CLEAR       0x2762a0u  /* clearFire: mov dword [rdi+0x1c0],0; ret  C */
#define LCE195_RVA_FIRE_COUNTDOWN   0x275f41u  /* baseTick: --[rbx+0x1c0]                  C */
/* held item (r72 nbt_item_count_195 pins) */
#define LCE195_OFF_PLAYER_INVENTORY 0x7d0u     /* shared_ptr<Inventory> object/control (+0x7d8) C */
#define LCE195_RVA_INVENTORY_REF_VT 0x12aa3e8u /* Inventory _Ref_count vtable             C */
#define LCE195_RVA_INVENTORY_VT     0x126e390u
#define LCE195_OFF_INV_ITEMS        0x8u       /* vector<shared_ptr<ItemInstance>> begin (24-byte records) C */
#define LCE195_OFF_INV_SELECTED     0xfcu      /* selected hotbar slot 0..8               C */
#define LCE195_OFF_INV_PLAYER       0x100u
#define LCE195_RVA_EMPTY_STACK      0x1416d98u /* ItemInstance::EMPTY global              C */
#define LCE195_OFF_STACK_COUNT      0x10u
#define LCE195_OFF_STACK_EMPTY      0x38u
#define LCE195_OFF_STACK_ITEM       0x28u
#define LCE195_RVA_STACK_SET_COUNT  0x3626c0u  /* setCount(stack, int); count<=0 sets +0x38 C */
#define LCE195_RVA_INV_SET_CHANGED  0x380b70u  /* Inventory::setChanged: ++[rdi+0x138]    C */

/* language */
#define LCE195_RVA_GAME_SETTINGS    0x14404c0u /* GameSettings* [pads]; +0x90 lang, +0x91 locale S */
#define LCE195_OFF_SETTINGS_LANG    0x90u
#define LCE195_OFF_SETTINGS_LOCALE  0x91u

#ifdef __cplusplus
extern "C" {
#endif
/* Game entry points used by services195. Filled from the RVAs above on the
 * console (lce195_natives_bind_rvas), by fakes in tests. */
typedef struct lce195_natives {
    uintptr_t base;
    void *(*alloc)(size_t);
    void (*free)(void *);
    int (*atomic_sub)(uintptr_t addr, int v, int order);
    void (*ws_assign)(void *ws, const uint16_t *units, uint64_t n);
    void (*chat_ctor)(void *pkt, const void *ws, int type, int custom);
    void (*enable_shared)(lce195_pair *out, uintptr_t obj, uintptr_t ctrl);
    void (*broadcast_all)(uintptr_t player_list, lce195_pair *sp);
    void (*conn_send)(uintptr_t conn, lce195_pair *sp);
    void (*key_ctor)(void *key, const void *ws);
    void (*key_dtor)(void *key);
    void (*effect_inst_ctor)(uintptr_t inst, uintptr_t effect, int ticks, int amp);
    uintptr_t (*item_by_id)(unsigned id);
    void (*stack_ctor)(uintptr_t stack, uintptr_t item, int32_t count, int32_t aux);
    void (*stack_dtor)(uintptr_t stack);
    void (*stack_bind)(lce195_pair *out, uintptr_t obj, uintptr_t ctrl);
    void (*pop_resource)(uintptr_t level, const int32_t *pos, const lce195_pair *stack);
    /* SCE system language (sceSystemServiceParamGetInt(1)); -1 unknown */
    int (*system_language)(void);
    /* API 1.2 (NULL = service unsupported) */
    float (*health_get)(uintptr_t entity);
    void (*health_set)(uintptr_t entity, float v);
    float (*health_max)(uintptr_t entity);
    uint8_t (*is_alive)(uintptr_t entity);   /* bool in al */
    void (*fire_clear)(uintptr_t entity);
    void (*stack_set_count)(uintptr_t stack, int32_t count);
    void (*inventory_set_changed)(uintptr_t inventory);
    uint8_t (*inventory_add)(uintptr_t inventory, const lce195_pair *stack);
    int32_t (*stack_max_damage)(uintptr_t stack);
    uint8_t (*stack_hurt)(uintptr_t stack, int32_t amount, uintptr_t random);
    void (*stack_set_damage)(uintptr_t stack, int32_t v);
    /* API 1.3: DamageSource::playerAttack (copies the pair; returns an owned EntityDamageSource*, delete via vt +8) */
    uintptr_t (*player_attack_source)(const lce195_pair *player);
    uintptr_t (*mob_attack_source)(const lce195_pair *mob);                       /* DamageSource::mobAttack */
    void (*weak_lock)(lce195_pair *out, uintptr_t obj, uintptr_t ctrl, int throw_if_expired);
    uintptr_t (*tag_get_or_create)(uintptr_t stack);
    uintptr_t (*tag_get)(uintptr_t tag, const void *key);
    void (*tag_remove)(uintptr_t tag, const void *key);
    void (*tag_put_int)(uintptr_t tag, const void *key, int32_t v);
    void (*tag_put_double)(uintptr_t tag, const void *key, double v);
    void (*tag_put_string)(uintptr_t tag, const void *key, const void *value);
    void (*payload_ctor)(void *pkt, const void *channel, uint8_t *data, int32_t len);
    void *(*alloc_array)(size_t);   /* operator new[] (the packet frees its data with delete[]) */
    void (*send_particles)(uintptr_t level, uintptr_t type, uint8_t override_limiter, int32_t count, const int32_t *data, int32_t ndata,
                           double x, double y, double z, double dx, double dy, double dz, double speed);
    /* API 1.5 entity NBT */
    void (*compound_ctor)(uintptr_t tag);
    uintptr_t (*entity_save)(uintptr_t entity, uintptr_t tag);                 /* saveWithoutId; returns tag */
    void (*tagset_insert)(void *out, uintptr_t set, int leftish, const void *ws);
    void (*tagset_erase)(uintptr_t set, uintptr_t root);
    /* API 1.6 client networking */
    void (*sb_payload_ctor)(void *pkt, const void *channel, uint8_t *data, int32_t len);
    void (*client_send)(uintptr_t listener, lce195_pair *sp);
    void (*send_tinted_particles)(uintptr_t level, uintptr_t type, int32_t count, const int32_t *data, int32_t ndata,
                                  double x, double y, double z, double dx, double dy, double dz, double speed,
                                  double red, double green, double blue);
} lce195_natives;
void lce195_natives_bind_rvas(lce195_natives *n, uintptr_t base);
#ifdef __cplusplus
}
#endif
