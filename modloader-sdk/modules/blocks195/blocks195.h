/* blocks195: generic N-block backend module for Minecraft PS4 Edition 1.95
 * (CUSA00265). Every RVA is from the 1.95 image (reconstructed ELF SHA-256
 * c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f); see
 * STATUS.md for the evidence behind each one. Never mix with 2.72 addresses.
 *
 * Phases (the module never runs game code outside them):
 *   1. b195_configure()       plugin startup, after image attestation. Pure
 *                             validation of the plan; no game memory access.
 *   2. b195_hook_install()    plugin startup (data hook, preferred): swaps the
 *                             block registry object's vptr (base+0x140ad68 ->
 *                             object) to a private copy of its 10-slot vtable
 *                             whose +0x28 forwards to the stock register
 *                             0xb8090. No .text byte is modified.
 *      -- or --               the lead's existing .text window at 0xb74de calls
 *                             b195_blocks_publish() with blocks_held()==1.
 *   3. block phase            relay sees the last stock registration (id 365,
 *                             0xb74d8 in Block init 0x957a0), restores the stock
 *                             vptr, then constructs + registers every SDK block.
 *   4. b195_items_publish()   inside the lead's held post-stock-items window
 *                             (0x477cc8 -> Item init 0x386070 returned):
 *                             BlockItems, block->item map, item names, fire odds.
 *   5. b195_terrain_*()       inside the lead's terrain reload lease, after the
 *                             grown terrain atlas was uploaded.
 *
 * Return convention of every publishing call: 1 complete, 0 refused before any
 * game-visible write (plugin-owned key storage may have been filled), -1
 * partial/uncertain (objects retained, never retry, keep IDs reserved). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../include/lce_mod_api.h"
#include "../../loader/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- pinned 1.95 RVAs ------------------------------------------------------ */
#define B195_RVA_ALLOC               0xe6cfd0u  /* operator new(size_t) */
#define B195_RVA_WSTRING             0x67840u   /* wstring ctor/assign (dst, const u16*, n) */
#define B195_RVA_KEY_CTOR            0xcda2f0u  /* ResourceLocation(dst[0x70], const wstring& "ns:path") */
#define B195_RVA_REGISTRY_GLOBAL     0x140ad68u /* Block registry object pointer (0xcc0 bytes, built by static ctor 0x150cd0) */
#define B195_RVA_REGISTRY_VTABLE     0x1218840u /* DefaultedMappedRegistry<ResourceLocation,Block*> primary vtable, 10 slots */
#define B195_RVA_REGISTRY_RTTI       0x121e8e0u
#define B195_RVA_REGISTER            0xb8090u   /* slot +0x28: (reg, int id, const ResourceLocation&, Block*) */
#define B195_RVA_PRESENT             0xb82c0u   /* slot +0x20: bool containsKey(reg, key) */
#define B195_RVA_BY_KEY              0xb8120u   /* slot +0x00: Block* get(reg, key) (fatal when absent: call only after PRESENT) */
#define B195_REGISTRY_SLOTS_OFF      0x150u     /* Block* slots[366] */
#define B195_REGISTRY_SLOTS          10u
#define B195_REGISTRY_REGISTER_SLOT  (0x28u / 8u)
#define B195_BLOCK_ID_MAX            365u
#define B195_STOCK_BLOCKS            345u       /* static: 345 registrations in 0x957a0; console ORE_PROBE blocks=345 */
#define B195_RVA_LAST_STOCK_CALL     0xb74d8u   /* call r13 = register(reg, 365, "jigsaw", JigsawBlock) */
#define B195_RVA_JIGSAW_VTABLE       0x126fa48u /* 11JigsawBlock, the block registered at id 365 */

#define B195_RVA_ORE_CTOR            0x503a80u  /* OreBlock(): Block(*0x1418ec0) 0x91fb0; vptr=0x1298cf0; tail 0x924c0 */
#define B195_RVA_BLOCK_CTOR          0x91fb0u   /* Block::Block(Material*) */
#define B195_RVA_BLOCK_INIT_STATES   0x924c0u   /* +0x158=1; StateDefinition via virtual +0x478; +0x188/+0x190 */
#define B195_RVA_ORE_VTABLE          0x1298cf0u /* 8OreBlock, 155 primary slots */
#define B195_RVA_ORE_RTTI            0x12a7070u
#define B195_ORE_SLOTS               155u
#define B195_BLOCK_SIZE              0x1e8u     /* stock alloc for OreBlock (0x972ee: mov edi,0x1e8) */
/* Storage blocks (block_entity_slots = 9): a BaseEntityBlock-shaped object reusing DispenserBlockEntity.
 * BaseEntityBlock ctor 0x424d0 = Block ctor + vptr + EntityBlock vptr at +0x1e8 + byte +0x14a = 1 (hasBlockEntity).
 * The game creates block entities at 0x4107fd: `cmp byte [block+0x14a],0`, then __dynamic_cast 0xe6d020 to EntityBlock
 * (typeinfo 0x12126a8) and calls the EntityBlock vtable +0x10 newBlockEntity (no null check at 0x40b6e9).
 * The module builds the same layout: OreBlock construction on a 0x1f0 object, +0x1e8 = a private EntityBlock vtable
 * {-0x1e8, typeinfo, 0x227af0, 0x227b00, 0x208ef0 DispenserBlock::newBlockEntity}, +0x14a = 1, and the primary RTTI slot =
 * a module __si_class_type_info deriving from BaseEntityBlock (typeinfo 0x12126c0 = vmi {Block 0x1218338 @0, EntityBlock
 * @0x1e8}), so dynamic_cast<EntityBlock*> lands on +0x1e8 exactly as for a dispenser. Primary slots copied from
 * DispenserBlock (vtable 0x1239608): +0x228 onRemove 0x209570 (drops the container, BaseEntityBlock::onRemove 0x508d0),
 * +0x2c0 use 0x2085a0 (getBlockEntity, type mask 0x3c00200, player vt +0xd70 open), +0x340 0x43140 and +0x398 0x53010
 * (BaseEntityBlock, shared with ChestBlock), +0x408/+0x410 comparator 0x209750/0x209760. Not copied: FACING/TRIGGERED
 * state slots, tick/neighbour (dispensing), +0x2d0 setPlacedBy (facing).                                           S */
#define B195_CONTAINER_SIZE          0x1f0u
#define B195_CONTAINER_SLOTS         9u
#define B195_F_HAS_BLOCK_ENTITY      0x14au
#define B195_F_ENTITY_VPTR           0x1e8u
#define B195_RVA_DISPENSER_VTABLE    0x1239608u
#define B195_RVA_DISPENSER_RTTI      0x1242080u /* 14DispenserBlock __si_class_type_info -> BaseEntityBlock */
#define B195_RVA_BASE_ENTITY_RTTI    0x12126c0u /* 15BaseEntityBlock __vmi_class_type_info */
#define B195_RVA_ENTITY_BLOCK_RTTI   0x12126a8u /* 11EntityBlock */
#define B195_RVA_BLOCK_RTTI          0x1218338u
#define B195_RVA_DISP_NEW_BE         0x208ef0u
#define B195_RVA_DISP_SEC_DTOR       0x227af0u
#define B195_RVA_DISP_SEC_DDTOR      0x227b00u
#define B195_RVA_BE_CHECK            0x4107fdu  /* cmp byte [rax+0x14a],0 */
#define B195_RVA_BE_CTOR_FLAG        0x424f9u   /* BaseEntityBlock ctor: mov byte [rbx+0x14a],1 */
/* Variants (API 1.7 variant_count 2..16): the ColoredBlock (vtable 0x122d060) state machinery. Its
 * createBlockStateDefinition +0x478 0x1a9950 builds a StateDefinition over the static 16-value COLOR property (no use of
 * `this` beyond passing it to the builder 0xcf850); +0x48 stateFromData 0x1a9850 (DyeColor::byId 0x21ff80 clamps > 15 to 0),
 * +0x50 dataFromState 0x1a9900, +0x268 drop aux 0x1a97b0. The Block ctor tail 0x924c0 calls vt +0x478, so the private vtable
 * is installed between Block::Block 0x91fb0 and 0x924c0 (as ColoredBlock's own ctor 0x1a96d0 does with its vtable).
 * Block +0x2d0 getStateForPlacement 0x940d0 tail-calls +0x48 with the item's level data; BlockItem +0x58 getLevelDataForAuxValue
 * (stock 0x3aeea0 returns 0) becomes MultiBlockItem's 0x49f950 (`mov eax,esi; ret`), and Item byte +0x31 (setStackedByData
 * 0x3a5a70) is set so stacks keep their aux. Textures per variant: +0x458 getTexture(face Direction*, state) (stock 0x95340
 * reads Direction +0x8 via 0x200600) is a module dispatcher.                                                         S */
#define B195_RVA_COLORED_VTABLE      0x122d060u
#define B195_RVA_COLORED_STATEDEF    0x1a9950u  /* vt+0x478 */
#define B195_RVA_COLORED_FROM_DATA   0x1a9850u  /* vt+0x48  BlockState* (block, int data) */
#define B195_RVA_COLORED_TO_DATA     0x1a9900u  /* vt+0x50  int (block, BlockState*) */
#define B195_RVA_COLORED_DROP_AUX    0x1a97b0u  /* vt+0x268 int (block, BlockState*) */
#define B195_RVA_MULTI_ITEM_VTABLE   0x128aca8u /* 14MultiBlockItem */
#define B195_RVA_MULTI_LEVEL_DATA    0x49f950u  /* MultiBlockItem +0x58: mov eax,esi; ret */
#define B195_RVA_SET_STACKED         0x3a5a70u  /* Item::setStackedByData: mov rax,rdi; mov [rdi+0x31],sil; ret */
#define B195_RVA_DIR_INDEX           0x200600u  /* Direction::get3DDataValue: mov eax,[rdi+8]; ret */
#define B195_RVA_STACK_AUX           0x3557c0u  /* ItemInstance::getAuxValue: mov eax,[rdi+0x3c]; ret (252 callers) */
#define B195_F_ITEM_STACKED          0x31u
#define B195_F_STACK_AUX             0x3cu
#define B195_MAX_VARIANTS            16u
/* Shapes (API 1.7 LCE_SHAPE_BOX, flags NO_COLLIDE / TRANSPARENT). Block vt +0x178 getShape returns an AABB* (0x30 bytes, 6
 * doubles min xyz / max xyz in block units; built by 0x170 box(x0,y0,z0,x1,y1,z1), 404 callers, e.g. EnchantmentTable 0.75 at
 * 0x2d38cd). Both the default collision +0x1c8 (0x93660 -> state +0x110 0xd0d80 -> block +0x178) and +0x190 copy it, so one
 * slot gives outline and collision. Non-full blocks (EnchantmentTable 0x1246108, DaylightDetector 0x1236748, WoolCarpet 0x12f9c10)
 * also return false from +0xd0 and +0x1d0 (isSolidRender: 0x924c0 derives +0x12c/+0x130 light blocking from it through state
 * +0xf8 0xd0c40) and a per-face shape from +0x1a0 (face Direction* in r8; 0 = SOLID, 8 = UNDEFINED; ench 0x252850).
 * NO_COLLIDE: +0x1c8 -> FlowerBlock 0x139170 (`xor eax,eax; ret`: no collision box).
 * TRANSPARENT: the GlassBlock (0x125dce0) set +0xd0 0x333d60, +0x100 0x333d70, +0x158 0x333d90, +0x188 0x353460 (skips faces
 * against the same block), +0x1d0 0x353450.                                                                           S */
#define B195_RVA_BOX                 0x170u
#define B195_RVA_FULL_SHAPE          0x1409f38u /* AABB* global returned by Block +0x178 0x92b40 (lea 0x1409f38; mov rax,[rax]; ret) */
#define B195_RVA_RET_FALSE           0x929d0u   /* Block +0xe8: xor eax,eax; ret */
#define B195_RVA_FLOWER_VTABLE       0x1258458u
#define B195_RVA_GLASS_VTABLE        0x125dce0u
#define B195_RVA_NO_COLLISION        0x139170u  /* FlowerBlock +0x1c8 */
#define B195_SHAPE_SOLID             0
/* API 1.11 multi-box shapes (LCE_SHAPE_BOXES). Block vt +0x190 (default 0x92ef0) returns a box LIST {AABB **data; uint32 count
 * at +8} (the default: a TLS list whose data[0] = +0x178's AABB, data[1] = data[2] = 0; every consumer skips null entries).
 * Consumers: render shape 10 (StairBlock +0xe8 0x678ad0 `mov eax, 0xa`) -> the chunk tesselator switch 0xb322d1 (case 10 at
 * 0xb32581 -> 0xb3b160), which renders EVERY list box as a cube through 0xb5be10 (shape floats at renderer +0x84..+0x98);
 * Block +0x188 0x92c90 (face culling against the list); StairBlock +0x1b0 0x6765c0 (collision: +0x60 state, +0x190 list, then
 * +0x1b8 addAABB per non-null box; only virtual calls on `this`). A mod BOXES block gets: +0xe8 = 0x678ad0, +0x190 = the module
 * list dispatcher (per state when the sets follow the variants), +0x1b0 = 0x6765c0 (unless NO_COLLIDE), +0x178 = the union. */
#define B195_RVA_BOX_LIST_DEFAULT    0x92ef0u   /* Block +0x190 */
#define B195_RVA_ADD_COLLISION       0x93500u   /* Block +0x1b0 (one box via state +0x100 -> +0x1b8) */
#define B195_RVA_STAIR_VTABLE        0x12d2af8u
#define B195_RVA_STAIR_RENDER_SHAPE  0x678ad0u  /* StairBlock +0xe8: mov eax, 0xa; ret */
#define B195_RVA_STAIR_COLLISION     0x6765c0u  /* StairBlock +0x1b0: addAABB for every box of +0x190 */
#define B195_RVA_TESS_MULTIBOX       0xb3b160u  /* render shape 10 tesselator: every +0x190 box through 0xb5be10 */
#define B195_RVA_TESS_SHAPE10_CALL   0xb3258cu  /* the switch's case-10 call to 0xb3b160 */
#define B195_MAX_BOXES               8u
#define B195_SHAPE_UNDEFINED         8
/* Chest storage blocks (block_entity_slots = 27): the mod block IS a ChestBlock. ChestBlock(type) 0x173060 (the stock alloc is
 * 0x1f8 at 0x9b3a1, type 0 = chest, 1 = trapped: +0x1f0) = BaseEntityBlock ctor 0x424d0 (Material WOOD 0x1418890) + vptr
 * 0x12250f0 (156 primary slots: ChestBlock adds +0x4d8 0x1755f0) + EntityBlock vptr +0x1e8 = 0x12255e0 ({-0x1e8, ti 0x1225af0,
 * 0x191fd0, 0x191fe0, newBlockEntity 0x175800 -> ChestBlockEntity 0x100, 27 slots}) + 0x924c0 (FACING, the only property, static
 * 0x140ff38). use 0x174ba0 -> getContainer 0x174d70 (CompoundContainer with a neighbouring chest BE, type mask 0x1e00201) ->
 * player vt +0xd70 0xd07d20 (ServerPlayer::openContainer: a Container that is neither InteractionObject nor
 * PlayerEnderChestContainer gets ClientboundContainerOpenPacket + ChestMenu 0x17a750 with rows = size / 9). The module copies
 * the ChestBlock vtable and puts back the Block cube slots: +0xd0 0x92970, +0xd8 0x929a0, +0xe8 getRenderShape 0x929d0 (chest:
 * 0x16 = entity-rendered), +0x178 0x92b40, +0x1a0 0x934c0, +0x1d0 0x93680, +0x438 0x94ea0 (texture by name).               S */
#define B195_CHEST_SIZE              0x1f8u
#define B195_CHEST_STORAGE_SLOTS     27u
#define B195_CHEST_VT_SLOTS          156u
#define B195_RVA_CHEST_CTOR          0x173060u
#define B195_RVA_CHEST_VTABLE        0x12250f0u
#define B195_RVA_CHEST_RTTI          0x1225af0u
#define B195_RVA_CHEST_ENTITY_VT     0x12255e0u /* +0x1e8 after the ctor (= vtable + 0x4f0) */
#define B195_RVA_CHEST_NEW_BE        0x175800u
#define B195_RVA_CHEST_ALLOC_SITE    0x9b3a1u   /* mov edi,0x1f8; call new; xor esi,esi; ...; call 0x173060 */
#define B195_F_CHEST_TYPE            0x1f0u
/* Storage titles and the chest model (2026-11-18, static). Both storage kinds get a private EntityBlock vtable whose
 * newBlockEntity (+0x10, sret: rdi = shared_ptr result, rsi = EntityBlock this, rdx = Level*, ecx = data; call sites
 * 0x40b6e4..0x40b701) is a module wrapper: it calls the stock 0x175800 / 0x208ef0, then
 *  - names the BE: RandomizableLockableContainerBlockEntity::setCustomName 0x597b30 (`mov rdi,[rdi+0x68]; jmp 0xb826f0`,
 *    the call ChestBlock::setPlacedBy 0x1735e0 makes at 0x173a1a for an anvil-named item, with ItemInstance's defaults
 *    second = L"", b1 = 0, b2 = 1). ServerPlayer::openContainer 0xd07d20 casts the Container to InteractionObject (0xaaa840;
 *    Chest/DispenserBlockEntity derive it through LockableContainer @+0x40) and sends InteractionObject +0x30 = 0x5985b0 ->
 *    0x598460 as the ContainerOpenPacket title: the text of the name record [be+0x68] (0xb82300). NBT "CustomName" keeps it
 *    (load 0x176d05 -> 0xb82450, save 0x177310 -> 0xb821e0). A double chest (CompoundContainer, InteractionObject +0x30 =
 *    0x65230 -> Nameable getName 0x1b5c00) shows the first half's custom name.
 *  - chest storage only: swaps the ChestBlockEntity primary vptr (0x1225618, prefix {0xf8, 0xf8, 0, ti 0x122a880}, 63 slots
 *    to 0x1225808) for a private copy whose +0x70 getViewDistance (stock 0xbad20 returns a constant) returns -1.0: the BE
 *    render pass 0xb2b9d0 skips a BE when viewDistance <= distanceToSqr (+0x68 0xbace0; `vucomisd; jbe` at 0xb2ba35), so the
 *    ChestRenderer (0x132e7f0, render 0xb83af0) never draws a chest model inside the cube. Slot 0 getType (0x1e00201: the
 *    renderer map key 0xb2b5a0, the save id 0xba78c, getContainer 0x174dbb) is untouched.                             S */
#define B195_RVA_SET_CUSTOM_NAME     0x597b30u
#define B195_F_BE_NAME               0x68u      /* name record pointer in RandomizableLockableContainerBlockEntity */
#define B195_RVA_CHEST_BE_VTABLE     0x1225618u /* 16ChestBlockEntity primary vtable (address point) */
#define B195_RVA_CHEST_BE_RTTI       0x122a880u
#define B195_CHEST_BE_PREFIX         4u         /* vbase 0xf8, vbase 0xf8, offset-to-top 0, typeinfo */
#define B195_CHEST_BE_SLOTS          63u        /* +0x000 .. +0x1f0; 0x1225810 starts the +0x40 group (vbase 0xb8) */
#define B195_RVA_BE_VIEW_DIST        0xbad20u   /* BlockEntity +0x70: vmovsd xmm0,[rip+..]; ret */
#define B195_RVA_BE_RENDER_CULL      0xb2ba35u  /* vucomisd xmm0,[rbp-0x68]; jbe (skip) */
#define B195_F_BE_VIEW_DIST          0x70u
#define B195_RVA_SET_HARDNESS        0x92a00u   /* vt+0x108: +0x140=h; +0x144=max(+0x144, h*5) */
#define B195_RVA_SET_RESISTANCE      0x92580u   /* vt+0xb0:  +0x144=r*3 */
#define B195_RVA_SET_SOUND           0x92540u   /* vt+0x98:  +0x160=SoundType* */
#define B195_RVA_SET_LIGHT           0x92560u   /* vt+0xa8:  +0x138=(int)(f*15) */
#define B195_RVA_SET_TEXTURE         0x94890u   /* vt+0x418: texture name (wstring) */
#define B195_RVA_BLOCK_NAME          0x94650u   /* vt+0x380: wstring* name(out, block) */
#define B195_RVA_ORE_GET_RESOURCE    0x503b00u  /* vt+0x240: Item* (block, state, Random*, fortune) */
#define B195_RVA_ORE_SPAWN           0x503c70u  /* vt+0x258: spawnResources; kept stock */
#define B195_RVA_ORE_RESOURCE_COUNT  0x503bb0u  /* vt+0x360: int (block, fortune, Random*) */
#define B195_RVA_TEXTURE_REGISTRAR   0x94ea0u   /* vt+0x438: fills +0x1a0 or +0x1b0[6] (+0x1e0 per-face flag) */
#define B195_RVA_FACE_GETTER         0x95340u   /* vt+0x458 */
#define B195_RVA_RANDOM_NEXT_INT     0x593950u  /* int nextInt(Random*, bound) */
#define B195_RVA_FLOAT_15            0xe7576cu  /* 15.0f used by 0x92560 */
#define B195_RVA_FLOAT_3             0xe75770u  /* 3.0f used by 0x92580 */
#define B195_RVA_FLOAT_5             0xe75774u  /* 5.0f used by 0x92a00 */

#define B195_RVA_ITEM_REGISTRY       0x1417638u
#define B195_RVA_ITEM_VECTOR         0x1417720u /* {begin,end,cap}: live 2268 slots, capacity 2398 */
#define B195_RVA_ITEM_REGISTER       0x3af660u  /* (reg, id, key, Item*) overwrites occupied slots */
#define B195_RVA_ITEM_BY_ID          0x385af0u
#define B195_RVA_ITEM_PRESENT        0x385d00u
#define B195_RVA_BLOCK_ITEM_CTOR     0x7d1d0u   /* BlockItem(item, block), 0xb0 bytes */
#define B195_RVA_BLOCK_ITEM_VTABLE   0x1218a50u /* 9BlockItem, 57 primary slots */
#define B195_BLOCK_ITEM_SLOTS        57u
#define B195_BLOCK_ITEM_SIZE         0xb0u
#define B195_RVA_ITEM_NAME           0x3af070u  /* Item vt+0x108 name(out, item, const shared_ptr<ItemInstance>&) */
#define B195_RVA_BLOCK_ITEM_MAP      0x1417788u /* Block* -> BlockItem* map (list-backed) */
#define B195_RVA_BLOCK_ITEM_MAP_SLOT 0x3af480u  /* Item** operator[](map, Block* const*) */
#define B195_ITEM_VECTOR_CAP         2398u

#define B195_RVA_FIRE_GLOBAL         0x140b328u /* Blocks::FIRE, set by 0xc1520 after Block init */
#define B195_RVA_FIRE_VTABLE         0x1252470u /* 9FireBlock */
#define B195_RVA_FIRE_ODDS_SLOT      0x1475c0u  /* int& unordered_map<Block*,int>::operator[] */
#define B195_FIRE_ENCOURAGE_OFF      0x1e8u     /* FireBlock::init 0x2ae7f0 writes 5 here for planks */
#define B195_FIRE_FLAMMABLE_OFF      0x238u     /* ... and 20 here */

#define B195_RVA_SPRITE_VTABLE       0x13451d0u /* 18TextureAtlasSprite */
#define B195_RVA_SPRITE_BIND         0xd53b40u
#define B195_SPRITE_SIZE             0xb8u

/* Block object fields (Block::Block 0x92150/0x91fb0 and the setters above). */
#define B195_F_LIGHT        0x138u  /* int */
#define B195_F_HARDNESS     0x140u  /* float */
#define B195_F_RESISTANCE   0x144u  /* float, stored *3 */
#define B195_F_SOUND        0x160u  /* SoundType* */
#define B195_F_MATERIAL     0x170u  /* Material* */
#define B195_F_FRICTION     0x180u  /* float, ctor 0.6f; ice ctor 0x373eec writes 0.98f */
#define B195_F_STATEDEF     0x188u
#define B195_F_DEFSTATE     0x190u
#define B195_F_ID           0x128u  /* written by 0xb8090 */
#define B195_F_SPRITE       0x1a0u  /* single-texture sprite */
#define B195_F_FACE_SPRITES 0x1b0u  /* 6 sprites, used when +0x1e0 != 0 */
#define B195_F_PER_FACE     0x1e0u  /* byte */

/* ---- limits ------------------------------------------------------------- */
#define B195_MAX_BLOCKS 19u          /* the live-probed free block allowlist (id_space_195.h) */
#define B195_MAX_TILES 256u           /* unique terrain tiles (faces x variants), atlas rows permitting */
#define B195_NAME_UNITS 96u
#define B195_TERRAIN_GENERATIONS 32u

/* ---- natives (bound from the RVAs above on console, fakes in host tests) -- */
typedef struct b195_natives {
    void *(*alloc)(size_t);
    void (*wstring)(void *dst, const uint16_t *units, uint64_t n);
    void (*key_ctor)(void *key, const void *wstr);
    void (*reg_register)(uintptr_t reg, uint32_t id, const void *key, uintptr_t block);
    uint8_t (*reg_present)(uintptr_t reg, const void *key);
    uintptr_t (*reg_by_key)(uintptr_t reg, const void *key);
    void (*ore_ctor)(void *block);
    void (*block_ctor)(void *block, uintptr_t material);
    void (*block_init_states)(void *block);
    void *(*set_hardness)(void *block, float);
    void *(*set_resistance)(void *block, float);
    void *(*set_sound)(void *block, uintptr_t sound);
    void *(*set_light)(void *block, float);
    void *(*set_texture)(void *block, void *wstr);
    void *(*block_name)(void *out, void *block);
    uintptr_t (*get_resource)(void *block, uintptr_t state, uintptr_t random, int fortune);
    int (*resource_count)(void *block, int fortune, uintptr_t random);
    int (*next_int)(uintptr_t random, int bound);
    uintptr_t (*item_by_id)(unsigned id);
    uint8_t (*item_present)(uintptr_t reg, const void *key);
    void (*item_register)(uintptr_t reg, uint32_t id, const void *key, uintptr_t item);
    void (*block_item_ctor)(void *item, uintptr_t block);
    uintptr_t (*map_slot)(uintptr_t map, const uintptr_t *block);
    void *(*item_name)(void *out, void *item, const void *shared_stack);
    int *(*fire_slot)(uintptr_t map, const uintptr_t *block);
    void (*sprite_bind)(uintptr_t sprite, uintptr_t texture, uint64_t zero, int x, int y, int w, int h, int flag);
    /* API 1.7 variants and shapes */
    uintptr_t (*state_from_data)(void *block, int data);           /* 0x1a9850 */
    int (*data_from_state)(void *block, uintptr_t state);          /* 0x1a9900 */
    uintptr_t (*face_sprite)(void *block, uintptr_t face, uintptr_t state);  /* 0x95340 */
    void *(*box)(double x0, double y0, double z0, double x1, double y1, double z1);  /* 0x170 */
    void (*chest_ctor)(void *block, int type);                     /* 0x173060 */
    /* storage titles: newBlockEntity (sret shared_ptr) and setCustomName(be, name, second, b1, b2) */
    void *(*chest_new_be)(void *out, void *entity_block, void *level, int data);    /* 0x175800 */
    void *(*disp_new_be)(void *out, void *entity_block, void *level, int data);     /* 0x208ef0 */
    void (*set_custom_name)(void *be, const void *name, const void *second, int b1, int b2);  /* 0x597b30 */
} b195_natives;

/* Memory/phase services. read/write check protection on the console
 * (sceKernelQueryMemoryProtection); write of 8 aligned bytes is one release
 * store; cas is an 8-byte compare-exchange. */
typedef struct b195_memory {
    void *ctx;
    int (*read)(void *, uintptr_t, void *, size_t);
    int (*write)(void *, uintptr_t, const void *, size_t);
    int (*cas)(void *, uintptr_t addr, uintptr_t expected, uintptr_t desired);
    uint64_t (*thread_id)(void *);
    int (*attested)(void *);       /* image attestation still valid */
    int (*blocks_held)(void *);    /* optional: 1 inside the lead's .text block window */
    int (*items_held)(void *);     /* 1 inside the lead's held post-stock-items window */
    int (*terrain_held)(void *);   /* 1 while the terrain reload/render lease is held */
    /* 1 only when this atlas generation is grown, every stock pixel/UV kept and uploaded. */
    int (*terrain_uploaded)(void *, uintptr_t atlas, uintptr_t texture, uint32_t generation);
} b195_memory;

enum {
    B195_IDLE = 0, B195_CONFIGURED = 1, B195_BLOCKS_DONE = 2, B195_ITEMS_DONE = 3,
    B195_REFUSED = 4, B195_UNCERTAIN = 5
};
enum { B195_HOOK_NONE = 0, B195_HOOK_ARMED = 1, B195_HOOK_FIRED = 2, B195_HOOK_DISARMED = 3, B195_HOOK_MISSED = 4 };
enum { B195_HOOK_MODE_VPTR = 0, B195_HOOK_MODE_SLOT = 1 };

typedef struct b195_block {
    const lce_block_desc *desc;
    uint32_t id, item_id;            /* item_id 0 = NO_ITEM */
    int32_t drop_item;               /* numeric item dropped */
    uint8_t drop_override, count_override, per_face, flammable;
    uint32_t tile[6];                /* index into the module's unique terrain tiles, per face */
    uintptr_t material_global, sound_global;
    uintptr_t object, item, drop_ptr, fire_object;
    uint32_t name_len;
    uint16_t name[B195_NAME_UNITS];
    char ascii_name[B195_NAME_UNITS];
    char texture_name[32];           /* "lcesdk_b<id>" */
    uint8_t key_text[0x30] __attribute__((aligned(16)));
    uint8_t key[0x70] __attribute__((aligned(16)));
    uint8_t texture_text[0x30] __attribute__((aligned(16)));
    uintptr_t vtable[2 + B195_CHEST_VT_SLOTS];     /* private OreBlock (155 slots) or ChestBlock (156) copy */
    uint32_t slots;                  /* block entity container size (0 = none, 9 = DispenserBlockEntity, 27 = ChestBlockEntity) */
    uintptr_t entity_vtable[2 + 3];  /* storage blocks: private EntityBlock vtable (+0x1e8) */
    uintptr_t typeinfo[3];           /* storage blocks: __si_class_type_info {vptr, name, BaseEntityBlock} */
    uintptr_t be_vtable[B195_CHEST_BE_PREFIX + B195_CHEST_BE_SLOTS];   /* chest storage: private ChestBlockEntity vtable */
    uint8_t title_text[0x30] __attribute__((aligned(16)));            /* storage: container title (the display name) */
    uint8_t empty_text[0x30] __attribute__((aligned(16)));
    uintptr_t item_vtable[2 + B195_BLOCK_ITEM_SLOTS];
    uintptr_t fallback[6];           /* terrain: atlas fallback sprite seen at bind */
    /* API 1.7 */
    uint8_t variants;                /* 0 = no state property; 2..16 */
    uint8_t shape, no_collide, transparent;
    /* API 1.11 multi-box (LCE_SHAPE_BOXES): sets = 1 or one per variant; box = the union of set 0 (outline, +0x178) */
    uint8_t box_count, box_sets, solid_mask;
    uint8_t boxes[B195_MAX_VARIANTS][B195_MAX_BOXES][6];
    uintptr_t box_aabb[B195_MAX_VARIANTS][B195_MAX_BOXES];   /* AABB* from 0x170 */
    uintptr_t union_aabb[B195_MAX_VARIANTS];
    struct { uintptr_t data; uint32_t count, pad; } box_list[B195_MAX_VARIANTS];   /* what +0x190 returns */
    uint8_t box[6];                  /* 1/16 units */
    uintptr_t aabb;                  /* LCE_SHAPE_BOX: AABB* from 0x170 */
    uint16_t vtile[B195_MAX_VARIANTS][6];          /* variant >= 1: module tile per face */
    uint8_t vname_len[B195_MAX_VARIANTS];
    uint16_t vname[B195_MAX_VARIANTS][48];        /* variant >= 1 display names (0 len = name) */
    /* a block whose construction failed is dropped alone (never registered; object 0, its half-built object leaks) */
    uint8_t dropped;
    char drop_reason[96];
} b195_block;

typedef struct b195_tile { int32_t plan_texture; const uint8_t *rgba; } b195_tile;

typedef struct b195_module {
    uintptr_t base;
    b195_natives n;
    b195_memory mem;
    uint32_t state, blocks_state, items_state, count, tile_count;
    uint32_t dropped;                /* blocks dropped at construction (published = count - dropped) */
    uint32_t phase;                  /* 1 inside the relay's block phase */
    uint64_t phase_thread;
    uintptr_t registry;
    b195_block blocks[B195_MAX_BLOCKS];
    b195_tile tiles[B195_MAX_TILES];
    uintptr_t stock_slots[B195_BLOCK_ID_MAX + 1];
    uintptr_t stock_items[B195_ITEM_VECTOR_CAP];
    uintptr_t item_header[3];
    /* data hook */
    uint32_t hook_state;
    uintptr_t hook_registry, hook_stock_vtable, hook_original_register;
    /* W1b (console 2026-11-19: the registry global 0x140ad68 is still null at plugin_load and at the
     * later retry, i.e. plugins load before the eboot's static constructors): the hook then patches the
     * register slot +0x28 of the registry CLASS vtable 0x1218840 (RELRO data, outside segment 0) instead
     * of the object vptr, and puts it back when id 365 registers. */
    uint32_t hook_mode;                       /* B195_HOOK_MODE_* */
    int (*patch_ro8)(void *ctx, uintptr_t at, uint64_t expected, uint64_t desired);
    void *patch_ro8_ctx;
    uintptr_t reg_vtable[2 + B195_REGISTRY_SLOTS];
    int hook_publish_result;
    /* terrain */
    uint32_t terrain_state, generations, generation;
    uintptr_t atlas, texture;
    uintptr_t sprites[B195_TERRAIN_GENERATIONS][B195_MAX_TILES];
    char last_error[160];
} b195_module;

/* Mirrors lce195_mapped_item (r72 additive_mapped_window_195/window.h) field for
 * field, so the lead can feed lce195_mapped_window_enter / Creative. */
typedef struct b195_mapped_item {
    const char *provider, *key, *display_name, *icon;
    uint32_t block_id, item_id;
    uintptr_t block, item;
    uintptr_t name_vtable, name_dispatch;
} b195_mapped_item;

#define B195_PROVIDER "lce-sdk-blocks"

/* Content this backend can represent faithfully. 1 ok, 0 unsupported (err says why). */
int b195_check_desc(const lce_block_desc *d, char *err, uint32_t err_cap);
/* Same check shaped as lce_backend.check_block (loader/backend.h): LCE_OK or LCE_E_UNSUPPORTED. */
lce_status b195_backend_check_block(void *self, const lce_block_desc *d, char *err, uint32_t err_cap);
/* Validate and stage a plan. No game memory access. 1 ok, 0 refused. Only one
 * module may be configured per process (dispatchers find it globally). */
int b195_configure(b195_module *m, uintptr_t base, const b195_natives *n, const b195_memory *mem,
                   const lce_plan_block *blocks, uint32_t count, const lce_plan_texture *textures, uint32_t texture_count,
                   const char *const *display_names /* per block, NULL = desc->name */);
/* Data hook (W1). 1 armed, 0 refused (registry not constructed yet -> retry later;
 * already past id 365; foreign vptr), -1 uncertain. */
int b195_hook_install(b195_module *m);
/* Optional (W1b): a CAS on a READ-ONLY data page (sceKernelMprotect RW, CAS, restore; 1 swapped, 0 mismatch,
 * -1 protection not restored), as lce_ps4_mem_patch_ro8 / biomes195. With it, b195_hook_install arms even while
 * the registry global is still null, by patching the registry vtable's register slot. Call after b195_configure. */
void b195_set_patch_ro8(b195_module *m, int (*patch_ro8)(void *ctx, uintptr_t at, uint64_t expected, uint64_t desired), void *ctx);
/* Restore the stock registry vptr if the relay never fired (e.g. missed). 1 restored/nothing to do, 0 foreign, -1 uncertain. */
int b195_hook_disarm(b195_module *m);
/* Relay installed at private registry vtable +0x28 (game ABI). */
void b195_register_relay(uintptr_t reg, uint32_t id, const void *key, uintptr_t block);
/* Block phase entry for a caller that owns a .text window (blocks_held()==1). */
int b195_blocks_publish(b195_module *m);
/* Items phase (items_held()==1). */
int b195_items_publish(b195_module *m);

/* Terrain. Unique tiles in face order of first use; the lead appends them to the
 * grown terrain atlas at cells first_index.. (row-major, 16 per 256 px row). */
uint32_t b195_terrain_tile_count(const b195_module *m);
/* Nearest-neighbour scaled RGBA for all tiles at (tile_size >> mip). */
int b195_terrain_compose(const b195_module *m, uint32_t tile_size, unsigned mip, uint8_t *out, size_t out_bytes);
int b195_terrain_bind(b195_module *m, uintptr_t atlas, uint32_t generation, uint32_t width, uint32_t height,
                      uint32_t stock_height, uint32_t tile_size, uint32_t first_index);
/* Bound sprites are restored only while the caller holds the terrain reload lease. */
int b195_terrain_detach(b195_module *m);

/* Descriptors for the lead's mapped window / Creative (after items phase). */
uint32_t b195_mapped_items(const b195_module *m, b195_mapped_item *out, uint32_t cap);
struct lce_sdk_creative_block_entry;
int b195_creative_entries(b195_module *m, struct lce_sdk_creative_block_entry *out, uint32_t cap);
const char *b195_creative_key(b195_module *m, uintptr_t item, uint32_t item_id);
uint32_t b195_status(const b195_module *m);

/* Private-vtable dispatchers (game ABI). */
void *b195_block_name_dispatch(void *out, void *block);
uintptr_t b195_get_resource_dispatch(void *block, uintptr_t state, uintptr_t random, int fortune);
int b195_resource_count_dispatch(void *block, int fortune, uintptr_t random);
void *b195_item_name_dispatch(void *out, void *item, const void *shared_stack);
uintptr_t b195_state_from_data_dispatch(void *block, int data);                       /* +0x48, variants */
uintptr_t b195_face_sprite_dispatch(void *block, uintptr_t face, uintptr_t state);   /* +0x458, variants */
uintptr_t b195_shape_dispatch(void *block, uintptr_t state, uintptr_t level, uintptr_t pos); /* +0x178, box */
int b195_face_shape_dispatch(void *block, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t face); /* +0x1a0 */
uintptr_t b195_box_list_dispatch(void *block, uintptr_t state, uintptr_t level, uintptr_t pos);       /* +0x190, API 1.11 boxes */
void *b195_new_block_entity_dispatch(void *out, void *entity_block, void *level, int data);   /* EntityBlock +0x10, storage */
double b195_be_view_distance(void *be);                                                      /* ChestBlockEntity +0x70 */

/* One-shot .text fallback for the 0xb74de call (only if the data hook cannot be
 * used). plan(): checks the 16 pristine bytes at 0xb74d8 and selects a relay
 * address in [region, region+size) whose rel32 low byte equals the original
 * (0x1d), so only the aligned word at 0xb74e0 changes. restore(): puts the
 * original word back after the relay fired, so the RX hash is stock again. */
typedef struct b195_text_patch { uintptr_t word, relay, original_target; uint64_t before, after; } b195_text_patch;
int b195_text_plan(uintptr_t base, const uint8_t window[16], uintptr_t region, size_t size, b195_text_patch *out);
typedef struct b195_text_ops {
    void *ctx;
    int (*protect)(void *, uintptr_t page, size_t len, int prot); /* 0 ok */
    int (*cas8)(void *, uintptr_t addr, uint64_t expected, uint64_t desired);
    int (*read8)(void *, uintptr_t addr, uint64_t *out);
} b195_text_ops;
int b195_text_restore(const b195_text_patch *p, const b195_text_ops *ops);

void b195_natives_bind(b195_natives *n, uintptr_t base);  /* console only */
#ifdef B195_HOST_TEST
void b195_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif
