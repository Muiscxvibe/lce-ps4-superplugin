"""Vanilla knowledge: Java -> LCE 1.95 name translation, the content level of 1.95,
class roles across mapping sets (SRG/MCP 1.7-1.12, Mojang 1.17+), tool/armour
materials, potion IDs, entity base mobs and ore-dictionary names.

Every translation returns (lce_name_or_None, note) so callers can report it;
nothing is ever substituted silently.
"""
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
import lce_modpack  # noqa: E402
import lce_port  # noqa: E402

VANILLA = lce_modpack.VANILLA_ITEMS          # 1.95 name -> numeric id (546 names)
VANILLA_BLOCKS = lce_modpack.VANILLA_BLOCKS

COLORS = lce_port.COLORS
WOODS = lce_port.WOODS

# ---------------------------------------------------------------------------
# The Java -> 1.95 name tables live in tools/lce_port.py (shared with the data-only porter):
# lce_port.JAVA_TO_LCE (1.13+ names, including MODERN_EXTRA) and lce_port.OLD_TO_LCE (1.7.10-1.12 names).
EXTRA_JAVA_TO_LCE = lce_port.MODERN_EXTRA
OLD_TO_LCE = lce_port.OLD_TO_LCE

# Java content newer than the 1.95 level (about Java 1.14). Names here are
# reported "not in 1.95 (Java <ver>)" instead of "unknown".
_NEWER_EXACT = {
    # 1.14 items that the 1.95 table lacks
    'cornflower': '1.14', 'lily_of_the_valley': '1.14', 'wither_rose': '1.14', 'suspicious_stew': '1.14', 'jigsaw': '1.14',
    'black_dye': '1.14', 'brown_dye': '1.14', 'blue_dye': '1.14', 'white_dye': '1.14', 'globe_banner_pattern': '1.14',
    'fox_spawn_egg': '1.14', 'trader_llama_spawn_egg': '1.14',
    # 1.15
    'bee_nest': '1.15', 'beehive': '1.15', 'honey_bottle': '1.15', 'honeycomb': '1.15', 'honey_block': '1.15',
    'honeycomb_block': '1.15', 'bee_spawn_egg': '1.15',
    # 1.16
    'ancient_debris': '1.16', 'basalt': '1.16', 'polished_basalt': '1.16', 'smooth_basalt': '1.17', 'blackstone': '1.16',
    'gilded_blackstone': '1.16', 'chain': '1.16', 'crying_obsidian': '1.16', 'lodestone': '1.16', 'respawn_anchor': '1.16',
    'target': '1.16', 'shroomlight': '1.16', 'nether_gold_ore': '1.16', 'nether_sprouts': '1.16', 'twisting_vines': '1.16',
    'weeping_vines': '1.16', 'quartz_bricks': '1.16', 'cracked_nether_bricks': '1.16', 'chiseled_nether_bricks': '1.16',
    'piglin_banner_pattern': '1.16', 'music_disc_pigstep': '1.16', 'warped_fungus_on_a_stick': '1.16', 'soul_campfire': '1.16',
    'soul_lantern': '1.16', 'soul_torch': '1.16', 'soul_soil': '1.16',
    # 1.17-1.21
    'spyglass': '1.17', 'bundle': '1.17', 'tuff': '1.17', 'calcite': '1.17', 'glow_ink_sac': '1.17', 'glow_item_frame': '1.17',
    'glow_berries': '1.17', 'glow_lichen': '1.17', 'powder_snow_bucket': '1.17', 'axolotl_bucket': '1.17', 'pointed_dripstone': '1.17',
    'dripstone_block': '1.17', 'moss_block': '1.17', 'moss_carpet': '1.17', 'azalea': '1.17', 'flowering_azalea': '1.17',
    'rooted_dirt': '1.17', 'hanging_roots': '1.17', 'big_dripleaf': '1.17', 'small_dripleaf': '1.17', 'spore_blossom': '1.17',
    'lightning_rod': '1.17', 'tinted_glass': '1.17', 'goat_horn': '1.19', 'echo_shard': '1.19', 'recovery_compass': '1.19',
    'disc_fragment_5': '1.19', 'frogspawn': '1.19', 'ochre_froglight': '1.19', 'verdant_froglight': '1.19',
    'pearlescent_froglight': '1.19', 'mud': '1.19', 'packed_mud': '1.19', 'mud_bricks': '1.19', 'sniffer_egg': '1.20',
    'brush': '1.20', 'torchflower': '1.20', 'pitcher_plant': '1.20', 'decorated_pot': '1.20', 'suspicious_sand': '1.20',
    'suspicious_gravel': '1.20', 'calibrated_sculk_sensor': '1.20', 'mace': '1.21', 'wind_charge': '1.21', 'heavy_core': '1.21',
    'crafter': '1.21', 'trial_key': '1.21', 'ominous_trial_key': '1.21', 'vault': '1.21', 'trial_spawner': '1.21', 'breeze_rod': '1.21',
    'ominous_bottle': '1.21', 'armadillo_scute': '1.21', 'wolf_armor': '1.21', 'creaking_heart': '1.21.4', 'resin_clump': '1.21.4',
    'pale_moss_block': '1.21.4',
}
_NEWER_RE = [
    (r'^netherite_|_netherite$|^netherite$', '1.16'), (r'^(crimson|warped)_', '1.16'), (r'^(polished_)?blackstone', '1.16'),
    (r'^soul_', '1.16'), (r'^(piglin|hoglin|strider|zoglin|zombified_piglin)', '1.16'),
    (r'(^|_)copper(_|$)|^raw_(iron|gold|copper)', '1.17'), (r'amethyst', '1.17'), (r'deepslate', '1.17'), (r'candle', '1.17'),
    (r'^(glow_squid|axolotl|goat)', '1.17'), (r'sculk', '1.19'), (r'^mangrove_', '1.19'), (r'^(frog|tadpole|allay|warden)', '1.19'),
    (r'^cherry_', '1.20'), (r'^bamboo_(planks|block|mosaic|slab|stairs|fence|door|trapdoor|sign|button|pressure_plate|raft|chest_raft|hanging_sign)',
                              '1.20'),
    (r'_hanging_sign$', '1.20'), (r'_pottery_sherd$', '1.20'), (r'_armor_trim_smithing_template$|^netherite_upgrade_smithing_template$', '1.20'),
    (r'^(camel|sniffer)', '1.20'), (r'^(chiseled_|polished_)?tuff', '1.21'), (r'(^|_)(breeze|bogged|armadillo)', '1.21'),
    (r'^pale_', '1.21.4'), (r'^(resin|creaking)', '1.21.4'), (r'(^|_)honey', '1.15'), (r'(^|_)bee(_|$)', '1.15'),
    (r'^(fox|trader_llama)', '1.14'),
]


def newer_content(name):
    """Java name that is newer than 1.95's ~1.14 level -> the Java version it came with, else None."""
    if name in _NEWER_EXACT:
        return _NEWER_EXACT[name]
    for rx, ver in _NEWER_RE:
        if re.search(rx, name):
            return ver
    return None


def in_195(lce):
    return lce.split('@')[0] in VANILLA


class VanillaResolver:
    """Translate vanilla names to 1.95 names; every decision is reported by the caller.

    era: 'old' (1.7.10-1.12, pre-flattening names) or 'modern' (1.13+).
    fallbacks: {'minecraft:netherite_ingot': 'minecraft:diamond', ...} from hints (explicit, reported).
    """

    def __init__(self, era='modern', fallbacks=None, aliases=None):
        self.era = era
        self.fallbacks = {k.split(':', 1)[-1]: v for k, v in (fallbacks or {}).items()}
        self.aliases = aliases or {}

    def resolve(self, name, aux=None):
        """'minecraft:x' or 'x' -> (lce_key_or_None, status, note). status: same|mapped|fallback|newer|unknown."""
        full = name if ':' in name else 'minecraft:' + name
        if full in self.aliases:
            return self.aliases[full], 'mapped', f'{full} -> {self.aliases[full]} (hints alias)'
        n = full.split(':', 1)[1]
        cand = None
        if self.era == 'old':
            cand = OLD_TO_LCE.get(n) or (n if n in VANILLA else None)
        if cand is None:
            cand = lce_port.JAVA_TO_LCE.get(n)
            if cand is None and n in VANILLA:
                cand = n
        if cand is not None and in_195(cand):
            if aux not in (None, -1, 32767) and '@' not in cand:
                cand = f'{cand}@{aux}'
            status = 'same' if cand == n else 'mapped'
            return 'minecraft:' + cand, status, f'minecraft:{n} -> minecraft:{cand}' if status == 'mapped' else ''
        ver = newer_content(n)
        if n in self.fallbacks:
            fb = self.fallbacks[n]
            return fb, 'fallback', (f'minecraft:{n} is not in 1.95 (Java {ver} content); hints fallback {fb} used' if ver else
                                    f'minecraft:{n} has no 1.95 equivalent; hints fallback {fb} used')
        if ver:
            return None, 'newer', f'minecraft:{n} is not in 1.95 (Java {ver} content, newer than the 1.95 ~1.14 level)'
        return None, 'unknown', f'minecraft:{n} has no known 1.95 equivalent'


# ---------------------------------------------------------------------------
# Forge ore dictionary (1.7.10-1.12) and common tags -> 1.95 ingredients
OREDICT = {
    'ingotIron': 'minecraft:iron_ingot', 'ingotGold': 'minecraft:gold_ingot', 'gemDiamond': 'minecraft:diamond',
    'gemEmerald': 'minecraft:emerald', 'gemLapis': 'minecraft:dye@4', 'gemQuartz': 'minecraft:quartz', 'dustRedstone': 'minecraft:redstone',
    'dustGlowstone': 'minecraft:glowstone_dust', 'nuggetGold': 'minecraft:gold_nugget', 'nuggetIron': 'minecraft:iron_nugget',
    'stickWood': 'minecraft:stick', 'plankWood': 'minecraft:planks', 'logWood': 'minecraft:log', 'slabWood': 'minecraft:wooden_slab',
    'stairWood': 'minecraft:oak_stairs', 'treeSapling': 'minecraft:sapling', 'treeLeaves': 'minecraft:leaves', 'stone': 'minecraft:stone',
    'cobblestone': 'minecraft:cobblestone', 'sand': 'minecraft:sand', 'sandstone': 'minecraft:sandstone', 'gravel': 'minecraft:gravel',
    'netherrack': 'minecraft:netherrack', 'obsidian': 'minecraft:obsidian', 'glowstone': 'minecraft:glowstone', 'endstone': 'minecraft:end_stone',
    'blockIron': 'minecraft:iron_block', 'blockGold': 'minecraft:gold_block', 'blockDiamond': 'minecraft:diamond_block',
    'blockEmerald': 'minecraft:emerald_block', 'blockRedstone': 'minecraft:redstone_block', 'blockLapis': 'minecraft:lapis_block',
    'blockCoal': 'minecraft:coal_block', 'blockQuartz': 'minecraft:quartz_block', 'blockGlass': 'minecraft:glass', 'paneGlass': 'minecraft:glass_pane',
    'blockWool': 'minecraft:wool', 'oreIron': 'minecraft:iron_ore', 'oreGold': 'minecraft:gold_ore', 'oreDiamond': 'minecraft:diamond_ore',
    'oreEmerald': 'minecraft:emerald_ore', 'oreRedstone': 'minecraft:redstone_ore', 'oreLapis': 'minecraft:lapis_ore', 'oreCoal': 'minecraft:coal_ore',
    'oreQuartz': 'minecraft:quartz_ore', 'string': 'minecraft:string', 'feather': 'minecraft:feather', 'leather': 'minecraft:leather',
    'paper': 'minecraft:paper', 'book': 'minecraft:book', 'bone': 'minecraft:bone', 'slimeball': 'minecraft:slime_ball', 'gunpowder': 'minecraft:gunpowder',
    'enderpearl': 'minecraft:ender_pearl', 'cropWheat': 'minecraft:wheat', 'cropPotato': 'minecraft:potato', 'cropCarrot': 'minecraft:carrot',
    'cropNetherWart': 'minecraft:nether_wart', 'cropPumpkin': 'minecraft:carved_pumpkin', 'egg': 'minecraft:egg', 'listAllegg': 'minecraft:egg',
    'listAllmilk': 'minecraft:milk_bucket', 'listAllsugar': 'minecraft:sugar', 'listAllwater': 'minecraft:water_bucket', 'chest': 'minecraft:chest',
    'chestWood': 'minecraft:chest', 'workbench': 'minecraft:crafting_table', 'torch': 'minecraft:torch', 'record': 'minecraft:record_13',
    'dye': 'minecraft:dye', 'cropApple': 'minecraft:apple', 'foodBread': 'minecraft:bread', 'blockCactus': 'minecraft:cactus',
    'vine': 'minecraft:vine', 'sugarcane': 'minecraft:reeds', 'blockSlime': 'minecraft:slime', 'blockPrismarine': 'minecraft:prismarine',
    'gemPrismarine': 'minecraft:prismarine_shard', 'dustPrismarine': 'minecraft:prismarine_crystals', 'rodBlaze': 'minecraft:blaze_rod',
    'blazeRod': 'minecraft:blaze_rod', 'blazePowder': 'minecraft:blaze_powder', 'enderEye': 'minecraft:ender_eye', 'netherStar': 'minecraft:nether_star',
    'dustSugar': 'minecraft:sugar', 'ingotBrick': 'minecraft:brick', 'ingotBrickNether': 'minecraft:netherbrick', 'clay': 'minecraft:clay_ball',
    'blockClay': 'minecraft:clay', 'fish': 'minecraft:fish', 'foodSalt': None,
}
_DYE_ORDER = ['Black', 'Red', 'Green', 'Brown', 'Blue', 'Purple', 'Cyan', 'LightGray', 'Gray', 'Pink', 'Lime', 'Yellow', 'LightBlue',
              'Magenta', 'Orange', 'White']
for _i, _n in enumerate(_DYE_ORDER):
    OREDICT['dye' + _n] = f'minecraft:dye@{_i}'
    OREDICT['blockWool' + _n] = f'minecraft:wool@{15 - _i}'
    OREDICT['blockGlass' + _n] = f'minecraft:stained_glass@{15 - _i}'

MODERN_TAGS = dict(lce_port.JAVA_TAGS)
MODERN_TAGS.update({
    'minecraft:stone_crafting_materials': 'minecraft:cobblestone', 'minecraft:stone_tool_materials': 'minecraft:cobblestone',
    'c:cobblestones': 'minecraft:cobblestone', 'c:rods/wooden': 'minecraft:stick', 'c:ingots/iron': 'minecraft:iron_ingot',
    'c:ingots/gold': 'minecraft:gold_ingot', 'c:gems/diamond': 'minecraft:diamond', 'c:gems/emerald': 'minecraft:emerald',
    'c:gems/lapis': 'minecraft:dye@4', 'c:gems/quartz': 'minecraft:quartz', 'c:dusts/redstone': 'minecraft:redstone',
    'c:dusts/glowstone': 'minecraft:glowstone_dust', 'c:nuggets/gold': 'minecraft:gold_nugget', 'c:nuggets/iron': 'minecraft:iron_nugget',
    'c:strings': 'minecraft:string', 'c:leathers': 'minecraft:leather', 'c:feathers': 'minecraft:feather', 'c:gunpowders': 'minecraft:gunpowder',
    'c:obsidians': 'minecraft:obsidian', 'c:storage_blocks/iron': 'minecraft:iron_block', 'c:storage_blocks/gold': 'minecraft:gold_block',
    'c:storage_blocks/diamond': 'minecraft:diamond_block', 'c:chests/wooden': 'minecraft:chest', 'c:chests': 'minecraft:chest',
    'c:glass_blocks': 'minecraft:glass', 'c:glass_blocks/colorless': 'minecraft:glass', 'c:glass_panes': 'minecraft:glass_pane',
    'c:eggs': 'minecraft:egg', 'c:ender_pearls': 'minecraft:ender_pearl', 'c:slime_balls': 'minecraft:slime_ball',
    'c:bones': 'minecraft:bone', 'c:sands': 'minecraft:sand', 'c:gravels': 'minecraft:gravel', 'c:stones': 'minecraft:stone',
    'c:rods/blaze': 'minecraft:blaze_rod', 'c:crops/wheat': 'minecraft:wheat', 'c:crops/carrot': 'minecraft:carrot',
    'c:crops/potato': 'minecraft:potato', 'c:ores/iron': 'minecraft:iron_ore', 'c:ores/gold': 'minecraft:gold_ore',
    'c:ores/diamond': 'minecraft:diamond_ore', 'forge:obsidian': 'minecraft:obsidian', 'forge:feathers': 'minecraft:feather',
    'forge:gunpowder': 'minecraft:gunpowder', 'forge:bones': 'minecraft:bone', 'forge:eggs': 'minecraft:egg',
    'forge:ender_pearls': 'minecraft:ender_pearl', 'forge:slimeballs': 'minecraft:slime_ball', 'forge:nuggets/gold': 'minecraft:gold_nugget',
    'forge:nuggets/iron': 'minecraft:iron_nugget', 'forge:dusts/glowstone': 'minecraft:glowstone_dust', 'forge:gems/lapis': 'minecraft:dye@4',
    'forge:gems/quartz': 'minecraft:quartz', 'forge:storage_blocks/iron': 'minecraft:iron_block', 'forge:storage_blocks/gold': 'minecraft:gold_block',
    'forge:storage_blocks/diamond': 'minecraft:diamond_block', 'forge:chests/wooden': 'minecraft:chest', 'forge:chests': 'minecraft:chest',
    'forge:glass/colorless': 'minecraft:glass', 'forge:glass_panes': 'minecraft:glass_pane', 'forge:sand': 'minecraft:sand',
    'forge:gravel': 'minecraft:gravel', 'forge:rods/blaze': 'minecraft:blaze_rod', 'forge:crops/wheat': 'minecraft:wheat',
    'minecraft:wooden_stairs': 'minecraft:oak_stairs', 'minecraft:saplings': 'minecraft:sapling', 'minecraft:leaves': 'minecraft:leaves',
    'minecraft:small_flowers': 'minecraft:red_flower', 'minecraft:wool_carpets': 'minecraft:carpet', 'minecraft:carpets': 'minecraft:carpet',
    'minecraft:fishes': 'minecraft:fish', 'minecraft:arrows': 'minecraft:arrow', 'minecraft:boats': 'minecraft:boat',
    'minecraft:oak_logs': 'minecraft:log@0', 'minecraft:spruce_logs': 'minecraft:log@1', 'minecraft:birch_logs': 'minecraft:log@2',
    'minecraft:jungle_logs': 'minecraft:log@3', 'minecraft:logs_that_burn': 'minecraft:log', 'minecraft:stone_bricks': 'minecraft:stonebrick',
    'minecraft:sand': 'minecraft:sand', 'minecraft:coals': 'minecraft:coal',
})

# ---------------------------------------------------------------------------
# Class roles. Keys are class simple names across mapping sets.
ITEM_ROLE = {
    'Item': 'generic', 'ItemFood': 'food', 'ItemSword': 'sword', 'ItemPickaxe': 'pickaxe', 'ItemAxe': 'axe', 'ItemSpade': 'shovel',
    'ItemHoe': 'hoe', 'ItemArmor': 'armor', 'ItemTool': 'tool', 'ItemSeeds': 'generic', 'ItemSoup': 'food', 'ItemBucketMilk': 'generic',
    'ItemBow': 'bow', 'ItemBlock': 'blockitem', 'ItemMultiTexture': 'blockitem', 'ItemSlab': 'blockitem', 'ItemRecord': 'record',
    'ItemPotion': 'potion', 'ItemShears': 'shears', 'ItemFishingRod': 'fishingrod', 'ItemAppleGold': 'food', 'ItemSnowball': 'throwable',
    'ItemEnderPearl': 'throwable', 'ItemEgg': 'throwable', 'ItemSimpleFoiled': 'generic', 'ItemMonsterPlacer': 'spawnegg',
    # Mojang names (1.17+) / MCP 1.13-1.16
    'SwordItem': 'sword', 'PickaxeItem': 'pickaxe', 'AxeItem': 'axe', 'ShovelItem': 'shovel', 'HoeItem': 'hoe', 'ArmorItem': 'armor',
    'DiggerItem': 'tool', 'TieredItem': 'tool', 'ToolItem': 'tool', 'BlockItem': 'blockitem', 'BowlFoodItem': 'food', 'SoupItem': 'food',
    'BowItem': 'bow', 'CrossbowItem': 'bow', 'RecordItem': 'record', 'MusicDiscItem': 'record', 'SpawnEggItem': 'spawnegg',
    'DeferredSpawnEggItem': 'spawnegg', 'ForgeSpawnEggItem': 'spawnegg', 'PotionItem': 'potion', 'ShearsItem': 'shears',
    'FishingRodItem': 'fishingrod', 'SnowballItem': 'throwable', 'EnderpearlItem': 'throwable', 'EggItem': 'throwable',
    'TridentItem': 'bow', 'ShieldItem': 'shield', 'ElytraItem': 'elytra', 'DyeableArmorItem': 'armor', 'AnimalArmorItem': 'generic',
    'ItemNameBlockItem': 'blockitem', 'StandingAndWallBlockItem': 'blockitem', 'DoubleHighBlockItem': 'blockitem', 'SignItem': 'blockitem',
    'BucketItem': 'generic', 'MaceItem': 'sword',
    # API 1.10 boat items (a stock boat of a wood type)
    'ItemBoat': 'boat', 'BoatItem': 'boat',
}
BLOCK_BASES = {'Block', 'BlockContainer', 'BlockOre', 'BlockCompressed', 'BlockStone', 'BlockGlass', 'BlockBreakable', 'BlockFalling',
               'BlockRotatedPillar', 'BlockLog', 'BlockLeaves', 'BlockBush', 'BlockFlower', 'BlockCrops', 'BlockStairs', 'BlockSlab',
               'BlockFence', 'BlockWall', 'BlockDoor', 'BlockTrapDoor', 'BlockPane', 'BlockTorch', 'BlockDirectional', 'BlockHorizontal',
               'BlockSand', 'BlockGravel', 'BlockOreStorage', 'BlockWorkbench', 'BlockChest', 'BlockFurnace', 'BlockSapling', 'BlockTNT',
               'BlockMushroom', 'BlockReed', 'BlockCactus', 'BlockLadder', 'BlockButton', 'BlockPressurePlate', 'BlockLever',
               'BlockRedstoneLight', 'BlockHalfSlab', 'BlockGrass', 'BlockDirt', 'BlockPumpkin', 'BlockFire', 'BlockLiquid', 'BlockFluidBase',
               'BlockFluidClassic', 'BlockFluidFinite', 'BlockBeacon', 'BlockCake', 'BlockColored', 'BlockGlowstone', 'BlockIce',
               'BlockPackedIce', 'BlockSnow', 'BlockWeb', 'BlockVine', 'BlockStainedGlass', 'BlockNetherrack', 'BlockObsidian',
               'BlockHay', 'BlockCarpet', 'BlockRail', 'BlockRailBase', 'BlockPortal', 'BlockEndPortal', 'BlockStoneBrick', 'BlockPlanks',
               # Mojang / MCP 1.13+
               'DropExperienceBlock', 'OreBlock', 'RotatedPillarBlock', 'FallingBlock', 'SandBlock', 'GlassBlock', 'TransparentBlock',
               'HalfTransparentBlock', 'LeavesBlock', 'BushBlock', 'FlowerBlock', 'CropBlock', 'StairBlock', 'SlabBlock', 'FenceBlock',
               'WallBlock', 'DoorBlock', 'TrapDoorBlock', 'IronBarsBlock', 'TorchBlock', 'DirectionalBlock', 'HorizontalDirectionalBlock',
               'BaseEntityBlock', 'SaplingBlock', 'TntBlock', 'MushroomBlock', 'LadderBlock', 'ButtonBlock', 'PressurePlateBlock',
               'LeverBlock', 'RedstoneLampBlock', 'LiquidBlock', 'ColoredFallingBlock', 'StainedGlassBlock', 'CarpetBlock', 'WoolCarpetBlock',
               'ChestBlock', 'FurnaceBlock', 'CraftingTableBlock', 'RedStoneOreBlock', 'PoweredBlock', 'FenceGateBlock', 'PaneBlock',
               'StainedGlassPaneBlock', 'IceBlock', 'SnowLayerBlock', 'WebBlock', 'VineBlock', 'CactusBlock', 'SugarCaneBlock'}
BLOCK_SHAPE_SPECIAL = {  # vanilla bases whose shape/state is not a full cube -> reported
    'BlockStairs', 'BlockSlab', 'BlockHalfSlab', 'BlockFence', 'BlockWall', 'BlockDoor', 'BlockTrapDoor', 'BlockPane', 'BlockTorch',
    'BlockBush', 'BlockFlower', 'BlockCrops', 'BlockSapling', 'BlockLadder', 'BlockButton', 'BlockPressurePlate', 'BlockLever',
    'BlockCarpet', 'BlockRail', 'BlockRailBase', 'BlockLiquid', 'BlockFluidBase', 'BlockFluidClassic', 'BlockFluidFinite', 'BlockFire',
    'BlockPortal', 'BlockCake', 'BlockSnow', 'BlockWeb', 'BlockVine', 'BlockReed', 'BlockCactus', 'BlockMushroom',
    'StairBlock', 'SlabBlock', 'FenceBlock', 'WallBlock', 'DoorBlock', 'TrapDoorBlock', 'IronBarsBlock', 'TorchBlock', 'BushBlock',
    'FlowerBlock', 'CropBlock', 'SaplingBlock', 'LadderBlock', 'ButtonBlock', 'PressurePlateBlock', 'LeverBlock', 'CarpetBlock',
    'WoolCarpetBlock', 'LiquidBlock', 'FenceGateBlock', 'PaneBlock', 'StainedGlassPaneBlock', 'SnowLayerBlock', 'WebBlock', 'VineBlock',
    'CactusBlock', 'SugarCaneBlock', 'MushroomBlock'}
BLOCK_CONTAINER = {'BlockContainer', 'BaseEntityBlock', 'BlockChest', 'BlockFurnace', 'ChestBlock', 'FurnaceBlock'}
TRANSPARENT_BASES = {'BlockGlass', 'BlockBreakable', 'BlockLeaves', 'BlockStainedGlass', 'BlockIce', 'GlassBlock', 'TransparentBlock',
                     'HalfTransparentBlock', 'LeavesBlock', 'StainedGlassBlock', 'IceBlock'}

ENTITY_BASE = {
    # MCP 1.7.10-1.12 class name -> 1.95 base mob (entities195 clone list)
    'EntityZombie': 'zombie', 'EntitySkeleton': 'skeleton', 'EntityCreeper': 'creeper', 'EntitySpider': 'spider',
    'EntityCaveSpider': 'cave_spider', 'EntityEnderman': 'enderman', 'EntityPigZombie': 'zombie_pigman', 'EntityBlaze': 'blaze',
    'EntityGhast': 'ghast', 'EntitySlime': 'slime', 'EntityMagmaCube': 'magma_cube', 'EntityWitch': 'witch', 'EntitySilverfish': 'silverfish',
    'EntityBat': 'bat', 'EntityPig': 'pig', 'EntityCow': 'cow', 'EntitySheep': 'sheep', 'EntityChicken': 'chicken', 'EntityWolf': 'wolf',
    'EntityOcelot': 'ocelot', 'EntityHorse': 'horse', 'EntityVillager': 'villager', 'EntityIronGolem': 'villager_golem',
    'EntitySnowman': 'snowman', 'EntitySquid': 'squid', 'EntityMooshroom': 'mooshroom', 'EntityRabbit': 'rabbit', 'EntityGuardian': 'guardian',
    'EntityElderGuardian': 'elder_guardian', 'EntityPolarBear': 'polar_bear', 'EntityLlama': 'llama', 'EntityParrot': 'parrot',
    'EntityShulker': 'shulker', 'EntityStray': 'stray', 'EntityHusk': 'husk', 'EntityWitherSkeleton': 'wither_skeleton', 'EntityVex': 'vex',
    'EntityEvoker': 'evocation_illager', 'EntityVindicator': 'vindication_illager', 'EntityIllusionIllager': 'illusion_illager',
    'EntityZombieVillager': 'zombie_villager', 'EntityDonkey': 'donkey', 'EntityMule': 'mule', 'EntityZombieHorse': 'zombie_horse',
    'EntitySkeletonHorse': 'skeleton_horse',
    # Mojang names
    'Zombie': 'zombie', 'Skeleton': 'skeleton', 'Creeper': 'creeper', 'Spider': 'spider', 'CaveSpider': 'cave_spider', 'EnderMan': 'enderman',
    'ZombifiedPiglin': 'zombie_pigman', 'Blaze': 'blaze', 'Ghast': 'ghast', 'Slime': 'slime', 'MagmaCube': 'magma_cube', 'Witch': 'witch',
    'Silverfish': 'silverfish', 'Bat': 'bat', 'Pig': 'pig', 'Cow': 'cow', 'Sheep': 'sheep', 'Chicken': 'chicken', 'Wolf': 'wolf', 'Cat': 'cat',
    'Ocelot': 'ocelot', 'Horse': 'horse', 'Villager': 'villager', 'IronGolem': 'villager_golem', 'SnowGolem': 'snowman', 'Squid': 'squid',
    'MushroomCow': 'mooshroom', 'Rabbit': 'rabbit', 'Guardian': 'guardian', 'ElderGuardian': 'elder_guardian', 'PolarBear': 'polar_bear',
    'Llama': 'llama', 'Parrot': 'parrot', 'Shulker': 'shulker', 'Stray': 'stray', 'Husk': 'husk', 'WitherSkeleton': 'wither_skeleton',
    'Vex': 'vex', 'Evoker': 'evocation_illager', 'Vindicator': 'vindication_illager', 'Illusioner': 'illusion_illager', 'Dolphin': 'dolphin',
    'Drowned': 'drowned', 'Phantom': 'phantom', 'Turtle': 'turtle', 'Panda': 'panda', 'Cod': 'fish', 'Salmon': 'salmon',
    'Pufferfish': 'pufferfish', 'TropicalFish': 'tropicalfish', 'WanderingTrader': 'wandering_trader', 'Donkey': 'donkey', 'Mule': 'mule',
    'ZombieHorse': 'zombie_horse', 'SkeletonHorse': 'skeleton_horse', 'ZombieVillager': 'zombie_villager',
}
ENTITY_GENERIC = {  # abstract bases -> (base mob, category) - always reported as a guess
    'EntityMob': ('zombie', 'monster'), 'EntityMonster': ('zombie', 'monster'), 'Monster': ('zombie', 'monster'),
    'EntityAnimal': ('pig', 'creature'), 'Animal': ('pig', 'creature'), 'EntityAgeable': ('pig', 'creature'), 'AgeableMob': ('pig', 'creature'),
    'EntityCreature': ('pig', 'creature'), 'PathfinderMob': ('pig', 'creature'), 'EntityTameable': ('wolf', 'creature'),
    'TamableAnimal': ('wolf', 'creature'), 'EntityWaterMob': ('squid', 'water'), 'WaterAnimal': ('squid', 'water'),
    'EntityFlying': ('ghast', 'monster'), 'FlyingMob': ('ghast', 'monster'), 'EntityAmbientCreature': ('bat', 'ambient'),
    'AmbientCreature': ('bat', 'ambient'), 'EntityLiving': ('zombie', 'monster'), 'Mob': ('zombie', 'monster'),
    'EntityGolem': ('villager_golem', 'creature'), 'AbstractGolem': ('villager_golem', 'creature'), 'AbstractVillager': ('villager', 'creature'),
    'AbstractSkeleton': ('skeleton', 'monster'), 'AbstractIllager': ('vindication_illager', 'monster'), 'AbstractHorse': ('horse', 'creature'),
    'AbstractChestedHorse': ('donkey', 'creature'), 'AbstractFish': ('fish', 'water'), 'AbstractSchoolingFish': ('fish', 'water'),
    'EntityLivingBase': ('zombie', 'monster'), 'LivingEntity': ('zombie', 'monster'),
}
NONLIVING_ENTITY = {'Entity', 'EntityThrowable', 'EntityArrow', 'EntityFireball', 'EntityItem', 'EntityMinecart', 'EntityBoat',
                    'EntityHanging', 'EntityFallingBlock', 'EntityTNTPrimed', 'Projectile', 'ThrowableProjectile', 'ThrowableItemProjectile',
                    'AbstractArrow', 'AbstractHurtingProjectile', 'ItemEntity', 'AbstractMinecart', 'Boat', 'HangingEntity', 'FallingBlockEntity',
                    'PrimedTnt', 'EntityLightningBolt', 'EntityWeatherEffect', 'LightningBolt', 'EntityEnderCrystal', 'EndCrystal', 'EntityPainting', 'EntityItemFrame', 'EntityArmorStand', 'ArmorStand', 'EntityXPOrb', 'EntityFX', 'Particle'}
ENTITY_CATEGORY = {'zombie': 'monster', 'skeleton': 'monster', 'creeper': 'monster', 'spider': 'monster', 'cave_spider': 'monster',
                   'enderman': 'monster', 'zombie_pigman': 'monster', 'blaze': 'monster', 'ghast': 'monster', 'slime': 'monster',
                   'magma_cube': 'monster', 'witch': 'monster', 'silverfish': 'monster', 'stray': 'monster', 'husk': 'monster',
                   'wither_skeleton': 'monster', 'vex': 'monster', 'evocation_illager': 'monster', 'vindication_illager': 'monster',
                   'illusion_illager': 'monster', 'shulker': 'monster', 'guardian': 'monster', 'elder_guardian': 'monster',
                   'drowned': 'monster', 'phantom': 'monster', 'zombie_villager': 'monster', 'bat': 'ambient', 'squid': 'water',
                   'dolphin': 'water', 'fish': 'water', 'salmon': 'water', 'pufferfish': 'water', 'tropicalfish': 'water'}


def base_mobs_195():
    """Names entities195 can clone (modules/entities195/base_mobs195.inc), or None if the module is absent."""
    p = TOOLS.parent / 'modules' / 'entities195' / 'base_mobs195.inc'
    try:
        return set(re.findall(r'\{"([a-z_]+)", "[A-Za-z]+"', p.read_text()))
    except OSError:
        return None


AI_GOALS = {  # task class simple name -> SDK goal
    'EntityAISwimming': 'swim', 'EntityAIWander': 'wander', 'EntityAIWatchClosest': 'look_at_player', 'EntityAIPanic': 'panic',
    'EntityAIAttackOnCollide': 'melee_attack', 'EntityAIAttackMelee': 'melee_attack', 'EntityAINearestAttackableTarget': 'target_player',
    'EntityAITempt': 'follow_tempt_item', 'EntityAIMate': 'breed', 'EntityAIAvoidEntity': 'avoid_entity', 'EntityAIFleeSun': 'flee_sun',
    'EntityAIWanderAvoidWater': 'wander', 'FloatGoal': 'swim', 'RandomStrollGoal': 'wander', 'WaterAvoidingRandomStrollGoal': 'wander',
    'LookAtPlayerGoal': 'look_at_player', 'PanicGoal': 'panic', 'MeleeAttackGoal': 'melee_attack', 'ZombieAttackGoal': 'melee_attack',
    'NearestAttackableTargetGoal': 'target_player', 'TemptGoal': 'follow_tempt_item', 'BreedGoal': 'breed',
    'AvoidEntityGoal': 'avoid_entity', 'FleeSunGoal': 'flee_sun',
}
AI_IGNORABLE = {'EntityAILookIdle', 'RandomLookAroundGoal', 'EntityAIHurtByTarget', 'HurtByTargetGoal', 'EntityAIMoveTowardsRestriction',
                'MoveTowardsRestrictionGoal', 'EntityAIFollowParent', 'FollowParentGoal', 'EntityAIRestrictSun', 'RestrictSunGoal'}

# Potion/effect numeric IDs (1.7.10-1.12; identical in 1.95's effect registry order, modules/items195/effects.cpp)
EFFECT_BY_ID = {1: 'speed', 2: 'slowness', 3: 'haste', 4: 'mining_fatigue', 5: 'strength', 6: 'instant_health', 7: 'instant_damage',
                8: 'jump_boost', 9: 'nausea', 10: 'regeneration', 11: 'resistance', 12: 'fire_resistance', 13: 'water_breathing',
                14: 'invisibility', 15: 'blindness', 16: 'night_vision', 17: 'hunger', 18: 'weakness', 19: 'poison', 20: 'wither',
                21: 'health_boost', 22: 'absorption', 23: 'saturation', 24: 'glowing', 25: 'levitation', 26: 'luck', 27: 'unluck'}
EFFECT_FIELDS = {  # MCP field (1.7.10 Potion.X / 1.12 MobEffects.X) and Mojang MobEffects.X -> 1.95 effect
    'movespeed': 'speed', 'moveslowdown': 'slowness', 'digspeed': 'haste', 'digslowdown': 'mining_fatigue', 'damageboost': 'strength',
    'heal': 'instant_health', 'harm': 'instant_damage', 'jump': 'jump_boost', 'confusion': 'nausea', 'regeneration': 'regeneration',
    'resistance': 'resistance', 'fireresistance': 'fire_resistance', 'waterbreathing': 'water_breathing', 'invisibility': 'invisibility',
    'blindness': 'blindness', 'nightvision': 'night_vision', 'hunger': 'hunger', 'weakness': 'weakness', 'poison': 'poison',
    'wither': 'wither', 'healthboost': 'health_boost', 'absorption': 'absorption', 'saturation': 'saturation', 'glowing': 'glowing',
    'levitation': 'levitation', 'luck': 'luck', 'unluck': 'unluck',
    'speed': 'speed', 'slowness': 'slowness', 'haste': 'haste', 'mining_fatigue': 'mining_fatigue', 'strength': 'strength',
    'instant_health': 'instant_health', 'instant_damage': 'instant_damage', 'jump_boost': 'jump_boost', 'nausea': 'nausea',
    'fire_resistance': 'fire_resistance', 'water_breathing': 'water_breathing', 'night_vision': 'night_vision',
    'health_boost': 'health_boost', 'bad_luck': 'unluck', 'slow_falling': 'slow_falling', 'conduit_power': 'conduit_power',
    'bad_omen': 'bad_omen', 'hero_of_the_village': 'village_hero', 'dolphins_grace': None, 'darkness': None, 'trial_omen': None,
    'raid_omen': None, 'wind_charged': None, 'weaving': None, 'oozing': None, 'infested': None,
    'movement_speed': 'speed', 'movement_slowdown': 'slowness', 'dig_speed': 'haste', 'dig_slowdown': 'mining_fatigue',
    'damage_boost': 'strength', 'damage_resistance': 'resistance', 'jump': 'jump_boost', 'heal': 'instant_health', 'harm': 'instant_damage',
}

# Tool / armour materials
TOOL_MATERIALS = {  # vanilla enum constant -> (harvest, uses, efficiency, damage, enchantability, tier)
    'WOOD': (0, 59, 2.0, 0.0, 15, 'wood'), 'STONE': (1, 131, 4.0, 1.0, 5, 'stone'), 'IRON': (2, 250, 6.0, 2.0, 14, 'iron'),
    'EMERALD': (3, 1561, 8.0, 3.0, 10, 'diamond'), 'DIAMOND': (3, 1561, 8.0, 3.0, 10, 'diamond'), 'GOLD': (0, 32, 12.0, 0.0, 22, 'gold'),
    'NETHERITE': (4, 2031, 9.0, 4.0, 15, 'diamond'),
}
ARMOR_MATERIALS = {  # name -> (durability factor, [helmet, chest, legs, boots], enchantability)
    'CLOTH': (5, [1, 3, 2, 1], 15), 'LEATHER': (5, [1, 3, 2, 1], 15), 'CHAIN': (15, [2, 5, 4, 1], 12), 'IRON': (15, [2, 6, 5, 2], 9),
    'GOLD': (7, [2, 5, 3, 1], 25), 'DIAMOND': (33, [3, 8, 6, 3], 10), 'TURTLE': (25, [2, 0, 0, 0], 9), 'NETHERITE': (37, [3, 8, 6, 3], 15),
}
ARMOR_BASE_DURABILITY = [11, 16, 15, 13]
ARMOR_KINDS = ['helmet', 'chestplate', 'leggings', 'boots']


def tier_from_harvest(level):
    try:
        level = int(level)
    except (TypeError, ValueError):
        return 'iron'
    return {0: 'wood', 1: 'stone', 2: 'iron'}.get(level, 'diamond' if level >= 3 else 'wood')


# Block materials / sounds (MCP 1.7.10-1.12 field names, lowercased; Mojang SoundType names)
MATERIAL_MAP = {'rock': 'stone', 'iron': 'metal', 'anvil': 'metal', 'wood': 'wood', 'ground': 'dirt', 'grass': 'dirt', 'sand': 'sand',
                'clay': 'dirt', 'glass': 'glass', 'ice': 'glass', 'packedice': 'glass', 'redstonelight': 'glass', 'cloth': 'wool',
                'carpet': 'wool', 'plants': 'plant', 'vine': 'plant', 'leaves': 'plant', 'cactus': 'plant', 'gourd': 'plant', 'web': 'wool',
                'sponge': 'wool', 'snow': 'dirt', 'craftedsnow': 'dirt', 'coral': 'stone', 'circuits': 'stone', 'piston': 'stone',
                'tnt': 'plant', 'cake': 'plant', 'dragonegg': 'stone', 'portal': 'glass', 'barrier': 'stone', 'structure_void': 'stone',
                'stone': 'stone', 'metal': 'metal', 'heavy_metal': 'metal', 'dirt': 'dirt', 'wool': 'wool', 'plant': 'plant',
                'replaceable_plant': 'plant', 'nether_wood': 'wood', 'bamboo': 'wood', 'amethyst': 'glass', 'shulker_shell': 'stone'}
_MATERIAL_EXTRA = {  # 1.12 MCP (upper case, underscores) and 1.13-1.16 MCP Material fields -> 1.95 material
    'earth': 'dirt', 'organic': 'dirt', 'tallplants': 'plant', 'oceanplant': 'plant', 'seagrass': 'plant', 'miscellaneous': 'stone',
    'snowblock': 'dirt', 'shulker': 'stone', 'bamboosapling': 'plant', 'structurevoid': 'stone', 'barrier': 'stone', 'wool': 'wool',
    'nonsolid': 'stone', 'bubblecolumn': 'glass', 'gourd': 'plant', 'packedice': 'glass', 'redstonelight': 'glass', 'dragonegg': 'stone',
    'craftedsnow': 'dirt', 'decoration': 'stone', 'replaceableplant': 'plant', 'replaceablewaterplant': 'plant', 'waterplant': 'plant',
    'replaceablefireproofplant': 'plant', 'topsnow': 'dirt', 'heavymetal': 'metal', 'netherwood': 'wood', 'clothdecoration': 'wool',
    'explosive': 'plant', 'buildableglass': 'glass', 'icesolid': 'glass', 'powdersnow': 'dirt', 'leaves': 'plant', 'egg': 'stone',
    'vegetable': 'plant', 'moss': 'plant', 'sculk': 'stone', 'froglight': 'glass', 'frogspawn': 'plant', 'amethyst': 'glass',
}


def material_195(name):
    """Material field (1.7.10 rock, 1.12 PACKED_ICE, 1.14 EARTH...) -> 1.95 material or None."""
    n = name.lower()
    return MATERIAL_MAP.get(n) or MATERIAL_MAP.get(n.replace('_', '')) or _MATERIAL_EXTRA.get(n.replace('_', ''))


SOUND_MAP = {'soundtypestone': 'stone', 'soundtypewood': 'wood', 'soundtypegravel': 'gravel', 'soundtypegrass': 'grass',
             'soundtypepiston': 'stone', 'soundtypemetal': 'metal', 'soundtypeglass': 'glass', 'soundtypecloth': 'cloth',
             'soundtypesand': 'sand', 'soundtypesnow': 'cloth', 'soundtypeladder': 'wood', 'soundtypeanvil': 'metal',
             'stone': 'stone', 'wood': 'wood', 'ground': 'gravel', 'gravel': 'gravel', 'plant': 'grass', 'grass': 'grass', 'metal': 'metal',
             'glass': 'glass', 'cloth': 'cloth', 'wool': 'cloth', 'sand': 'sand', 'snow': 'cloth', 'ladder': 'wood', 'anvil': 'metal',
             'slime': 'cloth', 'netherrack': 'stone', 'nether_bricks': 'stone', 'deepslate': 'stone', 'amethyst': 'glass',
             'copper': 'metal', 'chain': 'metal', 'lantern': 'metal', 'bone_block': 'stone', 'crop': 'grass', 'sweet_berry_bush': 'grass',
             'bamboo': 'wood', 'bamboo_wood': 'wood', 'cherry_wood': 'wood', 'nether_wood': 'wood', 'basalt': 'stone', 'tuff': 'stone',
             'plants': 'grass', 'snail': 'stone', 'cloth_': 'cloth', 'wet_grass': 'grass', 'coral': 'stone', 'bamboo_sapling': 'grass',
             'nether_wart': 'grass', 'lantern_': 'metal', 'scaffolding_': 'wood', 'stem': 'wood', 'nylium': 'stone', 'fungus': 'grass',
             'mud': 'gravel', 'honey_block': 'cloth', 'coral_block': 'stone', 'scaffolding': 'wood', 'lily_pad': 'grass'}

CREATIVE_TAB_MAP = {  # CreativeTabs MCP field (lowercased) / Mojang CreativeModeTabs -> SDK group
    'tabblock': 'minecraft:building_blocks', 'tabdecorations': 'minecraft:decorations', 'tabredstone': 'minecraft:redstone',
    'tabtransport': 'minecraft:transportation', 'tabmisc': 'minecraft:misc', 'tabfood': 'minecraft:food', 'tabtools': 'minecraft:tools',
    'tabcombat': 'minecraft:combat', 'tabbrewing': 'minecraft:brewing', 'tabmaterials': 'minecraft:materials',
    'building_blocks': 'minecraft:building_blocks', 'decorations': 'minecraft:decorations', 'redstone': 'minecraft:redstone',
    'transportation': 'minecraft:transportation', 'misc': 'minecraft:misc', 'food': 'minecraft:food', 'tools': 'minecraft:tools',
    'combat': 'minecraft:combat', 'brewing': 'minecraft:brewing', 'materials': 'minecraft:materials',
    'food_and_drinks': 'minecraft:food', 'ingredients': 'minecraft:materials', 'tools_and_utilities': 'minecraft:tools',
    'natural_blocks': 'minecraft:building_blocks', 'functional_blocks': 'minecraft:decorations', 'colored_blocks': 'minecraft:building_blocks',
    'redstone_blocks': 'minecraft:redstone', 'spawn_eggs': 'minecraft:misc',
}

ATTRIBUTE_MAP = {'maxhealth': 'health', 'max_health': 'health', 'generic.maxhealth': 'health', 'movementspeed': 'move_speed',
                 'movement_speed': 'move_speed', 'attackdamage': 'attack_damage', 'attack_damage': 'attack_damage'}

# Behaviour method names (MCP / Mojang) on items, blocks and entities -> (event, description)
ITEM_BEHAVIOUR = {
    'onItemRightClick': 'item_use', 'onItemUse': 'item_use', 'onItemUseFirst': 'item_use', 'use': 'item_use', 'useOn': 'item_use',
    'onItemUseFinish': 'item_finish', 'onEaten': 'item_finish', 'onFoodEaten': 'item_finish', 'finishUsingItem': 'item_finish',
    'hitEntity': 'hit_entity', 'onLeftClickEntity': 'hit_entity', 'hurtEnemy': 'hit_entity', 'postHurtEnemy': 'hit_entity',
    'onUpdate': 'inventory_tick', 'inventoryTick': 'inventory_tick', 'onArmorTick': 'inventory_tick',
    'onBlockDestroyed': 'mine_block', 'mineBlock': 'mine_block', 'itemInteractionForEntity': 'interact_entity',
    'interactLivingEntity': 'interact_entity', 'onCreated': 'crafted', 'onCraftedBy': 'crafted', 'onPlayerStoppedUsing': 'release_use',
    'releaseUsing': 'release_use', 'onUsingTick': 'using_tick', 'onUseTick': 'using_tick', 'onDroppedByPlayer': 'dropped',
    'onEntityItemUpdate': 'entity_item_tick',
}
BLOCK_BEHAVIOUR = {
    'onBlockActivated': 'block_use', 'use': 'block_use', 'useWithoutItem': 'block_use', 'useItemOn': 'block_use',
    'onEntityWalking': 'step_on', 'onEntityWalk': 'step_on', 'stepOn': 'step_on', 'onEntityCollidedWithBlock': 'entity_inside',
    'onEntityCollision': 'entity_inside', 'entityInside': 'entity_inside', 'updateTick': 'block_tick', 'tick': 'block_tick',
    'randomTick': 'block_tick', 'onBlockDestroyedByPlayer': 'block_break', 'breakBlock': 'block_break', 'playerDestroy': 'block_break',
    'destroy': 'block_break', 'onRemove': 'block_break', 'playerWillDestroy': 'block_break', 'onBlockHarvested': 'block_break',
    'onBlockPlacedBy': 'block_place', 'onBlockAdded': 'block_place', 'setPlacedBy': 'block_place', 'onPlace': 'block_place',
    'onBlockClicked': 'block_attack', 'attack': 'block_attack', 'randomDisplayTick': 'client_fx', 'animateTick': 'client_fx',
    'onNeighborBlockChange': 'neighbor', 'neighborChanged': 'neighbor', 'onFallenUpon': 'fall_on', 'fallOn': 'fall_on',
}
BLOCK_DATA_METHODS = {'getItemDropped', 'quantityDropped', 'quantityDroppedWithBonus', 'damageDropped', 'getDrops', 'isOpaqueCube',
                      'renderAsNormalBlock', 'getRenderType', 'getIcon', 'registerBlockIcons', 'getBlockTextureFromSide', 'getLightValue',
                      'createNewTileEntity', 'createTileEntity', 'hasTileEntity', 'getShape', 'getCollisionShape', 'getStateForPlacement',
                      'createBlockStateDefinition', 'newBlockEntity', 'getTicker', 'getRenderShape', 'isFullCube', 'getBlockLayer',
                      'getRenderLayer', 'getBoundingBox', 'getCollisionBoundingBox', 'getSelectedBoundingBox', 'setBlockBoundsBasedOnState',
                      'getMetaFromState', 'getStateFromMeta', 'createBlockState', 'getActualState', 'canHarvestBlock', 'getExpDrop',
                      'canSilkHarvest', 'getPickBlock', 'getCloneItemStack', 'canSustainPlant', 'isSideSolid', 'getSubBlocks',
                      'shouldSideBeRendered', 'getMixedBrightnessForBlock', 'colorMultiplier', 'getRenderColor', 'getBlockColor',
                      'isNormalCube', 'getMapColor', 'canPlaceBlockAt', 'canBlockStay', 'onBlockPlaced', 'isReplaceable', 'tickRate',
                      'getExplosionResistance', 'getBlockHardness', 'getPlayerRelativeBlockHardness', 'isToolEffective', 'getHarvestTool',
                      'getHarvestLevel', 'codec', 'getOcclusionShape', 'useShapeForLightOcclusion', 'propagatesSkylightDown',
                      'getLightBlock', 'getShadeBrightness', 'skipRendering', 'getVisualShape', 'getInteractionShape', 'rotate', 'mirror',
                      'updateShape', 'canSurvive', 'getFluidState', 'isPathfindable', 'getAnalogOutputSignal', 'hasAnalogOutputSignal',
                      'getSignal', 'getDirectSignal', 'isSignalSource', 'getMenuProvider', 'triggerEvent', 'getSeed', 'onBlockExploded',
                      'canDropFromExplosion', 'dropFromExplosion', 'canEntityDestroy', 'getEnchantPowerBonus', 'getFireSpreadSpeed',
                      'getFlammability', 'isFlammable', 'damageDropped', 'getUnlocalizedName', 'getLocalizedName', 'getItem', 'getCreativeTabToDisplayOn'}
ITEM_DATA_METHODS = {'getMaxItemUseDuration', 'getItemUseAction', 'hasEffect', 'addInformation', 'getItemStackDisplayName', 'getIconFromDamage',
                     'registerIcons', 'getItemEnchantability', 'getRarity', 'isFull3D', 'getUnlocalizedName', 'getSubItems', 'getIcon',
                     'getIconIndex', 'requiresMultipleRenderPasses', 'getColorFromItemStack', 'getRenderPasses', 'isRepairable',
                     'getIsRepairable', 'getContainerItem', 'hasContainerItem', 'getDamage', 'getMaxDamage', 'isDamageable',
                     'getItemStackLimit', 'getUseAnimation', 'getUseDuration', 'isFoil', 'appendHoverText', 'getName', 'getDescriptionId',
                     'isEnchantable', 'getEnchantmentValue', 'isValidRepairItem', 'getBarWidth', 'getBarColor', 'isBarVisible',
                     'getDestroySpeed', 'isCorrectToolForDrops', 'getDefaultAttributeModifiers', 'getAttributeModifiers', 'getArmorTexture',
                     'getArmorModel', 'initCapabilities', 'getBurnTime', 'getCreativeTab', 'getCreativeTabs', 'getStrVsBlock',
                     'func_150893_a', 'canHarvestBlock', 'getHarvestLevel', 'getToolClasses', 'getDigSpeed', 'getHealAmount',
                     'getSaturationModifier', 'isWolfsFavoriteMeat', 'getMetadata', 'getHasSubtypes', 'getShareTag', 'shouldCauseReequipAnimation',
                     'getEntityLifespan', 'hasCustomEntity', 'createEntity', 'getItemBurnTime', 'getModel', 'getItemAttributeModifiers',
                     'getPotionEffect', 'getContainerItemStack', 'getMaxStackSize', 'getTooltipImage', 'canAttackBlock', 'getEquipmentSlot'}
ENTITY_BEHAVIOUR = {
    'onUpdate': 'entity_tick', 'onLivingUpdate': 'entity_tick', 'tick': 'entity_tick', 'aiStep': 'entity_tick', 'customServerAiStep': 'entity_tick',
    'updateAITasks': 'entity_tick', 'updateAITick': 'entity_tick', 'attackEntityAsMob': 'entity_attack', 'doHurtTarget': 'entity_attack',
    'interact': 'entity_interact', 'processInteract': 'entity_interact', 'mobInteract': 'entity_interact', 'onDeath': 'entity_death',
    'die': 'entity_death', 'attackEntityFrom': 'entity_hurt', 'hurt': 'entity_hurt', 'onCollideWithPlayer': 'entity_touch',
    'playerTouch': 'entity_touch', 'onStruckByLightning': 'entity_misc', 'thunderHit': 'entity_misc', 'fall': 'entity_misc',
    'causeFallDamage': 'entity_misc', 'onKillEntity': 'entity_misc', 'killed': 'entity_misc', 'attackEntityWithRangedAttack': 'entity_attack',
    'performRangedAttack': 'entity_attack',
}
ENTITY_DATA_METHODS = {'applyEntityAttributes', 'createAttributes', 'entityInit', 'defineSynchedData', 'writeEntityToNBT', 'readEntityFromNBT',
                       'addAdditionalSaveData', 'readAdditionalSaveData', 'getLivingSound', 'getHurtSound', 'getDeathSound', 'getAmbientSound',
                       'getSoundVolume', 'getDropItem', 'dropFewItems', 'dropCustomDeathLoot', 'getDefaultLootTable', 'getLootTable',
                       'isAIEnabled', 'canDespawn', 'removeWhenFarAway', 'getTotalArmorValue', 'getArmorValue', 'getCreatureAttribute',
                       'getMobType', 'getCanSpawnHere', 'checkSpawnRules', 'registerGoals', 'getExperiencePoints', 'getBaseExperience',
                       'getMaxSpawnedInChunk', 'getMaxSpawnClusterSize', 'func_146066_aG', 'isValidLightLevel', 'getBlockPathWeight',
                       'getWalkTargetValue', 'getEyeHeight', 'getStandingEyeHeight', 'getYOffset', 'getMountedYOffset', 'canBreatheUnderwater',
                       'isBreedingItem', 'isFood', 'createChild', 'getBreedOffspring', 'getStepSound', 'playStepSound', 'func_145780_a',
                       'getTalkInterval', 'getAmbientSoundInterval', 'canBeLeashedTo', 'canBeLeashed', 'getDropChance', 'setEquipmentBasedOnDifficulty',
                       'onInitialSpawn', 'finalizeSpawn', 'getAddEntityPacket', 'getSpawnPacket', 'shouldDropExperience', 'isPushable', 'canBePushed'}

FORGE_EVENTS = {  # event class simple name -> (lce event, note)
    'PlayerInteractEvent': ('ITEM_USE', 'approximation: ITEM_USE fires for item use only'), 'RightClickItem': ('ITEM_USE', ''),
    'RightClickBlock': (None, 'no block-use event in lce_api 1.1'), 'LeftClickBlock': (None, 'no block-attack event in lce_api 1.1'),
    'EntityInteract': (None, 'no entity-interact event in lce_api 1.1'), 'BreakEvent': ('BLOCK_BREAK', ''), 'PlaceEvent': ('BLOCK_PLACE', ''),
    'EntityPlaceEvent': ('BLOCK_PLACE', ''), 'HarvestDropsEvent': ('BLOCK_BREAK', 'drops cannot be edited from BLOCK_BREAK'),
    'LivingHurtEvent': ('ENTITY_DAMAGE', ''), 'LivingAttackEvent': ('ENTITY_DAMAGE', ''), 'LivingDamageEvent': ('ENTITY_DAMAGE', ''),
    'LivingIncomingDamageEvent': ('ENTITY_DAMAGE', ''), 'LivingDeathEvent': ('ENTITY_DEATH', ''),
    'LivingDropsEvent': ('ENTITY_DEATH', 'drops cannot be edited from ENTITY_DEATH; use drop_item'),
    'PlayerLoggedInEvent': ('PLAYER_JOIN', ''), 'PlayerLoggedOutEvent': ('PLAYER_LEAVE', ''), 'ServerChatEvent': ('CHAT', ''),
    'ServerTickEvent': ('SERVER_TICK', ''), 'WorldTickEvent': ('SERVER_TICK', ''), 'LevelTickEvent': ('SERVER_TICK', ''),
    'PlayerTickEvent': ('SERVER_TICK', 'per-player tick has no LCE equivalent; SERVER_TICK fires once per tick'),
    'ClientTickEvent': ('CLIENT_TICK', ''), 'ServerStartedEvent': ('WORLD_LOAD', 'approximation: server started ~ world load'),
    'ServerStartingEvent': (None, 'server start (commands need register_command)'), 'FMLServerStartingEvent': (None, 'server start (commands need register_command)'),
    'ServerStoppingEvent': ('WORLD_UNLOAD', 'approximation'), 'ServerTickEvent$Post': ('SERVER_TICK', ''), 'Post': ('SERVER_TICK', 'tick Post phase'),
    'Pre': ('SERVER_TICK', 'tick Pre phase'), 'LevelEvent': (None, 'level event'), 'PlayerChangedDimensionEvent': (None, 'no dimension-change event'),
    'AddReloadListenerEvent': (None, 'data reload listener (mod data files are not ported)'), 'RegisterPayloadHandlersEvent': (None, 'networking: impossible'),
    'FMLCommonSetupEvent': (None, 'mod lifecycle'), 'FMLClientSetupEvent': (None, 'mod lifecycle'), 'EntityRenderersEvent': (None, 'rendering is impossible'),
    'RegisterRenderers': (None, 'rendering is impossible'), 'RegisterLayerDefinitions': (None, 'rendering is impossible'),
    'RegisterKeyMappingsEvent': (None, 'keybinds need register_keybind'), 'RegisterMenuScreensEvent': (None, 'GUIs are impossible on 1.95'), 'RenderTickEvent': (None, 'rendering hooks are impossible on 1.95'),
    'Load': ('WORLD_LOAD', 'WorldEvent.Load / LevelEvent.Load'), 'Unload': ('WORLD_UNLOAD', ''),
    'LivingUpdateEvent': ('SERVER_TICK', 'per-entity tick has no LCE equivalent'), 'LivingTickEvent': ('SERVER_TICK', 'per-entity tick'),
    'ItemCraftedEvent': (None, 'no crafting event in lce_api 1.1'), 'ItemSmeltedEvent': (None, 'no smelting event in lce_api 1.1'),
    'PlayerRespawnEvent': (None, 'no respawn event'), 'EntityJoinWorldEvent': (None, 'no entity-join event'),
    'EntityJoinLevelEvent': (None, 'no entity-join event'), 'LivingSpawnEvent': (None, 'no spawn event'), 'CheckSpawn': (None, 'no spawn event'),
    'RenderGameOverlayEvent': (None, 'HUD rendering is impossible on 1.95'), 'RenderLivingEvent': (None, 'rendering is impossible'),
    'TextureStitchEvent': (None, 'client resource event; textures come from the package'), 'ModelBakeEvent': (None, 'client models impossible'),
    'RegistryEvent': (None, 'registration (handled by the data port)'), 'Register': (None, 'registration (handled by the data port)'),
    'RegisterEvent': (None, 'registration (handled by the data port)'), 'FMLPreInitializationEvent': (None, 'mod lifecycle'),
    'BuildCreativeModeTabContentsEvent': (None, 'creative tab contents (handled: items go to the Mods tab)'),
    'EntityAttributeCreationEvent': (None, 'entity attributes (handled by the data port)'),
    'AttackEntityEvent': ('ENTITY_DAMAGE', ''), 'CommandEvent': (None, 'commands need register_command (native/script)'),
    'RegisterCommandsEvent': (None, 'commands need register_command (native/script)'), 'FuelBurnTimeEvent': (None, 'fuel (data)'),
    'FurnaceFuelBurnTimeEvent': (None, 'fuel (data)'), 'AnvilUpdateEvent': (None, 'no anvil event'), 'BonemealEvent': (None, 'no bonemeal event'),
    'EnderTeleportEvent': (None, 'no teleport event'), 'ExplosionEvent': (None, 'no explosion event'), 'Detonate': (None, 'no explosion event'),
    'ItemTooltipEvent': (None, 'tooltips are impossible on 1.95'), 'ItemExpireEvent': (None, 'no item-entity event'),
    'ItemTossEvent': (None, 'no item-toss event'), 'EntityItemPickupEvent': (None, 'no pickup event'), 'ItemPickupEvent': (None, 'no pickup event'),
    'LivingJumpEvent': (None, 'no jump event'), 'LivingFallEvent': (None, 'no fall event'), 'PlayerSleepInBedEvent': (None, 'no sleep event'),
    'ChunkEvent': (None, 'no chunk event'), 'PopulateChunkEvent': (None, 'worldgen events: use features/ores'),
    'DecorateBiomeEvent': (None, 'worldgen events: use features/ores'), 'BiomeLoadingEvent': (None, 'worldgen (biome modifiers in data)'),
    'ConfigChangedEvent': (None, 'config GUI'), 'OnConfigChangedEvent': (None, 'config GUI'), 'KeyInputEvent': (None, 'keybinds need register_keybind'),
    'InputEvent': (None, 'keybinds need register_keybind'), 'GuiOpenEvent': (None, 'GUIs are impossible on 1.95'),
    'ScreenEvent': (None, 'GUIs are impossible on 1.95'), 'ClientChatReceivedEvent': (None, 'client chat'),
}
