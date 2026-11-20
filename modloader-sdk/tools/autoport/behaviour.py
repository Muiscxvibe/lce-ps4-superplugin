"""Behaviour overrides -> Lua stubs (scripts/main.lua) for the SDK script layer.

The binding is modules/script/lce_script.c (Lua 5.4, API_STATUS.md G15).  The stubs follow it:
event names are the LCE_EV_* enum without the prefix, payload fields are the C
struct field names, services are the lce_api 1.1 runtime services.  All binding
names used are in LUA_BINDING below so they can be changed in one place.

Only provable patterns are translated: a method whose every side effect is a
constant potion effect, a constant heal (lce.heal), a constant set-on-fire
(lce.set_fire) or a constant chat message.  Anything else becomes a commented manual stub with
the method's Java (CFR, --decompile) or a bytecode listing.
"""
import os
import re
import shutil
import subprocess
import uuid
from pathlib import Path

from .classfile import disassemble, parse_method_desc, desc_class, ACC_STATIC, ACC_SYNTHETIC, ACC_ABSTRACT
from .interp import Obj, Ret, SField, Const, Param, SelfRef, FieldOf, Unknown, str_val
from .extract import simple, inner, num
from . import vanilla as VN

CFR_JAR = '/home/codey/crazy-craft-supermod/toolchain/cfr-0.152.jar'
JAVA = '/usr/bin/java'

LUA_BINDING = {
    'subscribe': 'lce.subscribe',          # lce.subscribe(lce.EV.<NAME>, function(ev) ... return 0 end)
    'ev': 'lce.EV',
    'resolve_item': 'lce.resolve_item',
    'resolve_block': 'lce.resolve_block',
    'add_effect': 'lce.add_effect',        # lce.add_effect(entity, {effect=, duration_ticks=, amplifier=})
    'send_chat': 'lce.send_chat',          # lce.send_chat(player_or_nil, text)
    'give_item': 'lce.give_item',
    'drop_item': 'lce.drop_item',
    'log': 'lce.log',
    # world services (modules/script/lce_script.c) and API 1.2 services
    'set_block': 'lce.set_block',          # lce.set_block(level, x, y, z, block, data)
    'get_block': 'lce.get_block',          # -> id | meta << 12
    'spawn_entity': 'lce.spawn_entity',    # lce.spawn_entity(level, key, x, y, z)
    'play_sound': 'lce.play_sound',        # lce.play_sound(level, x, y, z, sound)
    'explode': 'lce.explode',              # lce.explode(level, x, y, z, radius, fire, destroy_blocks)
    'set_fire': 'lce.set_fire',            # lce.set_fire(entity, ticks)
    'heal': 'lce.heal',                    # lce.heal(entity, amount)
    'damage_entity': 'lce.damage_entity',
    'damage_held_item': 'lce.damage_held_item',
    'consume_held_item': 'lce.consume_held_item',
    'teleport': 'lce.teleport',
    'set_motion': 'lce.set_motion',
    'spawn_particles': 'lce.spawn_particles', 'item_tag_get': 'lce.item_tag_get', 'item_tag_set': 'lce.item_tag_set',
    'item_tag_remove': 'lce.item_tag_remove', 'send_payload': 'lce.send_payload',
    # API 1.5 entity NBT: read-only vanilla save data and persistent per-entity mod data (never on players)
    'entity_nbt_get': 'lce.entity_nbt_get', 'entity_data_get': 'lce.entity_data_get', 'entity_data_set': 'lce.entity_data_set',
    'entity_data_remove': 'lce.entity_data_remove',
    'send_to_server': 'lce.send_to_server',   # API 1.6 client -> server (event ClientPayload is server -> client)
}

KIND_EVENT = {  # behaviour kind -> (LCE event or None, filter field, note)
    'item_use': ('ITEM_USE', 'item', ''),
    'item_finish': ('ITEM_USE', 'item', 'approximation: ITEM_USE fires when use starts, not when eating finishes'),
    'hit_entity': (None, None, 'ENTITY_DAMAGE carries no held item, so the attacker weapon cannot be filtered'),
    'inventory_tick': ('ITEM_INVENTORY_TICK', 'item', 'every tick per inventory slot holding the item (ev.slot, ev.selected); onArmorTick: worn-only filtering is up to the handler'),
    'mine_block': (None, None, 'BLOCK_BREAK carries no held item'),
    'interact_entity': ('ENTITY_INTERACT', 'entity', 'item-on-entity use: ENTITY_INTERACT fires for mod-spawned entities'),
    'crafted': (None, None, 'no crafting event in lce_api 1.1'),
    'release_use': (None, None, 'no release-use event'), 'using_tick': (None, None, 'no using-tick event'),
    'dropped': (None, None, 'no item-toss event'), 'entity_item_tick': (None, None, 'no item-entity tick'),
    'block_use': ('BLOCK_USE', 'block', ''),
    'step_on': ('BLOCK_STEP_ON', 'block', 'ev.entity is any entity walking on the block'),
    'entity_inside': ('BLOCK_ENTITY_INSIDE', 'block', 'ev.entity is any entity inside the block cell (non-full shapes only call it)'),
    'block_tick': ('BLOCK_TICK', 'block', 'scheduled and random ticks; random ticks need the block registered as random-ticking'), 'block_break': ('BLOCK_BREAK', 'block', ''),
    'block_place': ('BLOCK_PLACE', 'block', ''), 'block_attack': (None, None, 'no block-attack event'),
    'client_fx': (None, None, 'client-side particles/animation: impossible on 1.95 from mods'),
    'neighbor': ('BLOCK_NEIGHBOR', 'block', 'ev.face = side of the changed neighbour (-1 not adjacent)'), 'fall_on': (None, None, 'no fall-on event'),
    'entity_tick': ('ENTITY_TICK', 'entity', 'mod-spawned entities only'),
    'entity_attack': (None, None, 'no per-entity attack hook; ENTITY_DAMAGE has no attacker type filter'),
    'entity_interact': ('ENTITY_INTERACT', 'entity', 'mod-spawned entities only'), 'entity_death': ('ENTITY_DEATH', 'entity', ''),
    'entity_hurt': ('ENTITY_DAMAGE', 'entity', ''), 'entity_touch': (None, None, 'no entity-touch event'),
    'entity_misc': (None, None, 'no equivalent event'), 'effect_tick': ('EFFECT_TICK', 'effect', ''),
}

EFFECT_CALLS = {'addPotionEffect', 'addEffect', 'heal', 'setFire', 'setSecondsOnFire', 'igniteForSeconds', 'addChatMessage', 'addChatComponentMessage', 'sendMessage', 'sendSystemMessage',
                'displayClientMessage', 'sendStatusMessage'}
PURE_PREFIX = ('get', 'is', 'has', 'can', 'func_', 'equals', 'hashCode', 'toString', 'valueOf', 'next', 'random', 'nextInt',
               'nextFloat', 'nextDouble', 'nextBoolean', 'isClientSide', 'level', 'getLevel', 'translate', 'format', 'literal',
               'translatable', 'getCommandSenderName', 'getDisplayName', 'name', 'success', 'pass', 'fail', 'consume', 'sidedSuccess',
               'of', 'empty', 'getX', 'getY', 'getZ', 'blockPosition', 'position')
NON_TRANSLATABLE = {
    'createExplosion': 'explosion (lce.explode with ev.level/x/y/z)', 'newExplosion': 'explosion (lce.explode)',
    'explode': 'explosion (lce.explode needs the position: translate by hand)', 'setFire': 'set on fire (seconds not constant)',
    'setSecondsOnFire': 'set on fire (seconds not constant)', 'igniteForSeconds': 'set on fire (seconds not constant)',
    'spawnEntityInWorld': 'spawn entity (lce.spawn_entity with ev.level/x/y/z)',
    'addFreshEntity': 'spawn entity (lce.spawn_entity)', 'setBlock': 'set block (lce.set_block with ev.level)',
    'setBlockState': 'set block', 'setBlockToAir': 'set block', 'removeBlock': 'set block', 'destroyBlock': 'set block',
    'damageItem': 'item durability (lce.damage_held_item(player, n) for the held stack: translate by hand)', 'hurtAndBreak': 'item durability (lce.damage_held_item: translate by hand)',
    'shrink': 'consume item (lce.consume_held_item for the held stack: translate by hand)',
    'dropItem': 'drop item (drop_item needs a position, not in this payload)', 'entityDropItem': 'drop item (needs a position)',
    'spawnAtLocation': 'drop item (lce.drop_item with ev.level/x/y/z)', 'attackEntityFrom': 'damage entity (lce.damage_entity; attacker = the event player only)', 'hurt': 'damage entity (lce.damage_entity)',
    'playSound': 'play sound (lce.play_sound with ev.level/x/y/z)', 'playSoundAtEntity': 'play sound', 'playSoundEffect': 'play sound',
    'setHealth': 'set health (lce.heal for increases; no direct setter)', 'setPositionAndUpdate': 'teleport (lce.teleport)', 'teleportTo': 'teleport (lce.teleport)',
    'addExperience': 'experience (no service)', 'giveExperiencePoints': 'experience (no service)', 'addWeatherEffect': 'lightning (no service)',
    'setDead': 'remove entity (no service)', 'discard': 'remove entity (no service)', 'kill': 'remove entity', 'openGui': 'GUI: impossible on 1.95',
    'openMenu': 'GUI: impossible on 1.95', 'displayGui': 'GUI: impossible on 1.95', 'spawnParticle': 'particles (client side)',
    'addParticle': 'particles (client side)', 'getTagCompound': 'item NBT (lce.item_tag_get/set on the held stack)', 'getOrCreateTag': 'item NBT (lce.item_tag_get/set on the held stack)', 'setTagCompound': 'item NBT (lce.item_tag_set per key)', 'getTag': 'item NBT (lce.item_tag_get)', 'sendToServer': 'mod networking client -> server (lce.send_to_server(channel, bytes); replies arrive as ClientPayload)', 'sendTo': 'mod networking (lce.send_payload)', 'sendParticles': 'particles (lce.spawn_particles with a ParticleType id, API 1.3)', 'addStat': 'statistics', 'triggerAchievement': 'achievements',
    'setItemStackToSlot': 'inventory edit (no service)', 'setItemInHand': 'inventory edit', 'addItemStackToInventory': 'inventory edit (give_item?)',
    'add': 'collection/inventory edit', 'setTarget': 'AI target', 'setAttackTarget': 'AI target', 'setVelocity': 'motion (lce.set_motion)', 'setDeltaMovement': 'motion (lce.set_motion)',
    'addVelocity': 'motion (lce.set_motion)', 'push': 'motion (lce.set_motion)', 'setTileEntity': 'block entity', 'markDirty': 'block entity', 'setChanged': 'block entity',
    # entity NBT (API 1.5)
    'getPersistentData': 'entity mod data (lce.entity_data_get/set/remove per key; saved with the entity, not on players)',
    'getEntityData': 'entity mod data (lce.entity_data_get/set/remove per key; not on players)',
    'writeToNBT': 'entity NBT read (lce.entity_nbt_get(entity, "Key") / "List[i]" paths; read-only)',
    'saveWithoutId': 'entity NBT read (lce.entity_nbt_get paths; read-only)', 'serializeNBT': 'entity NBT read (lce.entity_nbt_get paths)',
    'readFromNBT': 'entity NBT write (vanilla keys are read-only; mod keys via lce.entity_data_set)',
    'deserializeNBT': 'entity NBT write (vanilla keys are read-only; mod keys via lce.entity_data_set)',
    'addTag': 'entity tag (lce.entity_data_set(entity, "modid:tag", 1))', 'removeTag': 'entity tag (lce.entity_data_remove)',
}


def _resolve_effect(v):
    """PotionEffect(Const|SField.id, dur, amp) -> dict or None."""
    if not isinstance(v, Obj) or not v.args or simple(v.cls) not in ('PotionEffect', 'MobEffectInstance', 'StatusEffectInstance'):
        return None
    a = v.args
    e = a[0]
    name = None
    if isinstance(e, Const) and isinstance(e.v, int):
        name = VN.EFFECT_BY_ID.get(e.v)
    elif isinstance(e, FieldOf) and isinstance(e.obj, SField):
        name = VN.EFFECT_FIELDS.get(e.obj.name.lower())
    elif isinstance(e, Ret) and isinstance(e.recv, SField):
        name = VN.EFFECT_FIELDS.get(e.recv.name.lower())
    elif isinstance(e, SField):
        name = VN.EFFECT_FIELDS.get(e.name.lower())
    dur = num(a[1]) if len(a) > 1 else None
    amp = num(a[2]) if len(a) > 2 else 0
    if name is None or dur is None or amp is None:
        return None
    return dict(effect='minecraft:' + name, duration_ticks=int(dur), amplifier=int(amp))


def _chat_text(v):
    if isinstance(v, Obj) and v.args and simple(v.cls) in ('ChatComponentText', 'TextComponentString', 'StringTextComponent',
                                                          'TextComponent', 'LiteralText'):
        return str_val(v.args[0])
    if isinstance(v, Ret) and v.mname in ('literal', 'nullToEmpty') and v.args:
        return str_val(v.args[0])
    return None


def translate(frame, actor_param_hint=None):
    """-> (lua_lines or None, reasons). lua_lines use `ev.player` / `ev.entity` as the actor."""
    if frame is None:
        return None, ['method could not be interpreted']
    lines, reasons = [], []
    for ev in frame.events:
        n = ev.name
        if ev.op == 'invokespecial' and n == '<init>':
            continue
        if n in ('addPotionEffect', 'addEffect') and ev.args:
            e = _resolve_effect(ev.args[0])
            if e is None:
                reasons.append(f'{n}: effect arguments are not constant')
                continue
            lines.append(f'{LUA_BINDING["add_effect"]}(ACTOR, {{effect = "{e["effect"]}", duration_ticks = {e["duration_ticks"]}, '
                         f'amplifier = {e["amplifier"]}}})')
        elif n == 'heal' and ev.args:
            h = num(ev.args[0])
            if h is None or not 0 < h <= 1024:
                reasons.append(f'heal({h}): amount is not a constant')
                continue
            lines.append(f'{LUA_BINDING["heal"]}(ACTOR, {h:g})  -- heal({h})')
        elif n in ('setFire', 'setSecondsOnFire', 'igniteForSeconds') and ev.args and num(ev.args[0]) is not None and 0 <= num(ev.args[0]) <= 3600:
            lines.append(f'{LUA_BINDING["set_fire"]}(ACTOR, {int(num(ev.args[0]) * 20)})  -- {n}({num(ev.args[0]):g} s)')
        elif n in ('addChatMessage', 'addChatComponentMessage', 'sendMessage', 'sendSystemMessage', 'displayClientMessage',
                   'sendStatusMessage') and ev.args:
            t = _chat_text(ev.args[0])
            if t is None:
                reasons.append(f'{n}: message is not a constant string')
                continue
            lines.append(f'{LUA_BINDING["send_chat"]}(ACTOR, {lua_str(t)})')
        elif n in NON_TRANSLATABLE:
            reasons.append(f'{n}: {NON_TRANSLATABLE[n]}')
        elif desc_returns_void(ev.desc) and not n.startswith(PURE_PREFIX) and n != '<init>':
            reasons.append(f'{simple(ev.owner)}.{n}: side effect with no lce_api 1.1 equivalent')
    for pc, v in frame.returns:
        if isinstance(v, Obj) and simple(v.cls) == 'ItemStack' and v.args and not isinstance(v.args[0], (Param, SelfRef)):
            reasons.append(f'returns {v.args[0].short()} after use (container item such as a bowl): no mod.json/lce_api equivalent')
            break
    for pc, owner, name, v in frame.stores:
        reasons.append(f'writes field {simple(owner)}.{name} (mod state)')
        break
    if reasons or not lines:
        if not lines and not reasons:
            reasons.append('no side effects recognised (pure override / return value only)')
        return (lines or None), reasons
    return lines, []


def effects_only(frame):
    """If every side effect of the method is a constant addPotionEffect on the eater, return the effects."""
    out = []
    for ev in frame.events:
        if ev.name == '<init>':
            continue
        if ev.name in ('addPotionEffect', 'addEffect') and ev.args:
            e = _resolve_effect(ev.args[0])
            if e is None:
                return None
            out.append(e)
        elif ev.op == 'invokespecial' and ev.name in ('onFoodEaten', 'onItemUseFinish', 'finishUsingItem'):
            continue
        elif desc_returns_void(ev.desc) and not ev.name.startswith(PURE_PREFIX):
            return None
    return out or None


def desc_returns_void(d):
    try:
        return parse_method_desc(d)[1] == 'V'
    except Exception:  # noqa: BLE001
        return False


def lua_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\r', '\\r') + '"'


def lua_comment_block(text):
    """Long comment that cannot be terminated early by the content."""
    level = 1
    while f']{"=" * level}]' in text:
        level += 1
    eq = '=' * level
    return f'--[{eq}[\n{text}\n]{eq}]'


# ---- discovery ------------------------------------------------------------------
def overrides(prog, cls, table, data_methods):
    """[(class, method, kind or None)] for behaviour methods declared in mod classes of cls's chain."""
    out = []
    seen = set()
    for c in prog.chain(cls):
        cf = prog.classes.get(c)
        if cf is None:
            break
        for m in cf.methods:
            if m.name in ('<init>', '<clinit>') or m.access & (ACC_STATIC | ACC_SYNTHETIC | ACC_ABSTRACT) or m.code is None:
                continue
            if m.name.startswith(('lambda$', 'access$')) or (m.name, m.desc) in seen:
                continue
            seen.add((m.name, m.desc))
            if m.name in table:
                out.append((cf, m, table[m.name]))
            elif m.name in data_methods:
                continue
            else:
                out.append((cf, m, None))
    return out


# ---- CFR ---------------------------------------------------------------------------
class Decompiler:
    """Optional CFR decompilation of selected classes into a fresh scratch directory."""

    def __init__(self, jar, work_root, reverse_names=None, timeout=90):
        self.jar, self.timeout = jar, timeout
        self.ok = os.path.isfile(CFR_JAR) and os.access(JAVA, os.X_OK)
        self.root = Path(work_root) / f'cfr-{uuid.uuid4().hex[:12]}'
        self.cache = {}
        self.reverse = reverse_names or {}
        self.error = None if self.ok else f'CFR not available ({CFR_JAR})'

    def source(self, cls_name, original_name=None):
        if not self.ok:
            return None
        if cls_name in self.cache:
            return self.cache[cls_name]
        orig = original_name or cls_name
        entries = [n for n in self.jar.names() if n == orig + '.class' or (n.startswith(orig + '$') and n.endswith('.class'))]
        if not entries:
            self.cache[cls_name] = None
            return None
        d = self.root / uuid.uuid4().hex[:8]
        classes = d / 'in'
        outdir = d / 'out'
        classes.mkdir(parents=True)
        outdir.mkdir()
        files = []
        for i, n in enumerate(entries[:64]):
            p = classes / f'c{i}.class'      # never use the entry name as a path
            p.write_bytes(self.jar.read(n))
            files.append(str(p))
        try:
            subprocess.run([JAVA, '-jar', CFR_JAR, *files, '--outputdir', str(outdir), '--silent', 'true', '--comments', 'false'],
                           cwd=str(d), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=self.timeout, check=False)
        except (subprocess.TimeoutExpired, OSError) as e:
            self.error = f'CFR failed: {e}'
            self.cache[cls_name] = None
            return None
        texts = []
        for p in sorted(outdir.rglob('*.java')):
            try:
                texts.append(p.read_text(errors='replace'))
            except OSError:
                pass
        src = '\n'.join(texts) or None
        self.cache[cls_name] = src
        return src

    def method(self, cls_name, mname, original_class=None):
        src = self.source(cls_name, original_class)
        if not src:
            return None
        names = {mname} | {k for k, v in self.reverse.items() if v == mname}
        for nm in names:
            m = re.search(rf'^[ \t]*(?:@\w+[^\n]*\n[ \t]*)*(?:public|protected|private|static|final|synchronized|\s)*[\w<>\[\], .$?]+\s+{re.escape(nm)}\s*\([^)]*\)[^{{;]*\{{',
                          src, re.M)
            if not m:
                continue
            i = m.end()
            depth = 1
            while i < len(src) and depth:
                if src[i] == '{':
                    depth += 1
                elif src[i] == '}':
                    depth -= 1
                i += 1
            return src[m.start():i].rstrip()
        return None

    def cleanup(self):
        shutil.rmtree(self.root, ignore_errors=True)


# ---- stub generation -------------------------------------------------------------------
# Events the Lua binding exposes as lce.EV.<NAME> (modules/script/lce_script.c install_bindings). Anything else has no
# script event: subscribing to a nil name would stop the whole script at load (luaL_checkstring).
LUA_EVENTS = ('SERVER_TICK', 'CLIENT_TICK', 'WORLD_LOAD', 'WORLD_UNLOAD', 'BLOCK_BREAK', 'BLOCK_PLACE', 'ITEM_USE', 'ENTITY_DAMAGE',
              'ENTITY_DEATH', 'PLAYER_JOIN', 'PLAYER_LEAVE', 'CHAT', 'BLOCK_USE', 'ENTITY_INTERACT', 'BLOCK_TICK', 'ENTITY_TICK',
              'ITEM_INVENTORY_TICK', 'BLOCK_NEIGHBOR', 'BLOCK_ENTITY_INSIDE', 'BLOCK_STEP_ON')


class LuaWriter:
    """Generated scripts/main.lua.

    The binding keeps ONE callback per event (registry["lce.cb"][ev] = fn; a later lce.subscribe replaces it), and a
    callback's result is read with lua_toboolean, so any number, 0 included, cancels the event. Every stub is therefore a
    local handler returning nil/false (true only to cancel), and each event gets a single dispatcher that runs them in
    order."""

    def __init__(self, mod_id, mod_name):
        self.mod_id, self.mod_name = mod_id, mod_name
        self.blocks = []
        self.auto = 0
        self.manual = 0
        self.handlers = {}   # event -> [handler names]

    def header(self):
        b = LUA_BINDING
        return '\n'.join([
            f'-- scripts/main.lua for {self.mod_id} ({self.mod_name}), generated by tools/lce_autoport.py.',
            '-- Behaviour stubs for the SDK Lua runtime (modules/script, API_STATUS.md G15):',
            f'--   {b["subscribe"]}({b["ev"]}.ITEM_USE, fn): ONE callback per event (a later subscribe replaces it), so this file',
            '--   registers one dispatcher per event at the end; a handler returns true to cancel the event, nil/false otherwise',
            '--   (the result is read with lua_toboolean: returning 0 would cancel).',
            '--   payload fields: ev.item_id ev.aux ev.player | ev.x ev.y ev.z ev.block_id ev.player | ev.entity ev.source ev.amount ev.type_id',
            '--   API 1.3: InventoryTick ev.slot ev.selected | BlockNeighbor ev.face | BlockStepOn/BlockEntityInside ev.entity',
            f'--   {b["resolve_item"]}/{b["resolve_block"]}(key) -> numeric id (RUNNING phase only, so ids are looked up inside handlers)',
            f'--   services: {b["add_effect"]}(entity, {{effect=, duration_ticks=, amplifier=}}), {b["send_chat"]}(player, text),',
            f'--   {b["give_item"]}(player, item, count, aux), {b["drop_item"]}(level, x, y, z, item, count)',
            '-- AUTO blocks were translated from provable bytecode patterns; MANUAL blocks keep the Java for reference.',
            '',
            'local MOD = ' + lua_str(self.mod_id),
            'local H = {}',
            '',
        ])

    def add(self, title, event, filt, key, lines, reasons, java, status_note=''):
        out = [f'-- ===== {title}']
        if lines and not reasons:
            self.auto += 1
            out.append('-- AUTO' + (f' ({status_note})' if status_note else ''))
        else:
            self.manual += 1
            out.append('-- MANUAL: ' + '; '.join(reasons or [status_note or 'needs re-implementation']))
        if event and event not in LUA_EVENTS:
            out.append(f'-- no Lua event for {event} in the script binding (lce.EV has {", ".join(LUA_EVENTS)})')
            event = None
        if event:
            actor = 'ev.player' if event in ('ITEM_USE', 'BLOCK_BREAK', 'BLOCK_PLACE', 'BLOCK_USE', 'ITEM_INVENTORY_TICK') else 'ev.entity'
            resolver = LUA_BINDING['resolve_block'] if filt == 'block' else LUA_BINDING['resolve_item']
            fld = {'item': 'ev.item_id', 'block': 'ev.block_id', 'entity': 'ev.type_id', 'effect': 'ev.type_id'}.get(filt)
            hn = f'h{sum(len(v) for v in self.handlers.values()) + 1}'
            self.handlers.setdefault(event, []).append(hn)
            out.append(f'local function {hn}(ev)  -- {event}')
            if filt in ('item', 'block') and key:
                out.append(f'  if {fld} ~= {resolver}({lua_str(key)}) then return end')
            elif filt and key:
                out.append(f'  -- TODO: filter on {fld} for {key} (no resolver for this id kind in lce_api 1.1)')
            body = [l.replace('ACTOR', actor) for l in (lines or [])]
            if reasons:
                body = ['-- ' + l for l in body] + ['-- TODO(manual): re-implement from the Java below']
            out += ['  ' + l for l in body]
            out.append('end')
            out.append(f'H.{event} = H.{event} or {{}}; table.insert(H.{event}, {hn})')
        else:
            out.append(f'-- no lce_api 1.1 event for this hook')
        if java:
            out.append(lua_comment_block(java))
        self.blocks.append('\n'.join(out))

    def footer(self):
        out = ['', '-- ===== dispatchers (one lce.subscribe per event)']
        for event in self.handlers:
            out.append(f'if H.{event} then {LUA_BINDING["subscribe"]}({LUA_BINDING["ev"]}.{event}, function(ev)')
            out.append(f'  for _, h in ipairs(H.{event}) do if h(ev) then return true end end')
            out.append('  return false')
            out.append('end) end')
        return '\n'.join(out) + '\n'

    def text(self):
        return self.header() + '\n\n'.join(self.blocks) + '\n' + self.footer()


def shrink(text, limit):
    """Cut long comment blocks to their first lines until the script fits the package limit."""
    keep = 30
    while len(text.encode()) > limit and keep >= 0:
        def cut(m):
            body = m.group(2).split('\n')
            if len(body) <= keep + 2:
                return m.group(0)
            return f'--[{m.group(1)}[' + '\n'.join(body[:keep + 1]) + f'\n... (truncated)\n]{m.group(1)}]'
        text = re.sub(r'--\[(=+)\[(.*?)\]\1\]', cut, text, flags=re.S)
        keep = keep // 2 if keep > 1 else -1
    if len(text.encode()) > limit:   # still too big: drop the Java excerpts, then whole stubs from the end
        text = re.sub(r'--\[(=+)\[.*?\]\1\]\n?', '', text, flags=re.S)
    footer = ''
    if len(text.encode()) > limit and '\n-- ===== dispatchers' in text:
        text, footer = text.split('\n-- ===== dispatchers', 1)
        footer = '\n-- ===== dispatchers' + footer
    if len(text.encode()) > limit - len(footer.encode()):
        limit -= len(footer.encode())
        parts = text.split('\n-- ===== ')
        while len(parts) > 1 and len('\n-- ===== '.join(parts).encode()) > limit - 200:
            parts.pop()
        text = '\n-- ===== '.join(parts) + '\n-- (further stubs omitted: script size limit)\n'
    return text + footer


def lua_sanity(text):
    """Cheap structural check of generated Lua (no Lua interpreter is available): balanced function/end
    outside comments and strings, and balanced long comments."""
    body = re.sub(r'--\[(=*)\[.*?\]\1\]', '', text, flags=re.S)
    body = re.sub(r'--[^\n]*', '', body)
    body = re.sub(r'"(?:\\.|[^"\\])*"', '""', body)
    opens = len(re.findall(r'\bfunction\b', body)) + len(re.findall(r'\bif\b[^\n]*\bthen\b', body)) + len(re.findall(r'\bdo\b', body))
    ends = len(re.findall(r'\bend\b', body))
    return opens == ends
