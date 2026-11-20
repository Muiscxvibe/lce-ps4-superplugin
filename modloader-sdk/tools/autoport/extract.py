"""Registry facts from mod bytecode (static: parse + abstract interpretation only).

analyze(jar, info, mapper) -> Facts with items, blocks, recipes, smelting, fuels,
ore generators, entities, effects, potions, enchantments, tabs, behaviour
overrides and event subscribers, each carrying its evidence (class.method).
"""
import re

from .classfile import ClassFile, ClassFormatError, parse_method_desc, desc_class
from .interp import (Program, Obj, Ret, Call, SField, Const, ClassRef, Arr, Lambda, Param, SelfRef, Arith, FieldOf, Unknown, StrCat,
                     NOT_HANDLED, str_val, const_val, MAX_DEPTH)
from . import vanilla as VN

LIB_PREFIXES = ('com/google/', 'org/apache/', 'org/slf4j/', 'kotlin/', 'kotlinx/', 'scala/', 'org/objectweb/', 'javax/', 'org/jetbrains/',
                'it/unimi/', 'io/netty/', 'org/lwjgl/', 'com/mojang/', 'net/minecraft/', 'org/spongepowered/', 'com/fasterxml/',
                'org/yaml/', 'com/electronwill/', 'org/codehaus/', 'gnu/trove/', 'org/intellij/', 'cpw/mods/', 'net/minecraftforge/',
                'net/neoforged/', 'net/fabricmc/', 'META-INF/')
MAX_CLASSES = 12000
SIMPLE_RE = re.compile(r'[/$]')


_SIMPLE = {}
_INNER = {}


def simple(name):
    r = _SIMPLE.get(name)
    if r is None:
        r = _SIMPLE[name] = SIMPLE_RE.split(name or '')[-1]
    return r


def inner(name):
    """'a/b/Item$Properties' -> 'Item$Properties'."""
    r = _INNER.get(name)
    if r is None:
        r = _INNER[name] = (name or '').rsplit('/', 1)[-1]
    return r


REG_OWNERS = {'GameRegistry', 'EntityRegistry', 'DeferredRegister', 'DeferredRegister$Items', 'DeferredRegister$Blocks',
              'DeferredRegister$DataComponents', 'DeferredRegister$Entities', 'Registry', 'IForgeRegistry', 'ForgeRegistry',
              'RegistryEvent$Register', 'RegisterEvent', 'RegisterEvent$RegisterHelper', 'EnumHelper', 'FurnaceRecipes', 'CraftingManager',
              'OreDictionary', 'AbstractRegistrate', 'Registrate', 'BrewingRecipeRegistry', 'PotionBrewing$Builder', 'IForgeRegistryModifiable',
              'FuelRegistry'}
REG_INTEREST = REG_OWNERS | {'Item', 'Block', 'ItemStack', 'ItemFood', 'Item$Properties', 'BlockBehaviour$Properties', 'Block$Properties',
                             'AbstractBlock$Properties', 'Material', 'EntityType$Builder', 'FoodProperties$Builder', 'Food$Builder',
                             'ShapedOreRecipe', 'ShapelessOreRecipe', 'WorldGenMinable', 'IWorldGenerator', 'MobEffect', 'Potion',
                             'Enchantment', 'CreativeTabs', 'CreativeModeTab', 'SpawnEggItem', 'ToolMaterial', 'Item$ToolMaterial',
                             'ItemArmor$ArmorMaterial', 'Tier', 'Tiers', 'SimpleTier', 'ForgeTier', 'ArmorMaterial'}


class Holder(Obj):
    """Result of DeferredRegister.register(name, supplier) / Registrate builders."""
    __slots__ = ()

    def __init__(self, name, supplier, registry, site=None):
        Obj.__init__(self, 'lce/autoport/Holder', site)
        self.meta['name'] = name
        self.meta['supplier'] = supplier
        self.meta['registry'] = registry


class Facts:
    def __init__(self):
        self.objects = {}        # id(obj) -> Registration
        self.regs = []           # Registration
        self.recipes = []        # dict(type, out, pattern|ingredients, keys, where)
        self.smelting = []       # dict(input, out, xp, where)
        self.fuels = []          # dict(ref, ticks, where)
        self.ores = []           # dict(block_ref, size, per_chunk, min_y, max_y, where, guessed)
        self.spawns = []         # dict(entity_cls, weight, min, max, category, where)
        self.entity_regs = []    # dict(cls, name, egg1, egg2, where)
        self.brewing = []        # dict(input, reagent, output, where)
        self.tabs = []           # dict(obj, label, icon, where)
        self.subscribers = []    # dict(cls, method, event_cls, where)
        self.notes = []          # (kind, message) kind in auto|guessed|manual|impossible
        self.classes = 0
        self.class_errors = []
        self.mapper = None
        self.prog = None
        self.world_generators = []
        self.oredict = {}        # mod OreDictionary.registerOre(name, item)

    def note(self, kind, msg):
        self.notes.append((kind, msg))


class Registration:
    def __init__(self, name, obj, kind, where, registry=None):
        self.name, self.obj, self.kind, self.where, self.registry = name, obj, kind, where, registry
        self.data = {}           # extracted properties
        self.notes = []          # (kind, msg)
        self.unloc = None
        self.texture = None

    def __repr__(self):
        return f'<{self.kind} {self.name} {self.obj!r}>'


# ---------------------------------------------------------------------------
def load_classes(jar, mapper, facts):
    classes = {}
    names = [n for n in jar.names() if n.endswith('.class') and not n.startswith('META-INF/')]
    if len(names) > MAX_CLASSES:
        facts.note('manual', f'{len(names)} classes; only the first {MAX_CLASSES} non-library classes were analysed')
    for n in names:
        if n.startswith(LIB_PREFIXES) or n.endswith('module-info.class') or n.endswith('package-info.class'):
            continue
        if len(classes) >= MAX_CLASSES:
            break
        try:
            cf = ClassFile(jar.read(n))
            if mapper is not None:
                mapper.apply(cf)
        except (ClassFormatError, Exception) as e:  # noqa: BLE001 - untrusted input: any parse failure skips the class
            facts.class_errors.append(f'{n}: {type(e).__name__}: {e}')
            continue
        classes[cf.name] = cf
    facts.classes = len(classes)
    return classes


# ---- value helpers ----------------------------------------------------------
def unwrap(v, prog=None, depth=0):
    """Follow holders / suppliers / getItemFromBlock / ItemStack-less wrappers to the underlying object."""
    for _ in range(8):
        if isinstance(v, SField) and prog is not None and prog.is_mod(v.owner):
            sv = prog.statics.get((v.owner, v.name))
            if sv is None or sv is v or (isinstance(sv, Const) and sv.v is None):
                return v
            v = sv
            continue
        if isinstance(v, Holder):
            sup = v.meta.get('supplier')
            obj = v.meta.get('obj')
            if obj is None and prog is not None:
                obj = supplier_value(prog, sup)
                v.meta['obj'] = obj
            if obj is None:
                return v
            v = obj
            continue
        if isinstance(v, Ret):
            n = v.mname
            if n in ('get', 'value', 'asItem', 'getItemFromBlock', 'byBlock', 'getItem', 'get()', 'getBlock', 'block', 'asBlock',
                     'getDelegate', 'delegate') and (v.recv is not None or v.args):
                if n in ('getItemFromBlock', 'byBlock') and v.args:
                    v = v.args[0]
                    continue
                if v.recv is not None and not (n == 'getItem' and inner(v.owner) == 'ItemStack'):
                    v = v.recv
                    continue
            return v
        if isinstance(v, Lambda) and prog is not None:
            return v
        return v
    return v


def supplier_value(prog, sup):
    if isinstance(sup, Lambda):
        if sup.cache is None:
            try:
                sup.cache = prog.invoke_lambda(sup, [], 0)
            except (ClassFormatError, RecursionError, IndexError, KeyError, TypeError, ValueError):
                sup.cache = None
        return sup.cache
    if isinstance(sup, Obj) and not isinstance(sup, Holder):
        return sup
    return None


def num(v):
    if isinstance(v, Const) and isinstance(v.v, (int, float)) and not isinstance(v.v, bool):
        return v.v
    if isinstance(v, Const) and isinstance(v.v, bool):
        return int(v.v)
    return None


def value_range(v):
    """Integer range of a symbolic value (constants, nextInt(n), +/- of those)."""
    if isinstance(v, Const) and isinstance(v.v, (int, float)):
        return (v.v, v.v)
    if isinstance(v, (Ret, Call)) and v.mname == 'nextInt' and len(v.args) == 1 and num(v.args[0]) is not None:
        return (0, num(v.args[0]) - 1)
    if isinstance(v, Arith) and v.op in ('+', '-'):
        a, b = value_range(v.a), value_range(v.b)
        if a and b:
            return (a[0] + b[0], a[1] + b[1]) if v.op == '+' else (a[0] - b[1], a[1] - b[0])
    return None


def chain_simple(prog, cls):
    return [simple(c) for c in prog.chain(cls)]


def chain_inner(prog, cls):
    return [inner(c) for c in prog.chain(cls)]


def classify(prog, v):
    """Object -> 'item' | 'block' | 'entity_type' | 'effect' | 'potion' | 'enchantment' | 'sound' | 'tab' | 'other'."""
    if isinstance(v, Ret):
        if v.mname == 'build' and isinstance(v.recv, Obj) and inner(v.recv.cls) == 'EntityType$Builder':
            return 'entity_type'
        if inner(v.owner) == 'EntityType$Builder' or (v.mname == 'of' and inner(v.owner) == 'EntityType$Builder'):
            return 'entity_type'
        if v.mname in ('createVariableRangeEvent', 'createFixedRangeEvent') or inner(v.owner) == 'SoundEvent':
            return 'sound'
        if inner(v.owner) == 'CreativeModeTab$Builder' or (v.mname == 'build' and isinstance(v.recv, Obj) and 'CreativeModeTab' in v.recv.cls):
            return 'tab'
        return 'other'
    if not isinstance(v, Obj):
        return 'other'
    full = prog.chain(v.cls)
    names = [inner(c) for c in full]
    simples = [simple(c) for c in full]
    for c, s in zip(full, simples):
        if s == 'Potion' and c.startswith('net/minecraft/'):
            return 'potion' if '/alchemy/' in c else 'effect'
        if s == 'MobEffect' and c.startswith('net/minecraft/'):
            return 'effect'
        if s == 'PotionType':
            return 'potion'
        if s == 'Enchantment':
            return 'enchantment'
        if s in ('SoundEvent',):
            return 'sound'
        if s in ('CreativeTabs', 'CreativeModeTab', 'ItemGroup'):
            return 'tab'
        if s == 'EntityType':
            return 'entity_type'
    for s in simples:
        if s in NOT_CONTENT:
            return 'other'
        if s in VN.ITEM_ROLE:
            return 'item'
        if s in VN.BLOCK_BASES:
            return 'block'
    last = simples[-1] if simples else ''
    if last in NOT_CONTENT or any(t in last for t in ('EntityType', 'Serializer', 'RecipeType', 'MenuType', 'ComponentType', 'Feature',
                                                       'Placement', 'Processor', 'Codec', 'Provider', 'Renderer', 'Model')):
        return 'other'
    if last.startswith('Item') or last.endswith('Item'):
        return 'item'
    if last.startswith('Block') or last.endswith('Block'):
        return 'block'
    return 'other'


NOT_CONTENT = {'BlockEntityType', 'TileEntityType', 'MenuType', 'ContainerType', 'RecipeSerializer', 'RecipeType', 'DataComponentType',
               'ParticleType', 'SimpleParticleType', 'PoiType', 'VillagerProfession', 'StructureType', 'Feature', 'ArgumentTypeInfo',
               'AttachmentType', 'LootItemFunctionType', 'LootItemConditionType', 'GameEvent', 'StatType', 'ItemSubPredicate', 'ArmorMaterial',
               'Fluid', 'FlowingFluid', 'FluidType', 'Schedule', 'Activity', 'MemoryModuleType', 'SensorType', 'BlockEntity', 'TileEntity',
               'EntityDataSerializer', 'ResourceLocation', 'CreativeModeTab', 'ItemGroup'}


# ---- interpreter hook ---------------------------------------------------------
def make_hook(facts):
    def hook(prog, op, owner, name, desc, recv, args, depth):
        o = inner(owner)
        so = simple(owner)
        # DeferredRegister.register("x", supplier) and friends -> Holder
        if so == 'DeferredRegister' or o.startswith('DeferredRegister$'):
            if name in ('register', 'registerItem', 'registerBlock', 'registerSimpleItem', 'registerSimpleBlock', 'registerSimpleBlockItem',
                        'registerEntityType') and args and str_val(args[0]) is not None:
                rname = str_val(args[0])
                sup = args[1] if len(args) > 1 else None
                if name == 'registerSimpleItem':
                    sup = _make_obj('net/minecraft/world/item/Item', [a for a in args[1:2]] or [Obj('net/minecraft/world/item/Item$Properties')])
                elif name == 'registerSimpleBlock':
                    sup = _make_obj('net/minecraft/world/level/block/Block', args[1:2])
                elif name in ('registerItem', 'registerBlock') and len(args) >= 3 and isinstance(args[1], Lambda):
                    base = 'net/minecraft/world/item/Item' if name == 'registerItem' else 'net/minecraft/world/level/block/Block'
                    v = prog.invoke_lambda(args[1], [args[2]], depth) if depth < MAX_DEPTH else None
                    sup = v if isinstance(v, Obj) else _make_obj(base, [args[2]])
                elif name == 'registerSimpleBlockItem':
                    sup = _make_obj('net/minecraft/world/item/BlockItem', [args[1] if len(args) > 1 else Unknown()])
                h = Holder(rname, sup, recv, site=('reg', owner, name))
                facts_add_holder(facts, prog, h, f'{owner.split("/")[-1]}.{name}("{rname}")')
                return h
            return NOT_HANDLED
        if so in ('AbstractRegistrate', 'Registrate', 'CreateRegistrate') or (so.endswith('Registrate') and name in ('item', 'block')):
            if name in ('item', 'block', 'entity') and args and str_val(args[0]) is not None:
                fac = next((a for a in args[1:] if isinstance(a, Lambda)), None)
                b = Obj(f'lce/autoport/Registrate${name}')
                b.meta.update(name=str_val(args[0]), factory=fac, kind=name)
                return b
            return NOT_HANDLED
        if isinstance(recv, Obj) and recv.cls.startswith('lce/autoport/Registrate$'):
            if name == 'register':
                fac = recv.meta.get('factory')
                base = 'net/minecraft/world/item/Item' if recv.meta['kind'] == 'item' else 'net/minecraft/world/level/block/Block'
                props = Obj(base + ('$Properties' if recv.meta['kind'] == 'item' else '') if recv.meta['kind'] == 'item'
                            else 'net/minecraft/world/level/block/state/BlockBehaviour$Properties')
                for n, d, a, ow in recv.calls:
                    if n == 'properties' and a and isinstance(a[0], Lambda):
                        try:
                            r = prog.invoke_lambda(a[0], [props], depth)
                            if isinstance(r, Obj):
                                props = r
                        except (ClassFormatError, RecursionError, IndexError, KeyError, TypeError, ValueError):
                            pass
                obj = None
                if isinstance(fac, Lambda):
                    try:
                        obj = prog.invoke_lambda(fac, [props], depth)
                    except (ClassFormatError, RecursionError, IndexError, KeyError, TypeError, ValueError):
                        obj = None
                if not isinstance(obj, Obj):
                    obj = _make_obj(base, [props])
                h = Holder(recv.meta['name'], obj, recv, site=('registrate', recv.meta['kind']))
                h.meta['obj'] = obj
                facts_add_holder(facts, prog, h, f'Registrate.{recv.meta["kind"]}("{recv.meta["name"]}")')
                return h
            recv.calls.append((name, desc, args, owner))
            return recv
        if isinstance(recv, Holder) and name in ('get', 'value', 'asItem', 'getDelegate', 'delegate'):
            v = unwrap(recv, prog)
            return v if v is not recv else NOT_HANDLED
        # Registry.register(BuiltInRegistries.ITEM, id, obj) (vanilla / Fabric)
        if so == 'Registry' and name in ('register', 'registerForHolder') and len(args) == 3:
            rname = resource_name(args[1])
            if rname:
                facts_add_direct(facts, prog, rname[1], args[2], f'Registry.register({args[0].short()}, "{rname[1]}")', rname[0])
                return args[2]
        # Item.getItemFromBlock(block) / BlockItem.byBlock(...)
        if name in ('getItemFromBlock', 'byBlock') and len(args) == 1:
            return args[0]
        # registry name / translation key of an object built in analysed code (block.getRegistryName() for its item,
        # super.getUnlocalizedName() + "." + NAMES[meta] in sub-item overrides)
        if isinstance(recv, Obj) and not args and not isinstance(recv, Holder) and not prog.find_method(recv.cls, name, desc)[1]:
            if name == 'getRegistryName':
                rn, rns = _obj_name(prog, recv)
                if rn:
                    return Const(f'{rns}:{rn}' if rns else rn, 'S')
            elif name in ('getUnlocalizedName', 'getTranslationKey') and desc.startswith('()'):
                u = _unloc(recv)
                if u is not None:
                    pre = 'tile.' if any(simple(c) in VN.BLOCK_BASES for c in prog.chain(recv.cls)) else 'item.'
                    return Const(pre + u, 'S')
        return NOT_HANDLED
    return hook


_SYN = [0]


def _make_obj(cls, args):
    _SYN[0] += 1
    o = Obj(cls, site=('synthetic', cls, _SYN[0]))
    o.args = list(args)
    return o


def resource_name(v):
    """ResourceLocation-ish value -> (namespace or None, path) or None."""
    s = str_val(v)
    if s is not None:
        return tuple(s.split(':', 1)) if ':' in s else (None, s)
    if isinstance(v, Obj):
        parts = [str_val(a) for a in (v.args or [])]
        if isinstance(v, Ret) and v.mname in ('fromNamespaceAndPath', 'of', 'tryBuild', 'parse', 'withDefaultNamespace', 'tryParse',
                                              'id', 'modLoc', 'location', 'rl', 'identifier', 'asResource', 'prefix', 'loc', 'res'):
            if len(parts) == 2 and all(p is not None for p in parts):
                return (parts[0], parts[1])
            if len(parts) == 1 and parts[0] is not None:
                return tuple(parts[0].split(':', 1)) if ':' in parts[0] else (None, parts[0])
        if simple(v.cls) in ('ResourceLocation', 'Identifier') and parts and all(p is not None for p in parts):
            if len(parts) == 2:
                return (parts[0], parts[1])
            return tuple(parts[0].split(':', 1)) if ':' in parts[0] else (None, parts[0])
    return None


def facts_add_holder(facts, prog, h, where):
    facts.regs.append(('holder', h, where))


def facts_add_direct(facts, prog, name, obj, where, ns=None):
    facts.regs.append(('direct', (name, obj, ns), where))


# ---------------------------------------------------------------------------
def analyze(jar, info, mapper=None, log=None):
    facts = Facts()
    facts.mapper = mapper
    from .jar import mc_tuple
    mc = mc_tuple(info.mc_version)
    # ResourceLocation lower-cases resource paths from 1.11 on (1.13+ rejects upper case); 1.7.10-1.10 names keep their case
    facts.lower_names = (mc is not None and mc >= (1, 11)) or (mc is None and info.era == 'modern')
    classes = load_classes(jar, mapper, facts)
    prog = Program(classes, hook=make_hook(facts))
    facts.prog = prog
    allocs = []
    prog.on_new = allocs.append

    def interesting(cf):
        refs = cf.cp.class_refs()
        if mapper is not None:
            refs = {mapper.cls(r) for r in refs}
        return any(inner(r) in REG_INTEREST for r in refs)
    strong = {'GameRegistry', 'RegistryEvent$Register', 'DeferredRegister', 'ForgeRegistries', 'IForgeRegistry', 'EntityRegistry',
              'RegisterEvent', 'Registry', 'OreDictionary', 'FurnaceRecipes', 'CraftingManager'}

    def priority(cf):
        refs = cf.cp.class_refs()
        if mapper is not None:
            refs = {mapper.cls(r) for r in refs}
        inn = {inner(r) for r in refs}
        return 0 if inn & strong else 1 if inn & {'Item', 'Block', 'ItemStack', 'Item$Properties', 'Block$Properties'} else 2
    prog.run_all(interesting, priority)
    facts.allocs = allocs
    # annotated event subscribers for registry events are run too (covered by interesting()).
    _collect_registrations(facts, prog)
    _collect_recipes(facts, prog)
    _collect_worldgen(facts, prog)
    _collect_entities(facts, prog)
    _collect_subscribers(facts, prog)
    _collect_brewing(facts, prog)
    from .interp import TOTAL_BUDGET
    if prog.steps >= TOTAL_BUDGET:
        facts.note('manual', f'interpretation budget ({TOTAL_BUDGET} steps) exhausted: bytecode facts are partial for this very large jar')
    if prog.errors:
        facts.note('manual', f'{len(prog.errors)} method(s) could not be interpreted (first: {prog.errors[0]})')
    return facts


# ---- registrations ------------------------------------------------------------
NAME_SETTERS = ('setRegistryName',)


def _obj_name(prog, obj):
    """Registry name declared on the object itself (setRegistryName)."""
    if not isinstance(obj, Obj):
        return None, None
    a = obj.call_args('setRegistryName')
    if a:
        if len(a) == 2 and str_val(a[0]) and str_val(a[1]):
            return str_val(a[1]), str_val(a[0])
        r = resource_name(a[0])
        if r:
            return r[1], r[0]
    return None, None


def _unloc(obj):
    a = obj.call_args('setUnlocalizedName', 'setBlockName', 'setTranslationKey') if isinstance(obj, Obj) else None
    return str_val(a[0]) if a else None


def _collect_registrations(facts, prog):
    seen = {}
    out = []

    def add(name, obj, where, ns=None, kind=None):
        obj = unwrap(obj, prog)
        if not isinstance(obj, Obj) or isinstance(obj, Holder):
            facts.note('manual', f'registration "{name}" at {where}: the registered object could not be resolved statically')
            return
        k = kind or classify(prog, obj)
        key = id(obj)
        if key in seen:
            r = seen[key]
            if name and not r.name:
                r.name = name
            return
        if not name:
            name = _obj_name(prog, obj)[0]
        if not name:
            u = _unloc(obj)
            name = u
        if not name:
            facts.note('manual', f'{k} {obj.short()} at {where}: no registry name could be derived')
            return
        if getattr(facts, 'lower_names', False):
            name = name.lower()
        r = Registration(name, obj, k, where, None)
        r.ns = ns
        seen[key] = r
        out.append(r)

    # 1. explicit registration calls seen during interpretation
    for ev in prog.events:
        o, n = inner(ev.owner), ev.name
        a = ev.args
        w = f'{ev.where[0].split("/")[-1]}.{ev.where[1]}'
        if simple(ev.owner) == 'GameRegistry':
            if n in ('registerItem', 'registerBlock') and a:
                nm = next((str_val(x) for x in a[1:] if str_val(x) is not None), None)
                if nm is None and len(a) >= 2:
                    nm = _derive_name(a[-1] if n == 'registerItem' else a[-1])
                add(nm, a[0], w + f': GameRegistry.{n}')
            elif n == 'register' and a:
                add(None, a[0], w + ': GameRegistry.register')
        elif o in ('IForgeRegistry', 'IForgeRegistryModifiable', 'ForgeRegistry') or (n in ('register', 'registerAll') and _is_forge_registry(ev.recv)):
            for x in (a[0].values() if a and isinstance(a[0], Arr) else a):
                add(None, x, w + f': registry.{n}')
        elif inner(ev.owner) in ('RegisterEvent', 'RegisterEvent$RegisterHelper') and n == 'register' and len(a) >= 2 \
                and not isinstance(a[-1], (Lambda, Param, Unknown)):
            r = resource_name(a[-2])
            add(r[1] if r else None, a[-1], w + ': RegisterEvent.register')
    # 2. DeferredRegister / Registry.register captured by the hook
    for kind, payload, where in facts.regs:
        if kind == 'holder':
            h = payload
            obj = unwrap(h, prog)
            if obj is h:
                facts.note('manual', f'registration "{h.meta["name"]}" at {where}: supplier could not be evaluated statically')
                continue
            add(h.meta['name'], obj, where)
        else:
            name, obj, ns = payload
            add(name, obj, where, ns)
    # 3. objects stored in static fields of the mod but never registered by a call we understood
    regged = {id(r.obj) for r in out}
    cands = []
    for (owner, fname), v0 in list(prog.statics.items()):
        v0 = unwrap(v0, prog)
        # objects in static fields, and in static arrays / collections built in analysed code (ITEMS.add(this) lists)
        if isinstance(v0, Arr):
            cands += [(owner, f'{fname}[]', x) for x in v0.values()[:4096]]
        elif isinstance(v0, Obj) and 'coll' in v0.meta:
            cands += [(owner, f'{fname}[]', x) for x in v0.meta['coll']]
        else:
            cands.append((owner, fname, v0))
    for owner, fname, v in cands:
        v = unwrap(v, prog)
        if isinstance(v, Obj) and not isinstance(v, (Ret, Holder)) and id(v) not in regged:
            k = classify(prog, v)
            if k in ('item', 'block'):
                rn = _obj_name(prog, v)[0]
                nm = (rn.lower() if rn and getattr(facts, 'lower_names', False) else rn) or _unloc(v)
                if nm:
                    r = Registration(nm, v, k, f'{owner.split("/")[-1]}.{fname} (static field; registration call not found)')
                    r.ns = None
                    r.notes.append(('guessed', f'{k} {nm}: found in static field {owner.split("/")[-1]}.{fname}; the registration call was not '
                                               'recognised, so the registry name comes from setRegistryName/setUnlocalizedName'))
                    out.append(r)
                    regged.add(id(v))
    facts.registrations = out


def _is_forge_registry(recv):
    if isinstance(recv, Ret):
        return recv.mname == 'getRegistry' or simple(recv.owner) in ('ForgeRegistries', 'GameRegistry')
    if isinstance(recv, SField):
        return simple(recv.owner) == 'ForgeRegistries'
    return False


def _derive_name(v):
    if isinstance(v, Ret) and v.mname in ('getUnlocalizedName', 'getTranslationKey'):
        u = _unloc(v.recv)
        if u:
            return u
    s = str_val(v)
    if isinstance(v, Ret) and v.mname == 'substring' and isinstance(v.recv, Ret):
        u = _unloc(v.recv.recv)
        k = num(v.args[0]) if v.args else None
        if u and k is not None:
            return ('item.' + u)[k:]
    return s


# ---- recipes -----------------------------------------------------------------
RECIPE_CLASSES = ('ShapedOreRecipe', 'ShapelessOreRecipe', 'ShapedRecipes', 'ShapelessRecipes')


def _recipe_obj_sig(r):
    """new ShapedOreRecipe(out, params...) (1.7.10) / (ResourceLocation group, out, params...) (1.12) -> recipe signature."""
    args = list(r.args or [])
    if len(args) >= 2 and (resource_name(args[0]) is not None or (isinstance(args[0], Const) and args[0].v is None)) and \
            simple(r.cls) in ('ShapedOreRecipe', 'ShapelessOreRecipe') and not (isinstance(args[0], Const) and args[0].t == 'S' and
                                                                               isinstance(args[1], Const) and args[1].t == 'S'):
        args = args[1:]   # 1.12 recipe group
    if simple(r.cls) in ('ShapedRecipes', 'ShapelessRecipes'):
        return None       # vanilla recipe classes take Ingredient lists (1.12) / ItemStack arrays: not reconstructed
    if not args:
        return None
    return ('shapeless' if 'Shapeless' in r.cls else 'shaped', args[0], _flatten(args[1:]))


def _collect_recipes(facts, prog):
    seen = set()
    recipe_objs = set()
    events = list(prog.events)
    # 1.12 registry recipes: event.getRegistry().register(new ShapedOreRecipe(...).setRegistryName(...)) and recipe objects
    # built anywhere in analysed code (each counted once)
    extra = [o for o in getattr(facts, 'allocs', []) if simple(o.cls) in RECIPE_CLASSES and o.args]
    for ev in events:
        n, a = ev.name, ev.args
        so = simple(ev.owner)
        w = f'{ev.where[0].split("/")[-1]}.{ev.where[1]}'
        sig = None
        if so in ('GameRegistry', 'CraftingManager') and n in ('addRecipe', 'addShapedRecipe', 'addShapelessRecipe'):
            if len(a) == 1 and isinstance(a[0], Obj) and simple(a[0].cls) in RECIPE_CLASSES:
                r = a[0]
                recipe_objs.add(id(r))
                sig = _recipe_obj_sig(r)
                if sig is None:
                    continue
            else:
                items = list(a)
                if len(items) >= 3 and resource_name(items[0]) and (resource_name(items[1]) or isinstance(items[1], Const)):
                    items = items[2:]  # 1.12 (name, group, output, ...)
                if not items:
                    continue
                sig = ('shapeless' if 'Shapeless' in n else 'shaped', items[0], _flatten(items[1:]))
        elif so in ('GameRegistry', 'FurnaceRecipes') and n in ('addSmelting', 'addSmeltingBlock', 'addSmeltingStack', 'addSmeltingRecipe',
                                                                 'addSmeltingRecipeForBlock') and len(a) >= 2:
            key = ('smelt', vsig(a[0]), vsig(a[1]))
            if key in seen:
                continue
            seen.add(key)
            facts.smelting.append(dict(input=a[0], out=a[1], xp=num(a[2]) if len(a) > 2 else 0.0, where=w))
            continue
        elif so == 'OreDictionary' and n == 'registerOre' and len(a) == 2 and str_val(a[0]):
            facts.oredict.setdefault(str_val(a[0]), []).append(a[1])
            continue
        elif so in ('GameRegistry',) and n == 'registerFuelHandler' and a:
            facts.notes.append(('auto', f'fuel handler {a[0].short()} registered at {w}'))
            continue
        if sig is None:
            continue
        key = (sig[0], vsig(sig[1]), tuple(vsig(x) for x in sig[2]))
        if key in seen:
            continue
        seen.add(key)
        _add_recipe(facts, sig, w)
    _collect_recipes_tail(facts, prog, extra, recipe_objs, seen)


def _add_recipe(facts, sig, w):
    if True:
        rtype, out, params = sig
        if rtype == 'shaped':
            pattern, keys, rest = [], {}, list(params)
            while rest and (str_val(rest[0]) is not None):
                pattern.append(str_val(rest.pop(0)))
            i = 0
            ok = True
            while i + 1 < len(rest):
                c = rest[i]
                ch = c.v if isinstance(c, Const) and c.t in ('C', 'S') else (chr(c.v) if isinstance(c, Const) and c.t == 'I' else None)
                if ch is None or len(str(ch)) != 1:
                    ok = False
                    break
                keys[str(ch)] = rest[i + 1]
                i += 2
            facts.recipes.append(dict(type='shaped', out=out, pattern=pattern, keys=keys, where=w, ok=ok and bool(pattern)))
        else:
            facts.recipes.append(dict(type='shapeless', out=out, ingredients=list(params), where=w, ok=bool(params)))


def _collect_recipes_tail(facts, prog, extra, recipe_objs, seen):
    for o in extra:
        if id(o) in recipe_objs:
            continue
        recipe_objs.add(id(o))
        sig = _recipe_obj_sig(o)
        if sig is None:
            continue
        key = (sig[0], vsig(sig[1]), tuple(vsig(x) for x in sig[2]))
        if key in seen:
            continue
        seen.add(key)
        _add_recipe(facts, sig, f'{o.site[0].split("/")[-1]}.{o.site[1]}' if isinstance(o.site, tuple) and len(o.site) > 1 else 'recipe object')
    # fuel handlers: getBurnTime(ItemStack) comparisons
    for cf in prog.classes.values():
        if not any(simple(i) == 'IFuelHandler' for i in prog.interfaces(cf.name)):
            continue
        for m in cf.methods:
            if m.name == 'getBurnTime' and m.code:
                fr = prog.frames.get((cf.name, m.name, m.desc)) or prog.run_root(cf.name, m.name, m.desc)
                if fr:
                    _fuel_pairs(facts, fr, f'{cf.name.split("/")[-1]}.getBurnTime')
    # Item.getBurnTime / getItemBurnTime overrides returning a constant
    facts.item_fuel_methods = {}


def vsig(v, depth=0):
    """Structural signature of a symbolic value (dedupe recipes seen through several call paths)."""
    if depth > 6:
        return '...'
    if isinstance(v, Const):
        return repr(v.v)
    if isinstance(v, SField):
        return f'{v.owner}.{v.name}'
    if isinstance(v, ClassRef):
        return v.name + '.class'
    if isinstance(v, Arr):
        return '[' + ','.join(vsig(x, depth + 1) for x in v.values()) + ']'
    if isinstance(v, Ret):
        return f'{v.owner}.{v.mname}(' + ','.join(vsig(x, depth + 1) for x in v.args or []) + ')'
    if isinstance(v, Obj):
        return f'new {v.cls}@{v.site}(' + ','.join(vsig(x, depth + 1) for x in v.args or []) + ')'
    return type(v).__name__


def _flatten(params):
    out = []
    for p in params:
        if isinstance(p, Arr):
            out += _flatten(p.values())
        else:
            out.append(p)
    return out


def _fuel_pairs(facts, fr, where):
    rets = sorted(fr.returns, key=lambda x: x[0])
    for pc, op, a, b, target in fr.compares:
        if op not in ('if_acmpne', 'if_acmpeq'):
            continue
        ref = None
        for x in (a, b):
            if isinstance(x, (SField, Obj)) and not (isinstance(x, Ret) and x.mname in ('getItem', 'func_77973_b')):
                ref = x
        if ref is None:
            continue
        lo = pc if op == 'if_acmpne' else target
        val = next((v for rpc, v in rets if rpc > lo and num(v) is not None), None)
        if op == 'if_acmpne':
            val = next((v for rpc, v in rets if pc < rpc < target and num(v) is not None), None)
        else:
            val = next((v for rpc, v in rets if rpc >= target and num(v) is not None), None)
        if val is not None and num(val) > 0:
            facts.fuels.append(dict(ref=ref, ticks=int(num(val)), where=where))


# ---- worldgen ------------------------------------------------------------------
def _collect_worldgen(facts, prog):
    gens = [cf for cf in prog.classes.values() if any(simple(i) == 'IWorldGenerator' for i in prog.interfaces(cf.name))]
    facts.world_generators = [cf.name for cf in gens]
    seen = set()
    for key, fr in list(prog.frames.items()):
        for pc, o in fr.news:
            if simple(o.cls) not in ('WorldGenMinable', 'WorldGenMinableBlock'):
                continue
            if id(o) in seen:
                continue
            seen.add(id(o))
            args = o.args or []
            block = args[0] if args else None
            if isinstance(block, Ret) and block.mname in ('getDefaultState', 'defaultBlockState') and block.recv is not None:
                block = block.recv
            size = None
            ints = [num(x) for x in args[1:] if num(x) is not None]
            if len(args) == 2 or (len(args) >= 2 and len(ints) == 1):
                size = ints[-1] if ints else None
            elif len(args) >= 3 and len(ints) >= 2:
                size = ints[1]  # (Block, meta, size, target)
            per_chunk, guessed = None, []
            for cpc, op, a, b, target in fr.compares:
                if op in ('if_icmplt', 'if_icmpge', 'if_icmple', 'if_icmpgt') and target <= pc <= cpc or (op in ('if_icmpge', 'if_icmpgt') and cpc < pc < target):
                    c = num(b) if num(b) is not None else num(a)
                    if c is not None and 0 < c < 256:
                        per_chunk = int(c) + (1 if op in ('if_icmple',) else 0)
            yr = None
            for n, d, ga, ow in o.calls:
                if n == 'generate' and len(ga) >= 4:
                    yv = ga[3] if len(ga) >= 5 else ga[2]
                    if isinstance(yv, Obj) and simple(yv.cls) == 'BlockPos' and yv.args and len(yv.args) == 3:
                        yv = yv.args[1]
                    yr = value_range(yv)
            entry = dict(block=block, size=size, per_chunk=per_chunk, min_y=yr[0] if yr else None, max_y=yr[1] if yr else None,
                         where=f'{fr.cls.split("/")[-1]}.{fr.method.name}')
            facts.ores.append(entry)


# ---- entities ----------------------------------------------------------------------
def _collect_entities(facts, prog):
    for ev in prog.events:
        so, n, a = simple(ev.owner), ev.name, ev.args
        w = f'{ev.where[0].split("/")[-1]}.{ev.where[1]}'
        if so == 'EntityRegistry' and n == 'registerModEntity':
            if a and resource_name(a[0]) and len(a) > 1 and isinstance(a[1], ClassRef):
                a = a[1:]
            if a and isinstance(a[0], ClassRef):
                ints = [num(x) for x in a[2:] if num(x) is not None]
                eggs = ints[-2:] if len(a) >= 9 else []
                facts.entity_regs.append(dict(cls=a[0].name, name=str_val(a[1]) if len(a) > 1 else None, egg=eggs, where=w))
        elif so == 'EntityRegistry' and n == 'registerGlobalEntityID' and a and isinstance(a[0], ClassRef):
            ints = [num(x) for x in a[2:] if num(x) is not None]
            facts.entity_regs.append(dict(cls=a[0].name, name=str_val(a[1]) if len(a) > 1 else None,
                                          egg=ints[1:3] if len(ints) >= 3 else [], where=w))
        elif so == 'EntityRegistry' and n == 'addSpawn' and a and isinstance(a[0], ClassRef) and len(a) >= 5:
            cat = a[4]
            catname = cat.name.lower() if isinstance(cat, SField) else None
            facts.spawns.append(dict(cls=a[0].name, weight=num(a[1]), min=num(a[2]), max=num(a[3]), category=catname,
                                     biomes=[b.short() for b in _flatten(a[5:])], where=w))
        elif so in ('EntityList',) and n in ('addMapping',) and a and isinstance(a[0], ClassRef):
            ints = [num(x) for x in a[2:] if num(x) is not None]
            facts.entity_regs.append(dict(cls=a[0].name, name=str_val(a[1]) if len(a) > 1 else None,
                                          egg=ints[1:3] if len(ints) >= 3 else [], where=w))


def entity_type_info(prog, v):
    """EntityType.Builder chain -> (entity class, category, width, height) for modern registrations."""
    b = v.recv if isinstance(v, Ret) and v.mname == 'build' else v
    cls, cat, w, h = None, None, None, None
    seen = 0
    while isinstance(b, Obj) and seen < 32:
        seen += 1
        if isinstance(b, Ret) and b.mname in ('of', 'create', 'createNothing'):
            for x in b.args or []:
                if isinstance(x, Lambda):
                    k, owner, name, desc = x.impl
                    if name == '<init>' or k == 8:
                        cls = owner
                    else:
                        r = prog.invoke_lambda(x, [Unknown(), Unknown()], 0)
                        if isinstance(r, Obj):
                            cls = r.cls
                elif isinstance(x, SField) and simple(x.owner) in ('MobCategory', 'EnumCreatureType'):
                    cat = x.name.lower()
            for n, d, a, o in b.calls:
                if n in ('sized', 'size', 'dimensions') and len(a) == 2:
                    w, h = num(a[0]), num(a[1])
            break
        for n, d, a, o in b.calls:
            if n in ('sized', 'size', 'dimensions') and len(a) == 2:
                w, h = num(a[0]), num(a[1])
        b = b.recv if isinstance(b, Ret) else None
    return cls, cat, w, h


# ---- subscribers -----------------------------------------------------------------------
SUBSCRIBE_ANNOS = ('SubscribeEvent', 'EventHandler', 'ForgeSubscribe')


def _collect_subscribers(facts, prog):
    for cf in prog.classes.values():
        for m in cf.methods:
            ann = [simple(desc_class(t) or t) for t, _ in m.annotations]
            if not any(a in SUBSCRIBE_ANNOS for a in ann):
                continue
            argd = parse_method_desc(m.desc)[0]
            ecls = desc_class(argd[0]) if argd else None
            if ecls is None:
                continue
            if 'EventHandler' in ann and simple(ecls).startswith('FML'):
                continue  # mod lifecycle (@Mod.EventHandler preInit/init)
            facts.subscribers.append(dict(cls=cf.name, method=m.name, desc=m.desc, event=ecls,
                                          where=f'{cf.name.split("/")[-1]}.{m.name}'))


def _collect_brewing(facts, prog):
    for ev in prog.events:
        if ev.name == 'addMix' and len(ev.args) == 3:
            facts.brewing.append(dict(input=ev.args[0], reagent=ev.args[1], output=ev.args[2],
                                      where=f'{ev.where[0].split("/")[-1]}.{ev.where[1]}'))
        elif simple(ev.owner) == 'BrewingRecipeRegistry' and ev.name == 'addRecipe' and len(ev.args) == 3:
            facts.brewing.append(dict(input=ev.args[0], reagent=ev.args[1], output=ev.args[2],
                                      where=f'{ev.where[0].split("/")[-1]}.{ev.where[1]}'))
