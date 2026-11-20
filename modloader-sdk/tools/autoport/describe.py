"""Turn interpreted objects (items, blocks, entities, effects...) into SDK mod.json fields.

Every value carries provenance: describe_* return (fields, notes) where notes are
(kind, message) with kind in auto | guessed | manual | impossible.
"""
from .interp import Obj, Ret, SField, Const, ClassRef, Arr, Lambda, Param, Unknown, Arith, FieldOf, str_val
from .extract import simple, inner, unwrap, num, classify, resource_name, entity_type_info, Holder
from . import vanilla as VN

EPS = 1e-6


def site_key(o):
    return o.site if isinstance(getattr(o, 'site', None), tuple) else id(o)


class RefResolver:
    """Symbolic value -> 'modid:key' / 'minecraft:name[@aux]' with reported translation."""

    def __init__(self, prog, lce_id, resolver, regmap, mapper=None):
        self.prog, self.lce_id, self.resolver, self.regmap, self.mapper = prog, lce_id, resolver, regmap, mapper
        self.log = []          # (kind, msg)
        self.unmapped = set()
        self.oredict = {}
        self.last = ''         # reason of the last failed key()
        self.projectiles = {}  # API 1.9: projectile entity class -> throwable item name

    def note(self, kind, msg):
        if kind in ('manual', 'impossible'):
            self.last = msg.split(': ', 1)[-1]
        if (kind, msg) not in self.log:
            self.log.append((kind, msg))

    def stack(self, v):
        """ItemStack-ish -> (ref value, count, aux)."""
        v = unwrap(v, self.prog)
        if isinstance(v, Obj) and simple(v.cls) == 'ItemStack' and v.args:
            cnt = num(v.args[1]) if len(v.args) > 1 else 1
            aux = num(v.args[2]) if len(v.args) > 2 else None
            return v.args[0], int(cnt) if cnt is not None else 1, aux
        return v, 1, None

    def key(self, v, where, allow_tag=True):
        """-> key string or None (reason logged)."""
        base, _cnt, aux = self.stack(v)
        base = unwrap(base, self.prog)
        if isinstance(base, Obj) and not isinstance(base, Holder):
            reg = self.regmap.get(site_key(base))
            if reg is not None:
                if reg.get('key') is None:
                    self.note('manual', f'{where}: references {reg["kind"]} {reg["name"]}, which was not ported')
                    return None
                return self.variant(reg['key'], aux, where)
            if isinstance(base, Ret) and base.mname in ('getItemFromBlock', 'asItem', 'byBlock') and base.args:
                return self.key(base.args[0], where)
            self.note('manual', f'{where}: ingredient {base.short()} could not be resolved')
            return None
        if isinstance(base, SField):
            return self._field(base, aux, where)
        s = str_val(base)
        if s is not None and allow_tag:
            if s in self.oredict:
                for v2 in self.oredict[s]:
                    k = self.key(v2, where, allow_tag=False)
                    if k:
                        self.note('guessed', f'{where}: ore dictionary "{s}" -> {k} (registered by this mod)')
                        return k
            if s in VN.OREDICT and VN.OREDICT[s]:
                tgt = VN.OREDICT[s]
                self.note('guessed', f'{where}: ore dictionary "{s}" -> {tgt} (vanilla member only)')
                return tgt
            if ':' in s:
                return self.name(s, aux, where)
            self.note('manual', f'{where}: ore dictionary name "{s}" has no 1.95 mapping; add hints "aliases"')
            return None
        self.note('manual', f'{where}: ingredient {base.short() if hasattr(base, "short") else base!r} is not a constant item reference')
        return None

    def variant(self, key, aux, where):
        """Mod key + data value -> the key of that sub-item (sub-items are ported as separate items), or None."""
        vmap = getattr(self, 'variant_keys', {}).get(key)
        if aux in (None, -1, 32767):
            if vmap and aux in (-1, 32767):
                self.note('guessed', f'{where}: {key} with any data value -> only the base sub-item {key} is accepted on 1.95')
            return key
        if vmap:
            if aux in vmap:
                return vmap[aux]
            self.note('manual', f'{where}: {key} has no sub-item with data value {aux}')
            return None
        if aux == 0:
            return key
        self.note('manual', f'{where}: {key} with data value {aux} (its sub-items could not be enumerated statically)')
        return None

    def _field(self, f, aux, where):
        owner = simple(f.owner)
        name = f.name
        if owner in ('Items', 'Blocks', 'ItemInit', 'BlockInit') or f.owner.startswith('net/minecraft/'):
            if name.startswith('field_') or name.startswith('f_'):
                self.unmapped.add(name)
                self.note('manual', f'{where}: vanilla {owner}.{name} is an unmapped SRG name (supply MCP mappings with --mappings)')
                return None
            via = ''
            if self.mapper is not None and not getattr(self.mapper, 'verified', False) and any(v == name for v in self.mapper.used.values()):
                via = ' (via the built-in SRG table)'
            key = self.name('minecraft:' + name.lower(), aux, where)
            if key and via:
                self.note('guessed', f'{where}: {owner}.{name} -> {key}{via}')
            return key
        if self.prog.is_mod(f.owner):
            self.note('manual', f'{where}: mod field {owner}.{name} is never assigned a constructed object in analysable code '
                                '(set by reflection, @ObjectHolder, a config loop or another mod)')
        else:
            self.note('manual', f'{where}: static field {owner}.{name} is not a known item')
        return None

    def name(self, full, aux, where):
        ns, _, path = full.partition(':')
        if ns != 'minecraft':
            if ns == self.lce_id or ns == getattr(self, 'ns', None):
                k = f'{self.lce_id}:{normalize_name(path)}'
                if any(r.get('key') == k for r in self.regmap.values()):
                    return k
                if k in getattr(self, 'extra_keys', set()):
                    return k
            self.note('manual', f'{where}: references {full} (another mod or an unported entry)')
            return None
        key, status, note = self.resolver.resolve(full, aux)
        if status == 'mapped' and note:
            self.note('auto', f'{where}: {note}')
        elif status == 'fallback':
            self.note('guessed', f'{where}: {note}')
        elif status == 'newer':
            self.note('impossible', f'{where}: {note}')
        elif status == 'unknown':
            self.note('manual', f'{where}: {note}; add hints "aliases"')
        return key


def normalize_name(n):
    """Registry name -> LCE key path ([a-z0-9_./-])."""
    import re
    s = re.sub(r'(?<=[a-z0-9])(?=[A-Z])', '_', n)
    s = re.sub(r'[^a-z0-9_./-]', '_', s.lower())
    s = re.sub(r'_+', '_', s).strip('_') or 'x'
    return s[:48]


# ---- items ---------------------------------------------------------------------
def _vanilla_init(prog, obj):
    """(vanilla base class, its ctor args) for an object of a mod or vanilla class."""
    chain = prog.chain(obj.cls)
    base = chain[-1] if chain else obj.cls
    if not prog.is_mod(obj.cls):
        return obj.cls, obj.args or []
    for owner, desc, args in obj.inits:
        if not prog.is_mod(owner):
            return owner, args
    return base, []


def _role(prog, obj):
    for c in prog.chain(obj.cls):
        r = VN.ITEM_ROLE.get(simple(c))
        if r:
            return r, c
    return None, None


def _calls(obj, *names):
    out = []
    for n, d, a, o in obj.calls:
        if n in names:
            out.append(a)
    return out


def _last(obj, *names):
    c = _calls(obj, *names)
    return c[-1] if c else None


def _props(obj):
    """Item.Properties / BlockBehaviour.Properties object passed to a modern ctor."""
    for a in (obj.args or []) + [x for _o, _d, args in obj.inits for x in args]:
        a2 = a.recv if isinstance(a, Ret) and a.mname in ('setId',) else a
        if isinstance(a2, Obj) and ('Properties' in inner(a2.cls) or 'Settings' in inner(a2.cls) or
                                    (isinstance(a2, Ret) and 'Properties' in inner(a2.owner))):
            return a2
    return None


def _method_return(prog, cls, names, want_num=True):
    """Constant returned by an overriding method (e.g. getMaxItemUseDuration -> 16)."""
    for c in prog.chain(cls):
        cf = prog.classes.get(c)
        if cf is None:
            break
        for m in cf.methods:
            if m.name in names and m.code is not None:
                fr = prog.frames.get((cf.name, m.name, m.desc)) or prog.run_root(cf.name, m.name, m.desc)
                vals = [v for _pc, v in (fr.returns if fr else [])]
                if vals and all(isinstance(v, Const) for v in vals) and len({v.v for v in vals}) == 1:
                    return vals[0].v, f'{cf.name.split("/")[-1]}.{m.name}'
                if vals and not want_num:
                    return vals, f'{cf.name.split("/")[-1]}.{m.name}'
                return None, f'{cf.name.split("/")[-1]}.{m.name}'
    return None, None


def _effect_name(v, rr, where):
    """Potion/effect reference -> 'minecraft:x' / mod key / None."""
    v = unwrap(v, rr.prog)
    if isinstance(v, Const) and isinstance(v.v, int):
        n = VN.EFFECT_BY_ID.get(v.v)
        return f'minecraft:{n}' if n else None
    if isinstance(v, FieldOf) and v.name in ('id', 'field_76415_H'):
        return _effect_name(v.obj, rr, where)
    if isinstance(v, Ret) and v.mname in ('getId', 'value', 'get', 'getIdFromPotion', 'func_76396_c'):
        return _effect_name(v.recv if v.recv is not None else (v.args[0] if v.args else None), rr, where)
    if isinstance(v, SField):
        n = VN.EFFECT_FIELDS.get(v.name.lower()) if simple(v.owner) in ('Potion', 'MobEffects', 'Effects', 'StatusEffects') else None
        if n:
            return f'minecraft:{n}'
        if simple(v.owner) in ('Potion', 'MobEffects', 'Effects'):
            rr.note('manual', f'{where}: effect {v.short()} not in 1.95 or unmapped')
        return None
    if isinstance(v, Obj):
        reg = rr.regmap.get(site_key(v))
        if reg and reg.get('key'):
            return reg['key']
    return None


def _effect_instance(v, rr, where, seconds=False):
    """PotionEffect / MobEffectInstance -> dict(effect, duration, amplifier)."""
    v = unwrap(v, rr.prog)
    if isinstance(v, Lambda):
        v = rr.prog.invoke_lambda(v, [], 0)
    if not isinstance(v, Obj) or not v.args:
        return None
    a = v.args
    eff = _effect_name(a[0], rr, where)
    dur = num(a[1]) if len(a) > 1 else 200
    amp = num(a[2]) if len(a) > 2 else 0
    if eff is None or dur is None or amp is None:
        return None
    return dict(effect=eff, duration=int(dur * (20 if seconds else 1)), amplifier=int(amp))


# ---- API 1.9 throwables -------------------------------------------------------------------------
THROWABLE_ENTITY = {'EntityThrowable', 'EntitySnowball', 'EntityEgg', 'EntityEnderPearl', 'ThrowableEntity', 'ProjectileItemEntity',
                    'ThrowableProjectile', 'ThrowableItemProjectile', 'Snowball', 'ThrownEgg', 'ThrownEnderpearl', 'SnowballEntity',
                    'EggEntity', 'EnderPearlEntity', 'ThrownItemEntity', 'SnowballEntity'}
THROW_USE_METHODS = ('onItemRightClick', 'use')
THROW_IMPACT_METHODS = ('onImpact', 'onHit', 'onHitEntity', 'onHitBlock', 'onEntityHit', 'onBlockHit', 'onCollision')
THROW_SHOOT = ('shoot', 'setHeadingFromThrower', 'shootFromRotation', 'setThrowableHeading', 'setVelocity', 'setProperties')
THROW_DAMAGE = ('attackEntityFrom', 'hurt', 'damage')
THROW_FIRE_SECONDS = ('setFire', 'setSecondsOnFire', 'setOnFireFor')
THROW_FIRE_TICKS = ('setRemainingFireTicks', 'setFireTicks')
THROW_EXPLODE = ('createExplosion', 'newExplosion', 'explode')
THROW_EFFECT = ('addPotionEffect', 'addEffect', 'addStatusEffect')


def _throw_chain(prog, cls):
    return [simple(c) for c in prog.chain(cls)]


BOAT_TYPES = ('oak', 'spruce', 'birch', 'jungle', 'acacia', 'dark_oak')   # Boat::Type 0..5 (1.95 Items::init 0x399422..0x3a2424)


def boat_type(args, w):
    """API 1.10: the stock wood type of a boat item from its ctor args (an enum constant) -> (type name, notes)."""
    hit = [a for a in args if isinstance(a, SField)]
    chest = any(num(a) == 1 for a in args if isinstance(a, Const))
    notes = []
    for a in hit:
        n = a.name.lower()
        if n in BOAT_TYPES:
            notes.append(('auto', f'{w}: {a.short()} -> "boat": "{n}" (API 1.10, places the stock boat)'))
            break
        notes.append(('guessed', f'{w}: boat type {a.short()} has no 1.95 boat; ported as an oak boat'))
        n = 'oak'
        break
    else:
        n = 'oak'
        notes.append(('guessed', f'{w}: boat type not found in the constructor; ported as an oak boat'))
    if chest:
        notes.append(('manual', f'{w}: chest boats do not exist in 1.95; it places a plain boat'))
    notes.append(('auto', f'{w}: the placed boat is the stock one (stock model; it drops the stock boat item)'))
    return n, notes


def throwable_info(prog, cls, rr, w):
    """API 1.9: an item whose use spawns a throwable projectile (EntityThrowable / ThrowableItemProjectile subclass) ->
    (mod.json "throwable" value, projectile class, notes). (None, None, []) when the use spawns none."""
    proj, shoot = None, None
    for cf, m in _methods_named(prog, cls, THROW_USE_METHODS):
        fr = prog.frames.get((cf.name, m.name, m.desc)) or prog.run_root(cf.name, m.name, m.desc)
        for _pc, o in (fr.news if fr else []):
            if isinstance(o, Obj) and any(c in THROWABLE_ENTITY for c in _throw_chain(prog, o.cls)):
                proj = o
                break
        for e in (fr.events if fr else []):
            if e.name in THROW_SHOOT:
                shoot = e.args
        break
    if proj is None:
        return None, None, []
    if proj.call_args(*THROW_SHOOT) is not None:
        shoot = proj.call_args(*THROW_SHOOT)
    t, notes = {}, []
    pw = f'{w} ({simple(proj.cls)})'
    if shoot:
        vals = [num(unwrap(a, prog)) for a in shoot]
        if len(vals) >= 2 and vals[-1] is not None and vals[-2] is not None:
            speed, inacc = float(vals[-2]), float(vals[-1])
            if abs(speed - 1.5) > EPS:
                t['speed'] = round(min(4.0, max(0.1, speed)), 3)
                if t['speed'] != round(speed, 3):
                    notes.append(('guessed', f'{pw}: speed {speed} clamped to {t["speed"]} (projectiles195: 0.1..4)'))
            if abs(inacc - 1.0) > EPS:
                t['inaccuracy'] = round(min(20.0, max(0.0, inacc)), 3)
    # the projectile's impact handler (mod classes only: the stock snowball behaviour stays)
    for c in prog.chain(proj.cls):
        if c not in prog.classes:
            break
        for cf, m in _methods_named(prog, c, THROW_IMPACT_METHODS):
            if cf.name != c:
                continue
            fr = prog.frames.get((cf.name, m.name, m.desc)) or prog.run_root(cf.name, m.name, m.desc)
            for e in (fr.events if fr else []):
                args = [unwrap(a, prog) for a in (e.args or [])]
                if e.name in THROW_DAMAGE and args and num(args[-1]) is not None and 'damage' not in t:
                    t['damage'] = round(min(1000.0, max(0.0, float(num(args[-1])))), 3)
                elif e.name in THROW_FIRE_SECONDS and args and num(args[-1]) is not None:
                    t['fire_ticks'] = int(min(12000, max(0, num(args[-1]) * 20)))
                elif e.name in THROW_FIRE_TICKS and args and num(args[-1]) is not None:
                    t['fire_ticks'] = int(min(12000, max(0, num(args[-1]))))
                elif e.name in THROW_EXPLODE and len(args) >= 5 and num(args[4]) is not None:
                    r = float(num(args[4]))
                    t['explode'] = round(min(8.0, max(0.0, r)), 3)
                    if r > 8:
                        notes.append(('guessed', f'{pw}: explosion radius {r} clamped to 8'))
                elif e.name in THROW_EFFECT and args:
                    ei = _effect_instance(args[0], rr, pw) if rr is not None else None
                    if ei:
                        t.setdefault('effects', []).append(ei)
                    else:
                        notes.append(('manual', f'{pw}: a potion effect on impact could not be resolved'))
    if len(t.get('effects', [])) > 8:
        t['effects'] = t['effects'][:8]
        notes.append(('guessed', f'{pw}: more than 8 impact effects: the first 8 kept'))
    notes.append(('auto', f'{w}: throws {simple(proj.cls)} -> "throwable" {t or "(snowball defaults)"}; it flies and renders as a snowball '
                          '(API 1.9: no per-type projectile model; a projectile saved in flight reloads as a snowball)'))
    return (t or True), proj.cls, notes


def describe_item(prog, reg, rr, era, ns):
    obj = reg.obj
    reg.mc = getattr(rr, 'mc', None)
    f, notes = {}, []
    w = reg.where
    role, role_cls = _role(prog, obj)
    base, bargs = _vanilla_init(prog, obj)
    props = _props(obj)
    kind = {'food': 'food', 'sword': 'sword', 'pickaxe': 'pickaxe', 'axe': 'axe', 'shovel': 'shovel', 'hoe': 'hoe'}.get(role, 'generic')
    ev = f'{simple(obj.cls)} ({w})'
    if role in ('blockitem',):
        return None, [('auto', f'item {reg.name}: block item (the block creates it)')]
    if role in ('bow', 'potion', 'record', 'shears', 'fishingrod', 'spawnegg', 'shield', 'elytra'):
        notes.append(('manual', f'item {reg.name}: vanilla behaviour class {simple(role_cls)} cannot be cloned in mod.json; ported as a generic item'))
    # API 1.9 throwables: a snowball-class item, or a use that spawns an EntityThrowable / ThrowableItemProjectile subclass
    thr, proj_cls, tn = throwable_info(prog, obj.cls, rr, f'item {reg.name}')
    if thr is not None:
        f['throwable'] = thr
        notes += tn
        if rr is not None:
            rr.projectiles[proj_cls] = reg.name
    elif role == 'throwable':
        f['throwable'] = True
        rc = simple(role_cls)
        if rc in ('ItemSnowball', 'SnowballItem'):
            notes.append(('auto', f'item {reg.name}: {rc} -> "throwable": true (API 1.9, a snowball)'))
        else:
            notes.append(('guessed', f'item {reg.name}: {rc} -> "throwable": true; it flies as a snowball (the egg chicken / pearl teleport is not ported)'))
    # API 1.10 boat items: ItemBoat(EntityBoat.Type) / BoatItem([hasChest,] Boat.Type, props) -> "boat": wood type
    if role == 'boat':
        bt, bn = boat_type(bargs + list(obj.args or []), f'item {reg.name}')
        f['boat'] = bt
        notes += bn
    # stack / damage (old setters, modern Properties)
    st = _last(obj, 'setMaxStackSize', 'stacksTo', 'maxCount', 'maxStackSize')
    if st and num(st[0]) is not None:
        f['max_stack'] = int(num(st[0]))
    elif num(obj.fields.get('maxStackSize')) is not None:
        f['max_stack'] = int(num(obj.fields['maxStackSize']))
    if num(obj.fields.get('hasSubtypes')):
        notes.append(('manual', f'item {reg.name}: has sub-types (metadata variants); only the base item is ported'))
    if props is not None:
        st = _last(props, 'stacksTo', 'maxCount', 'maxStackSize')
        if st and num(st[0]) is not None:
            f['max_stack'] = int(num(st[0]))
        d = _last(props, 'durability', 'defaultDurability', 'maxDamage', 'defaultMaxDamage')
        if d and num(d[0]) is not None:
            f['max_damage'] = int(num(d[0]))
        elif d and isinstance(d[0], Ret) and d[0].mname == 'getDurability' and isinstance(d[0].recv, SField) and d[0].args:
            m = num(d[0].args[0])
            slot = d[0].recv.name.lower()
            idx = {'helmet': 0, 'chestplate': 1, 'leggings': 2, 'boots': 3}.get(slot)
            if m is not None and idx is not None:
                f['max_damage'] = int(VN.ARMOR_BASE_DURABILITY[idx] * m)
        if _calls(props, 'fireResistant', 'fireproof'):
            notes.append(('impossible', f'item {reg.name}: fireResistant (1.16 netherite behaviour) has no 1.95 equivalent; dropped'))
        if _calls(props, 'rarity'):
            notes.append(('guessed', f'item {reg.name}: rarity colour dropped (1.95 mod items use the default name colour)'))
    dm = _last(obj, 'setMaxDamage', 'setMaxDurability')
    if dm and num(dm[0]) is not None:
        f['max_damage'] = int(num(dm[0]))
    if _calls(obj, 'setFull3D'):
        f['handheld'] = True
    if _calls(obj, 'setHasSubtypes'):
        notes.append(('manual', f'item {reg.name}: has sub-types (metadata variants); only the base item is ported'))
    tab = _last(obj, 'setCreativeTab', 'tab')
    if props is not None and _last(props, 'group', 'tab'):
        tab = _last(props, 'group', 'tab')
    if tab:
        g = _tab(tab[0])
        if g:
            f['creative_tab'] = g
    foil, fw = _method_return(prog, obj.cls, ('hasEffect', 'isFoil', 'hasGlint'))
    if foil is True or foil == 1:
        f['foil'] = True
        notes.append(('auto', f'item {reg.name}: enchantment glint from {fw}'))
    ench, ew = _method_return(prog, obj.cls, ('getItemEnchantability', 'getEnchantmentValue'))
    if isinstance(ench, int) and ench > 0:
        f['enchantability'] = ench
    burn, bw = _method_return(prog, obj.cls, ('getBurnTime', 'getItemBurnTime'))
    if isinstance(burn, int) and burn > 0:
        f['fuel_ticks'] = burn
        notes.append(('auto', f'item {reg.name}: fuel {burn} ticks from {bw}'))
    # food
    if kind == 'food' or (props is not None and _calls(props, 'food')):
        kind = 'food'
        food, fn = _food(prog, obj, base, bargs, props, rr, reg, era)
        notes += fn
        if food is None:
            kind = 'generic'
        else:
            f['food'] = food
            dur, dw = _method_return(prog, obj.cls, ('getMaxItemUseDuration', 'getUseDuration'))
            if isinstance(dur, int) and dur > 0 and dur != 32:
                food['eat_ticks'] = dur
    # tools
    if kind in ('sword', 'pickaxe', 'axe', 'shovel', 'hoe') or role == 'tool' or (props is not None and _calls(props, 'sword', 'pickaxe', 'axe', 'shovel', 'hoe')):
        tk, tn = _tool(prog, obj, kind, role, base, bargs, props, reg)
        notes += tn
        if tk:
            kind = tk['kind']
            for k, v in tk.items():
                if k != 'kind' and k not in f:
                    f[k] = v
            if 'max_damage' in tk and 'max_damage' in f and dm is None and props is None:
                f['max_damage'] = tk['max_damage']
    if role == 'armor' or (props is not None and _calls(props, 'humanoidArmor', 'armor')):
        ar, an = _armor(prog, obj, base, bargs, props, reg, era)
        notes += an
        if ar:
            kind = ar.pop('kind')
            for k, v in ar.items():
                f.setdefault(k, v)
    f['kind'] = kind
    if 'throwable' in f and kind != 'generic':
        f.pop('throwable')
        notes.append(('manual', f'item {reg.name}: throws a projectile but is a {kind}; a throwable must be a generic item (API 1.9): use not ported'))
    if kind not in ('generic', 'food'):
        f['max_stack'] = 1
        if not f.get('max_damage'):
            f['max_damage'] = 250
            notes.append(('guessed', f'item {reg.name}: {kind} without a resolvable durability; 250 assumed'))
        f.setdefault('handheld', kind in ('sword', 'pickaxe', 'axe', 'shovel', 'hoe'))
        if kind in ('helmet', 'chestplate', 'leggings', 'boots'):
            f.pop('handheld', None)
    elif f.get('max_damage') and kind == 'generic':
        notes.append(('guessed', f'item {reg.name}: generic item with durability {f["max_damage"]}; durability kept (no use action on 1.95)'))
    tex = _last(obj, 'setTextureName')
    if tex and str_val(tex[0]):
        reg.texture = str_val(tex[0])
    reg.unloc = _unloc(obj)
    notes.insert(0, ('auto', f'item {reg.name}: {kind} from {ev}' + (f' (vanilla base {simple(base)})' if base else '')))
    return f, notes


def icon_strings(prog, cls):
    """Constant icon names passed to IIconRegister.registerIcon in registerIcons/registerBlockIcons (1.7.10)."""
    out = []
    for c in prog.chain(cls):
        cf = prog.classes.get(c)
        if cf is None:
            break
        for m in cf.methods:
            if m.name in ('registerIcons', 'registerBlockIcons', 'registerIcon') and m.code is not None:
                fr = prog.frames.get((cf.name, m.name, m.desc)) or prog.run_root(cf.name, m.name, m.desc)
                for ev in (fr.events if fr else []):
                    if ev.name == 'registerIcon' and ev.args:
                        sv = str_val(ev.args[0])
                        if sv and sv not in out:
                            out.append(sv)
        if out:
            break
    return out


def _unloc(obj):
    a = _last(obj, 'setUnlocalizedName', 'setBlockName', 'setTranslationKey')
    return str_val(a[0]) if a else None


def _tab(v):
    if isinstance(v, SField):
        g = VN.CREATIVE_TAB_MAP.get(v.name.lower())
        if g:
            return g
    return None


def _food(prog, obj, base, bargs, props, rr, reg, era):
    notes = []
    food = None
    w = f'item {reg.name}'
    if simple(base) in ('ItemFood', 'ItemSoup', 'ItemAppleGold') and bargs:
        h = num(bargs[0])
        if len(bargs) >= 3:
            sat = num(bargs[1])
        elif len(bargs) == 2:
            sat = 0.6
            notes.append(('auto', f'{w}: saturation 0.6 (ItemFood(int, boolean) default)'))
        else:
            sat = None
        if h is not None and sat is not None:
            food = {'hunger': int(h), 'saturation': round(float(sat), 4)}
            notes.append(('auto', f'{w}: food hunger {int(h)} saturation {sat} from {simple(base)}{tuple(x.short() for x in bargs)}'))
    if props is not None:
        fa = _last(props, 'food', 'component')
        if fa:
            fp = unwrap(fa[0], prog)
            b = fp.recv if isinstance(fp, Ret) and fp.mname in ('build', 'm_38767_') else fp
            if isinstance(b, Obj):
                n = _last(b, 'nutrition', 'hunger')
                s = _last(b, 'saturationModifier', 'saturationMod', 'saturation')
                if n and num(n[0]) is not None:
                    food = {'hunger': int(num(n[0])), 'saturation': round(float(num(s[0])), 4) if s and num(s[0]) is not None else 0.6}
                    notes.append(('auto', f'{w}: food from FoodProperties.Builder (nutrition {food["hunger"]}, saturation {food["saturation"]})'))
                elif b.args and simple(b.cls) in ('FoodProperties', 'FoodComponent') and num(b.args[0]) is not None:
                    food = {'hunger': int(num(b.args[0])), 'saturation': round(float(num(b.args[1]) or 0.6), 4)}
                if food and (_calls(b, 'alwaysEdible', 'alwaysEat', 'setAlwaysEdible')):
                    food['always_edible'] = True
                if food and _calls(b, 'fast', 'snack', 'fastToEat'):
                    food['eat_ticks'] = 16
                if food:
                    for a in _calls(b, 'effect'):
                        e = _effect_instance(a[0], rr, w) if a else None
                        if e:
                            if len(a) > 1 and num(a[1]) is not None:
                                e['chance'] = int(round(num(a[1]) * 100))
                            food.setdefault('effects', []).append(e)
                        else:
                            notes.append(('manual', f'{w}: a food effect could not be resolved'))
    if food is None:
        if props is not None or simple(base) in ('ItemFood',):
            notes.append(('manual', f'{w}: food values not constant; set hints.items.{reg.name}.food'))
        return None, notes
    if _calls(obj, 'setAlwaysEdible'):
        food['always_edible'] = True
    for a in _calls(obj, 'setPotionEffect'):
        if len(a) == 4:
            eid, dur, amp, prob = (num(x) for x in a)
            en = _effect_name(a[0], rr, w)
            n = en.split(':', 1)[1] if en and en.startswith('minecraft:') else None
            if n and dur is not None:
                food.setdefault('effects', []).append({'effect': f'minecraft:{n}', 'duration': int(dur * 20), 'amplifier': int(amp or 0),
                                                       'chance': int(round((prob if prob is not None else 1.0) * 100))})
                notes.append(('auto', f'{w}: food effect {n} {dur}s (setPotionEffect)'))
            else:
                notes.append(('manual', f'{w}: setPotionEffect arguments not constant'))
        elif len(a) == 2:
            e = _effect_instance(a[0], rr, w)
            if e:
                e['chance'] = int(round((num(a[1]) or 1.0) * 100))
                food.setdefault('effects', []).append(e)
            else:
                notes.append(('manual', f'{w}: setPotionEffect effect not constant'))
    if food['hunger'] <= 0:
        notes.append(('guessed', f'{w}: hunger {food["hunger"]} raised to 1 (the SDK requires > 0)'))
        food['hunger'] = 1
    if food['saturation'] < 0:
        food['saturation'] = 0.0
    return food, notes


def _tool_material(prog, v, w):
    """-> dict(harvest, uses, eff, dmg, ench, tier) or None."""
    v = unwrap(v, prog)
    if isinstance(v, SField) and v.name.upper() in VN.TOOL_MATERIALS:
        h, u, e, d, en, t = VN.TOOL_MATERIALS[v.name.upper()]
        return dict(harvest=h, uses=u, eff=e, dmg=d, ench=en, tier=t, src=f'vanilla {v.short()}')
    if isinstance(v, Ret) and v.mname == 'addToolMaterial':
        a = [num(x) for x in v.args[1:]]
        if len(a) >= 5 and None not in a[:5]:
            return dict(harvest=a[0], uses=a[1], eff=a[2], dmg=a[3], ench=a[4], tier=VN.tier_from_harvest(a[0]),
                        src=f'EnumHelper.addToolMaterial({str_val(v.args[0])!r}, {", ".join(str(x) for x in a[:5])})')
    if isinstance(v, Obj):
        ints = [num(x) for x in (v.args or []) if isinstance(x, Const) and x.t == 'I']
        flts = [num(x) for x in (v.args or []) if isinstance(x, Const) and x.t in ('F', 'D')]
        if v.args and isinstance(v.args[0], Const) and v.args[0].t == 'S' and prog.classes.get(v.cls) is not None and prog.classes[v.cls].is_enum:
            ints = ints[1:]  # enum (name, ordinal, ...)
        if len(ints) >= 3 and len(flts) >= 2:
            return dict(harvest=ints[0], uses=ints[1], eff=flts[0], dmg=flts[1], ench=ints[2], tier=VN.tier_from_harvest(ints[0]),
                        src=f'tier {simple(v.cls)}{tuple(x.short() for x in v.args)} (field order level/uses/speed/damage/enchant assumed)',
                        guessed=True)
        if len(ints) >= 2 and len(flts) >= 2:
            tag = next((x for x in v.args if isinstance(x, (SField, Ret))), None)
            lvl = None
            if tag is not None:
                s = tag.short().lower()
                for t, lv in (('wood', 0), ('stone', 1), ('iron', 2), ('diamond', 3), ('netherite', 4), ('gold', 0)):
                    if t in s:
                        lvl = lv
            return dict(harvest=lvl if lvl is not None else 2, uses=ints[0], eff=flts[0], dmg=flts[1], ench=ints[1],
                        tier=VN.tier_from_harvest(lvl if lvl is not None else 2),
                        src=f'tier {simple(v.cls)}{tuple(x.short() for x in v.args)}', guessed=lvl is None)
    return None


def _tool(prog, obj, kind, role, base, bargs, props, reg):
    notes = []
    w = f'item {reg.name}'
    mat = None
    for a in bargs:
        mat = _tool_material(prog, a, w)
        if mat:
            break
    if props is not None:
        for nm in ('sword', 'pickaxe', 'axe', 'shovel', 'hoe'):
            c = _last(props, nm)
            if c:
                kind = nm
                mat = _tool_material(prog, c[0], w) or mat
    if kind == 'generic' or role == 'tool':
        for k in ('pickaxe', 'shovel', 'sword', 'axe', 'hoe'):
            if reg.name.lower().endswith(k) or reg.name.lower().endswith(k + 's'):
                kind = k
        if kind == 'generic':
            notes.append(('guessed', f'{w}: tool class {simple(base)} with no recognisable kind; ported as pickaxe'))
            kind = 'pickaxe'
    out = {'kind': kind}
    if mat:
        out['tier'] = mat['tier']
        out['max_damage'] = int(mat['uses'])
        out['mining_speed'] = float(mat['eff'])
        if mat.get('ench'):
            out['enchantability'] = int(mat['ench'])
        notes.append(('guessed' if mat.get('guessed') else 'auto',
                      f'{w}: {kind} tier {mat["tier"]}, {int(mat["uses"])} uses, speed {mat["eff"]}, damage {mat["dmg"]} from {mat["src"]}'))
        if kind != 'hoe':
            total, how = java_attack_damage(prog, kind, base, bargs, mat, getattr(reg, 'mc', None) or (1, 12, 2), props)
            bonus = round(total - LCE_TOOL_BASE[kind], 3)
            # attack_bonus replaces the tier's damage (items195 tier_for); 1.95 adds the kind base (sword 4, axe 3, pickaxe 2,
            # shovel 1: legacy-console combat), so bonus = Java total damage - that base
            if abs(bonus) < 1e-6 and LCE_TIER_DAMAGE.get(mat['tier'], 0.0) != 0.0:
                bonus = 0.001  # 0 means "tier default" in the package format; 0.001 keeps the Java damage exactly (0 extra)
            out['attack_bonus'] = bonus
            notes.append(('auto', f'{w}: attack damage {round(total, 3)} in Java ({how}) -> attack_bonus {bonus} '
                                  f'(1.95 {kind} = {LCE_TOOL_BASE[kind]} + bonus)'))
    else:
        notes.append(('guessed', f'{w}: {kind} material not constant; tier iron / 250 uses assumed'))
        out.update(tier='iron', max_damage=250)
    return out, notes


LCE_TOOL_BASE = {'sword': 4.0, 'axe': 3.0, 'pickaxe': 2.0, 'shovel': 1.0, 'hoe': 0.0}
LCE_TIER_DAMAGE = {'wood': 0.0, 'stone': 1.0, 'iron': 2.0, 'diamond': 3.0, 'gold': 0.0}
# Java 1.12 ItemAxe(ToolMaterial) attack damages by vanilla material ordinal (WOOD, STONE, IRON, DIAMOND, GOLD)
_AXE_112 = {'WOOD': 6.0, 'STONE': 8.0, 'IRON': 8.0, 'DIAMOND': 8.0, 'EMERALD': 8.0, 'GOLD': 6.0}


def java_attack_damage(prog, kind, base, bargs, mat, mc, props=None):
    """Total melee damage of a Java tool (player base included from 1.9 on) -> (damage, explanation)."""
    m = float(mat['dmg'])
    sb = simple(base) if base else ''
    nums = [num(a) for a in bargs if isinstance(a, Const) and a.t in ('F', 'D', 'I')]
    if mc < (1, 9):
        if sb in ('ItemTool',) and nums:
            return nums[0] + m, f'ItemTool({nums[0]}) + material {m}'
        return {'sword': 4.0, 'axe': 3.0, 'pickaxe': 2.0, 'shovel': 1.0}[kind] + m, f'1.7/1.8 {kind} base + material {m}'
    if mc < (1, 13):
        if sb == 'ItemTool' and nums:
            return 1 + nums[0] + m, f'1 + ItemTool({nums[0]}) + material {m}'
        if kind == 'axe':
            if sb == 'ItemAxe' and len(bargs) >= 3 and num(bargs[1]) is not None:
                return 1 + num(bargs[1]) + m, f'1 + ItemAxe(material, {num(bargs[1])}, ...) + material {m}'
            src = mat.get('src', '')
            for k, v in _AXE_112.items():
                if src.endswith('.' + k) or src.endswith(k):
                    return 1 + v + m, f'1 + vanilla axe table {v} + material {m}'
            return 1 + 8.0 + m, f'1 + ItemAxe default 8 (custom material) + material {m}'
        return 1 + {'sword': 3.0, 'pickaxe': 1.0, 'shovel': 1.5}[kind] + m, f'1.9-1.12 {kind}: 1 + base + material {m}'
    # 1.13+: SwordItem/PickaxeItem(tier, int), AxeItem/ShovelItem(tier, float), ToolItem(float, float, tier, set)
    if nums:
        return 1 + nums[0] + m, f'1 + {sb}(tier, {nums[0]}, ...) + tier {m}'
    return 1 + {'sword': 3.0, 'pickaxe': 1.0, 'axe': 5.0, 'shovel': 1.5}[kind] + m, f'1.13+ {kind} default + tier {m}'


def _armor(prog, obj, base, bargs, props, reg, era):
    notes = []
    w = f'item {reg.name}'
    mat_v, slot_v = None, None
    if bargs:
        mat_v = bargs[0]
        slot_v = bargs[-1] if len(bargs) >= 3 else (bargs[1] if len(bargs) == 2 else None)
        if len(bargs) >= 2 and isinstance(bargs[1], SField):
            slot_v = bargs[1]
    if props is not None:
        c = _last(props, 'humanoidArmor', 'armor')
        if c and len(c) >= 2:
            mat_v, slot_v = c[0], c[1]
    idx = None
    s = slot_v
    if isinstance(s, Const) and isinstance(s.v, int) and 0 <= s.v <= 3:
        idx = s.v
    elif isinstance(s, SField):
        idx = {'head': 0, 'helmet': 0, 'chest': 1, 'chestplate': 1, 'body': 1, 'legs': 2, 'leggings': 2, 'feet': 3, 'boots': 3}.get(s.name.lower())
    if idx is None:
        for i, k in enumerate(VN.ARMOR_KINDS):
            if k in reg.name.lower() or (k == 'chestplate' and 'chest' in reg.name.lower()):
                idx = i
        if idx is None:
            notes.append(('manual', f'{w}: armour slot unknown'))
            return None, notes
        notes.append(('guessed', f'{w}: armour slot {VN.ARMOR_KINDS[idx]} from the name'))
    kind = VN.ARMOR_KINDS[idx]
    out = {'kind': kind}
    m = unwrap(mat_v, prog)
    if isinstance(m, SField) and m.name.upper() in VN.ARMOR_MATERIALS:
        fac, red, en = VN.ARMOR_MATERIALS[m.name.upper()]
        out.update(max_damage=VN.ARMOR_BASE_DURABILITY[idx] * fac, armor_points=max(1, red[idx]), enchantability=en)
        notes.append(('auto', f'{w}: {kind}, vanilla material {m.name}'))
    elif isinstance(m, Ret) and m.mname == 'addArmorMaterial':
        a = m.args
        ints = [num(x) for x in a[1:] if isinstance(x, Const) and x.t == 'I']
        arr = next((x for x in a if isinstance(x, Arr)), None)
        red = [num(x) for x in arr.values()] if arr else []
        if ints and len(red) == 4 and None not in red:
            if era == 'old' and len(a) >= 6:  # 1.9+ (name, texture, durability, int[] feet-first, ench, sound, toughness)
                red = red[::-1]
            fac = ints[0]
            out.update(max_damage=VN.ARMOR_BASE_DURABILITY[idx] * fac, armor_points=max(1, min(20, int(red[idx]))))
            if len(ints) > 1:
                out['enchantability'] = ints[1]
            notes.append(('auto', f'{w}: {kind} from EnumHelper.addArmorMaterial({str_val(a[0])!r}, {fac}, {red}, ...)'))
    if 'armor_points' not in out:
        out.setdefault('armor_points', [2, 6, 5, 2][idx])
        notes.append(('guessed', f'{w}: armour points {out["armor_points"]} (iron-like) assumed; material not constant'))
    return out, notes


# ---- block shapes (API 1.7 "box") -------------------------------------------------------------
# Vanilla bases with a fixed 1/16 outline that 1.95 can express as one box (bottom slab, carpet, ...).
BASE_BOXES = {
    'BlockSlab': [0, 0, 0, 16, 8, 16], 'BlockHalfSlab': [0, 0, 0, 16, 8, 16], 'SlabBlock': [0, 0, 0, 16, 8, 16],
    'BlockCarpet': [0, 0, 0, 16, 1, 16], 'CarpetBlock': [0, 0, 0, 16, 1, 16], 'WoolCarpetBlock': [0, 0, 0, 16, 1, 16],
    'BlockCake': [1, 0, 1, 15, 8, 15], 'BlockSnow': [0, 0, 0, 16, 2, 16], 'SnowLayerBlock': [0, 0, 0, 16, 2, 16],
    'BlockPressurePlate': [1, 0, 1, 15, 1, 15], 'PressurePlateBlock': [1, 0, 1, 15, 1, 15],
    'BlockEnchantmentTable': [0, 0, 0, 16, 12, 16], 'EnchantmentTableBlock': [0, 0, 0, 16, 12, 16],
    'BlockDaylightDetector': [0, 0, 0, 16, 6, 16], 'DaylightDetectorBlock': [0, 0, 0, 16, 6, 16],
}
BASE_NO_COLLISION = {'BlockPressurePlate', 'PressurePlateBlock', 'BlockWeb', 'WebBlock'}
SHAPE_METHODS = ('getBoundingBox', 'func_185496_a', 'getShape', 'func_220053_a', 'getOutlineShape', 'getCollisionShape',
                 'func_220071_b', 'getCollisionBoundingBox', 'func_180646_a', 'getSelectedBoundingBox')
# (call name, owner simple names or None = any, scale): Block.box/makeCuboidShape take 1/16 units, the rest 0..1.
SHAPE_CALLS = {'makeCuboidShape': 16, 'func_208617_a': 16, 'box': 16, 'createCuboidShape': 16, 'create': 1, 'func_197873_a': 1, 'cuboid': 1}


def shape_box(prog, v):
    """A constant AABB / VoxelShape value -> [x0,y0,z0,x1,y1,z1] in 1/16 (None if not one constant box), approx flag."""
    v = unwrap(v, prog)
    args, scale = None, None
    if isinstance(v, Ret) and v.mname in SHAPE_CALLS:
        args, scale = v.args, SHAPE_CALLS[v.mname]
        if v.mname == 'box' and simple(getattr(v, 'owner', '') or '') in ('Shapes', 'VoxelShapes'):
            scale = 1                     # Mojang Shapes.box(0..1); Block.box is 1/16
        if v.mname == 'cuboid' and simple(getattr(v, 'owner', '') or '') == 'Block':
            scale = 16
    elif isinstance(v, Obj) and simple(v.cls) in ('AxisAlignedBB', 'AABB', 'Box') and v.args:
        args, scale = v.args, 1
    if not args or len(args) != 6:
        return None, False
    vals = [num(unwrap(a, prog)) for a in args]
    if any(x is None for x in vals):
        return None, False
    f = [float(x) * (16 / scale) for x in vals]
    box = [max(0, min(16, int(round(x)))) for x in f]
    approx = any(abs(a - b) > 1e-6 for a, b in zip(f, box))
    lo, hi = [min(box[a], box[a + 3]) for a in range(3)], [max(box[a], box[a + 3]) for a in range(3)]
    return lo + hi, approx


def block_shape(prog, cls, bases, w):
    """API 1.7 shape fields for a block class: {'box': [...]} / {'no_collision': True} plus notes."""
    f, notes = {}, []
    for b in bases:
        if b in BASE_BOXES:
            f['box'] = list(BASE_BOXES[b])
            notes.append(('auto', f'{w}: {b} outline -> box {f["box"]} (1.95 holds one box: no top/double halves, layers or states)'))
            break
    if any(b in BASE_NO_COLLISION for b in bases):
        f['no_collision'] = True
    vals, where = _method_return(prog, cls, SHAPE_METHODS, want_num=False)
    if where:
        vals = vals if isinstance(vals, list) else []
        # the interpreter follows one path: a shape method with branches or several returns is state-dependent
        for cf, m in _methods_named(prog, cls, SHAPE_METHODS):
            ops = [i.op for i in m.code.insns(cf.cp)]
            if sum(o.endswith('return') for o in ops) > 1 or any(o.startswith('if') or o.endswith('switch') for o in ops):
                vals = [Unknown(why='branch')]
            break
        boxes = []
        for v in vals:
            bx, approx = shape_box(prog, v)
            if bx is None:
                boxes = None
                break
            boxes.append((bx, approx))
        if boxes and len({tuple(b) for b, _ in boxes}) == 1:
            bx, approx = boxes[0]
            if bx[0] < bx[3] and bx[1] < bx[4] and bx[2] < bx[5] and bx != [0, 0, 0, 16, 16, 16]:
                f['box'] = bx
                notes.append(('guessed' if approx else 'auto', f'{w}: {where} returns one constant box -> box {bx}' +
                              (' (rounded to 1/16)' if approx else '')))
            elif bx == [0, 0, 0, 16, 16, 16]:
                f.pop('box', None)
        else:
            notes.append(('manual', f'{w}: {where} is not one constant box (state-dependent or several boxes); '
                                    + ('kept the base outline' if 'box' in f else 'ported as a full cube')))
    return f, notes


# ---- blocks ------------------------------------------------------------------------------
def describe_block(prog, reg, rr, era):
    obj = reg.obj
    f, notes = {}, []
    w = f'block {reg.name}'
    chain = prog.chain(obj.cls)
    bases = [simple(c) for c in chain]
    base, bargs = _vanilla_init(prog, obj)
    sf, sn = block_shape(prog, obj.cls, bases, w)
    f.update(sf)
    notes += sn
    shape = [b for b in bases if b in VN.BLOCK_SHAPE_SPECIAL and b not in BASE_BOXES]
    if shape and 'box' not in f:
        notes.append(('manual', f'{w}: {shape[0]} shape/state behaviour cannot be expressed in mod.json; ported as a full cube'))
    if any(b in VN.BLOCK_CONTAINER for b in bases):
        notes.append(('manual', f'{w}: block entity / container ({base}); for a plain inventory set storage_slots: 9 by hand '
                                'or 27 (retail 1.95: 9 = DispenserBlockEntity with the 3x3 screen, 27 = a wooden ChestBlock with the chest screen; '
                                '54 = two 27-slot blocks side by side; the screen is titled with the block name; other sizes are refused, custom GUIs are not possible)'))
    if any(b in VN.TRANSPARENT_BASES for b in bases):
        f['transparent'] = True
    mat = next((a for a in bargs if isinstance(a, SField) and simple(a.owner) == 'Material'), None)
    props = _props(obj)
    if mat is None and props is not None:
        # 1.13-1.16 MCP: Block.Properties.create(Material.X[, MaterialColor]); Mojang: BlockBehaviour.Properties.of(Material.X)
        p = props
        for _ in range(64):
            if not isinstance(p, Obj):
                break
            if isinstance(p, Ret) and p.mname in ('create', 'of') and p.args and isinstance(p.args[0], SField) and simple(p.args[0].owner) == 'Material':
                mat = p.args[0]
                break
            p = p.recv if isinstance(p, Ret) else None
    if mat:
        m = VN.material_195(mat.name)
        if m:
            f['material'] = m
        else:
            notes.append(('guessed', f'{w}: material {mat.name} -> stone'))
    h = _last(obj, 'setHardness', 'hardness')
    if h and num(h[0]) is not None:
        f['hardness'] = float(num(h[0]))
    elif num(obj.fields.get('blockHardness')) is not None:
        f['hardness'] = float(num(obj.fields['blockHardness']))
    r = _last(obj, 'setResistance', 'resistance')
    if r and num(r[0]) is not None:
        f['resistance'] = round(float(num(r[0])) * 3 / 5, 3) if era == 'old' else float(num(r[0]))
        if era == 'old':
            notes.append(('auto', f'{w}: setResistance({num(r[0])}) -> blast resistance {f["resistance"]} (x3/5, 1.7-1.12 scale)'))
    if _calls(obj, 'setBlockUnbreakable'):
        f['hardness'] = 100000.0
        f['resistance'] = 6000000.0 / 1000
        notes.append(('guessed', f'{w}: unbreakable -> hardness 100000'))
    ll = _last(obj, 'setLightLevel')
    if ll and num(ll[0]) is not None:
        f['light'] = max(0, min(15, int(round(num(ll[0]) * 15))))
    snd = _last(obj, 'setStepSound', 'setSoundType', 'sound')
    if snd:
        s = snd[0]
        nm = s.name.lower() if isinstance(s, SField) else None
        if nm and nm in VN.SOUND_MAP:
            f['sound'] = VN.SOUND_MAP[nm]
        elif s is not None:
            notes.append(('guessed', f'{w}: step sound {s.short()} -> stone'))
    hv = _calls(obj, 'setHarvestLevel')
    for a in hv:
        if a and str_val(a[0]) and len(a) >= 2 and num(a[1]) is not None:
            tool = str_val(a[0])
            if tool in ('pickaxe', 'axe', 'shovel', 'hoe', 'shears', 'sword'):
                f['harvest_tool'] = tool
            f['harvest_tier'] = VN.tier_from_harvest(num(a[1]))
    tab = _last(obj, 'setCreativeTab')
    if tab:
        g = _tab(tab[0])
        if g:
            f['creative_tab'] = g
    if props is not None:
        pchain = []
        p = props
        while isinstance(p, Obj) and len(pchain) < 64:
            pchain.append(p)
            p = p.recv if isinstance(p, Ret) else None
        for p in pchain:
            if isinstance(p, Ret) and p.mname in ('ofFullCopy', 'copy', 'ofLegacyCopy', 'of') and p.args and isinstance(p.args[0], SField):
                notes.append(('guessed', f'{w}: properties copied from vanilla {p.args[0].short()}; hardness/sound left at defaults'))
        for n, d, a, o in props.calls:
            if n == 'hardnessAndResistance' and a:   # 1.13-1.16 MCP name of strength()
                n = 'strength'
            if n == 'strength' and a:
                hv1 = num(a[0])
                if hv1 is not None:
                    f['hardness'] = float(hv1)
                    f['resistance'] = float(num(a[1])) if len(a) > 1 and num(a[1]) is not None else float(hv1)
            elif n == 'destroyTime' and a and num(a[0]) is not None:
                f['hardness'] = float(num(a[0]))
            elif n == 'explosionResistance' and a and num(a[0]) is not None:
                f['resistance'] = float(num(a[0]))
            elif n in ('instabreak', 'zeroHardnessAndResistance'):
                f['hardness'] = 0.0
                if n == 'zeroHardnessAndResistance':
                    f['resistance'] = 0.0
            elif n == 'lightValue' and a and num(a[0]) is not None:   # 1.13-1.15 MCP: lightValue(int)
                f['light'] = max(0, min(15, int(num(a[0]))))
            elif n == 'harvestLevel' and a and num(a[0]) is not None:  # Forge 1.13-1.16 Block.Properties
                f['harvest_tier'] = VN.tier_from_harvest(num(a[0]))
            elif n == 'harvestTool' and a and isinstance(a[0], SField):
                t = a[0].name.lower()
                if t in ('pickaxe', 'axe', 'shovel', 'hoe', 'shears', 'sword'):
                    f['harvest_tool'] = t
            elif n == 'doesNotBlockMovement':
                f['no_collision'] = True
            elif n == 'slipperiness' and a and num(a[0]) is not None:
                f['slipperiness'] = float(num(a[0]))
            elif n == 'tickRandomly':
                notes.append(('manual', f'{w}: random ticks (block_tick behaviour) need a script'))
            elif n == 'noDrops':
                notes.append(('manual', f'{w}: noDrops (drops nothing) has no mod.json field; the block drops itself on 1.95'))
            elif n == 'variableOpacity':
                f['transparent'] = True
            elif n == 'sound' and a and isinstance(a[0], SField):
                f['sound'] = VN.SOUND_MAP.get(a[0].name.lower(), 'stone')
                if a[0].name.lower() not in VN.SOUND_MAP:
                    notes.append(('guessed', f'{w}: sound {a[0].short()} -> stone'))
            elif n == 'lightLevel' and a:
                lv = a[0]
                if isinstance(lv, Lambda):
                    try:
                        lv = prog.invoke_lambda(lv, [Unknown()], 0)
                    except Exception:  # noqa: BLE001
                        lv = None
                if num(lv) is not None:
                    f['light'] = max(0, min(15, int(num(lv))))
                else:
                    notes.append(('manual', f'{w}: light level depends on block state; not ported'))
            elif n == 'requiresCorrectToolForDrops':
                f.setdefault('harvest_tier', 'wood')
            elif n in ('noOcclusion', 'noOcclusion'):
                f['transparent'] = True
            elif n in ('noCollission', 'noCollision'):
                f['no_collision'] = True
            elif n == 'friction' and a and num(a[0]) is not None:
                f['slipperiness'] = float(num(a[0]))
            elif n == 'replaceable':
                f['replaceable'] = True
            elif n == 'randomTicks':
                notes.append(('manual', f'{w}: random ticks (block_tick behaviour) need a script'))
            elif n == 'mapColor' and a and isinstance(a[0], SField):
                mc = a[0].name.lower()
                for k, v in (('wood', 'wood'), ('stone', 'stone'), ('metal', 'metal'), ('dirt', 'dirt'), ('sand', 'sand'), ('wool', 'wool'),
                             ('plant', 'plant'), ('grass', 'dirt'), ('ice', 'glass')):
                    if k in mc and 'material' not in f:
                        f['material'] = v
    drop, dw = _method_return(prog, obj.cls, ('getItemDropped',), want_num=False)
    if dw:
        vals = drop if isinstance(drop, list) else None
        if vals and len({id(v) for v in vals}) == 1:
            k = rr.key(vals[0], f'{w} drop ({dw})')
            if k:
                f['drop'] = k
                notes.append(('auto', f'{w}: drops {k} ({dw})'))
        else:
            notes.append(('manual', f'{w}: getItemDropped ({dw}) is not a single constant item'))
    q, qw = _method_return(prog, obj.cls, ('quantityDropped',), want_num=False)
    if qw:
        if isinstance(q, int):
            f['drops'] = [q, q]
        elif isinstance(q, list) and len(q) == 1 and value_range_safe(q[0]):
            lo, hi = value_range_safe(q[0])
            f['drops'] = [max(0, int(lo)), min(64, int(hi))]
            notes.append(('auto', f'{w}: drop count {f["drops"]} from {qw}'))
        else:
            notes.append(('manual', f'{w}: quantityDropped ({qw}) not constant'))
    tex = _last(obj, 'setBlockTextureName', 'setTextureName')
    if tex and str_val(tex[0]):
        reg.texture = str_val(tex[0])
    reg.unloc = _unloc(obj)
    notes.insert(0, ('auto', f'block {reg.name}: from {simple(obj.cls)} ({reg.where}), vanilla base {simple(base)}'))
    return f, notes


def value_range_safe(v):
    from .extract import value_range
    try:
        return value_range(v)
    except (TypeError, ValueError):
        return None


# ---- metadata sub-items (1.7.10-1.12 setHasSubtypes) -------------------------------------------------------
def _methods_named(prog, cls, names):
    for c in prog.chain(cls):
        cf = prog.classes.get(c)
        if cf is None:
            return
        for m in cf.methods:
            if m.name in names and m.code is not None:
                yield cf, m


def _call_on(prog, cf, m, this, args):
    from .interp import Frame, ROOT_BUDGET
    fr = Frame(cf.name, m)
    try:
        v = prog.run(cf, m, args, this, 1, fr, ROOT_BUDGET // 2)
    except Exception:  # noqa: BLE001 - untrusted bytecode: any interpretation failure means "unknown"
        return None, fr
    return v, fr


def _stack(obj, meta):
    st = Obj('net/minecraft/item/ItemStack', site=('variant', id(obj), meta))
    st.args = [obj, Const(1, 'I'), Const(meta, 'I')]
    st.meta['stack_meta'] = meta
    return st


def has_subtypes(obj):
    return bool(_calls(obj, 'setHasSubtypes')) or bool(num(obj.fields.get('hasSubtypes')))


def item_variants(prog, reg):
    """Sub-items of a metadata item -> [dict(meta, unloc, icon, heal, sat)] (meta order), or [] when not provable.

    Metas come from the getSubItems override (the creative list, loops unrolled); names from getUnlocalizedName(stack)
    (getTranslationKey in 1.12 MCP) evaluated per meta; 1.7.10 icons from getIconFromDamage(meta) after registerIcons."""
    obj = reg.obj
    if not isinstance(obj, Obj) or not prog.is_mod(obj.cls) or not has_subtypes(obj):
        return []
    metas = []
    for cf, m in _methods_named(prog, obj.cls, ('getSubItems',)):
        from .classfile import parse_method_desc
        lst = Obj('java/util/ArrayList', site=('variants', id(obj)))
        lst.meta['coll'] = []
        args = []
        for d in parse_method_desc(m.desc)[0]:
            dc = desc_class_simple(d)
            args.append(obj if dc == 'Item' else lst if dc in ('List', 'NonNullList') else Unknown(why='arg'))
        _call_on(prog, cf, m, obj, args)
        for st in lst.meta['coll']:
            if isinstance(st, Obj) and simple(st.cls) == 'ItemStack' and st.args:
                it = unwrap(st.args[0], prog)
                mv = num(st.args[2]) if len(st.args) > 2 else 0
                if (it is obj or (isinstance(st.args[0], Param))) and isinstance(mv, int) and mv not in metas:
                    metas.append(mv)
        break
    if len(metas) < 2 or len(metas) > 256:
        return []
    out = []
    unloc_m = list(_methods_named(prog, obj.cls, ('getUnlocalizedName', 'getTranslationKey')))
    unloc_m = [(cf, m) for cf, m in unloc_m if m.desc.startswith('(L') and 'ItemStack' in m.desc]
    icons = list(_methods_named(prog, obj.cls, ('registerIcons',)))
    for cf, m in icons[:1]:
        _call_on(prog, cf, m, obj, [Unknown(why='iconreg')])
    icon_m = [(cf, m) for cf, m in _methods_named(prog, obj.cls, ('getIconFromDamage', 'getIconIndex', 'getIcon'))
              if m.desc.startswith('(I)') or m.desc.startswith('(Lnet/minecraft/item/ItemStack;')]
    dur_m = [(cf, m) for cf, m in _methods_named(prog, obj.cls, ('getMaxItemUseDuration',)) if 'ItemStack' in m.desc]
    heal_m = [(cf, m) for cf, m in _methods_named(prog, obj.cls, ('getHealAmount',)) if 'ItemStack' in m.desc]
    sat_m = [(cf, m) for cf, m in _methods_named(prog, obj.cls, ('getSaturationModifier',)) if 'ItemStack' in m.desc]
    models = {}
    for c in prog.chain(obj.cls):
        cf = prog.classes.get(c)
        if cf is None:
            break
        for m in cf.methods:
            if m.code is None or m.name.startswith('<'):
                continue
            try:
                hit = any(i.op.startswith('invoke') and i.op != 'invokedynamic' and i.arg[1] == 'setCustomModelResourceLocation'
                          for i in m.code.insns(cf.cp))
            except Exception:  # noqa: BLE001
                hit = False
            if not hit:
                continue
            from .classfile import parse_method_desc
            _r, fr = _call_on(prog, cf, m, obj, [Unknown(why='arg') for _ in parse_method_desc(m.desc)[0]])
            for ev in fr.events:
                if ev.name == 'setCustomModelResourceLocation' and len(ev.args) >= 3 and isinstance(ev.args[2], Obj) and ev.args[2].args:
                    mv2 = num(ev.args[1])
                    mrl = ev.args[2]
                    nm = str_val(mrl.args[0])
                    if nm is None:
                        rn = resource_name(mrl.args[0])
                        nm = (f'{rn[0]}:{rn[1]}' if rn[0] else rn[1]) if rn else None
                    if isinstance(mv2, int) and nm:
                        models.setdefault(mv2, (nm, str_val(mrl.args[1]) if len(mrl.args) > 1 else 'inventory'))
    for mv in metas:
        v = dict(meta=mv)
        if mv in models:
            v['model'] = models[mv]
        if unloc_m:
            r, _ = _call_on(prog, unloc_m[0][0], unloc_m[0][1], obj, [_stack(obj, mv)])
            v['unloc'] = str_val(r)
        for cf_i, m_i in icon_m:
            from .classfile import parse_method_desc
            pa = [Const(mv, 'I') if d == 'I' else _stack(obj, mv) if 'ItemStack' in d else Unknown(why='arg')
                  for d in parse_method_desc(m_i.desc)[0]]
            _r, fr = _call_on(prog, cf_i, m_i, obj, pa)
            ics = [str_val(x.args[0]) for _pc, x in fr.returns if isinstance(x, Ret) and x.mname == 'registerIcon' and x.args]
            if ics and ics[0]:
                v['icon'] = ics[0]
                break
        if dur_m:
            r, _ = _call_on(prog, dur_m[0][0], dur_m[0][1], obj, [_stack(obj, mv)])
            if isinstance(num(r), int):
                v['eat_ticks'] = num(r)
        for key, ms in (('heal', heal_m), ('sat', sat_m)):
            if ms:
                r, _ = _call_on(prog, ms[0][0], ms[0][1], obj, [_stack(obj, mv)])
                if num(r) is not None:
                    v[key] = num(r)
        out.append(v)
    return out


def desc_class_simple(d):
    if d.startswith('L') and d.endswith(';'):
        return d[1:-1].rsplit('/', 1)[-1]
    return d


def model_variants(prog):
    """1.8-1.12 ModelLoader.setCustomModelResourceLocation(item, meta, new ModelResourceLocation(name, variant)) calls
    -> {(site key of the item, meta): (model name, variant)}."""
    out = {}
    for ev in prog.events:
        if ev.name not in ('setCustomModelResourceLocation', 'registerItemModel') or len(ev.args) < 3:
            continue
        it = unwrap(ev.args[0], prog)
        mv = num(ev.args[1])
        mrl = ev.args[2]
        if not isinstance(it, Obj) or not isinstance(mv, int) or not isinstance(mrl, Obj) or not mrl.args:
            continue
        nm = str_val(mrl.args[0])
        if nm is None and isinstance(mrl.args[0], Obj):
            r = resource_name(mrl.args[0])
            nm = (f'{r[0]}:{r[1]}' if r[0] else r[1]) if r else None
        var = str_val(mrl.args[1]) if len(mrl.args) > 1 else 'inventory'
        if nm:
            out.setdefault((site_key(it), mv), (nm, var))
    return out
