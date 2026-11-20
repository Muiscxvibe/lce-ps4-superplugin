"""Forge / NeoForge config specs -> mod.json "config" (API 1.8, /data/lce-mods/<id>/config; MODDING.md 8.5).

Recovered statically from the interpreter's call trace (never executes mod code):

* ForgeConfigSpec.Builder (Forge 1.13+) and ModConfigSpec.Builder (NeoForge): comment / push / pop / define /
  defineInRange / defineEnum / defineList(AllowEmpty) / defineInList, per builder object, in call order.  The key is
  the pushed path plus the name ("general.oreRate"), the default and range come from the constant arguments.
* net.minecraftforge.common.config.Configuration (Forge 1.7.10-1.12): getInt / getBoolean / getFloat / getString /
  getStringList and get(category, key, default[, comment[, min, max]]).  The key is "<category>.<name>".

Forge keeps COMMON / CLIENT / SERVER specs in separate files; the LCE loader has one file per mod, so all specs share
one key space (a repeated key keeps the first definition and is reported).  The result also maps each static field
that holds a config value (Config.ORE_RATE) to its key, for the Lua header and the port report.
"""
import re

from .interp import Obj, Ret, Const, SField, Arr, str_val
from .extract import inner
from .classfile import parse_method_desc

BUILDERS = ('ForgeConfigSpec$Builder', 'ModConfigSpec$Builder')
CONFIGURATION = 'net/minecraftforge/common/config/Configuration'
_PART = re.compile(r'[^A-Za-z0-9_-]+')


def _is_builder(owner):
    return inner(owner) in BUILDERS


def interesting(cf, mapper=None):
    refs = cf.cp.class_refs()
    if mapper is not None:
        refs = {mapper.cls(r) for r in refs}
    return any(inner(r) in BUILDERS or r == CONFIGURATION for r in refs)


def _part(s):
    s = _PART.sub('_', s.strip()).strip('_')
    return s or None


def _strings(v):
    """String, String[] (varargs) or a List of strings -> [str] or None."""
    s = str_val(v)
    if s is not None:
        return [s]
    vals = v.values() if isinstance(v, Arr) else v.meta.get('coll') if isinstance(v, Obj) else None
    if vals is None:
        return None
    out = [str_val(x) for x in vals]
    return out if all(x is not None for x in out) else None


def _scalar(v, t=None):
    """Constant default -> python value (t: descriptor char of the parameter)."""
    if not isinstance(v, Const) or v.v is None:
        return None
    x = v.v
    if t == 'Z' or v.t == 'Z':
        return bool(x)
    if t in ('F', 'D') or v.t in ('F', 'D'):
        return float(x) if isinstance(x, (int, float)) else None
    if isinstance(x, bool):
        return x
    if isinstance(x, (int, float, str)):
        return x
    return None


def _enum_name(v):
    if isinstance(v, SField):
        return v.name
    if isinstance(v, Obj) and v.args and str_val(v.args[0]) is not None:
        return str_val(v.args[0])
    return None


def _list_default(v):
    vals = v.values() if isinstance(v, Arr) else v.meta.get('coll') if isinstance(v, Obj) else None
    if vals is None:
        return None
    out = []
    for x in vals:
        y = _scalar(x)
        if y is None:
            return None
        out.append(y)
    return out


def _params(desc):
    """'(Ljava/lang/String;II[Ljava/lang/String;)...' -> ['L', 'I', 'I', '[']."""
    return [d[0] for d in parse_method_desc(desc)[0]]


def _root(recv):
    """A chained builder call on a receiver we could not model (a Builder parameter) returns a Ret: follow it back."""
    for _ in range(64):
        if isinstance(recv, Ret) and _is_builder(recv.owner) and not recv.mname.startswith('define') and recv.mname != 'build':
            recv = recv.recv
        else:
            break
    return recv


class _Spec:
    def __init__(self):
        self.path, self.comment = [], None


def extract(prog, mapper=None):
    """-> (config dict for mod.json, notes [(kind, msg)], fields {(owner, field): key})."""
    for cf in list(prog.classes.values()):
        if interesting(cf, mapper):
            for m in cf.methods:
                if m.code is not None:
                    prog.run_root(cf.name, m.name, m.desc)
    cfg, notes, results = {}, [], {}
    specs = {}
    skipped = []

    def add(key_parts, spec, where, result):
        parts = [p for p in (_part(x) for x in key_parts) if p]
        if not parts:
            skipped.append(f'{where}: no usable key')
            return
        key = '.'.join(parts)
        if len(key) >= 64:
            skipped.append(f'{key}: key longer than 63 bytes')
            return
        d = spec['default']
        if isinstance(d, str) and len(d.encode()) > 255:
            skipped.append(f'{key}: default string longer than 255 bytes')
            return
        if key in cfg:
            if cfg[key]['default'] != d:
                notes.append(('guessed', f'config {key} is defined twice (separate Forge spec files?); kept the first default {cfg[key]["default"]!r}'))
            if result is not None:
                results[id(result)] = key
            return
        spec = {k: v for k, v in spec.items() if v is not None}
        cfg[key] = spec
        if result is not None:
            results[id(result)] = key

    for ev in prog.events:
        owner, name, args = ev.owner, ev.name, ev.args
        if _is_builder(owner):
            st = specs.setdefault(id(_root(ev.recv)), _Spec())
            if name == 'comment' and args:
                st.comment = '\n'.join(_strings(args[0]) or []) or None
            elif name == 'push' and args:
                st.path += [p for s in (_strings(args[0]) or ['?']) for p in s.split('.')]
                st.comment = None
            elif name == 'pop':
                n = args[0].v if args and isinstance(args[0], Const) and isinstance(args[0].v, int) else 1
                del st.path[max(0, len(st.path) - n):]
            elif name.startswith('define') and args:
                names = _strings(args[0])
                if not names:
                    skipped.append(f'{inner(owner)}.{name} with a computed name')
                    st.comment = None
                    continue
                key_parts = st.path + [p for s in names for p in s.split('.')]
                ps = _params(ev.desc)
                spec = dict(default=None, min=None, max=None, comment=st.comment)
                st.comment = None
                if name == 'defineInRange' and len(args) >= 4:
                    t = ps[1]
                    spec.update(default=_scalar(args[1], t), min=_scalar(args[2], t), max=_scalar(args[3], t))
                elif name == 'defineEnum' and len(args) >= 2:
                    e = _enum_name(args[1])
                    spec['default'] = e
                    if e and spec['comment'] is None:
                        spec['comment'] = 'Enum value name'
                elif name.startswith('defineList') and len(args) >= 2:
                    spec['default'] = _list_default(args[1])
                elif name in ('define', 'defineInList') and len(args) >= 2:
                    spec['default'] = _scalar(args[1], ps[1] if len(ps) > 1 else None)
                    if spec['default'] is None and isinstance(args[1], Obj) and 'coll' in args[1].meta:
                        spec['default'] = _list_default(args[1])
                if spec['default'] is None:
                    skipped.append(f'{".".join(key_parts)}: default is not a constant ({name})')
                    continue
                if isinstance(spec['default'], (bool, str, list)):
                    spec['min'] = spec['max'] = None
                add(key_parts, spec, f'{inner(owner)}.{name}', ev.result)
        elif owner == CONFIGURATION and args:
            ps = _params(ev.desc)
            spec = dict(default=None, min=None, max=None, comment=None)
            if name in ('getInt', 'getFloat') and len(args) >= 5:
                key_parts = [str_val(args[1]) or '', str_val(args[0]) or '']
                spec.update(default=_scalar(args[2], ps[2]), min=_scalar(args[3], ps[3]), max=_scalar(args[4], ps[4]),
                            comment=str_val(args[5]) if len(args) > 5 else None)
            elif name in ('getBoolean', 'getString') and len(args) >= 3:
                key_parts = [str_val(args[1]) or '', str_val(args[0]) or '']
                spec.update(default=_scalar(args[2], ps[2]), comment=str_val(args[3]) if len(args) > 3 else None)
            elif name == 'getStringList' and len(args) >= 3:
                key_parts = [str_val(args[1]) or '', str_val(args[0]) or '']
                spec.update(default=_list_default(args[2]), comment=str_val(args[3]) if len(args) > 3 else None)
            elif name == 'get' and len(args) >= 3 and ps[0] == 'L' and ps[1] == 'L':
                key_parts = [str_val(args[0]) or '', str_val(args[1]) or '']
                spec['default'] = _list_default(args[2]) if ps[2] == '[' else _scalar(args[2], ps[2])
                if len(args) > 3 and ps[3] == 'L':
                    spec['comment'] = str_val(args[3])
                if len(args) > 5 and ps[4] in 'IDF' and ps[4] == ps[5]:
                    spec.update(min=_scalar(args[4], ps[4]), max=_scalar(args[5], ps[5]))
            else:
                continue
            if not all(key_parts):
                skipped.append(f'Configuration.{name} with a computed name or category')
                continue
            if spec['default'] is None:
                skipped.append(f'{".".join(key_parts)}: default is not a constant (Configuration.{name})')
                continue
            add(key_parts, spec, f'Configuration.{name}', ev.result)

    # static fields that hold a config value -> key (Config.ORE_RATE -> general.oreRate)
    fields = {}
    for (owner, fname), v in prog.statics.items():
        k = results.get(id(v))
        if k:
            fields[(owner, fname)] = k
    for (_, spec) in cfg.items():
        if isinstance(spec.get('min'), float) or isinstance(spec.get('max'), float) or isinstance(spec['default'], float):
            for b in ('min', 'max'):
                if b in spec and spec[b] is not None and abs(spec[b]) > 1e300:
                    spec.pop(b)   # Double.MAX_VALUE-style "no bound"
        for b in ('min', 'max'):
            if isinstance(spec.get(b), int) and not isinstance(spec.get(b), bool) and abs(spec[b]) >= 2 ** 31 - 1:
                spec.pop(b)       # Integer.MAX_VALUE-style "no bound"
    if cfg:
        notes.append(('auto', f'{len(cfg)} config value(s) from the Forge config spec -> mod.json "config" '
                              f'(written to <out>/<id>/config; scripts read them with lce.config)'))
    if len(cfg) > 128:
        notes.append(('manual', f'{len(cfg)} config values; the loader reads at most 128 (the rest were dropped)'))
        cfg = dict(list(cfg.items())[:128])
    for msg in skipped[:20]:
        notes.append(('manual', f'config value not ported: {msg}'))
    if len(skipped) > 20:
        notes.append(('manual', f'... and {len(skipped) - 20} more config values not ported'))
    return cfg, notes, fields


def lua_header(cfg, fields):
    """Lua lines that read the ported config values (prepended to scripts/main.lua)."""
    if not cfg:
        return []
    out = ['-- Config values ported from the Forge config spec (mod.json "config", /data/lce-mods/<id>/config).',
           '-- Java Config.X.get() reads become CFG["key"] (the file is read once at startup).',
           'local CFG = {}']
    by_key = {}
    for (owner, f), k in sorted(fields.items()):
        by_key.setdefault(k, []).append(f'{owner.split("/")[-1]}.{f}')
    for k, spec in cfg.items():
        d = spec['default']
        if isinstance(d, bool):
            dv = 'true' if d else 'false'
        elif isinstance(d, (int, float)):
            dv = repr(float(d)) if isinstance(d, float) else str(d)
        elif isinstance(d, str):
            dv = '"' + d.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
        else:
            dv = None   # lists: raw text
        rng = ''
        if isinstance(d, (int, float)) and not isinstance(d, bool) and ('min' in spec or 'max' in spec):
            lo = spec.get('min')
            hi = spec.get('max')
            rng = f', {"nil" if lo is None else lo}, {"nil" if hi is None else hi}'
        src = f'  -- {", ".join(by_key[k])}' if k in by_key else ''
        call = f'lce.config("{k}", {dv}{rng})' if dv is not None else f'lce.config("{k}")  -- list, raw text'
        out.append(f'CFG["{k}"] = {call}{src}')
    return out + ['']
