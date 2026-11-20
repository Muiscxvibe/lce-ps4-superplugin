"""Name mappings applied to bytecode before analysis.

Release jars of Forge 1.7.10-1.12 refer to Minecraft members by SRG names
(func_77655_b, field_151045_i).  Without MCP mappings those cannot be named;
this module carries a small BUILT-IN table of the SRG names the porter needs
(registry builder methods, behaviour overrides, Items/Blocks/Material/Potion
fields).  It is hand-compiled and NOT verified against MCP data on this machine:
every vanilla reference resolved through it is reported as "guessed (built-in
SRG table)" in PORT_REPORT.md.  Supplying real mappings (--mappings fields.csv
methods.csv, or a Tiny v2 file) overrides it.

Fabric jars use intermediary names (net/minecraft/class_1792, method_7889);
a Tiny v2 file with an intermediary and a named column maps them (the porter
looks for known read-only local copies).
"""
import csv
import io
import re
from pathlib import Path

# ---- built-in SRG -> MCP (methods) ------------------------------------------
SRG_METHODS = {
    # Item
    'func_77655_b': 'setUnlocalizedName', 'func_111206_d': 'setTextureName', 'func_77637_a': 'setCreativeTab',
    'func_77625_d': 'setMaxStackSize', 'func_77656_e': 'setMaxDamage', 'func_77664_n': 'setFull3D', 'func_77627_a': 'setHasSubtypes',
    'func_77642_a': 'setContainerItem', 'func_77659_a': 'onItemRightClick', 'func_77648_a': 'onItemUse', 'func_180614_a': 'onItemUse',
    'func_77644_a': 'hitEntity', 'func_77663_a': 'onUpdate', 'func_77654_b': 'onItemUseFinish', 'func_77626_a': 'getMaxItemUseDuration',
    'func_77661_b': 'getItemUseAction', 'func_77615_a': 'onPlayerStoppedUsing', 'func_150894_a': 'onBlockDestroyed',
    'func_179218_a': 'onBlockDestroyed', 'func_111207_a': 'itemInteractionForEntity', 'func_77622_d': 'onCreated',
    'func_77636_d': 'hasEffect', 'func_77624_a': 'addInformation', 'func_77653_i': 'getItemStackDisplayName',
    'func_77650_f': 'getIconFromDamage', 'func_94581_a': 'registerIcons', 'func_77619_b': 'getItemEnchantability',
    'func_77613_e': 'getRarity', 'func_150898_a': 'getItemFromBlock', 'func_150899_d': 'getItemById', 'func_77667_c': 'getUnlocalizedName',
    'func_77658_a': 'getUnlocalizedName', 'func_77640_w': 'getCreativeTab', 'func_77666_t': 'getSubItems', 'func_150895_a': 'getSubItems',
    'func_82789_a': 'getIsRepairable', 'func_77617_a': 'getIconFromDamage',
    # ItemFood
    'func_77844_a': 'setPotionEffect', 'func_77848_i': 'setAlwaysEdible', 'func_77849_c': 'onFoodEaten',
    'func_150905_g': 'getHealAmount', 'func_150906_h': 'getSaturationModifier', 'func_77845_h': 'isWolfsFavoriteMeat',
    # Block
    'func_149711_c': 'setHardness', 'func_149752_b': 'setResistance', 'func_149715_a': 'setLightLevel', 'func_149672_a': 'setStepSound',
    'func_149663_c': 'setBlockName', 'func_149658_d': 'setBlockTextureName', 'func_149647_a': 'setCreativeTab',
    'func_149713_g': 'setLightOpacity', 'func_149722_s': 'setBlockUnbreakable', 'func_149675_a': 'setTickRandomly',
    'func_149727_a': 'onBlockActivated', 'func_180639_a': 'onBlockActivated', 'func_149724_b': 'onEntityWalking',
    'func_176199_a': 'onEntityWalk', 'func_149670_a': 'onEntityCollidedWithBlock', 'func_180634_a': 'onEntityCollision',
    'func_149674_a': 'updateTick', 'func_180650_b': 'updateTick', 'func_149664_b': 'onBlockDestroyedByPlayer',
    'func_176206_d': 'onPlayerDestroy', 'func_149689_a': 'onBlockPlacedBy', 'func_180633_a': 'onBlockPlacedBy',
    'func_149734_b': 'randomDisplayTick', 'func_180655_c': 'randomDisplayTick', 'func_149726_b': 'onBlockAdded',
    'func_176213_c': 'onBlockAdded', 'func_149749_a': 'breakBlock', 'func_180663_b': 'breakBlock', 'func_149650_a': 'getItemDropped',
    'func_180660_a': 'getItemDropped', 'func_149745_a': 'quantityDropped', 'func_149679_a': 'quantityDroppedWithBonus',
    'func_149692_a': 'damageDropped', 'func_180651_a': 'damageDropped', 'func_149662_c': 'isOpaqueCube', 'func_149686_d': 'renderAsNormalBlock',
    'func_149645_b': 'getRenderType', 'func_149691_a': 'getIcon', 'func_149651_a': 'registerBlockIcons', 'func_149699_a': 'onBlockClicked',
    'func_180649_a': 'onBlockClicked', 'func_149695_a': 'onNeighborBlockChange', 'func_149746_a': 'onFallenUpon',
    'func_149634_a': 'getBlockFromItem', 'func_149682_b': 'getIdFromBlock', 'func_149739_a': 'getUnlocalizedName',
    'func_149915_a': 'createNewTileEntity', 'func_149688_o': 'getMaterial', 'func_176223_P': 'getDefaultState',
    # Entity
    # EntityThrowable (API 1.9 throwables: impact handler and throw heading)
    'func_70184_a': 'onImpact', 'func_70186_c': 'setThrowableHeading', 'func_184538_a': 'setHeadingFromThrower',
    'func_70071_h_': 'onUpdate', 'func_70636_d': 'onLivingUpdate', 'func_70652_k': 'attackEntityAsMob', 'func_70085_c': 'interact',
    'func_184645_a': 'processInteract', 'func_70628_a': 'dropFewItems', 'func_146068_u': 'getDropItem', 'func_70645_a': 'onDeath',
    'func_70097_a': 'attackEntityFrom', 'func_110147_ax': 'applyEntityAttributes', 'func_70639_aQ': 'getLivingSound',
    'func_70621_aR': 'getHurtSound', 'func_70673_aS': 'getDeathSound', 'func_70650_aV': 'isAIEnabled', 'func_70088_a': 'entityInit',
    'func_70014_b': 'writeEntityToNBT', 'func_70037_a': 'readEntityFromNBT', 'func_110148_a': 'getEntityAttribute',
    'func_111128_a': 'setBaseValue', 'func_70100_b_': 'onCollideWithPlayer', 'func_82196_d': 'attackEntityWithRangedAttack',
    'func_70690_d': 'addPotionEffect', 'func_70691_i': 'heal', 'func_70015_d': 'setFire', 'func_145779_a': 'dropItem',
    'func_70099_a': 'entityDropItem', 'func_145778_a': 'dropItemWithOffset', 'func_70692_ba': 'canDespawn', 'func_70601_bi': 'getCanSpawnHere',
    'func_70634_a': 'setPositionAndUpdate', 'func_70106_y': 'setDead', 'func_70606_j': 'setHealth', 'func_110143_aJ': 'getHealth',
    # player / chat / world
    'func_146105_b': 'addChatComponentMessage', 'func_145747_a': 'addChatMessage', 'func_71064_a': 'addStat',
    'func_72876_a': 'createExplosion', 'func_72885_a': 'newExplosion', 'func_72838_d': 'spawnEntityInWorld', 'func_147449_b': 'setBlock',
    'func_147465_d': 'setBlock', 'func_175656_a': 'setBlockState', 'func_147468_f': 'setBlockToAir', 'func_72942_c': 'addWeatherEffect',
    'func_72956_a': 'playSoundAtEntity', 'func_72908_a': 'playSoundEffect', 'func_147439_a': 'getBlock', 'func_180495_p': 'getBlockState',
    'func_77972_a': 'damageItem', 'func_77973_b': 'getItem', 'func_77960_j': 'getMetadata', 'func_70093_af': 'isSneaking',
    'func_70694_bm': 'getHeldItem', 'func_184586_b': 'getHeldItem', 'func_71045_bC': 'getCurrentEquippedItem',
    'func_72869_a': 'spawnParticle', 'func_175688_a': 'spawnParticle', 'func_71029_a': 'triggerAchievement',
    # potion / enchantment
    'func_76390_b': 'setPotionName', 'func_76399_b': 'setIconIndex', 'func_76394_a': 'performEffect', 'func_76397_a': 'isReady',
    'func_76396_c': 'getId', 'func_76403_b': 'isInstant', 'func_76398_a': 'isBadEffect', 'func_76401_j': 'getLiquidColor',
    'func_77325_b': 'getMaxLevel', 'func_77319_d': 'getMinLevel', 'func_77321_a': 'getMinEnchantability', 'func_77317_b': 'getMaxEnchantability',
    'func_77322_b': 'setName',
    # worldgen / recipes
    'func_76484_a': 'generate', 'func_180709_b': 'generate', 'func_77602_a': 'instance', 'func_151396_a': 'addSmelting',
    'func_151393_a': 'addSmeltingRecipeForBlock', 'func_151394_a': 'addSmeltingRecipe', 'func_92103_a': 'addRecipe',
    'func_92102_a': 'addRecipe', 'func_77594_a': 'getInstance', 'func_94245_a': 'registerIcon', 'func_75776_a': 'addTask',
    'func_150896_a': 'func_150896_a', 'func_111150_b': 'registerAttribute',
}

# ---- built-in SRG -> MCP (fields) --------------------------------------------
# Item fields (MCP 1.7.10 names; 1.12 uses the same SRG with upper-case MCP names).
_ITEMS = """field_151037_a iron_shovel field_151035_b iron_pickaxe field_151036_c iron_axe field_151033_d flint_and_steel
field_151034_e apple field_151031_f bow field_151032_g arrow field_151044_h coal field_151045_i diamond field_151042_j iron_ingot
field_151043_k gold_ingot field_151040_l iron_sword field_151041_m wooden_sword field_151038_n wooden_shovel field_151039_o wooden_pickaxe
field_151053_p wooden_axe field_151052_q stone_sword field_151051_r stone_shovel field_151050_s stone_pickaxe field_151049_t stone_axe
field_151048_u diamond_sword field_151047_v diamond_shovel field_151046_w diamond_pickaxe field_151056_x diamond_axe field_151055_y stick
field_151054_z bowl field_151009_A mushroom_stew field_151010_B golden_sword field_151011_C golden_shovel field_151005_D golden_pickaxe
field_151006_E golden_axe field_151007_F string field_151008_G feather field_151016_H gunpowder field_151017_I wooden_hoe
field_151018_J stone_hoe field_151019_K iron_hoe field_151012_L diamond_hoe field_151013_M golden_hoe field_151014_N wheat_seeds
field_151015_O wheat field_151025_P bread field_151024_Q leather_helmet field_151027_R leather_chestplate field_151026_S leather_leggings
field_151021_T leather_boots field_151020_U chainmail_helmet field_151023_V chainmail_chestplate field_151022_W chainmail_leggings
field_151029_X chainmail_boots field_151028_Y iron_helmet field_151030_Z iron_chestplate field_151165_aa iron_leggings field_151167_ab iron_boots
field_151161_ac diamond_helmet field_151163_ad diamond_chestplate field_151173_ae diamond_leggings field_151175_af diamond_boots
field_151169_ag golden_helmet field_151171_ah golden_chestplate field_151149_ai golden_leggings field_151151_aj golden_boots field_151145_ak flint
field_151147_al porkchop field_151157_am cooked_porkchop field_151159_an painting field_151153_ao golden_apple field_151155_ap sign
field_151135_aq wooden_door field_151133_ar bucket field_151131_as water_bucket field_151129_at lava_bucket field_151143_au minecart
field_151141_av saddle field_151139_aw iron_door field_151137_ax redstone field_151126_ay snowball field_151124_az boat field_151116_aA leather
field_151117_aB milk_bucket field_151118_aC brick field_151119_aD clay_ball field_151120_aE reeds field_151121_aF paper field_151122_aG book
field_151123_aH slime_ball field_151108_aI chest_minecart field_151109_aJ furnace_minecart field_151110_aK egg field_151111_aL compass
field_151112_aM fishing_rod field_151113_aN clock field_151114_aO glowstone_dust field_151115_aP fish field_151101_aQ cooked_fish
field_151100_aR dye field_151103_aS bone field_151102_aT sugar field_151105_aU cake field_151104_aV bed field_151107_aW repeater
field_151106_aX cookie field_151098_aY filled_map field_151097_aZ shears field_151127_ba melon field_151080_bb pumpkin_seeds
field_151081_bc melon_seeds field_151082_bd beef field_151083_be cooked_beef field_151076_bf chicken field_151077_bg cooked_chicken
field_151078_bh rotten_flesh field_151079_bi ender_pearl field_151072_bj blaze_rod field_151073_bk ghast_tear field_151074_bl gold_nugget
field_151075_bm nether_wart field_151068_bn potion field_151069_bo glass_bottle field_151070_bp spider_eye field_151071_bq fermented_spider_eye
field_151065_br blaze_powder field_151064_bs magma_cream field_151067_bt brewing_stand field_151066_bu cauldron field_151061_bv ender_eye
field_151060_bw speckled_melon field_151063_bx spawn_egg field_151062_by experience_bottle field_151059_bz fire_charge
field_151099_bA writable_book field_151164_bB written_book field_151166_bC emerald field_151160_bD item_frame field_151162_bE flower_pot
field_151172_bF carrot field_151174_bG potato field_151168_bH baked_potato field_151170_bI poisonous_potato field_151148_bJ map
field_151150_bK golden_carrot field_151144_bL skull field_151146_bM carrot_on_a_stick field_151156_bN nether_star field_151158_bO pumpkin_pie
field_151152_bP fireworks field_151154_bQ firework_charge field_151134_bR enchanted_book field_151132_bS comparator field_151130_bT netherbrick
field_151128_bU quartz field_151142_bV tnt_minecart field_151140_bW hopper_minecart field_151138_bX iron_horse_armor
field_151136_bY golden_horse_armor field_151125_bZ diamond_horse_armor field_151058_ca lead field_151057_cb name_tag
field_151095_cc command_block_minecart field_151096_cd record_13"""
_BLOCKS = """field_150350_a air field_150348_b stone field_150349_c grass field_150346_d dirt field_150347_e cobblestone field_150344_f planks
field_150345_g sapling field_150357_h bedrock field_150358_i flowing_water field_150355_j water field_150356_k flowing_lava field_150353_l lava
field_150354_m sand field_150351_n gravel field_150352_o gold_ore field_150366_p iron_ore field_150365_q coal_ore field_150364_r log
field_150363_s log2 field_150362_t leaves field_150361_u leaves2 field_150360_v sponge field_150359_w glass field_150369_x lapis_ore
field_150368_y lapis_block field_150367_z dispenser field_150322_A sandstone field_150323_B noteblock field_150324_C bed
field_150318_D golden_rail field_150319_E detector_rail field_150320_F sticky_piston field_150321_G web field_150329_H tallgrass
field_150330_I deadbush field_150331_J piston field_150332_K piston_head field_150325_L wool field_150326_M piston_extension
field_150327_N yellow_flower field_150328_O red_flower field_150338_P brown_mushroom field_150337_Q red_mushroom field_150340_R gold_block
field_150339_S iron_block field_150334_T double_stone_slab field_150333_U stone_slab field_150336_V brick_block field_150335_W tnt
field_150342_X bookshelf field_150341_Y mossy_cobblestone field_150343_Z obsidian field_150478_aa torch field_150480_ab fire
field_150474_ac mob_spawner field_150476_ad oak_stairs field_150486_ae chest field_150488_af redstone_wire field_150482_ag diamond_ore
field_150484_ah diamond_block field_150462_ai crafting_table field_150464_aj wheat field_150458_ak farmland field_150460_al furnace
field_150470_am lit_furnace field_150472_an standing_sign field_150466_ao wooden_door field_150468_ap ladder field_150448_aq rail
field_150446_ar stone_stairs field_150444_as wall_sign field_150442_at lever field_150456_au stone_pressure_plate field_150454_av iron_door
field_150452_aw wooden_pressure_plate field_150450_ax redstone_ore field_150439_ay lit_redstone_ore field_150437_az unlit_redstone_torch
field_150429_aA redstone_torch field_150430_aB stone_button field_150431_aC snow_layer field_150432_aD ice field_150433_aE snow
field_150434_aF cactus field_150435_aG clay field_150436_aH reeds field_150421_aI jukebox field_150422_aJ fence field_150423_aK pumpkin
field_150424_aL netherrack field_150425_aM soul_sand field_150426_aN glowstone field_150427_aO portal field_150428_aP lit_pumpkin
field_150414_aQ cake field_150413_aR unpowered_repeater field_150416_aS powered_repeater field_150399_cn stained_glass
field_150415_aT trapdoor field_150418_aU monster_egg field_150417_aV stonebrick field_150420_aW brown_mushroom_block
field_150419_aX red_mushroom_block field_150411_aY iron_bars field_150410_aZ glass_pane field_150440_ba melon_block field_150393_bb pumpkin_stem
field_150394_bc melon_stem field_150395_bd vine field_150396_be fence_gate field_150389_bf brick_stairs field_150390_bg stone_brick_stairs
field_150391_bh mycelium field_150392_bi waterlily field_150385_bj nether_brick field_150386_bk nether_brick_fence
field_150387_bl nether_brick_stairs field_150388_bm nether_wart field_150381_bn enchanting_table field_150382_bo brewing_stand
field_150383_bp cauldron field_150384_bq end_portal field_150378_br end_portal_frame field_150377_bs end_stone field_150380_bt dragon_egg
field_150379_bu redstone_lamp field_150374_bv lit_redstone_lamp field_150373_bw double_wooden_slab field_150376_bx wooden_slab field_150375_by cocoa
field_150372_bz sandstone_stairs field_150412_bA emerald_ore field_150477_bB ender_chest field_150479_bC tripwire_hook field_150473_bD tripwire
field_150475_bE emerald_block field_150485_bF spruce_stairs field_150487_bG birch_stairs field_150481_bH jungle_stairs field_150483_bI command_block
field_150461_bJ beacon field_150463_bK cobblestone_wall field_150457_bL flower_pot field_150459_bM carrots field_150469_bN potatoes
field_150471_bO wooden_button field_150465_bP skull field_150467_bQ anvil field_150447_bR trapped_chest field_150445_bS light_weighted_pressure_plate
field_150443_bT heavy_weighted_pressure_plate field_150441_bU unpowered_comparator field_150455_bV powered_comparator field_150453_bW daylight_detector
field_150451_bX redstone_block field_150449_bY quartz_ore field_150438_bZ hopper field_150371_ca quartz_block field_150370_cb quartz_stairs
field_150408_cc activator_rail field_150409_cd dropper field_150406_ce stained_hardened_clay field_150397_co stained_glass_pane
field_150407_cf hay_block field_150404_cg carpet field_150405_ch hardened_clay field_150402_ci coal_block field_150403_cj packed_ice
field_150398_cm double_plant field_150401_cB dark_oak_stairs field_150400_ck acacia_stairs"""
_MATERIALS = """field_151579_a air field_151577_b grass field_151578_c ground field_151575_d wood field_151576_e rock field_151573_f iron
field_151574_g anvil field_151586_h water field_151587_i lava field_151584_j leaves field_151585_k plants field_151582_l vine
field_151583_m sponge field_151580_n cloth field_151581_o fire field_151595_p sand field_151594_q circuits field_151593_r carpet
field_151592_s glass field_151591_t redstoneLight field_151590_u tnt field_151589_v coral field_151588_w ice field_151598_x packedIce
field_151597_y snow field_151596_z craftedSnow field_151570_A cactus field_151571_B clay field_151572_C gourd field_151566_D dragonEgg
field_151567_E portal field_151568_F cake field_151569_G web"""
_SOUNDS = """field_149769_e soundTypeStone field_149766_f soundTypeWood field_149767_g soundTypeGravel field_149779_h soundTypeGrass
field_149780_i soundTypePiston field_149777_j soundTypeMetal field_149778_k soundTypeGlass field_149775_l soundTypeCloth
field_149776_m soundTypeSand field_149773_n soundTypeSnow field_149774_o soundTypeLadder field_149788_p soundTypeAnvil"""
_POTIONS = """field_76424_c moveSpeed field_76421_d moveSlowdown field_76422_e digSpeed field_76419_f digSlowdown field_76420_g damageBoost
field_76432_h heal field_76433_i harm field_76430_j jump field_76431_k confusion field_76428_l regeneration field_76429_m resistance
field_76426_n fireResistance field_76427_o waterBreathing field_76441_p invisibility field_76440_q blindness field_76439_r nightVision
field_76438_s hunger field_76437_t weakness field_76436_u poison field_82731_v wither field_76434_w healthBoost field_180152_w health_boost
field_76444_x absorption field_76443_y saturation field_76415_H id"""
_ATTRS = """field_111267_a maxHealth field_111265_b followRange field_111266_c knockbackResistance field_111263_d movementSpeed
field_111264_e attackDamage"""
_TABS = """field_78030_b tabBlock field_78031_c tabDecorations field_78028_d tabRedstone field_78029_e tabTransport field_78026_f tabMisc
field_78027_g tabAllSearch field_78039_h tabFood field_78040_i tabTools field_78037_j tabCombat field_78038_k tabBrewing
field_78035_l tabMaterials field_72995_K isRemote field_77994_a stackSize field_70170_p worldObj field_70165_t posX field_70163_u posY
field_70161_v posZ field_70714_bg tasks field_70715_bh targetTasks field_71071_by inventory field_77777_bU maxStackSize
field_77787_bX hasSubtypes field_77791_bV itemIcon field_149761_L blockIcon field_149768_d blockTextureName field_111218_cA iconString
field_149782_v blockHardness field_149781_w blockResistance field_149784_t lightValue field_149762_H stepSound"""


def _pairs(text):
    t = text.split()
    return dict(zip(t[0::2], t[1::2]))


SRG_FIELDS = {}
for _t in (_ITEMS, _BLOCKS, _MATERIALS, _SOUNDS, _POTIONS, _ATTRS, _TABS):
    SRG_FIELDS.update(_pairs(_t))

SRG_NAME_RE = re.compile(r'^(func|field)_\d+_[A-Za-z_]+$')
MOJ_SRG_RE = re.compile(r'^[mf]_\d+_$')
INTERMEDIARY_RE = re.compile(r'^(method|field|class)_\d+$')


class Mapper:
    """Rewrites class/member names. Unknown names are kept (and counted)."""

    def __init__(self, classes=None, methods=None, fields=None, source='built-in SRG subset (unverified)'):
        self.classes = classes or {}
        self.methods = methods or {}
        self.fields = fields or {}
        self.source = source
        self.used = {}        # srg -> mcp actually applied (for the report)
        self.unmapped = {}    # srg-looking name -> count
        self._desc_cache = {}
        self.verified = False   # True for official MCP / Tiny data; False for the built-in hand table

    def cls(self, name):
        if name is None:
            return None
        if name.startswith('['):
            return self.desc(name)
        return self.classes.get(name, name)

    def desc(self, d):
        if not self.classes or 'L' not in d:
            return d
        r = self._desc_cache.get(d)
        if r is None:
            r = re.sub(r'L([^;]+);', lambda m: 'L' + self.classes.get(m.group(1), m.group(1)) + ';', d)
            self._desc_cache[d] = r
        return r

    def member(self, name, is_method):
        table = self.methods if is_method else self.fields
        v = table.get(name)
        if v is not None:
            self.used[name] = v
            return v
        if SRG_NAME_RE.match(name) or MOJ_SRG_RE.match(name) or INTERMEDIARY_RE.match(name):
            self.unmapped[name] = self.unmapped.get(name, 0) + 1
        return name

    def apply(self, cf):
        """Remap a parsed ClassFile in place (names in members, descriptors and decoded instructions)."""
        cf.name = self.cls(cf.name)
        cf.super_name = self.cls(cf.super_name)
        cf.interfaces = [self.cls(i) for i in cf.interfaces]
        for f in cf.fields:
            f.name = self.member(f.name, False)
            f.desc = self.desc(f.desc)
        for m in cf.methods:
            m.name = self.member(m.name, True)
            m.desc = self.desc(m.desc)
            if m.code is None:
                continue
            for ins in m.code.insns(cf.cp):
                a = ins.arg
                if ins.op in ('getstatic', 'putstatic', 'getfield', 'putfield'):
                    ins.arg = (self.cls(a[0]), self.member(a[1], False), self.desc(a[2]))
                elif ins.op.startswith('invoke') and ins.op != 'invokedynamic':
                    ins.arg = (self.cls(a[0]), self.member(a[1], True), self.desc(a[2]))
                elif ins.op == 'invokedynamic':
                    ins.arg = (a[0], self.desc(a[1]))
                elif ins.op in ('new', 'anewarray', 'checkcast', 'instanceof', 'multianewarray') and isinstance(a, str):
                    ins.arg = self.cls(a)
                elif ins.op in ('ldc', 'ldc_w') and a[0] == 'class':
                    ins.arg = ('class', self.cls(a[1]))
        # bootstrap method handles are resolved lazily through cp; wrap the lookup
        orig = cf.bootstrap_info
        mapper = self

        def bootstrap_info(idx, _orig=orig):
            (kind, owner, name, desc), args = _orig(idx)
            out = []
            for k, v in args:
                if k == 'mhandle':
                    v = (v[0], mapper.cls(v[1]), mapper.member(v[2], v[0] >= 5), mapper.desc(v[3]))
                elif k == 'mtype':
                    v = mapper.desc(v)
                out.append((k, v))
            return (kind, mapper.cls(owner), name, desc), out
        cf.bootstrap_info = bootstrap_info


def builtin_srg():
    return Mapper(methods=dict(SRG_METHODS), fields=dict(SRG_FIELDS))


# ---- official MCP mappings (auto-detected per Minecraft version) ----------------------------------------
# The porter never downloads anything. It looks for the official MCP archives (mcp_stable-<n>-<mc>.zip /
# mcp_snapshot-<date>-<mc>.zip from the Forge maven, de/oceanlabs/mcp/...) in these directories:
def mcp_dirs():
    import os
    sdk = Path(__file__).resolve().parent.parent.parent
    out = [Path(d) for d in os.environ.get('LCE_MCP_DIR', '').split(os.pathsep) if d]
    out += [sdk / 'build' / 'forge100' / 'mappings', sdk / 'build' / 'mappings', sdk / 'tools' / 'data' / 'mcp',
            Path.home() / '.cache' / 'lce-autoport' / 'mcp']
    return out


_ARCHIVE_RE = re.compile(r'^mcp_(stable|snapshot)-([0-9]+)-(1\.[0-9]+(?:\.[0-9]+)?)\.zip$')
# Only archives of the same minor version fill members missing from the primary one (1.14.4 <- 1.14.3 <- 1.14.2):
# SRG ids of removed members are re-used by later versions (func_70650_aV is isAIEnabled in 1.8.9 but
# ableToCauseSkullDrop in 1.12; func_111206_d is setTextureName in 1.7.10 but getByNameOrId in 1.8.9).
_GENERATIONS = [((1, n), (1, n)) for n in range(7, 17)]


def _mc(v):
    return tuple(int(x) for x in v.split('.'))


def find_mcp(mc_version, dirs=None):
    """-> [archive paths], best first: exact minor version (newest build), then neighbours of the same generation."""
    if not mc_version:
        return []
    m = re.match(r'^(\d+)\.(\d+)(?:\.(\d+))?', str(mc_version))
    if not m:
        return []
    want = tuple(int(x) for x in m.groups() if x is not None)
    found = []
    for d in dirs or mcp_dirs():
        try:
            entries = sorted(d.iterdir())
        except OSError:
            continue
        for f in entries:
            mm = _ARCHIVE_RE.match(f.name)
            if mm and f.is_file():
                found.append((_mc(mm.group(3)), mm.group(1), int(mm.group(2)), f))
    gen = next((g for g in _GENERATIONS if g[0] <= want[:2] <= g[1]), None)
    if gen is None:
        return []

    def rank(e):
        v, kind, build, _f = e
        exact = v == want or (v[:2] == want[:2] and len(want) == 2)
        return (0 if v[:2] == want[:2] else 1, 0 if exact else 1, abs(v[1] - want[1]), -(v[2] if len(v) > 2 else 0),
                0 if kind == 'stable' else 1, -build)
    cands = [e for e in found if gen[0] <= e[0][:2] <= gen[1]]
    seen, out = set(), []
    for e in sorted(cands, key=rank):
        if e[3].name not in seen:
            seen.add(e[3].name)
            out.append(e[3])
    return out


def _read_mcp_zip(path, max_bytes=32 << 20):
    import zipfile
    methods, fields = {}, {}
    with zipfile.ZipFile(path) as z:
        for name in ('fields.csv', 'methods.csv'):
            info = next((i for i in z.infolist() if i.filename.rsplit('/', 1)[-1] == name), None)
            if info is None or info.file_size > max_bytes:
                continue
            data = z.read(info)[:max_bytes].decode('utf-8', errors='replace')
            for row in csv.DictReader(io.StringIO(data)):
                sg, n = row.get('searge'), row.get('name')
                if sg and n and SRG_NAME_RE.match(sg):
                    (methods if sg.startswith('func_') else fields)[sg] = n
    return methods, fields


_MCP_CACHE = {}


def load_mcp_auto(mc_version, dirs=None):
    """Mapper from the official MCP archives for this Minecraft version (None when none is installed)."""
    files = find_mcp(mc_version, dirs)
    if not files:
        return None
    key = tuple(str(f) for f in files)
    if key not in _MCP_CACHE:
        methods, fields = {}, {}
        used = []
        for f in files:
            try:
                m2, f2 = _read_mcp_zip(f)
            except (OSError, ValueError, KeyError) as e:  # noqa: PERF203 - a damaged archive is skipped
                continue
            nm = sum(1 for k in m2 if k not in methods) + sum(1 for k in f2 if k not in fields)
            for k, v in m2.items():
                methods.setdefault(k, v)
            for k, v in f2.items():
                fields.setdefault(k, v)
            used.append(f'{f.stem}' + ('' if not used else f' (+{nm} ids)'))
        if not used:
            return None
        _MCP_CACHE[key] = (methods, fields, used)
    methods, fields, used = _MCP_CACHE[key]
    mp = Mapper(methods=dict(methods), fields=dict(fields), source=f'MCP {used[0]}' + (f'; neighbours {", ".join(used[1:])}' if len(used) > 1 else ''))
    mp.verified = True
    mp.archives = list(files)
    return mp


def load_mcp_csv(paths):
    """MCP fields.csv / methods.csv (searge,name,side,desc)."""
    methods, fields = {}, {}
    for p in paths:
        with open(p, newline='', encoding='utf-8') as fh:
            for row in csv.DictReader(fh):
                s, n = row.get('searge'), row.get('name')
                if s and n:
                    (methods if s.startswith('func_') else fields)[s] = n
    mp = Mapper(methods=methods, fields=fields, source=f'MCP CSV {", ".join(str(p) for p in paths)}')
    mp.verified = True
    return mp


def load_tiny(path, src_ns='intermediary', dst_ns='named', max_bytes=64 << 20):
    """Tiny v2 file (header 'tiny 2 0 <ns...>') -> Mapper from src_ns to dst_ns names."""
    p = Path(path)
    if p.stat().st_size > max_bytes:
        raise ValueError(f'{p}: too large')
    classes, methods, fields = {}, {}, {}
    with open(p, encoding='utf-8') as fh:
        header = fh.readline().rstrip('\n').split('\t')
        if header[:2] != ['tiny', '2']:
            raise ValueError(f'{p}: not a Tiny v2 file')
        ns = header[3:]
        if src_ns not in ns or dst_ns not in ns:
            raise ValueError(f'{p}: namespaces {ns} lack {src_ns}/{dst_ns}')
        si, di = ns.index(src_ns), ns.index(dst_ns)
        for line in fh:
            parts = line.rstrip('\n').split('\t')
            depth = len(parts) - len(line.lstrip('\t').split('\t'))
            body = parts[depth:]
            if depth == 0 and body and body[0] == 'c' and len(body) >= 1 + len(ns):
                a, b = body[1 + si], body[1 + di]
                if a and b:
                    classes[a] = b
            elif depth == 1 and body and body[0] in ('m', 'f') and len(body) >= 2 + len(ns):
                a, b = body[2 + si], body[2 + di]
                if a and b and a != b:
                    (methods if body[0] == 'm' else fields)[a] = b
    mp = Mapper(classes=classes, methods=methods, fields=fields, source=f'Tiny v2 {p} ({src_ns} -> {dst_ns})')
    mp.verified = True
    return mp


# Known read-only local mapping files (searched, never modified).
LOCAL_TINY_CANDIDATES = [
    '/home/codey/Desktop/mc mods/codeys barrotine mods/build two test/test/baritone-1.21.11/fabric/.gradle/unimined/local/mappings/mappings.tiny',
]


def find_local_tiny():
    for c in LOCAL_TINY_CANDIDATES:
        try:
            with open(c, encoding='utf-8') as fh:
                head = fh.readline()
            if head.startswith('tiny\t2') and 'intermediary' in head and 'named' in head:
                return c
        except OSError:
            continue
    return None


def merge(*mappers):
    out = Mapper(source=' + '.join(m.source for m in mappers if m))
    out.verified = all(m.verified for m in mappers if m)
    for m in mappers:
        if m:
            out.classes.update(m.classes)
            out.methods.update(m.methods)
            out.fields.update(m.fields)
    return out
