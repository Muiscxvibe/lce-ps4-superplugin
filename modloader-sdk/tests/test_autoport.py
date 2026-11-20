"""Tests for tools/lce_autoport.py and tools/autoport/ (run: python3 -I tests/test_autoport.py).

Unit tests compile tiny Java sources with the local javac (stub classes with the
same names as the Forge / NeoForge / Minecraft APIs, so the bytecode looks like a
real mod's), pack them into jars and run the parser, interpreter and porter on
them. Integration tests port real mod jars found on this machine (read-only)
and are skipped when the jars are absent.

Scratch output goes to $LCE_AUTOPORT_SCRATCH (if set) or a fresh temp dir.
"""
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import lce_modpack  # noqa: E402
import lce_autoport  # noqa: E402
import lce_port  # noqa: E402
from autoport import classfile as CF, interp as I, extract as X, vanilla as VN, mappings as M, behaviour as B, jar as J, port as P, describe as DS  # noqa: E402

JAVAC = shutil.which('javac') or ('/usr/bin/javac' if os.path.exists('/usr/bin/javac') else None)
BASE = Path(os.environ.get('LCE_AUTOPORT_SCRATCH') or tempfile.mkdtemp(prefix='autoport_tests_'))
BASE.mkdir(parents=True, exist_ok=True)

# ---------------------------------------------------------------------------
# Java stubs: 1.7.10-style Forge (MCP names, as in a deobfuscated dev jar)
OLD_STUBS = {
    'cpw/mods/fml/common/registry/GameRegistry.java': '''package cpw.mods.fml.common.registry;
import net.minecraft.item.*; import net.minecraft.block.Block;
public class GameRegistry {
  public static void registerItem(Item i, String n) {}
  public static void registerBlock(Block b, String n) {}
  public static void addRecipe(ItemStack o, Object... p) {}
  public static void addShapelessRecipe(ItemStack o, Object... p) {}
  public static void addSmelting(Item i, ItemStack o, float xp) {}
  public static void registerFuelHandler(cpw.mods.fml.common.IFuelHandler h) {}
  public static void registerWorldGenerator(cpw.mods.fml.common.IWorldGenerator g, int w) {}
}''',
    'cpw/mods/fml/common/registry/EntityRegistry.java': '''package cpw.mods.fml.common.registry;
public class EntityRegistry {
  public static void registerModEntity(Class<?> c, String n, int id, Object mod, int r, int f, boolean v) {}
  public static void registerGlobalEntityID(Class<?> c, String n, int id, int bg, int fg) {}
  public static void addSpawn(Class<?> c, int w, int mn, int mx, net.minecraft.entity.EnumCreatureType t, Object... biomes) {}
}''',
    'cpw/mods/fml/common/IFuelHandler.java': 'package cpw.mods.fml.common; public interface IFuelHandler { int getBurnTime(net.minecraft.item.ItemStack s); }',
    'cpw/mods/fml/common/IWorldGenerator.java': 'package cpw.mods.fml.common; public interface IWorldGenerator { void generate(java.util.Random r, int cx, int cz, net.minecraft.world.World w, Object a, Object b); }',
    'net/minecraft/world/World.java': 'package net.minecraft.world; public class World { public boolean isRemote; }',
    'net/minecraft/item/Item.java': '''package net.minecraft.item;
public class Item {
  public enum ToolMaterial { WOOD, STONE, IRON, EMERALD, GOLD }
  public Item setUnlocalizedName(String s) { return this; }
  public Item setTextureName(String s) { return this; }
  public Item setMaxStackSize(int n) { return this; }
  public Item setCreativeTab(net.minecraft.creativetab.CreativeTabs t) { return this; }
  public static Item getItemFromBlock(net.minecraft.block.Block b) { return null; }
  public ItemStack onItemRightClick(ItemStack s, net.minecraft.world.World w, net.minecraft.entity.player.EntityPlayer p) { return s; }
}''',
    'net/minecraft/item/ItemFood.java': '''package net.minecraft.item;
public class ItemFood extends Item { public ItemFood(int h, float s, boolean w) {} public ItemFood setPotionEffect(int id, int d, int a, float p) { return this; } }''',
    'net/minecraft/item/ItemSword.java': 'package net.minecraft.item; public class ItemSword extends Item { public ItemSword(Item.ToolMaterial m) {} }',
    'net/minecraft/item/ItemPickaxe.java': 'package net.minecraft.item; public class ItemPickaxe extends Item { protected ItemPickaxe(Item.ToolMaterial m) {} }',
    'net/minecraft/item/ItemArmor.java': '''package net.minecraft.item;
public class ItemArmor extends Item { public enum ArmorMaterial { CLOTH, CHAIN, IRON, GOLD, DIAMOND } public ItemArmor(ArmorMaterial m, int r, int t) {} }''',
    'net/minecraft/item/ItemStack.java': '''package net.minecraft.item;
public class ItemStack { public ItemStack(Item i) {} public ItemStack(Item i, int c) {} public ItemStack(Item i, int c, int m) {}
  public ItemStack(net.minecraft.block.Block b) {} public ItemStack(net.minecraft.block.Block b, int c, int m) {} public Item getItem() { return null; } }''',
    'net/minecraft/creativetab/CreativeTabs.java': 'package net.minecraft.creativetab; public class CreativeTabs { public static CreativeTabs tabFood, tabCombat, tabMaterials; }',
    'net/minecraft/init/Items.java': 'package net.minecraft.init; import net.minecraft.item.Item; public class Items { public static Item diamond, stick, iron_ingot, dye, coal, apple; }',
    'net/minecraft/init/Blocks.java': 'package net.minecraft.init; import net.minecraft.block.Block; public class Blocks { public static Block stone, planks, pumpkin, obsidian; }',
    'net/minecraft/block/material/Material.java': 'package net.minecraft.block.material; public class Material { public static Material rock, iron, wood; }',
    'net/minecraft/block/Block.java': '''package net.minecraft.block;
public class Block { public static Object soundTypeMetal, soundTypeStone; public Block(net.minecraft.block.material.Material m) {}
  public Block setHardness(float h) { return this; } public Block setResistance(float r) { return this; } public Block setLightLevel(float l) { return this; }
  public Block setStepSound(Object s) { return this; } public Block setBlockName(String n) { return this; } public Block setBlockTextureName(String n) { return this; }
  public void setHarvestLevel(String t, int l) {} }''',
    'net/minecraft/world/gen/feature/WorldGenMinable.java': '''package net.minecraft.world.gen.feature;
public class WorldGenMinable { public WorldGenMinable(net.minecraft.block.Block b, int n) {} public boolean generate(net.minecraft.world.World w, java.util.Random r, int x, int y, int z) { return true; } }''',
    'net/minecraft/entity/EnumCreatureType.java': 'package net.minecraft.entity; public enum EnumCreatureType { monster, creature }',
    'net/minecraft/entity/SharedMonsterAttributes.java': 'package net.minecraft.entity; public class SharedMonsterAttributes { public static Object maxHealth, movementSpeed, attackDamage; }',
    'net/minecraft/entity/ai/attributes/IAttributeInstance.java': 'package net.minecraft.entity.ai.attributes; public interface IAttributeInstance { void setBaseValue(double v); }',
    'net/minecraft/entity/EntityLivingBase.java': '''package net.minecraft.entity;
public class EntityLivingBase { public void addPotionEffect(net.minecraft.potion.PotionEffect e) {} public void heal(float f) {}
  public net.minecraft.entity.ai.attributes.IAttributeInstance getEntityAttribute(Object a) { return null; } protected void applyEntityAttributes() {} }''',
    'net/minecraft/entity/monster/EntityZombie.java': 'package net.minecraft.entity.monster; public class EntityZombie extends net.minecraft.entity.EntityLivingBase { public EntityZombie(net.minecraft.world.World w) {} }',
    'net/minecraft/entity/player/EntityPlayer.java': '''package net.minecraft.entity.player;
public class EntityPlayer extends net.minecraft.entity.EntityLivingBase { public void addChatMessage(net.minecraft.util.ChatComponentText t) {} }''',
    'net/minecraft/util/ChatComponentText.java': 'package net.minecraft.util; public class ChatComponentText { public ChatComponentText(String s) {} }',
    'net/minecraft/potion/PotionEffect.java': 'package net.minecraft.potion; public class PotionEffect { public PotionEffect(int id, int d, int a) {} }',
    'net/minecraftforge/common/util/EnumHelper.java': '''package net.minecraftforge.common.util;
public class EnumHelper { public static net.minecraft.item.Item.ToolMaterial addToolMaterial(String n, int h, int u, float e, float d, int en) { return null; }
  public static net.minecraft.item.ItemArmor.ArmorMaterial addArmorMaterial(String n, int d, int[] r, int e) { return null; } }''',
}

OLD_MOD = {
    'rubymod/RubyMod.java': '''package rubymod;
import cpw.mods.fml.common.registry.*; import net.minecraft.item.*; import net.minecraft.block.*; import net.minecraft.init.*;
import net.minecraft.creativetab.CreativeTabs; import net.minecraftforge.common.util.EnumHelper;
public class RubyMod {
  public static Item ruby, rubyApple, rubySword, rubyPick, rubyHelmet, wand;
  public static Block rubyOre, rubyBlock;
  public static Item.ToolMaterial RUBY = EnumHelper.addToolMaterial("RUBY", 2, 900, 7.0F, 2.5F, 18);
  public static ItemArmor.ArmorMaterial RUBY_ARMOR = EnumHelper.addArmorMaterial("RUBY", 20, new int[]{3, 7, 6, 3}, 12);
  public void preInit() {
    ruby = new Item().setUnlocalizedName("ruby").setTextureName("rubymod:ruby").setCreativeTab(CreativeTabs.tabMaterials);
    rubyApple = new ItemFood(6, 0.8F, false).setPotionEffect(10, 5, 1, 0.5F).setUnlocalizedName("rubyApple").setTextureName("rubymod:ruby_apple");
    rubySword = new ItemSword(RUBY).setUnlocalizedName("rubySword").setTextureName("rubymod:ruby_sword");
    rubyPick = new RubyPickaxe(RUBY).setTextureName("rubymod:ruby_pickaxe");
    rubyHelmet = new ItemArmor(RUBY_ARMOR, 0, 0).setUnlocalizedName("rubyHelmet").setTextureName("rubymod:ruby_helmet");
    wand = new Wand().setUnlocalizedName("wand").setTextureName("rubymod:wand").setMaxStackSize(1);
    rubyOre = new Block(net.minecraft.block.material.Material.rock).setHardness(3.0F).setResistance(5.0F).setStepSound(Block.soundTypeStone)
        .setBlockName("rubyOre").setBlockTextureName("rubymod:ruby_ore");
    rubyOre.setHarvestLevel("pickaxe", 2);
    rubyBlock = new Block(net.minecraft.block.material.Material.iron).setHardness(5.0F).setLightLevel(1.0F).setStepSound(Block.soundTypeMetal)
        .setBlockName("rubyBlock").setBlockTextureName("rubymod:ruby_block");
    register(ruby, "ruby");
    GameRegistry.registerItem(rubyApple, "ruby_apple");
    GameRegistry.registerItem(rubySword, "ruby_sword");
    GameRegistry.registerItem(rubyPick, "ruby_pickaxe");
    GameRegistry.registerItem(rubyHelmet, "ruby_helmet");
    GameRegistry.registerItem(wand, "wand");
    GameRegistry.registerBlock(rubyOre, "ruby_ore");
    GameRegistry.registerBlock(rubyBlock, "ruby_block");
    GameRegistry.addRecipe(new ItemStack(rubyBlock), "RRR", "RRR", "RRR", 'R', ruby);
    GameRegistry.addRecipe(new ItemStack(rubySword), " R ", " R ", " S ", 'R', ruby, 'S', Items.stick);
    GameRegistry.addShapelessRecipe(new ItemStack(ruby, 9), rubyBlock);
    GameRegistry.addShapelessRecipe(new ItemStack(Items.dye, 2, 1), ruby, new ItemStack(Blocks.pumpkin));
    GameRegistry.addSmelting(Item.getItemFromBlock(rubyOre), new ItemStack(ruby), 1.0F);
    GameRegistry.registerFuelHandler(new Fuel());
    GameRegistry.registerWorldGenerator(new OreGen(), 0);
    EntityRegistry.registerModEntity(RubyGolem.class, "ruby_golem", 0, this, 64, 3, true);
    EntityRegistry.addSpawn(RubyGolem.class, 8, 1, 3, net.minecraft.entity.EnumCreatureType.monster);
  }
  static void register(Item i, String name) { GameRegistry.registerItem(i, name); }
}''',
    'rubymod/RubyPickaxe.java': '''package rubymod;
public class RubyPickaxe extends net.minecraft.item.ItemPickaxe { public RubyPickaxe(net.minecraft.item.Item.ToolMaterial m) { super(m); setUnlocalizedName("rubyPickaxe"); } }''',
    'rubymod/Wand.java': '''package rubymod;
import net.minecraft.item.*; import net.minecraft.entity.player.EntityPlayer; import net.minecraft.potion.PotionEffect;
public class Wand extends Item {
  public ItemStack onItemRightClick(ItemStack s, net.minecraft.world.World w, EntityPlayer p) {
    p.addPotionEffect(new PotionEffect(1, 200, 1));
    p.heal(4.0F);
    p.addChatMessage(new net.minecraft.util.ChatComponentText("Whoosh!"));
    return s;
  }
}''',
    'rubymod/Fuel.java': '''package rubymod;
import net.minecraft.item.ItemStack;
public class Fuel implements cpw.mods.fml.common.IFuelHandler {
  public int getBurnTime(ItemStack s) { if (s.getItem() == RubyMod.ruby) return 2400; return 0; } }''',
    'rubymod/OreGen.java': '''package rubymod;
import java.util.Random; import net.minecraft.world.World; import net.minecraft.world.gen.feature.WorldGenMinable;
public class OreGen implements cpw.mods.fml.common.IWorldGenerator {
  public void generate(Random r, int cx, int cz, World w, Object a, Object b) {
    for (int i = 0; i < 6; i++) {
      int x = cx * 16 + r.nextInt(16); int y = 10 + r.nextInt(30); int z = cz * 16 + r.nextInt(16);
      new WorldGenMinable(RubyMod.rubyOre, 5).generate(w, r, x, y, z);
    }
  } }''',
    'rubymod/RubyGolem.java': '''package rubymod;
import net.minecraft.entity.SharedMonsterAttributes;
public class RubyGolem extends net.minecraft.entity.monster.EntityZombie {
  public RubyGolem(net.minecraft.world.World w) { super(w); }
  protected void applyEntityAttributes() { super.applyEntityAttributes();
    getEntityAttribute(SharedMonsterAttributes.maxHealth).setBaseValue(40.0D);
    getEntityAttribute(SharedMonsterAttributes.movementSpeed).setBaseValue(0.3D); } }''',
}

# NeoForge 1.21-style (Mojang names)
NEO_STUBS = {
    'net/neoforged/neoforge/common/ModConfigSpec.java': '''package net.neoforged.neoforge.common;
import java.util.*; import java.util.function.*;
public class ModConfigSpec {
  public static class ConfigValue<T> { public T get() { return null; } }
  public static class IntValue extends ConfigValue<Integer> {} public static class LongValue extends ConfigValue<Long> {}
  public static class DoubleValue extends ConfigValue<Double> {} public static class BooleanValue extends ConfigValue<Boolean> {}
  public static class EnumValue<E extends Enum<E>> extends ConfigValue<E> {}
  public static class Builder {
    public Builder comment(String c) { return this; } public Builder comment(String... c) { return this; }
    public Builder translation(String k) { return this; } public Builder worldRestart() { return this; }
    public Builder push(String p) { return this; } public Builder push(List<String> p) { return this; }
    public Builder pop() { return this; } public Builder pop(int n) { return this; }
    public <T> ConfigValue<T> define(String p, T d) { return null; } public BooleanValue define(String p, boolean d) { return null; }
    public <T> ConfigValue<T> define(String p, Supplier<T> d, Predicate<Object> v) { return null; }
    public IntValue defineInRange(String p, int d, int lo, int hi) { return null; }
    public LongValue defineInRange(String p, long d, long lo, long hi) { return null; }
    public DoubleValue defineInRange(String p, double d, double lo, double hi) { return null; }
    public <E extends Enum<E>> EnumValue<E> defineEnum(String p, E d) { return null; }
    public <T> ConfigValue<List<? extends T>> defineList(String p, List<? extends T> d, Predicate<Object> v) { return null; }
    public ModConfigSpec build() { return null; }
  }
}''',
    'net/neoforged/neoforge/registries/DeferredRegister.java': '''package net.neoforged.neoforge.registries;
import java.util.function.Supplier;
public class DeferredRegister<T> { public static <T> DeferredRegister<T> create(Object reg, String ns) { return new DeferredRegister<>(); }
  public <I extends T> DeferredHolder<T, I> register(String n, Supplier<I> s) { return null; } }''',
    'net/neoforged/neoforge/registries/DeferredHolder.java': 'package net.neoforged.neoforge.registries; public class DeferredHolder<R, T> { public T get() { return null; } }',
    'net/minecraft/core/registries/Registries.java': 'package net.minecraft.core.registries; public class Registries { public static Object ITEM, BLOCK; }',
    'net/minecraft/world/item/Item.java': '''package net.minecraft.world.item;
public class Item { public Item(Properties p) {}
  public static class Properties { public Properties stacksTo(int n) { return this; } public Properties durability(int n) { return this; }
    public Properties food(net.minecraft.world.food.FoodProperties f) { return this; } } }''',
    'net/minecraft/world/food/FoodProperties.java': '''package net.minecraft.world.food;
public class FoodProperties { public static class Builder { public Builder nutrition(int n) { return this; } public Builder saturationModifier(float s) { return this; }
  public Builder alwaysEdible() { return this; } public FoodProperties build() { return new FoodProperties(); } } }''',
    'net/minecraft/world/level/block/Block.java': 'package net.minecraft.world.level.block; public class Block { public Block(net.minecraft.world.level.block.state.BlockBehaviour.Properties p) {} }',
    'net/minecraft/world/level/block/SoundType.java': 'package net.minecraft.world.level.block; public class SoundType { public static SoundType METAL, STONE; }',
    'net/minecraft/world/level/block/state/BlockBehaviour.java': '''package net.minecraft.world.level.block.state;
public class BlockBehaviour { public static class Properties { public static Properties of() { return new Properties(); }
  public Properties strength(float h, float r) { return this; } public Properties lightLevel(java.util.function.ToIntFunction<Object> f) { return this; }
  public Properties sound(net.minecraft.world.level.block.SoundType s) { return this; } public Properties requiresCorrectToolForDrops() { return this; } } }''',
}
NEO_MOD = {
    'gemmod/GemConfig.java': '''package gemmod;
import net.neoforged.neoforge.common.ModConfigSpec;
public class GemConfig {
  public final ModConfigSpec.IntValue lampLight;
  GemConfig(ModConfigSpec.Builder b) {
    b.push("lamp");
    lampLight = b.comment("Lamp light level").defineInRange("light", 15, 0, 15);
    b.pop();
    b.define("debug", false);
  }
  public static final ModConfigSpec.Builder B = new ModConfigSpec.Builder();
  public static final GemConfig CONFIG = new GemConfig(B);
}''',
    'gemmod/ModItems.java': '''package gemmod;
import net.neoforged.neoforge.registries.*; import net.minecraft.world.item.Item; import net.minecraft.world.level.block.*;
import net.minecraft.world.level.block.state.BlockBehaviour; import net.minecraft.world.food.FoodProperties; import net.minecraft.core.registries.Registries;
public class ModItems {
  public static final DeferredRegister<Item> ITEMS = DeferredRegister.create(Registries.ITEM, "gemmod");
  public static final DeferredRegister<Block> BLOCKS = DeferredRegister.create(Registries.BLOCK, "gemmod");
  public static final FoodProperties GEM_PIE = new FoodProperties.Builder().nutrition(7).saturationModifier(0.6F).alwaysEdible().build();
  public static final DeferredHolder<Item, Item> GEM = ITEMS.register("gem", () -> new Item(new Item.Properties().stacksTo(16)));
  public static final DeferredHolder<Item, Item> PIE = ITEMS.register("gem_pie", () -> new Item(new Item.Properties().food(GEM_PIE)));
  public static final DeferredHolder<Block, Block> LAMP = BLOCKS.register("gem_lamp",
      () -> new Block(BlockBehaviour.Properties.of().strength(2.5F, 9.0F).lightLevel(s -> 15).sound(SoundType.METAL).requiresCorrectToolForDrops()));
}''',
}


# Forge 1.12.2 (MCP stable_39 names, as in a deobfuscated dev jar): registry events, ITEMS lists, enums, sub-items,
# JSON recipes with data values and the ore dictionary
F112_STUBS = {
    'net/minecraftforge/common/config/Property.java': '''package net.minecraftforge.common.config;
public class Property { public int getInt() { return 0; } public boolean getBoolean() { return false; } public double getDouble() { return 0; } }''',
    'net/minecraftforge/common/config/Configuration.java': '''package net.minecraftforge.common.config;
public class Configuration { public Configuration(java.io.File f) {}
  public int getInt(String n, String c, int d, int lo, int hi, String cm) { return d; }
  public boolean getBoolean(String n, String c, boolean d, String cm) { return d; }
  public float getFloat(String n, String c, float d, float lo, float hi, String cm) { return d; }
  public String getString(String n, String c, String d, String cm) { return d; }
  public String[] getStringList(String n, String c, String[] d, String cm) { return d; }
  public Property get(String c, String k, int d, String cm, int lo, int hi) { return null; }
  public Property get(String c, String k, boolean d) { return null; }
  public Property get(String c, String k, double d, String cm) { return null; }
  public void save() {} }''',
    'net/minecraftforge/fml/common/registry/GameRegistry.java': """package net.minecraftforge.fml.common.registry;
public class GameRegistry { public static void addSmelting(net.minecraft.item.Item i, net.minecraft.item.ItemStack o, float xp) {} }""",
    'net/minecraftforge/registries/IForgeRegistry.java': 'package net.minecraftforge.registries; public interface IForgeRegistry<V> { void register(V v); void registerAll(V... v); }',
    'net/minecraftforge/event/RegistryEvent.java': """package net.minecraftforge.event;
public class RegistryEvent { public static class Register<T> { public net.minecraftforge.registries.IForgeRegistry<T> getRegistry() { return null; } } }""",
    'net/minecraftforge/fml/common/eventhandler/SubscribeEvent.java': """package net.minecraftforge.fml.common.eventhandler;
@java.lang.annotation.Retention(java.lang.annotation.RetentionPolicy.RUNTIME) public @interface SubscribeEvent {}""",
    'net/minecraftforge/oredict/OreDictionary.java': 'package net.minecraftforge.oredict; public class OreDictionary { public static void registerOre(String n, net.minecraft.item.Item i) {} }',
    'net/minecraftforge/oredict/ShapedOreRecipe.java': """package net.minecraftforge.oredict;
public class ShapedOreRecipe { public ShapedOreRecipe(net.minecraft.util.ResourceLocation g, net.minecraft.item.ItemStack o, Object... p) {}
  public ShapedOreRecipe setRegistryName(String n) { return this; } }""",
    'net/minecraftforge/common/util/EnumHelper.java': """package net.minecraftforge.common.util;
public class EnumHelper { public static net.minecraft.item.Item.ToolMaterial addToolMaterial(String n, int h, int u, float e, float d, int en) { return null; } }""",
    'net/minecraftforge/client/model/ModelLoader.java': """package net.minecraftforge.client.model;
public class ModelLoader { public static void setCustomModelResourceLocation(net.minecraft.item.Item i, int meta, net.minecraft.client.renderer.block.model.ModelResourceLocation m) {} }""",
    'net/minecraft/client/renderer/block/model/ModelResourceLocation.java': """package net.minecraft.client.renderer.block.model;
public class ModelResourceLocation { public ModelResourceLocation(String n, String v) {} }""",
    'net/minecraft/util/ResourceLocation.java': 'package net.minecraft.util; public class ResourceLocation { public ResourceLocation(String a, String b) {} public ResourceLocation(String a) {} }',
    'net/minecraft/util/NonNullList.java': """package net.minecraft.util;
public class NonNullList<E> extends java.util.AbstractList<E> { public E get(int i) { return null; } public int size() { return 0; } public boolean add(E e) { return true; } }""",
    'net/minecraft/item/Item.java': """package net.minecraft.item;
public class Item {
  public enum ToolMaterial { WOOD, STONE, IRON, DIAMOND, GOLD }
  public Item setTranslationKey(String s) { return this; } public Item setRegistryName(String n) { return this; }
  public net.minecraft.util.ResourceLocation getRegistryName() { return null; }
  public Item setCreativeTab(net.minecraft.creativetab.CreativeTabs t) { return this; } public Item setMaxStackSize(int n) { return this; }
  public Item setHasSubtypes(boolean b) { return this; } public Item setMaxDamage(int d) { return this; }
  public String getTranslationKey() { return ""; } public String getTranslationKey(ItemStack s) { return ""; }
  public void getSubItems(net.minecraft.creativetab.CreativeTabs tab, net.minecraft.util.NonNullList<ItemStack> items) {}
  public static Item getItemFromBlock(net.minecraft.block.Block b) { return null; } }""",
    'net/minecraft/item/ItemStack.java': """package net.minecraft.item;
public class ItemStack { public ItemStack(Item i) {} public ItemStack(Item i, int c) {} public ItemStack(Item i, int c, int m) {}
  public ItemStack(net.minecraft.block.Block b) {} public int getMetadata() { return 0; } public Item getItem() { return null; } }""",
    'net/minecraft/item/ItemSword.java': 'package net.minecraft.item; public class ItemSword extends Item { public ItemSword(Item.ToolMaterial m) {} }',
    'net/minecraft/item/ItemTool.java': 'package net.minecraft.item; public class ItemTool extends Item { protected ItemTool(float d, float s, Item.ToolMaterial m, java.util.Set<?> e) {} }',
    'net/minecraft/item/ItemAxe.java': 'package net.minecraft.item; public class ItemAxe extends ItemTool { protected ItemAxe(Item.ToolMaterial m, float d, float s) { super(d, s, m, null); } }',
    'net/minecraft/item/ItemFood.java': 'package net.minecraft.item; public class ItemFood extends Item { public ItemFood(int h, float s, boolean w) {} }',
    'net/minecraft/creativetab/CreativeTabs.java': 'package net.minecraft.creativetab; public class CreativeTabs { public static CreativeTabs MATERIALS, COMBAT, FOOD, TOOLS; }',
    'net/minecraft/init/Items.java': 'package net.minecraft.init; import net.minecraft.item.Item; public class Items { public static Item DIAMOND, STICK, IRON_INGOT, DYE; }',
    'net/minecraft/init/Blocks.java': 'package net.minecraft.init; import net.minecraft.block.Block; public class Blocks { public static Block STONE, OAK_FENCE, PUMPKIN; }',
    'net/minecraft/block/material/Material.java': 'package net.minecraft.block.material; public class Material { public static Material ROCK, IRON, PACKED_ICE; }',
    'net/minecraft/block/SoundType.java': 'package net.minecraft.block; public class SoundType { public static SoundType STONE, METAL; }',
    'net/minecraft/block/Block.java': """package net.minecraft.block;
public class Block { public Block(net.minecraft.block.material.Material m) {}
  public Block setHardness(float h) { return this; } public Block setResistance(float r) { return this; } public Block setLightLevel(float l) { return this; }
  public Block setSoundType(SoundType s) { return this; } public Block setTranslationKey(String n) { return this; } public Block setRegistryName(String n) { return this; }
  public void setHarvestLevel(String t, int l) {} }""",
    'net/minecraft/item/crafting/IRecipe.java': 'package net.minecraft.item.crafting; public interface IRecipe {}',
}
F112_MOD = {
    'tinmod/TinConfig.java': '''package tinmod;
import net.minecraftforge.common.config.Configuration;
public class TinConfig {
  public static int veins; public static boolean hard; public static float mult; public static String motd; public static String[] list;
  public static void load(java.io.File f) {
    Configuration c = new Configuration(f);
    veins = c.getInt("veinsPerChunk", "world gen", 6, 0, 32, "Tin veins per chunk");
    hard = c.getBoolean("hardMode", "general", false, "Harder recipes");
    mult = c.getFloat("xpMultiplier", "general", 1.5F, 0.0F, 10.0F, "");
    motd = c.getString("motd", "general", "Welcome", "Join message");
    list = c.getStringList("blacklist", "general", new String[] {"minecraft:stone", "minecraft:dirt"}, "Blocked");
    int legacy = c.get("general", "legacyCount", 3, "Old style", 1, 9).getInt();
    boolean b2 = c.get("general", "flag", true).getBoolean();
    c.save();
  }
}''',
    'tinmod/ModItems.java': """package tinmod;
import java.util.*; import net.minecraft.item.*; import net.minecraftforge.common.util.EnumHelper;
public class ModItems {
  public static final List<Item> ITEMS = new ArrayList<>();
  public static Item.ToolMaterial TIN_MAT = EnumHelper.addToolMaterial("TIN", 2, 300, 6.5F, 2.0F, 12);
  public static final Item TIN = new ItemBase("tin");
  public static final Item GEMS = new ItemGem("gem");
  public static final Item TIN_AXE = new TinAxe("tin_axe");
}""",
    'tinmod/ItemBase.java': """package tinmod;
import net.minecraft.item.Item; import net.minecraft.creativetab.CreativeTabs;
public class ItemBase extends Item { public ItemBase(String n) { setTranslationKey(n); setRegistryName(n); setCreativeTab(CreativeTabs.MATERIALS); ModItems.ITEMS.add(this); } }""",
    'tinmod/ItemGem.java': """package tinmod;
import net.minecraft.item.*; import net.minecraft.creativetab.CreativeTabs; import net.minecraft.util.NonNullList;
import net.minecraftforge.client.model.ModelLoader; import net.minecraft.client.renderer.block.model.ModelResourceLocation;
public class ItemGem extends ItemBase {
  static final String[] TYPES = {"ruby", "sapphire", "topaz"};
  public ItemGem(String n) { super(n); setHasSubtypes(true); }
  public String getTranslationKey(ItemStack s) { return super.getTranslationKey() + "." + TYPES[s.getMetadata()]; }
  public void getSubItems(CreativeTabs tab, NonNullList<ItemStack> items) { for (int i = 0; i < TYPES.length; i++) items.add(new ItemStack(this, 1, i)); }
  public void registerModels() {
    for (int i = 0; i < TYPES.length; i++) ModelLoader.setCustomModelResourceLocation(this, i, new ModelResourceLocation(getRegistryName() + "_" + TYPES[i], "inventory"));
  } }""",
    'tinmod/TinAxe.java': """package tinmod;
public class TinAxe extends net.minecraft.item.ItemAxe { public TinAxe(String n) { super(ModItems.TIN_MAT, 7.0F, -3.1F); setRegistryName(n); setTranslationKey(n); } }""",
    'tinmod/Metal.java': """package tinmod;
import net.minecraft.item.Item;
public enum Metal { COPPER("copper"), SILVER("silver");
  final Item ingot;
  Metal(String name) { ingot = new Item().setTranslationKey(name + "_ingot").setRegistryName(name + "_ingot"); } }""",
    'tinmod/ModBlocks.java': """package tinmod;
import net.minecraft.block.*; import net.minecraft.block.material.Material;
public class ModBlocks { public static final Block TIN_BLOCK = make();
  static Block make() { Block b = new Block(Material.ROCK).setHardness(3.0F).setResistance(10.0F).setSoundType(SoundType.METAL).setLightLevel(0.5F)
      .setTranslationKey("tin_block").setRegistryName("tin_block"); b.setHarvestLevel("pickaxe", 1); return b; } }""",
    'tinmod/Registration.java': """package tinmod;
import net.minecraft.item.*; import net.minecraft.block.Block; import net.minecraft.item.crafting.IRecipe; import net.minecraft.util.ResourceLocation;
import net.minecraftforge.event.RegistryEvent; import net.minecraftforge.fml.common.eventhandler.SubscribeEvent;
import net.minecraftforge.oredict.*; import net.minecraftforge.fml.common.registry.GameRegistry;
public class Registration {
  @SubscribeEvent public static void items(RegistryEvent.Register<Item> e) {
    e.getRegistry().registerAll(ModItems.ITEMS.toArray(new Item[0]));
    for (Metal m : Metal.values()) e.getRegistry().register(m.ingot);
    e.getRegistry().register(new ItemSword(ModItems.TIN_MAT).setRegistryName("tin_sword").setTranslationKey("tin_sword"));
  }
  @SubscribeEvent public static void blocks(RegistryEvent.Register<Block> e) { e.getRegistry().register(ModBlocks.TIN_BLOCK); }
  @SubscribeEvent public static void recipes(RegistryEvent.Register<IRecipe> e) {
    e.getRegistry().register((IRecipe) (Object) new ShapedOreRecipe(new ResourceLocation("tinmod", "g"), new ItemStack(ModItems.GEMS, 1, 2), "TT", "TT", 'T', "ingotTin").setRegistryName("topaz"));
  }
  public static void init() {
    OreDictionary.registerOre("ingotTin", ModItems.TIN);
    GameRegistry.addSmelting(Item.getItemFromBlock(ModBlocks.TIN_BLOCK), new ItemStack(ModItems.GEMS, 1, 1), 0.5F);
  } }""",
}

# Forge 1.14.4 (MCP stable_58 names): Item.Properties / Food.Builder / Block.Properties
F114_STUBS = {
    'net/minecraftforge/common/ForgeConfigSpec.java': '''package net.minecraftforge.common;
import java.util.*; import java.util.function.*;
public class ForgeConfigSpec {
  public static class ConfigValue<T> { public T get() { return null; } }
  public static class IntValue extends ConfigValue<Integer> {} public static class LongValue extends ConfigValue<Long> {}
  public static class DoubleValue extends ConfigValue<Double> {} public static class BooleanValue extends ConfigValue<Boolean> {}
  public static class EnumValue<E extends Enum<E>> extends ConfigValue<E> {}
  public static class Builder {
    public Builder comment(String c) { return this; } public Builder comment(String... c) { return this; }
    public Builder translation(String k) { return this; } public Builder worldRestart() { return this; }
    public Builder push(String p) { return this; } public Builder push(List<String> p) { return this; }
    public Builder pop() { return this; } public Builder pop(int n) { return this; }
    public <T> ConfigValue<T> define(String p, T d) { return null; } public BooleanValue define(String p, boolean d) { return null; }
    public <T> ConfigValue<T> define(String p, Supplier<T> d, Predicate<Object> v) { return null; }
    public IntValue defineInRange(String p, int d, int lo, int hi) { return null; }
    public LongValue defineInRange(String p, long d, long lo, long hi) { return null; }
    public DoubleValue defineInRange(String p, double d, double lo, double hi) { return null; }
    public <E extends Enum<E>> EnumValue<E> defineEnum(String p, E d) { return null; }
    public <T> ConfigValue<List<? extends T>> defineList(String p, List<? extends T> d, Predicate<Object> v) { return null; }
    public ForgeConfigSpec build() { return null; }
  }
}''',
    'net/minecraftforge/registries/IForgeRegistry.java': 'package net.minecraftforge.registries; public interface IForgeRegistry<V> { void register(V v); void registerAll(V... v); }',
    'net/minecraftforge/event/RegistryEvent.java': """package net.minecraftforge.event;
public class RegistryEvent { public static class Register<T> { public net.minecraftforge.registries.IForgeRegistry<T> getRegistry() { return null; } } }""",
    'net/minecraftforge/eventbus/api/SubscribeEvent.java': """package net.minecraftforge.eventbus.api;
@java.lang.annotation.Retention(java.lang.annotation.RetentionPolicy.RUNTIME) public @interface SubscribeEvent {}""",
    'net/minecraftforge/common/ToolType.java': 'package net.minecraftforge.common; public class ToolType { public static ToolType PICKAXE, AXE; }',
    'net/minecraft/item/ItemGroup.java': 'package net.minecraft.item; public class ItemGroup { public static ItemGroup MATERIALS, FOOD, COMBAT; }',
    'net/minecraft/item/Food.java': """package net.minecraft.item;
public class Food { public static class Builder { public Builder hunger(int n) { return this; } public Builder saturation(float s) { return this; }
  public Builder setAlwaysEdible() { return this; } public Builder fastToEat() { return this; } public Food build() { return new Food(); } } }""",
    'net/minecraft/item/Item.java': """package net.minecraft.item;
public class Item { public Item(Properties p) {} public Item setRegistryName(String n) { return this; }
  public static class Properties { public Properties group(ItemGroup g) { return this; } public Properties maxStackSize(int n) { return this; }
    public Properties maxDamage(int n) { return this; } public Properties food(Food f) { return this; } } }""",
    'net/minecraft/item/IItemTier.java': 'package net.minecraft.item; public interface IItemTier {}',
    'net/minecraft/item/ItemTier.java': 'package net.minecraft.item; public enum ItemTier implements IItemTier { WOOD, STONE, IRON, DIAMOND, GOLD }',
    'net/minecraft/item/SwordItem.java': 'package net.minecraft.item; public class SwordItem extends Item { public SwordItem(IItemTier t, int d, float s, Item.Properties p) { super(p); } }',
    'net/minecraft/item/BlockItem.java': 'package net.minecraft.item; public class BlockItem extends Item { public BlockItem(net.minecraft.block.Block b, Item.Properties p) { super(p); } }',
    'net/minecraft/block/material/Material.java': 'package net.minecraft.block.material; public class Material { public static Material ROCK, IRON, EARTH; }',
    'net/minecraft/block/SoundType.java': 'package net.minecraft.block; public class SoundType { public static SoundType STONE, METAL; }',
    'net/minecraft/block/Block.java': """package net.minecraft.block;
public class Block { public Block(Properties p) {} public Block setRegistryName(String n) { return this; }
  public static class Properties { public static Properties create(net.minecraft.block.material.Material m) { return new Properties(); }
    public Properties hardnessAndResistance(float h, float r) { return this; } public Properties lightValue(int l) { return this; }
    public Properties sound(SoundType s) { return this; } public Properties harvestLevel(int l) { return this; }
    public Properties harvestTool(net.minecraftforge.common.ToolType t) { return this; } } }""",
}
F114_MOD = {
    'opalmod/OpalConfig.java': '''package opalmod;
import net.minecraftforge.common.ForgeConfigSpec; import java.util.*;
public class OpalConfig {
  public enum Mode { FAST, FANCY }
  private static final ForgeConfigSpec.Builder BUILDER = new ForgeConfigSpec.Builder();
  public static final ForgeConfigSpec.IntValue ORE_RATE; public static final ForgeConfigSpec.DoubleValue SPEED;
  public static final ForgeConfigSpec.BooleanValue ENABLE_WAND; public static final ForgeConfigSpec.ConfigValue<String> GREETING;
  public static final ForgeConfigSpec.EnumValue<Mode> MODE; public static final ForgeConfigSpec.ConfigValue<List<? extends String>> ORES;
  public static final ForgeConfigSpec.LongValue BIG; public static final ForgeConfigSpec.ConfigValue<Integer> BOXED;
  public static final ForgeConfigSpec.ConfigValue<String> COMPUTED;
  public static final ForgeConfigSpec SPEC;
  static {
    BUILDER.comment("General settings").push("general");
    ORE_RATE = BUILDER.comment("Opal veins per chunk", "Set to 1 for rare ore").defineInRange("oreRate", 8, 1, 64);
    SPEED = BUILDER.defineInRange("speed", 1.25D, 0.1D, Double.MAX_VALUE);
    ENABLE_WAND = BUILDER.comment("Register the wand").define("enableWand", true);
    GREETING = BUILDER.define("greeting", "Hello \\"opal\\"");
    BOXED = BUILDER.define("boxed", 7);
    BUILDER.push("client.render");
    MODE = BUILDER.defineEnum("mode", Mode.FANCY);
    BUILDER.pop(2);
    ORES = BUILDER.defineList("ores", Arrays.asList("minecraft:iron_ore", "minecraft:gold_ore"), o -> true);
    BIG = BUILDER.defineInRange("big", 5L, 0L, Long.MAX_VALUE);
    COMPUTED = BUILDER.define("computed", () -> "x", o -> true);
    SPEC = BUILDER.build();
  }
}''',
    'opalmod/Reg.java': """package opalmod;
import net.minecraft.item.*; import net.minecraft.block.Block; import net.minecraftforge.event.RegistryEvent; import net.minecraftforge.eventbus.api.SubscribeEvent;
public class Reg {
  @SubscribeEvent public static void items(RegistryEvent.Register<Item> e) {
    e.getRegistry().registerAll(
      new Item(new Item.Properties().group(ItemGroup.MATERIALS).maxStackSize(16)).setRegistryName("opal"),
      new Item(new Item.Properties().group(ItemGroup.FOOD).food(new Food.Builder().hunger(5).saturation(0.6F).setAlwaysEdible().build())).setRegistryName("opal_apple"),
      new SwordItem(ItemTier.IRON, 3, -2.4F, new Item.Properties().group(ItemGroup.COMBAT)).setRegistryName("opal_sword"),
      new BlockItem(OpalBlocks.OPAL_BLOCK, new Item.Properties()).setRegistryName("opal_block"));
  }
  @SubscribeEvent public static void blocks(RegistryEvent.Register<Block> e) { e.getRegistry().register(OpalBlocks.OPAL_BLOCK); }
}""",
    'opalmod/OpalBlocks.java': """package opalmod;
import net.minecraft.block.*; import net.minecraft.block.material.Material; import net.minecraftforge.common.ToolType;
public class OpalBlocks { public static final Block OPAL_BLOCK = new Block(Block.Properties.create(Material.IRON).hardnessAndResistance(5.0F, 6.0F)
    .lightValue(7).sound(SoundType.METAL).harvestLevel(2).harvestTool(ToolType.PICKAXE)).setRegistryName("opal_block"); }""",
}

def javac(srcs, out):
    src = out / 'src'
    for rel, text in srcs.items():
        p = src / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    cls = out / 'classes'
    cls.mkdir(parents=True, exist_ok=True)
    files = [str(p) for p in src.rglob('*.java')]
    r = subprocess.run([JAVAC, '-nowarn', '-source', '8', '-target', '8', '-d', str(cls), *files], capture_output=True, text=True)
    if r.returncode != 0:
        r = subprocess.run([JAVAC, '-nowarn', '--release', '8', '-d', str(cls), *files], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr)
    return cls


def png(color=(200, 30, 60, 255), size=(16, 16)):
    from PIL import Image
    b = io.BytesIO()
    Image.new('RGBA', size, color).save(b, 'PNG')
    return b.getvalue()


def make_jar(path, classes_dir, mod_prefixes, extra):
    with zipfile.ZipFile(path, 'w') as z:
        for p in sorted(classes_dir.rglob('*.class')):
            rel = p.relative_to(classes_dir).as_posix()
            if rel.startswith(mod_prefixes):
                z.write(p, rel)
        for n, data in extra.items():
            z.writestr(n, data)
    return path


_built = {}


def old_jar():
    if 'old' not in _built:
        d = BASE / 'old_build'
        shutil.rmtree(d, ignore_errors=True)
        cls = javac({**OLD_STUBS, **OLD_MOD}, d)
        extra = {'mcmod.info': json.dumps([{'modid': 'rubymod', 'name': 'Ruby Mod', 'version': '1.2', 'mcversion': '1.7.10'}]),
                 'assets/rubymod/lang/en_US.lang': 'item.ruby.name=Ruby\nitem.rubyApple.name=Ruby Apple\ntile.rubyOre.name=Ruby Ore\nentity.ruby_golem.name=Ruby Golem\n'}
        for n in ('ruby', 'ruby_apple', 'ruby_sword', 'ruby_pickaxe', 'ruby_helmet', 'wand'):
            extra[f'assets/rubymod/textures/items/{n}.png'] = png()
        for n in ('ruby_ore', 'ruby_block'):
            extra[f'assets/rubymod/textures/blocks/{n}.png'] = png((90, 90, 90, 255))
        extra['assets/rubymod/textures/entity/ruby_golem.png'] = png(size=(64, 64))
        extra['assets/rubymod/shaders/post/example.json'] = '{}'
        _built['old'] = (make_jar(d / 'rubymod.jar', cls, ('rubymod/',), extra), cls)
    return _built['old']


def neo_jar():
    if 'neo' not in _built:
        d = BASE / 'neo_build'
        shutil.rmtree(d, ignore_errors=True)
        cls = javac({**NEO_STUBS, **NEO_MOD}, d)
        toml = '''modLoader="javafml"\nloaderVersion="[4,)"\n[[mods]]\nmodId="gemmod"\nversion="2.0.1"\ndisplayName="Gem Mod"\n
[[dependencies.gemmod]]\nmodId="minecraft"\ntype="required"\nversionRange="[1.21.1,1.21.2)"\n'''
        extra = {'META-INF/neoforge.mods.toml': toml,
                 'assets/gemmod/lang/en_us.json': json.dumps({'item.gemmod.gem': 'Gem', 'item.gemmod.gem_pie': 'Gem Pie', 'block.gemmod.gem_lamp': 'Gem Lamp'}),
                 'assets/gemmod/models/item/gem.json': json.dumps({'parent': 'item/generated', 'textures': {'layer0': 'gemmod:item/gem'}}),
                 'assets/gemmod/models/item/gem_pie.json': json.dumps({'parent': 'item/generated', 'textures': {'layer0': 'gemmod:item/gem_pie'}}),
                 'assets/gemmod/blockstates/gem_lamp.json': json.dumps({'variants': {'': {'model': 'gemmod:block/gem_lamp'}}}),
                 'assets/gemmod/models/block/gem_lamp.json': json.dumps({'parent': 'block/cube_all', 'textures': {'all': 'gemmod:block/gem_lamp'}}),
                 'assets/gemmod/textures/item/gem.png': png(), 'assets/gemmod/textures/item/gem_pie.png': png((250, 200, 0, 255)),
                 'assets/gemmod/textures/block/gem_lamp.png': png((250, 250, 100, 255)),
                 'data/gemmod/recipe/gem_lamp.json': json.dumps({'type': 'minecraft:crafting_shaped', 'pattern': ['GGG', 'GTG', 'GGG'],
                                                                 'key': {'G': 'gemmod:gem', 'T': 'minecraft:torch'}, 'result': {'id': 'gemmod:gem_lamp'}}),
                 'data/gemmod/recipe/gem_from_honey.json': json.dumps({'type': 'minecraft:crafting_shapeless', 'ingredients': ['minecraft:honeycomb'],
                                                                      'result': {'id': 'gemmod:gem'}}),
                 'data/gemmod/recipe/gem_pie.json': json.dumps({'type': 'minecraft:crafting_shapeless', 'ingredients': ['gemmod:gem', '#c:eggs', 'minecraft:sugar'],
                                                               'result': {'id': 'gemmod:gem_pie'}}),
                 'data/gemmod/loot_table/blocks/gem_lamp.json': json.dumps({'pools': [{'rolls': 1, 'entries': [{'type': 'minecraft:item', 'name': 'gemmod:gem',
                                                                                                                 'functions': [{'function': 'minecraft:set_count', 'count': {'min': 2, 'max': 4}}]}]}]}),
                 'data/minecraft/tags/block/mineable/pickaxe.json': json.dumps({'values': ['gemmod:gem_lamp']}),
                 'data/minecraft/tags/block/needs_iron_tool.json': json.dumps({'values': ['gemmod:gem_lamp']})}
        _built['neo'] = (make_jar(d / 'gemmod.jar', cls, ('gemmod/',), extra), cls)
    return _built['neo']


def f112_jar():
    if 'f112' not in _built:
        d = BASE / 'f112_build'
        shutil.rmtree(d, ignore_errors=True)
        cls = javac({**F112_STUBS, **F112_MOD}, d)
        extra = {'mcmod.info': json.dumps([{'modid': 'tinmod', 'name': 'Tin Mod', 'version': '1.0', 'mcversion': '1.12.2'}]),
                 'assets/tinmod/lang/en_us.lang': 'item.tin.name=Tin Ingot\nitem.gem.ruby.name=Ruby\nitem.gem.sapphire.name=Sapphire\n'
                                                  'item.gem.topaz.name=Topaz\ntile.tin_block.name=Block of Tin\nitem.copper_ingot.name=Copper Ingot\n',
                 'assets/tinmod/recipes/sapphire_dye.json': json.dumps({'type': 'minecraft:crafting_shaped', 'pattern': ['GG', 'GG'],
                                                                         'key': {'G': {'item': 'tinmod:gem', 'data': 1}},
                                                                         'result': {'item': 'minecraft:dye', 'data': 4, 'count': 2}}),
                 'assets/tinmod/recipes/tin_fence.json': json.dumps({'type': 'forge:ore_shaped', 'pattern': ['TFT'],
                                                                      'key': {'T': {'type': 'forge:ore_dict', 'ore': 'ingotTin'},
                                                                              'F': {'item': 'minecraft:fence'}},
                                                                      'result': {'item': 'tinmod:tin_block'}})}
        for n in ('tin', 'gem_ruby', 'gem_sapphire', 'gem_topaz', 'tin_axe', 'copper_ingot', 'silver_ingot', 'tin_sword'):
            extra[f'assets/tinmod/textures/items/{n}.png'] = png((30 * len(n) % 255, 90, 200, 255))
            extra[f'assets/tinmod/models/item/{n}.json'] = json.dumps({'parent': 'item/generated', 'textures': {'layer0': f'tinmod:items/{n}'}})
        extra['assets/tinmod/textures/blocks/tin_block.png'] = png((180, 180, 190, 255))
        _built['f112'] = (make_jar(d / 'tinmod.jar', cls, ('tinmod/',), extra), cls)
    return _built['f112']


def f114_jar():
    if 'f114' not in _built:
        d = BASE / 'f114_build'
        shutil.rmtree(d, ignore_errors=True)
        cls = javac({**F114_STUBS, **F114_MOD}, d)
        toml = 'modLoader="javafml"\nloaderVersion="[28,)"\n[[mods]]\nmodId="opalmod"\nversion="1.0.0"\ndisplayName="Opal Mod"\n'
        extra = {'META-INF/mods.toml': toml,
                 'assets/opalmod/lang/en_us.json': json.dumps({'item.opalmod.opal': 'Opal', 'item.opalmod.opal_apple': 'Opal Apple',
                                                              'item.opalmod.opal_sword': 'Opal Sword', 'block.opalmod.opal_block': 'Block of Opal'}),
                 'assets/opalmod/blockstates/opal_block.json': json.dumps({'variants': {'': {'model': 'opalmod:block/opal_block'}}}),
                 'assets/opalmod/models/block/opal_block.json': json.dumps({'parent': 'block/cube_all', 'textures': {'all': 'opalmod:block/opal_block'}}),
                 'assets/opalmod/textures/block/opal_block.png': png((240, 240, 255, 255)),
                 'data/opalmod/recipes/opal_block.json': json.dumps({'type': 'minecraft:crafting_shaped', 'pattern': ['OOO', 'OOO', 'OOO'],
                                                                     'key': {'O': {'item': 'opalmod:opal'}}, 'result': {'item': 'opalmod:opal_block'}}),
                 'data/opalmod/recipes/opal_sword.json': json.dumps({'type': 'minecraft:crafting_shaped', 'pattern': ['O', 'O', 'N'],
                                                                     'key': {'O': {'item': 'opalmod:opal'}, 'N': {'item': 'minecraft:nether_brick'}},
                                                                     'result': {'item': 'opalmod:opal_sword'}})}
        for n in ('opal', 'opal_apple', 'opal_sword'):
            extra[f'assets/opalmod/models/item/{n}.json'] = json.dumps({'parent': 'item/generated', 'textures': {'layer0': f'opalmod:item/{n}'}})
            extra[f'assets/opalmod/textures/item/{n}.png'] = png((200, 40 * len(n) % 255, 220, 255))
        _built['f114'] = (make_jar(d / 'opalmod.jar', cls, ('opalmod/',), extra), cls)
    return _built['f114']


def port(jar, name, **kw):
    out = BASE / 'ports' / name
    p = P.Porter(jar, out, work=BASE / 'work' / name, **kw)
    m = p.run()
    return p, m, out


@unittest.skipUnless(JAVAC, 'javac not available')
class ClassParserTests(unittest.TestCase):
    def test_parse_and_decode(self):
        src = {'t/Ops.java': '''package t;
public class Ops {
  static final long BIG = 1234567890123L; static double D = 2.5;
  int sw(int x) { switch (x) { case 1: return 10; case 2: return 20; case 3: return 30; default: return 0; } }
  int lk(int x) { switch (x) { case 1: return 1; case 1000: return 2; case -50000: return 3; default: return 4; } }
  int wide() { int a0=0,a1=1,a2=2,a3=3,a4=4,a5=5,a6=6,a7=7,a8=8,a9=9; long[] big = new long[300]; int x = 0;
    for (int i = 0; i < 260; i++) x += i; int v255=0; v255 += 200; return x + a9; }
  String cat(String s, int n) { return "a" + s + n; }
  Runnable lam() { return () -> System.out.println("hi"); }
}'''}
        cls = javac(src, BASE / 'ops')
        cf = CF.ClassFile((cls / 't' / 'Ops.class').read_bytes())
        self.assertEqual(cf.name, 't/Ops')
        self.assertEqual(cf.super_name, 'java/lang/Object')
        names = {m.name for m in cf.methods}
        self.assertTrue({'sw', 'lk', 'wide', 'cat', 'lam', '<init>', '<clinit>'} <= names)
        for m in cf.methods:
            if m.code is None:
                continue
            ins = m.code.insns(cf.cp)
            self.assertEqual(sum(i.length for i in ins), len(m.code.code), m.name)
            self.assertEqual([i.pc for i in ins], sorted(i.pc for i in ins))
        ops = {i.op for i in cf.method('sw').code.insns(cf.cp)} | {i.op for i in cf.method('lk').code.insns(cf.cp)}
        self.assertIn('tableswitch', ops)
        self.assertIn('lookupswitch', ops)
        lk = next(i for i in cf.method('lk').code.insns(cf.cp) if i.op == 'lookupswitch')
        self.assertEqual(sorted(lk.arg), [-50000, 1, 1000])
        clinit = [i for i in cf.method('<clinit>').code.insns(cf.cp)]
        self.assertTrue(any(i.op == 'ldc2_w' and i.arg == ('double', 2.5) for i in clinit))
        self.assertTrue(any(i.op == 'invokedynamic' for i in cf.method('lam').code.insns(cf.cp)))
        self.assertTrue(cf.bootstrap)

    def test_bad_input(self):
        for data in (b'', b'\xca\xfe\xba\xbe', b'\x00' * 40, b'\xca\xfe\xba\xbe\x00\x00\x00\x34\xff\xff' + b'\x01' * 20):
            with self.assertRaises(CF.ClassFormatError):
                CF.ClassFile(data)

    def test_mutf8(self):
        self.assertEqual(CF.mutf8(b'abc'), 'abc')
        self.assertEqual(CF.mutf8(b'a\xc0\x80b'), 'a\x00b')
        self.assertEqual(CF.mutf8(b'\xed\xa0\xbd\xed\xb8\x80'), '\U0001f600')

    def test_descriptors(self):
        self.assertEqual(CF.parse_method_desc('(I[JLjava/lang/String;[[Lx/Y;D)V'), (['I', '[J', 'Ljava/lang/String;', '[[Lx/Y;', 'D'], 'V'))
        self.assertEqual(CF.desc_class('Lnet/x/Y;'), 'net/x/Y')
        self.assertIsNone(CF.desc_class('[I'))


@unittest.skipUnless(JAVAC, 'javac not available')
class BlockShapeTests(unittest.TestCase):
    """API 1.7: constant getBoundingBox / getShape outlines and vanilla slab-like bases -> mod.json "box"."""
    def setUp(self):
        src = {'net/minecraft/util/math/AxisAlignedBB.java': 'package net.minecraft.util.math; public class AxisAlignedBB { public AxisAlignedBB(double a, double b, double c, double d, double e, double f) {} }',
               'net/minecraft/block/Block.java': 'package net.minecraft.block; public class Block { public static Object makeCuboidShape(double a, double b, double c, double d, double e, double f) { return null; } }',
               'net/minecraft/block/BlockSlab.java': 'package net.minecraft.block; public class BlockSlab extends Block {}',
               's/Plate.java': """package s; import net.minecraft.util.math.AxisAlignedBB;
public class Plate extends net.minecraft.block.Block { static final AxisAlignedBB AABB = new AxisAlignedBB(0.0D, 0.0D, 0.0D, 1.0D, 0.25D, 1.0D);
  public AxisAlignedBB getBoundingBox(Object state, Object w, Object pos) { return AABB; } }""",
               's/Post.java': """package s;
public class Post extends net.minecraft.block.Block { static final Object SHAPE = net.minecraft.block.Block.makeCuboidShape(6, 0, 6, 10, 16, 10);
  public Object getShape(Object state, Object w, Object pos, Object ctx) { return SHAPE; } }""",
               's/Odd.java': """package s; import net.minecraft.util.math.AxisAlignedBB;
public class Odd extends net.minecraft.block.Block {
  public AxisAlignedBB getBoundingBox(Object state, Object w, Object pos) { return state == w ? new AxisAlignedBB(0, 0, 0, 1, 1, 1) : new AxisAlignedBB(0, 0, 0, 1, 0.5, 1); } }""",
               's/Slab.java': 'package s; public class Slab extends net.minecraft.block.BlockSlab {}'}
        cls = javac(src, BASE / 'shapes')
        classes = {}
        for p in (cls / 's').rglob('*.class'):   # mod classes only: game calls stay symbolic, as in a real jar
            cf = CF.ClassFile(p.read_bytes())
            classes[cf.name] = cf
        self.prog = I.Program(classes)

    def shape(self, name, bases=()):
        return DS.block_shape(self.prog, f's/{name}', list(bases), name)

    def test_constant_boxes(self):
        f, notes = self.shape('Plate')
        self.assertEqual(f, {'box': [0, 0, 0, 16, 4, 16]})
        f, notes = self.shape('Post')
        self.assertEqual(f, {'box': [6, 0, 6, 10, 16, 10]})

    def test_state_dependent_and_bases(self):
        f, notes = self.shape('Odd')
        self.assertNotIn('box', f)
        self.assertTrue(any(k == 'manual' for k, _ in notes))
        f, notes = self.shape('Slab', ['Slab', 'BlockSlab', 'Block'])
        self.assertEqual(f, {'box': [0, 0, 0, 16, 8, 16]})
        f, _ = DS.block_shape(self.prog, 's/Slab', ['PressurePlateBlock'], 'p')
        self.assertEqual(f, {'box': [1, 0, 1, 15, 1, 15], 'no_collision': True})


@unittest.skipUnless(JAVAC, 'javac not available')
class ThrowableTests(unittest.TestCase):
    """API 1.9: an item use that spawns an EntityThrowable subclass -> mod.json "throwable" (speed, damage, fire, explosion, effects)."""
    def setUp(self):
        src = {'net/minecraft/world/World.java': 'package net.minecraft.world; public class World { public boolean spawnEntity(Object e) { return true; } '
                                                 'public Object createExplosion(Object e, double x, double y, double z, float s, boolean b) { return null; } }',
               'net/minecraft/entity/Entity.java': 'package net.minecraft.entity; public class Entity { public net.minecraft.world.World world; public double posX, posY, posZ; '
                                                   'public boolean attackEntityFrom(Object src, float amount) { return true; } public void setFire(int s) {} '
                                                   'public void addPotionEffect(Object e) {} }',
               'net/minecraft/entity/projectile/EntityThrowable.java': 'package net.minecraft.entity.projectile; public class EntityThrowable extends net.minecraft.entity.Entity { '
                                                                      'public EntityThrowable(Object w, Object thrower) {} '
                                                                      'public void shoot(Object e, float p, float y, float z, float v, float i) {} protected void onImpact(Object r) {} }',
               'net/minecraft/potion/PotionEffect.java': 'package net.minecraft.potion; public class PotionEffect { public PotionEffect(Object p, int d, int a) {} }',
               'net/minecraft/init/MobEffects.java': 'package net.minecraft.init; public class MobEffects { public static Object SLOWNESS; }',
               'net/minecraft/item/Item.java': 'package net.minecraft.item; public class Item {}',
               's/Shard.java': """package s;
public class Shard extends net.minecraft.entity.projectile.EntityThrowable {
  public Shard(Object w, Object t) { super(w, t); }
  protected void onImpact(Object r) {
    net.minecraft.entity.Entity hit = (net.minecraft.entity.Entity) r;
    hit.attackEntityFrom(null, 4.5F); hit.setFire(3);
    hit.addPotionEffect(new net.minecraft.potion.PotionEffect(net.minecraft.init.MobEffects.SLOWNESS, 100, 1));
    world.createExplosion(this, posX, posY, posZ, 1.5F, true); } }""",
               's/ShardItem.java': """package s;
public class ShardItem extends net.minecraft.item.Item {
  public Object onItemRightClick(net.minecraft.world.World w, Object player, Object hand) {
    Shard e = new Shard(w, player); e.shoot(player, 0F, 0F, 0F, 2.5F, 0.5F); w.spawnEntity(e); return null; } }""",
               's/Plain.java': """package s;
public class Plain extends net.minecraft.item.Item {
  public Object onItemRightClick(net.minecraft.world.World w, Object player, Object hand) { return null; } }"""}
        cls = javac(src, BASE / 'throwables')
        classes = {}
        for p in (cls / 's').rglob('*.class'):   # mod classes only, as in a real jar
            cf = CF.ClassFile(p.read_bytes())
            classes[cf.name] = cf
        self.prog = I.Program(classes)
        self.rr = types.SimpleNamespace(prog=self.prog, regmap={}, projectiles={}, notes=[])
        self.rr.note = lambda kind, msg: self.rr.notes.append((kind, msg))

    def test_throwable_fields(self):
        t, proj, notes = DS.throwable_info(self.prog, 's/ShardItem', self.rr, 'item shard')
        self.assertEqual(proj, 's/Shard')
        self.assertEqual(t, {'speed': 2.5, 'inaccuracy': 0.5, 'damage': 4.5, 'fire_ticks': 60, 'explode': 1.5,
                             'effects': [{'effect': 'minecraft:slowness', 'duration': 100, 'amplifier': 1}]})
        self.assertTrue(any(k == 'auto' and 'snowball' in m for k, m in notes))

    def test_no_projectile(self):
        self.assertEqual(DS.throwable_info(self.prog, 's/Plain', self.rr, 'item plain'), (None, None, []))


@unittest.skipUnless(JAVAC, 'javac not available')
class InterpreterTests(unittest.TestCase):
    def setUp(self):
        src = {'q/A.java': '''package q;
public class A {
  public static Object O; public static String S; public static int N; public static Object FROM_HELPER;
  static { O = new B(7, "seven", 2.5F).with(3).with(4); S = "x" + 5 + "y"; N = (3 + 4) * 2 - 1; FROM_HELPER = help("h", 9); }
  static Object help(String s, int k) { return new B(k, s.toUpperCase(), 1.0F); }
  public static java.util.function.Supplier<Object> sup() { return () -> new B(1, "lam", 0.5F); }
  public static String sb(int v) { StringBuilder b = new StringBuilder(); b.append("v="); b.append(v); return b.toString(); }
}''', 'q/B.java': '''package q;
public class B { int k; public B(int k, String s, float f) { this.k = k; init(); } void init() {} public B with(int x) { return this; } }'''}
        cls = javac(src, BASE / 'interp')
        classes = {}
        for p in cls.rglob('*.class'):
            cf = CF.ClassFile(p.read_bytes())
            classes[cf.name] = cf
        self.prog = I.Program(classes)

    def test_constants_and_new(self):
        p = self.prog
        p.run_root('q/A', '<clinit>', '()V')
        o = p.statics[('q/A', 'O')]
        self.assertIsInstance(o, I.Obj)
        self.assertEqual(o.cls, 'q/B')
        self.assertEqual([a.v for a in o.args], [7, 'seven', 2.5])
        self.assertEqual([a[0].v for n, a in o.all_calls('with')], [3, 4])
        self.assertEqual(o.fields['k'].v, 7)          # constructor ran with the real arguments
        self.assertEqual(p.statics[('q/A', 'S')].v, 'x5y')
        self.assertEqual(p.statics[('q/A', 'N')].v, 13)
        h = p.statics[('q/A', 'FROM_HELPER')]       # helper inlined with constant args
        self.assertEqual([a.v for a in h.args], [9, 'H', 1.0])

    def test_lambda_and_stringbuilder(self):
        p = self.prog
        fr = p.run_root('q/A', 'sup', '()Ljava/util/function/Supplier;')
        lam = fr.returns[0][1]
        self.assertIsInstance(lam, I.Lambda)
        v = p.invoke_lambda(lam, [], 0)
        self.assertEqual(v.args[1].v, 'lam')
        fr = p.run_root('q/A', 'sb', '(I)Ljava/lang/String;')
        self.assertIsInstance(fr.returns[0][1], I.StrCat)

    def test_f32(self):
        self.assertEqual(I.f32(0.6000000238418579), 0.6)
        self.assertEqual(I.f32(0.30000001192092896), 0.3)


class VanillaTests(unittest.TestCase):
    def test_extra_targets_exist(self):
        bad = [(k, v) for k, v in VN.EXTRA_JAVA_TO_LCE.items() if v.split('@')[0] not in VN.VANILLA]
        self.assertEqual(bad, [])
        bad = [(k, v) for k, v in VN.OLD_TO_LCE.items() if v.split('@')[0] not in VN.VANILLA]
        self.assertEqual(bad, [])

    def test_content_level(self):
        """1.13 Aquatic and 1.14 Village & Pillage are in 1.95; 1.15 bees and 1.16 nether are not."""
        bad = [r for r in lce_autoport.content_table() if not r[4]]
        self.assertEqual(bad, [])
        r = VN.VanillaResolver('modern')
        for n in ('trident', 'conduit', 'kelp', 'turtle_egg', 'phantom_membrane', 'crossbow', 'lantern', 'bell', 'campfire', 'barrel',
                  'scaffolding', 'bamboo', 'sweet_berries', 'lectern', 'loom', 'composter', 'smoker', 'blast_furnace', 'grindstone', 'stonecutter',
                  'cartography_table', 'fletching_table', 'smithing_table', 'stone_stairs', 'andesite_stairs'):
            self.assertIsNotNone(r.resolve('minecraft:' + n)[0], n)
        for n, ver in (('honeycomb', '1.15'), ('bee_nest', '1.15'), ('netherite_ingot', '1.16'), ('crimson_planks', '1.16'),
                       ('cornflower', '1.14'), ('suspicious_stew', '1.14'), ('copper_ingot', '1.17')):
            k, st, note = r.resolve('minecraft:' + n)
            self.assertIsNone(k)
            self.assertEqual(st, 'newer')
            self.assertIn(f'Java {ver}', note)
        r2 = VN.VanillaResolver('modern', fallbacks={'minecraft:netherite_ingot': 'minecraft:diamond'})
        self.assertEqual(r2.resolve('minecraft:netherite_ingot')[:2], ('minecraft:diamond', 'fallback'))
        self.assertEqual(VN.VanillaResolver('old').resolve('minecraft:pumpkin')[0], 'minecraft:carved_pumpkin')
        self.assertEqual(VN.VanillaResolver('modern').resolve('minecraft:pumpkin')[0], 'minecraft:pumpkin')
        self.assertEqual(VN.VanillaResolver('modern').resolve('minecraft:nether_brick')[0], 'minecraft:netherbrick')

    def test_ids_match_java_112(self):
        v = VN.VANILLA
        for name, i in (('stone', 1), ('carved_pumpkin', 86), ('slime', 165), ('stone_slab_3', 182), ('diamond', 264), ('netherbrick', 405),
                        ('iron_nugget', 452)):
            self.assertEqual(v[name], i, name)

    def test_lce_id(self):
        self.assertEqual(J.lce_id('PacmanMod'), 'pacman_mod')
        self.assertEqual(J.lce_id('minecraft'), 'minecraft_mod')
        self.assertEqual(J.lce_id('x'), 'x_mod')
        self.assertTrue(lce_modpack.MODID_RE.match(J.lce_id('Some-Mod.Name' * 5)))


class LuaTests(unittest.TestCase):
    def test_comment_block_cannot_close_early(self):
        t = 'a ]] b ]=] c'
        blk = B.lua_comment_block(t)
        self.assertTrue(blk.startswith('--[==['))
        self.assertTrue(B.lua_sanity('local x = 1\n' + blk + '\nlce.subscribe(1, function(ev)\n  if a then return 0 end\n  return 0\nend)\n'))
        self.assertFalse(B.lua_sanity('function f()\n'))

    def test_lua_str(self):
        self.assertEqual(B.lua_str('a"b\\c\n'), '"a\\"b\\\\c\\n"')


@unittest.skipUnless(JAVAC, 'javac not available')
class OldForgePortTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        jar, _ = old_jar()
        cls.porter, cls.m, cls.out = port(jar, 'old', mappings=None)
        cls.items = {i['key']: i for i in cls.m['items']}
        cls.blocks = {b['key']: b for b in cls.m['blocks']}

    def test_detect(self):
        self.assertEqual(self.porter.info.loader, 'forge-legacy')
        self.assertEqual(self.porter.info.mc_version, '1.7.10')
        self.assertEqual(self.m['id'], 'rubymod')

    def test_items(self):
        it = self.items
        self.assertEqual(set(it), {'rubymod:ruby', 'rubymod:ruby_apple', 'rubymod:ruby_sword', 'rubymod:ruby_pickaxe', 'rubymod:ruby_helmet', 'rubymod:wand'})
        self.assertEqual(it['rubymod:ruby']['name'], 'Ruby')
        self.assertEqual(it['rubymod:ruby']['creative_tab'], 'minecraft:materials')
        apple = it['rubymod:ruby_apple']
        self.assertEqual(apple['kind'], 'food')
        self.assertEqual((apple['food']['hunger'], apple['food']['saturation']), (6, 0.8))
        self.assertEqual(apple['food']['effects'], [{'effect': 'minecraft:regeneration', 'duration': 100, 'amplifier': 1, 'chance': 50}])
        sw = it['rubymod:ruby_sword']
        self.assertEqual((sw['kind'], sw['tier'], sw['max_damage'], sw['mining_speed'], sw['enchantability']), ('sword', 'iron', 900, 7.0, 18))
        # attack_bonus replaces the 1.95 tier damage (items195 tier_for); the console adds the sword base 4 itself,
        # so a 1.7.10 ItemSword(RUBY damage 2.5) = 6.5 damage in Java = 4 + 2.5 on 1.95
        self.assertEqual(sw['attack_bonus'], 2.5)
        pk = it['rubymod:ruby_pickaxe']          # subclass with protected super(material)
        self.assertEqual((pk['kind'], pk['max_damage']), ('pickaxe', 900))
        hm = it['rubymod:ruby_helmet']
        self.assertEqual((hm['kind'], hm['max_damage'], hm['armor_points']), ('helmet', 11 * 20, 3))
        self.assertEqual(it['rubymod:wand']['max_stack'], 1)
        self.assertEqual(it['rubymod:ruby']['fuel_ticks'], 2400)

    def test_blocks(self):
        ore, blk = self.blocks['rubymod:ruby_ore'], self.blocks['rubymod:ruby_block']
        self.assertEqual((ore['hardness'], ore['resistance'], ore['material'], ore['sound'], ore['harvest_tool'], ore['harvest_tier']),
                         (3.0, 3.0, 'stone', 'stone', 'pickaxe', 'iron'))
        self.assertEqual(ore['name'], 'Ruby Ore')
        self.assertEqual((blk['light'], blk['material'], blk['sound']), (15, 'metal', 'metal'))

    def test_recipes(self):
        rs = self.m['recipes']
        shaped = [r for r in rs if r['type'] == 'shaped']
        self.assertTrue(any(r['result'] == 'rubymod:ruby_block' and r['pattern'] == ['RRR'] * 3 and r['keys'] == {'R': 'rubymod:ruby'} for r in shaped))
        self.assertTrue(any(r['result'] == 'rubymod:ruby_sword' and r['keys'] == {'R': 'rubymod:ruby', 'S': 'minecraft:stick'} for r in shaped))
        sl = [r for r in rs if r['type'] == 'shapeless']
        self.assertTrue(any(r['result'] == 'rubymod:ruby' and r['count'] == 9 and r['ingredients'] == ['rubymod:ruby_block'] for r in sl))
        # vanilla output with data value, old pre-flattening name mapped by ID (pumpkin 86 -> carved_pumpkin)
        self.assertTrue(any(r['result'] == 'minecraft:dye@1' and r['count'] == 2 and 'minecraft:carved_pumpkin' in r['ingredients'] for r in sl))
        self.assertEqual(len(rs), 4)  # no duplicates although the helper paths are interpreted twice
        sm = self.m['smelting']
        self.assertEqual([(s['input'], s['result'], s['xp']) for s in sm], [('rubymod:ruby_ore', 'rubymod:ruby', 1.0)])

    def test_worldgen_and_entities(self):
        ore = self.m['ores'][0]
        self.assertEqual((ore['block'], ore['size'], ore['per_chunk'], ore['min_y'], ore['max_y']), ('rubymod:ruby_ore', 5, 6, 10, 39))
        e = self.m['entities'][0]
        self.assertEqual((e['key'], e['base_mob'], e['health'], e['move_speed'], e['category']),
                         ('rubymod:ruby_golem', 'minecraft:zombie', 40.0, 0.3, 'monster'))
        self.assertEqual((e['spawn_weight'], e['min_group'], e['max_group']), (8, 1, 3))
        self.assertEqual(e['name'], 'Ruby Golem')
        self.assertEqual(self.m['textures'][e['texture']]['atlas'], 'entity')

    def test_lua_translation(self):
        lua = (self.out / 'scripts' / 'main.lua').read_text()
        self.assertIn('lce.subscribe(lce.EV.ITEM_USE, function(ev)', lua)
        self.assertIn('if ev.item_id ~= lce.resolve_item("rubymod:wand") then return end', lua)
        # one dispatcher per event (the binding keeps one callback per event) and no handler returns 0 (= cancel)
        code = '\n'.join(l for l in lua.splitlines() if not l.lstrip().startswith('--'))
        self.assertEqual(code.count('lce.subscribe(lce.EV.ITEM_USE'), 1)
        self.assertNotIn('return 0', lua)
        self.assertIn('lce.add_effect(ev.player, {effect = "minecraft:speed", duration_ticks = 200, amplifier = 1})', lua)
        self.assertIn('lce.heal(ev.player, 4)  -- heal(4.0)', lua)   # API 1.2 heal service (exact amount)
        self.assertIn('lce.send_chat(ev.player, "Whoosh!")', lua)
        self.assertTrue(B.lua_sanity(lua))
        self.assertEqual(self.porter.lua.auto, 1)

    def test_check_full_passes(self):
        rc, text = P.run_check(self.out, 'full')
        self.assertEqual(rc, 0, text)
        rc2, text2 = P.run_check(self.out, 'retail195')
        self.assertEqual(rc2, 2, text2)  # blocks/food/tools are not on the retail backend yet: reported, not hidden
        dst, data = lce_modpack.build(self.out, BASE / 'pkg')
        self.assertTrue((dst / 'mod.lcm').exists())

    def test_report_categories(self):
        P.write_report(self.porter, {'full': P.run_check(self.out, 'full')}, None)
        rep = (self.out / 'PORT_REPORT.md').read_text()
        for t in ('## Ported automatically', '## Guessed', '## Needs manual work', '## Impossible on 1.95', '--profile full',
                  '## Source features requiring manual work', 'assets/rubymod/shaders/post/example.json'):
            self.assertIn(t, rep)

    def test_srg_mapping(self):
        """The same mod with SRG names (as in a release jar) maps through the built-in table."""
        mp = M.builtin_srg()
        self.assertEqual(mp.member('func_77655_b', True), 'setUnlocalizedName')
        self.assertEqual(mp.member('field_151045_i', False), 'diamond')
        self.assertEqual(mp.member('func_999999_x', True), 'func_999999_x')
        self.assertIn('func_999999_x', mp.unmapped)


@unittest.skipUnless(JAVAC, 'javac not available')
class NeoForgePortTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        jar, _ = neo_jar()
        cls.porter, cls.m, cls.out = port(jar, 'neo')
        cls.items = {i['key']: i for i in cls.m['items']}
        cls.blocks = {b['key']: b for b in cls.m['blocks']}

    def test_detect(self):
        self.assertEqual(self.porter.info.loader, 'neoforge')
        self.assertEqual(self.porter.info.mc_version, '1.21.1')
        self.assertEqual(self.m['version'], '2.0.1')

    def test_deferred_register(self):
        self.assertEqual(self.items['gemmod:gem']['max_stack'], 16)
        pie = self.items['gemmod:gem_pie']
        self.assertEqual(pie['kind'], 'food')
        self.assertEqual(pie['food'], {'hunger': 7, 'saturation': 0.6, 'always_edible': True})
        lamp = self.blocks['gemmod:gem_lamp']
        self.assertEqual((lamp['hardness'], lamp['resistance'], lamp['light'], lamp['sound']), (2.5, 9.0, 15, 'metal'))
        self.assertEqual((lamp['harvest_tool'], lamp['harvest_tier']), ('pickaxe', 'iron'))
        self.assertEqual((lamp['drop'], lamp['drops']), ('gemmod:gem', [2, 4]))
        self.assertEqual(lamp['name'], 'Gem Lamp')

    def test_recipes_and_newer_content(self):
        keys = {r['key'] for r in self.m['recipes']}
        self.assertIn('gemmod:gem_lamp', keys)
        self.assertIn('gemmod:gem_pie', keys)
        pie = next(r for r in self.m['recipes'] if r['key'] == 'gemmod:gem_pie')
        self.assertIn('minecraft:egg', pie['ingredients'])
        self.assertNotIn('gemmod:gem_from_honey', keys)
        imp = '\n'.join(self.porter.rep.lines['impossible'])
        self.assertIn('minecraft:honeycomb is not in 1.95 (Java 1.15 content', imp)

    def test_check_full_passes(self):
        rc, text = P.run_check(self.out, 'full')
        self.assertEqual(rc, 0, text)

    def test_hints_and_newer_fallback(self):
        jar, _ = neo_jar()
        hints = {'newer_fallbacks': {'minecraft:honeycomb': 'minecraft:sugar'}, 'items': {'gem': {'foil': True, 'max_stack': 8}}}
        p, m, out = port(jar, 'neo_hints', hints=hints)
        gem = next(i for i in m['items'] if i['key'] == 'gemmod:gem')
        self.assertEqual((gem['foil'], gem['max_stack']), (True, 8))
        r = next(r for r in m['recipes'] if r['key'] == 'gemmod:gem_from_honey')
        self.assertEqual(r['ingredients'], ['minecraft:sugar'])
        self.assertIn('hints fallback minecraft:sugar used', '\n'.join(p.rep.lines['guessed']))
        self.assertEqual(P.run_check(out, 'full')[0], 0)


@unittest.skipUnless(JAVAC, 'javac not available')
class Forge112PortTests(unittest.TestCase):
    """1.12.2 registry events, ITEMS lists, enum loops, sub-items, ShapedOreRecipe objects, JSON data values, 1.9+ combat."""
    @classmethod
    def setUpClass(cls):
        jar, _ = f112_jar()
        cls.porter, cls.m, cls.out = port(jar, 'f112')
        cls.items = {i['key']: i for i in cls.m['items']}
        cls.blocks = {b['key']: b for b in cls.m['blocks']}

    def test_registrations_from_lists_enums_and_events(self):
        self.assertEqual(set(self.items), {'tinmod:tin', 'tinmod:gem', 'tinmod:gem_sapphire', 'tinmod:gem_topaz', 'tinmod:tin_axe',
                                           'tinmod:copper_ingot', 'tinmod:silver_ingot', 'tinmod:tin_sword'})
        self.assertEqual(self.items['tinmod:copper_ingot']['name'], 'Copper Ingot')
        self.assertEqual(set(self.blocks), {'tinmod:tin_block'})

    def test_sub_items(self):
        it = self.items
        self.assertEqual([it[k]['name'] for k in ('tinmod:gem', 'tinmod:gem_sapphire', 'tinmod:gem_topaz')], ['Ruby', 'Sapphire', 'Topaz'])
        tex = {k: self.m['textures'][it[k]['texture']]['file'] for k in ('tinmod:gem', 'tinmod:gem_topaz')}
        self.assertTrue(tex['tinmod:gem'].endswith('gem_ruby.png'), tex)
        self.assertTrue(tex['tinmod:gem_topaz'].endswith('gem_topaz.png'), tex)

    def test_tools_use_195_damage_base(self):
        ax, sw = self.items['tinmod:tin_axe'], self.items['tinmod:tin_sword']
        # 1.12 ItemAxe(mat, 7, -3.1): 1 + 7 + 2.0 = 10 damage; 1.95 axe = 3 + attack_bonus
        self.assertEqual((ax['kind'], ax['tier'], ax['max_damage'], ax['mining_speed'], ax['attack_bonus']), ('axe', 'iron', 300, 6.5, 7.0))
        # 1.12 sword: 1 + 3 + 2.0 = 6; 1.95 sword = 4 + attack_bonus
        self.assertEqual((sw['kind'], sw['attack_bonus']), ('sword', 2.0))

    def test_block(self):
        b = self.blocks['tinmod:tin_block']
        self.assertEqual((b['hardness'], b['resistance'], b['material'], b['sound'], b['light'], b['harvest_tool'], b['harvest_tier']),
                         (3.0, 6.0, 'stone', 'metal', 8, 'pickaxe', 'stone'))
        self.assertEqual(b['name'], 'Block of Tin')

    def test_recipes(self):
        rs = self.m['recipes']
        # ShapedOreRecipe registered through RegistryEvent.Register<IRecipe>, mod ore dictionary, result data value -> sub-item
        self.assertTrue(any(r['result'] == 'tinmod:gem_topaz' and r['keys'] == {'T': 'tinmod:tin'} for r in rs), rs)
        # 1.12 JSON: ingredient data 1 -> the sapphire sub-item, vanilla result data 4 -> dye@4
        self.assertTrue(any(r['result'] == 'minecraft:dye@4' and r['count'] == 2 and r['keys'] == {'G': 'tinmod:gem_sapphire'} for r in rs), rs)
        # forge:ore_dict ingredient registered by the mod itself
        self.assertTrue(any(r['result'] == 'tinmod:tin_block' and r['keys'].get('T') == 'tinmod:tin' for r in rs), rs)
        self.assertEqual([(s['input'], s['result']) for s in self.m['smelting']], [('tinmod:tin_block', 'tinmod:gem_sapphire')])

    def test_check_full_passes(self):
        rc, text = P.run_check(self.out, 'full')
        self.assertEqual(rc, 0, text)


@unittest.skipUnless(JAVAC, 'javac not available')
class Forge114PortTests(unittest.TestCase):
    """1.13-1.14 MCP names: Item.Properties / Food.Builder / Block.Properties, RegistryEvent registerAll, loaderVersion detection."""
    @classmethod
    def setUpClass(cls):
        jar, _ = f114_jar()
        cls.porter, cls.m, cls.out = port(jar, 'f114')
        cls.items = {i['key']: i for i in cls.m['items']}
        cls.blocks = {b['key']: b for b in cls.m['blocks']}

    def test_detect(self):
        self.assertEqual((self.porter.info.loader, self.porter.info.mc_version), ('forge', '1.14.4'))

    def test_items(self):
        it = self.items
        self.assertEqual(set(it), {'opalmod:opal', 'opalmod:opal_apple', 'opalmod:opal_sword'})
        self.assertEqual((it['opalmod:opal']['max_stack'], it['opalmod:opal']['creative_tab']), (16, 'minecraft:materials'))
        self.assertEqual(it['opalmod:opal_apple']['food'], {'hunger': 5, 'saturation': 0.6, 'always_edible': True})
        sw = it['opalmod:opal_sword']
        # SwordItem(IRON, 3, -2.4): 1 + 3 + 2 = 6 damage; 1.95 sword = 4 + 2
        self.assertEqual((sw['kind'], sw['tier'], sw['max_damage'], sw['attack_bonus'], sw['creative_tab']),
                         ('sword', 'iron', 250, 2.0, 'minecraft:combat'))

    def test_block(self):
        b = self.blocks['opalmod:opal_block']
        self.assertEqual((b['hardness'], b['resistance'], b['light'], b['material'], b['sound'], b['harvest_tool'], b['harvest_tier']),
                         (5.0, 6.0, 7, 'metal', 'metal', 'pickaxe', 'iron'))

    def test_recipes(self):
        rs = {r['key']: r for r in self.m['recipes']}
        self.assertEqual(rs['opalmod:opal_block']['keys'], {'O': 'opalmod:opal'})
        self.assertEqual(rs['opalmod:opal_sword']['keys']['N'], 'minecraft:netherbrick')   # 1.13 item name, not the block

    def test_check_full_passes(self):
        rc, text = P.run_check(self.out, 'full')
        self.assertEqual(rc, 0, text)


@unittest.skipUnless(JAVAC, 'javac not available')
class ConfigPortTests(unittest.TestCase):
    """Forge config specs -> mod.json "config" (API 1.8) for ForgeConfigSpec, NeoForge ModConfigSpec and 1.12 Configuration."""

    def test_forge_config_spec(self):
        jar, _ = f114_jar()
        p, m, out = port(jar, 'f114_cfg')
        c = m['config']
        self.assertEqual(c['general.oreRate'], {'default': 8, 'min': 1, 'max': 64, 'comment': 'Opal veins per chunk\nSet to 1 for rare ore'})
        self.assertEqual(c['general.speed'], {'default': 1.25, 'min': 0.1})                    # Double.MAX_VALUE = no upper bound
        self.assertEqual(c['general.enableWand'], {'default': True, 'comment': 'Register the wand'})
        self.assertEqual(c['general.greeting']['default'], 'Hello "opal"')
        self.assertEqual(c['general.boxed']['default'], 7)
        self.assertEqual(c['general.client.render.mode']['default'], 'FANCY')
        self.assertEqual(c['general.ores']['default'], ['minecraft:iron_ore', 'minecraft:gold_ore'])  # pop(2) undoes push("client.render")
        self.assertEqual(c['general.big'], {'default': 5, 'min': 0})
        self.assertNotIn('general.computed', c)
        self.assertNotIn('general', c)                                                          # the push comment is not a value
        self.assertIn('config value not ported: general.computed: default is not a constant', '\n'.join(p.rep.lines['manual']))
        self.assertEqual(P.run_check(out, 'full')[0], 0)
        dst, _ = lce_modpack.build(out, BASE / 'pkg_cfg')
        text = (dst / 'config').read_text()
        self.assertIn('[general]\n# Opal veins per chunk\n# Set to 1 for rare ore\n# Range: 1 ~ 64\noreRate = 8\n', text)
        self.assertIn('[general.client.render]\n# Enum value name\nmode = "FANCY"\n', text)
        self.assertEqual(p.config_fields[('opalmod/OpalConfig', 'ORE_RATE')], 'general.oreRate')
        head = '\n'.join(lce_autoport_header(p))
        self.assertIn('CFG["general.oreRate"] = lce.config("general.oreRate", 8, 1, 64)  -- OpalConfig.ORE_RATE', head)
        self.assertIn('CFG["general.speed"] = lce.config("general.speed", 1.25, 0.1, nil)', head)
        self.assertIn('CFG["general.ores"] = lce.config("general.ores")  -- list, raw text', head)

    def test_neoforge_builder_parameter(self):
        jar, _ = neo_jar()
        p, m, out = port(jar, 'neo_cfg')
        self.assertEqual(m['config'], {'lamp.light': {'default': 15, 'min': 0, 'max': 15, 'comment': 'Lamp light level'}, 'debug': {'default': False}})
        self.assertEqual(P.run_check(out, 'full')[0], 0)

    def test_forge_112_configuration(self):
        jar, _ = f112_jar()
        p, m, out = port(jar, 'f112_cfg')
        c = m['config']
        self.assertEqual(c['world_gen.veinsPerChunk'], {'default': 6, 'min': 0, 'max': 32, 'comment': 'Tin veins per chunk'})
        self.assertEqual(c['general.hardMode'], {'default': False, 'comment': 'Harder recipes'})
        self.assertEqual(c['general.xpMultiplier'], {'default': 1.5, 'min': 0.0, 'max': 10.0, 'comment': ''})
        self.assertEqual(c['general.motd']['default'], 'Welcome')
        self.assertEqual(c['general.blacklist']['default'], ['minecraft:stone', 'minecraft:dirt'])
        self.assertEqual(c['general.legacyCount'], {'default': 3, 'min': 1, 'max': 9, 'comment': 'Old style'})
        self.assertEqual(c['general.flag'], {'default': True})
        self.assertEqual(P.run_check(out, 'full')[0], 0)


def lce_autoport_header(p):
    from autoport import configs as C
    return C.lua_header(p.config, p.config_fields)


@unittest.skipUnless(JAVAC, 'javac not available')
class McpAutoMappingTests(unittest.TestCase):
    """A release-style 1.12 jar (SRG names) is mapped with the MCP archive found for its version (no --mappings)."""
    def test_auto_detected_archive(self):
        d = BASE / 'srg_build'
        shutil.rmtree(d, ignore_errors=True)
        stubs = {
            'net/minecraft/item/Item.java': """package net.minecraft.item;
public class Item { public Item func_77655_b(String s) { return this; } public Item func_77625_d(int n) { return this; } public Item setRegistryName(String n) { return this; } }""",
            'net/minecraftforge/fml/common/registry/GameRegistry.java': """package net.minecraftforge.fml.common.registry;
public class GameRegistry { public static void addShapelessRecipe(net.minecraft.util.ResourceLocation n, net.minecraft.util.ResourceLocation g, net.minecraft.item.ItemStack o, Object... p) {} }""",
            'net/minecraft/util/ResourceLocation.java': 'package net.minecraft.util; public class ResourceLocation { public ResourceLocation(String a) {} }',
            'net/minecraft/item/ItemStack.java': 'package net.minecraft.item; public class ItemStack { public ItemStack(Item i, int c) {} }',
            'net/minecraft/init/Items.java': 'package net.minecraft.init; import net.minecraft.item.Item; public class Items { public static Item field_151045_i; }',
            'net/minecraftforge/registries/IForgeRegistry.java': 'package net.minecraftforge.registries; public interface IForgeRegistry<V> { void register(V v); }',
            'srgmod/SrgMod.java': """package srgmod;
import net.minecraft.item.*;
public class SrgMod { public static Item SHARD = new Item().func_77655_b("shard").func_77625_d(8).setRegistryName("shard");
  public static void reg(net.minecraftforge.registries.IForgeRegistry<Item> r) { r.register(SHARD);
    net.minecraftforge.fml.common.registry.GameRegistry.addShapelessRecipe(new net.minecraft.util.ResourceLocation("x"), null,
        new ItemStack(net.minecraft.init.Items.field_151045_i, 1), SHARD, SHARD); } }""",
        }
        cls = javac(stubs, d)
        extra = {'mcmod.info': json.dumps([{'modid': 'srgmod', 'name': 'Srg', 'version': '1', 'mcversion': '1.12.2'}]),
                 'assets/srgmod/textures/items/shard.png': png(), 'assets/srgmod/lang/en_us.lang': 'item.shard.name=Shard\n'}
        jar = make_jar(d / 'srgmod.jar', cls, ('srgmod/',), extra)
        mdir = d / 'mcp'
        mdir.mkdir()
        with zipfile.ZipFile(mdir / 'mcp_stable-39-1.12.zip', 'w') as z:
            z.writestr('fields.csv', 'searge,name,side,desc\nfield_151045_i,DIAMOND,2,\n')
            z.writestr('methods.csv', 'searge,name,side,desc\nfunc_77655_b,setTranslationKey,2,\nfunc_77625_d,setMaxStackSize,2,\n')
        self.assertEqual(M.find_mcp('1.12.2', [mdir])[0].name, 'mcp_stable-39-1.12.zip')
        self.assertEqual(M.find_mcp('1.7.10', [mdir]), [])   # SRG ids are re-used across minor versions: no cross-version fill
        old = os.environ.get('LCE_MCP_DIR')
        os.environ['LCE_MCP_DIR'] = str(mdir)
        try:
            p, m, out = port(jar, 'srgmod')
        finally:
            if old is None:
                os.environ.pop('LCE_MCP_DIR', None)
            else:
                os.environ['LCE_MCP_DIR'] = old
        self.assertTrue(any('official MCP mcp_stable-39-1.12' in l for l in p.rep.lines['auto']), p.rep.lines['auto'][:5])
        shard = next(i for i in m['items'] if i['key'] == 'srgmod:shard')
        self.assertEqual((shard['max_stack'], shard['name']), (8, 'Shard'))
        self.assertTrue(any(r['result'] == 'minecraft:diamond' and r['ingredients'] == ['srgmod:shard'] * 2 for r in m['recipes']), m['recipes'])


LUA_HOST_C = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
static int nsub = 0;
static int f_subscribe(lua_State *L) {
  luaL_checkstring(L, 1); luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_getglobal(L, "__subs"); lua_pushvalue(L, 2); lua_rawseti(L, -2, ++nsub); lua_pop(L, 1); return 0; }
static int f_one(lua_State *L) { lua_pushinteger(L, 1); return 1; }
static int f_nop(lua_State *L) { (void)L; return 0; }
int main(int argc, char **argv) {
  lua_State *L = luaL_newstate(); luaL_openlibs(L);
  lua_newtable(L); lua_setglobal(L, "__subs");
  lua_newtable(L);
  lua_pushcfunction(L, f_subscribe); lua_setfield(L, -2, "subscribe");
  lua_pushcfunction(L, f_one); lua_setfield(L, -2, "resolve_item");
  lua_pushcfunction(L, f_one); lua_setfield(L, -2, "resolve_block");
  const char *svc[] = {"add_effect", "send_chat", "give_item", "drop_item", "log", "set_block", "get_block", "spawn_entity",
                       "play_sound", "explode", "set_fire", "heal", "damage_entity", "damage_held_item", "consume_held_item",
                       "teleport", "set_motion", "spawn_particles", "item_tag_get", "item_tag_set", "item_tag_remove", "send_payload",
                       "entity_nbt_get", "entity_data_get", "entity_data_set", "entity_data_remove", "send_to_server", 0};
  for (int i = 0; svc[i]; i++) { lua_pushcfunction(L, f_nop); lua_setfield(L, -2, svc[i]); }
  const char *ev[] = {"SERVER_TICK", "CLIENT_TICK", "WORLD_LOAD", "WORLD_UNLOAD", "BLOCK_BREAK", "BLOCK_PLACE", "ITEM_USE", "ENTITY_DAMAGE",
                      "ENTITY_DEATH", "PLAYER_JOIN", "PLAYER_LEAVE", "CHAT", "BLOCK_USE", "ENTITY_INTERACT", "BLOCK_TICK", "ENTITY_TICK",
                      "ITEM_INVENTORY_TICK", "BLOCK_NEIGHBOR", "BLOCK_ENTITY_INSIDE", "BLOCK_STEP_ON", 0};
  lua_newtable(L); for (int i = 0; ev[i]; i++) { lua_pushstring(L, ev[i]); lua_setfield(L, -2, ev[i]); } lua_setfield(L, -2, "EV");
  lua_setglobal(L, "lce");
  if (luaL_dofile(L, argv[1]) != LUA_OK) { printf("LOAD %s\n", lua_tostring(L, -1)); return 1; }
  /* every dispatcher must return false (no cancel) for an event of another item/block */
  for (int i = 1; i <= nsub; i++) {
    lua_getglobal(L, "__subs"); lua_rawgeti(L, -1, i);
    lua_newtable(L); lua_pushinteger(L, 2); lua_setfield(L, -2, "item_id"); lua_pushinteger(L, 2); lua_setfield(L, -2, "block_id");
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) { printf("CALL %s\n", lua_tostring(L, -1)); return 1; }
    if (lua_toboolean(L, -1)) { printf("CANCELS %d\n", i); return 1; }
    lua_pop(L, 2);
  }
  printf("OK %d\n", nsub); return 0; }
"""
_lua_host = []


def lua_host():
    """A Lua 5.4 host built from the SDK's own sources (modules/script/lua) with a mock lce binding, or None."""
    if _lua_host:
        return _lua_host[0]
    gcc = shutil.which('gcc') or shutil.which('cc')
    src = ROOT / 'modules' / 'script' / 'lua'
    exe = None
    if gcc and src.is_dir():
        d = BASE / 'luahost'
        d.mkdir(parents=True, exist_ok=True)
        (d / 'host.c').write_text(LUA_HOST_C)
        files = [str(f) for f in src.glob('*.c') if f.name not in ('lua.c', 'luac.c')]
        r = subprocess.run([gcc, '-std=c11', '-O1', '-w', '-DLUA_USE_POSIX', '-I', str(src), str(d / 'host.c'), *files, '-lm', '-o', str(d / 'host')],
                           capture_output=True, text=True)
        exe = d / 'host' if r.returncode == 0 else None
    _lua_host.append(exe)
    return exe


@unittest.skipUnless(JAVAC, 'javac not available')
class LuaRuntimeTests(unittest.TestCase):
    """The generated scripts/main.lua loads in a real Lua 5.4 with the binding's shape, and no dispatcher cancels foreign events."""
    def test_generated_scripts_load(self):
        host = lua_host()
        if host is None:
            self.skipTest('no C compiler for the Lua host')
        for jar_fn, name in ((old_jar, 'old'), (f112_jar, 'f112')):
            jar, _ = jar_fn()
            out = BASE / 'ports' / name
            if not (out / 'scripts' / 'main.lua').exists():
                port(jar, name)
            if not (out / 'scripts' / 'main.lua').exists():
                continue   # no behaviour to script
            r = subprocess.run([str(host), str(out / 'scripts' / 'main.lua')], capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertTrue(r.stdout.startswith('OK'), r.stdout)


class LoopUnrollingTests(unittest.TestCase):
    def test_decide(self):
        self.assertTrue(I._decide('if_icmplt', I.Const(1, 'I'), I.Const(3, 'I')))
        self.assertFalse(I._decide('ifeq', I.Const(True, 'Z'), None))
        self.assertIsNone(I._decide('if_icmpge', I.Unknown(), I.Const(3, 'I')))
        self.assertTrue(I._decide('ifnonnull', I.Obj('x'), None))


class ValidatorRangeTests(unittest.TestCase):
    """Values that do not fit their packed slot are reported as validation errors (never struct.error / a crash)."""
    def _check(self, item=None, block=None, smelt=None):
        d = BASE / 'validator_range'
        shutil.rmtree(d, ignore_errors=True)
        (d / 'textures').mkdir(parents=True)
        (d / 'textures' / 'a.png').write_bytes(png())
        m = {'id': 'vr', 'name': 'VR', 'version': '1', 'textures': {'vr:a': 'textures/a.png', 'vr:t': {'file': 'textures/a.png', 'atlas': 'terrain'}},
             'items': [dict({'key': 'vr:i', 'texture': 'vr:a'}, **(item or {}))],
             'blocks': [dict({'key': 'vr:b', 'texture': 'vr:t'}, **(block or {}))]}
        if smelt:
            m['smelting'] = [dict({'key': 'vr:s', 'input': 'vr:i', 'result': 'vr:b'}, **smelt)]
        try:
            lce_modpack.validate(m, d)
            return ''
        except lce_modpack.PackError as e:
            return str(e)

    def test_ranges(self):
        self.assertEqual(self._check(), '')
        e = self._check(item={'kind': 'sword', 'max_stack': 1, 'max_damage': 10, 'tier': 'iron', 'attack_bonus': 1e9, 'mining_speed': -1})
        self.assertIn('attack_bonus out of range', e)
        e = self._check(item={'kind': 'food', 'food': {'hunger': 70000, 'saturation': 'x'}})
        self.assertIn('food.hunger', e)
        # one bad value no longer aborts validation: the block and smelting problems are reported in the same run
        e = self._check(item={'max_damage': 1 << 20}, block={'hardness': -5, 'light': 99}, smelt={'xp': 1000})
        self.assertIn('max_damage', e)
        self.assertIn('hardness out of range', e)
        self.assertIn('smelting vr:s', e)


class LcePortTests(unittest.TestCase):
    def test_registrations_at_one_factory_site_keep_distinct_keys(self):
        porter = P.Porter('synthetic.jar', BASE / 'synthetic', work=BASE)
        porter.ns = porter.lid = 'rubymod'
        porter.era, porter.harvest, porter.rr = 'old', {}, None
        shared_site = ('x', 'make', 7)
        porter.facts = types.SimpleNamespace(
            prog=I.Program({}),
            registrations=[X.Registration(name, I.Obj('net/minecraft/item/Item', shared_site), 'item', 'sample')
                           for name in ('ruby', 'lapis')],
        )
        porter._assign_keys()
        porter._describe()
        self.assertEqual({key: entry['key'] for key, entry in porter.item_entries.items()},
                         {'ruby': 'rubymod:ruby', 'lapis': 'rubymod:lapis'})
        self.assertTrue(any('share an analysed factory site' in line for line in porter.rep.lines['manual']))

    def test_potion_roles_use_minecraft_class_hierarchy(self):
        prog = I.Program({
            'sample/Potion': types.SimpleNamespace(super_name='java/lang/Object'),
            'sample/MobEffect': types.SimpleNamespace(super_name='java/lang/Object'),
            'sample/RealEffect': types.SimpleNamespace(super_name='net/minecraft/potion/Potion'),
        })
        for name, kind in (
            ('sample/Potion', 'other'),
            ('sample/MobEffect', 'other'),
            ('sample/RealEffect', 'effect'),
            ('net/minecraft/world/effect/MobEffect', 'effect'),
            ('net/minecraft/world/item/alchemy/Potion', 'potion'),
        ):
            self.assertEqual(X.classify(prog, I.Obj(name)), kind, name)

    def test_112_data_and_modern_names(self):
        rep = lce_port.Report()
        self.assertEqual(lce_port.vanilla_ref('minecraft:dye', rep, 'w', old=True, data=4), 'minecraft:dye@4')
        self.assertEqual(lce_port.vanilla_ref('minecraft:nether_brick', rep, 'w', old=True), 'minecraft:nether_brick')
        self.assertEqual(lce_port.vanilla_ref('minecraft:nether_brick', rep, 'w'), 'minecraft:netherbrick')
        self.assertEqual(lce_port.JAVA_TO_LCE['nether_brick'], 'netherbrick')
        self.assertIs(VN.EXTRA_JAVA_TO_LCE, lce_port.MODERN_EXTRA)


FORGE100 = ROOT / 'build' / 'forge100'


@unittest.skipUnless(os.environ.get('LCE_FORGE100_FULL') and (FORGE100 / 'manifest.json').exists(),
                     'full 100-mod corpus: set LCE_FORGE100_FULL=1 (needs build/forge100/jars, see build/forge100/RESULTS.md)')
class Forge100CorpusTests(unittest.TestCase):
    """Optional: every corpus jar ports, its package passes check --profile full, and totals do not regress."""
    def test_corpus(self):
        man = json.loads((FORGE100 / 'manifest.json').read_text())
        floor = json.loads((FORGE100 / 'expected_totals.json').read_text()) if (FORGE100 / 'expected_totals.json').exists() else {}
        tot = {}
        for mod in man['mods']:
            jar = FORGE100 / 'jars' / mod['file']
            if not jar.exists():
                continue
            p, m, out = port(jar, 'f100_' + mod['slug'])
            rc, text = P.run_check(out, 'full')
            self.assertEqual(rc, 0, f'{mod["slug"]}: {text[:2000]}')
            for k, v in p.stats.items():
                if isinstance(v, int):
                    tot[k] = tot.get(k, 0) + v
            shutil.rmtree(out, ignore_errors=True)
        for k, v in floor.items():
            self.assertGreaterEqual(tot.get(k, 0), v, k)

# ---------------------------------------------------------------------------
REAL = {
    'FoodExpansionmc': '/home/codey/Desktop/minecraft dev/mods/FoodExpansionmc.jar',
    'inventorypets': '/home/codey/crazy-craft-supermod/original-reference/inventorypetsuniversal.jar',
    'Statues': '/home/codey/Desktop/minecraft dev/mods/Statues.jar',
    'luckyblock': '/home/codey/lucky-block-neoforge-port/downloads/luckyblock-1.21.1-neoforge-0.5.0.jar',
    'KillerPacman': '/home/codey/Desktop/minecraft dev/mods/KillerPacman.jar',
}


class RealJarTests(unittest.TestCase):
    def _port(self, name):
        path = REAL[name]
        if not os.path.isfile(path):
            self.skipTest(f'{path} absent')
        p, m, out = port(path, 'real_' + name)
        rc, text = P.run_check(out, 'full')
        self.assertEqual(rc, 0, text)
        return p, m

    def test_food_expansion(self):
        p, m = self._port('FoodExpansionmc')
        items = {i['key']: i for i in m['items']}
        self.assertGreaterEqual(len(items), 26)
        b = items['fe:item_bacon']
        self.assertEqual((b['kind'], b['food']['hunger'], b['food']['saturation']), ('food', 1, 0.2))
        self.assertGreaterEqual(len(m['recipes']) + len(m['smelting']), 25)

    def test_inventory_pets(self):
        p, m = self._port('inventorypets')
        self.assertGreaterEqual(len(m['items']), 80)
        self.assertGreaterEqual(len(m['recipes']), 100)
        self.assertTrue(any(e['base_mob'] for e in m.get('entities', [])))

    def test_statues(self):
        p, m = self._port('Statues')
        self.assertGreaterEqual(len(m['items']) + len(m['blocks']), 3)

    def test_lucky_block_neoforge(self):
        p, m = self._port('luckyblock')
        self.assertEqual([b['key'] for b in m['blocks']], ['lucky:lucky_block'])
        self.assertTrue(any(r['result'] == 'lucky:lucky_block' for r in m['recipes']))

    def test_killer_pacman(self):
        p, m = self._port('KillerPacman')
        self.assertTrue(p.rep.lines['manual'])



class BoatPortTests(unittest.TestCase):
    """API 1.10: ItemBoat / BoatItem -> mod.json "boat" (a stock boat of the wood type in the ctor args)."""
    def test_roles(self):
        self.assertEqual(VN.ITEM_ROLE['ItemBoat'], 'boat')
        self.assertEqual(VN.ITEM_ROLE['BoatItem'], 'boat')

    def test_types(self):
        t, notes = DS.boat_type([I.SField('net/minecraft/entity/item/EntityBoat$Type', 'SPRUCE', 'L;')], 'item b')
        self.assertEqual(t, 'spruce')
        self.assertTrue(any(k == 'auto' and '"spruce"' in m for k, m in notes))
        t, notes = DS.boat_type([I.Const(0, 'Z'), I.SField('net/minecraft/world/entity/vehicle/Boat$Type', 'DARK_OAK', 'L;'), I.Unknown()], 'item b')
        self.assertEqual(t, 'dark_oak')
        self.assertFalse(any(k == 'manual' for k, _ in notes))
        t, notes = DS.boat_type([I.Const(1, 'Z'), I.SField('x/Boat$Type', 'MANGROVE', 'L;')], 'item b')   # chest boat, no 1.95 wood
        self.assertEqual(t, 'oak')
        self.assertTrue(any(k == 'guessed' for k, _ in notes) and any(k == 'manual' and 'chest' in m for k, m in notes))
        t, notes = DS.boat_type([], 'item b')
        self.assertEqual(t, 'oak')
        self.assertTrue(any(k == 'guessed' for k, _ in notes))
        for name, want in (('oak', 0), ('jungle', 3), ('dark_oak', 5)):
            self.assertEqual(lce_modpack.BOAT_TYPES.index(name), want)


if __name__ == '__main__':
    unittest.main(verbosity=1)
