"""Read-only access to an untrusted mod jar, plus loader / Minecraft-version detection.

The jar is opened read-only with zipfile and read into memory entry by entry,
with size caps (zip bombs) and no path ever written to disk from an entry name.
"""
import json
import re
import zipfile
from pathlib import Path

try:
    import tomllib
except ImportError:  # pragma: no cover
    tomllib = None

MAX_ENTRY = 64 << 20
MAX_TOTAL = 768 << 20


class JarError(Exception):
    pass


class Jar:
    def __init__(self, path):
        self.path = Path(path)
        if self.path.is_dir():
            self.zip = None
            self._names = sorted(p.relative_to(self.path).as_posix() for p in self.path.rglob('*') if p.is_file())
            self._sizes = {n: (self.path / n).stat().st_size for n in self._names}
        else:
            try:
                self.zip = zipfile.ZipFile(self.path, 'r')
            except (zipfile.BadZipFile, OSError) as e:
                raise JarError(f'{self.path}: not a readable jar/zip ({e})')
            infos = [i for i in self.zip.infolist() if not i.is_dir()]
            self._names = [i.filename for i in infos]
            self._sizes = {i.filename: i.file_size for i in infos}
        self._read_total = 0
        self.skipped = []
        self._set = set(self._names)

    def names(self):
        return self._names

    def __contains__(self, n):
        return n in self._set

    def size(self, n):
        return self._sizes.get(n, 0)

    def read(self, n):
        sz = self._sizes.get(n)
        if sz is None:
            raise KeyError(n)
        if sz > MAX_ENTRY or self._read_total + sz > MAX_TOTAL:
            self.skipped.append(n)
            raise JarError(f'{n}: entry too large ({sz} bytes) or jar read budget exhausted')
        self._read_total += sz
        if self.zip is None:
            p = (self.path / n).resolve()
            if self.path.resolve() not in p.parents:
                raise JarError(f'{n}: outside the source directory')
            return p.read_bytes()
        data = self.zip.read(n)
        if len(data) > MAX_ENTRY:
            raise JarError(f'{n}: decompressed larger than declared')
        return data

    def text(self, n):
        return self.read(n).decode('utf-8-sig', errors='replace')

    def close(self):
        if self.zip:
            self.zip.close()


def load_json_lenient(text):
    text = re.sub(r'^\s*//.*$', '', text, flags=re.M)
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r',(\s*[}\]])', r'\1', text)
    return json.loads(text, strict=False)  # strict=False: raw newlines/tabs inside strings (common in mcmod.info)


# Class references that only exist in some Minecraft/Forge versions (fingerprints for jars that do not declare
# their version). Checked newest first; the first hit wins.
CLASS_FINGERPRINTS = [
    ('net/minecraft/world/item/Item', '1.17'), ('net/minecraft/util/math/vector/Vector3d', '1.16'),
    ('com/mojang/blaze3d/matrix/MatrixStack', '1.15'), ('net/minecraft/item/Food', '1.14'),
    ('net/minecraft/entity/EntityClassification', '1.14'), ('net/minecraft/inventory/container/ContainerType', '1.14'),
    ('net/minecraft/world/server/ServerWorld', '1.14'), ('net/minecraftforge/registries/DeferredRegister', '1.14'),
    ('net/minecraft/item/ItemGroup', '1.13'),
    ('net/minecraftforge/registries/IForgeRegistry', '1.12'), ('net/minecraft/util/NonNullList', '1.11'),
    ('net/minecraft/util/math/BlockPos', '1.9'), ('net/minecraft/util/BlockPos', '1.8'), ('cpw/mods/fml/common/Mod', '1.7.10'),
]
# Forge major version (mods.toml loaderVersion) -> Minecraft version
FORGE_LOADER_MC = {25: '1.13.2', 26: '1.14.2', 27: '1.14.3', 28: '1.14.4', 29: '1.15', 30: '1.15.1', 31: '1.15.2', 32: '1.16.1',
                   33: '1.16.2', 34: '1.16.3', 35: '1.16.4', 36: '1.16.5', 37: '1.17.1', 38: '1.18', 39: '1.18.1', 40: '1.18.2',
                   41: '1.19', 42: '1.19.1', 43: '1.19.2', 44: '1.19.3', 45: '1.19.4', 46: '1.20', 47: '1.20.1'}
_MC_VER_RE = re.compile(rb'\[?\(?(1\.(?:[7-9]|1[0-6])(?:\.\d+)?)')


def version_from_classes(jar, limit=4000):
    """(mc version, evidence) from @Mod(acceptedMinecraftVersions=...) or version-specific class references."""
    names = [n for n in jar.names() if n.endswith('.class')][:limit]
    refs = set()
    for n in names:
        try:
            data = jar.read(n)
        except (JarError, KeyError):
            continue
        if b'acceptedMinecraftVersions' in data:
            i = data.find(b'acceptedMinecraftVersions')
            m = _MC_VER_RE.search(data, i, i + 400)
            if m:
                return m.group(1).decode(), f'@Mod(acceptedMinecraftVersions) in {n}'
        for fp, _v in CLASS_FINGERPRINTS:
            if fp.encode() in data:
                refs.add(fp)
    for fp, v in CLASS_FINGERPRINTS:
        if fp in refs:
            return v, f'class reference {fp}'
    return None, None


class ModInfo:
    def __init__(self):
        self.loader = 'unknown'      # neoforge | forge | forge-legacy | fabric | quilt | unknown
        self.mc_version = None       # e.g. '1.7.10', '1.21.1' (as declared)
        self.mc_range = None
        self.modid = None
        self.name = None
        self.version = None
        self.authors = ''
        self.description = ''
        self.mods = []               # all declared mod ids
        self.evidence = []
        self.nested_jars = []

    @property
    def era(self):
        """'old' for <= 1.12 (pre-flattening), 'modern' for >= 1.13, guessed from version/loader."""
        v = mc_tuple(self.mc_version)
        if v is not None:
            return 'old' if v < (1, 13) else 'modern'
        return 'old' if self.loader == 'forge-legacy' else 'modern'

    def as_dict(self):
        return dict(loader=self.loader, mc_version=self.mc_version, mc_range=self.mc_range, modid=self.modid, name=self.name,
                    version=self.version, mods=self.mods, era=self.era)


def mc_tuple(v):
    if not v:
        return None
    m = re.match(r'^\s*(\d+)\.(\d+)(?:\.(\d+))?', str(v))
    if not m:
        return None
    t = tuple(int(x) for x in m.groups() if x is not None)
    if t[0] >= 20 and len(t) >= 2:  # 26.2-style year versions (post-1.21) -> treat as modern
        return (1, 30) + t
    return t


def _range_low(r):
    """Maven range '[1.20.1,1.21)' -> '1.20.1'."""
    if not r:
        return None
    m = re.search(r'(\d+\.\d+(?:\.\d+)?)', str(r))
    return m.group(1) if m else None


def detect(jar):
    info = ModInfo()
    names = jar.names()
    nset = set(names)
    info.nested_jars = [n for n in names if n.endswith('.jar') and (n.startswith('META-INF/jarjar/') or n.startswith('META-INF/jars/'))]
    toml_name = None
    if 'META-INF/neoforge.mods.toml' in nset:
        info.loader, toml_name = 'neoforge', 'META-INF/neoforge.mods.toml'
    elif 'META-INF/mods.toml' in nset:
        info.loader, toml_name = 'forge', 'META-INF/mods.toml'
    if toml_name:
        info.evidence.append(toml_name)
        txt = jar.text(toml_name)
        data = None
        if tomllib is not None:
            try:
                data = tomllib.loads(txt)
            except Exception:  # noqa: BLE001 - malformed TOML from untrusted input
                data = None
        if data:
            mods = data.get('mods') or []
            if mods:
                m0 = mods[0]
                info.modid = m0.get('modId')
                info.name = m0.get('displayName')
                info.version = m0.get('version')
                info.authors = m0.get('authors', '') if isinstance(m0.get('authors', ''), str) else ', '.join(m0.get('authors'))
                info.description = (m0.get('description') or '').strip()
            info.mods = [m.get('modId') for m in mods if m.get('modId')]
            deps_all = data.get('dependencies') or {}
            dep_lists = deps_all.values() if isinstance(deps_all, dict) else [deps_all]
            for deps in dep_lists:
                for d in deps if isinstance(deps, list) else []:
                    if not isinstance(d, dict):
                        continue
                    if d.get('modId') == 'minecraft':
                        info.mc_range = d.get('versionRange')
                        info.mc_version = _range_low(info.mc_range)
                    if d.get('modId') == 'neoforge' and info.loader == 'forge':
                        info.loader = 'neoforge'
        else:
            m = re.search(r'modId\s*=\s*"([^"]+)"', txt)
            info.modid = m.group(1) if m else None
            info.mods = re.findall(r'modId\s*=\s*"([^"]+)"', txt)
            mm = re.search(r'modId\s*=\s*"minecraft".*?versionRange\s*=\s*"([^"]+)"', txt, re.S)
            if mm:
                info.mc_range = mm.group(1)
                info.mc_version = _range_low(mm.group(1))
        if not info.mc_version:
            lv = re.search(r'loaderVersion\s*=\s*"\[?\s*(\d+)', txt)
            if lv and info.loader == 'forge' and int(lv.group(1)) in FORGE_LOADER_MC:
                info.mc_version = FORGE_LOADER_MC[int(lv.group(1))]
                info.evidence.append(f'mods.toml loaderVersion {lv.group(1)} (Forge {lv.group(1)} = Minecraft {info.mc_version})')
        if info.version and '${' in str(info.version):
            mf = jar.text('META-INF/MANIFEST.MF') if 'META-INF/MANIFEST.MF' in nset else ''
            mv = re.search(r'Implementation-Version:\s*(\S+)', mf)
            info.version = mv.group(1) if mv else '1.0.0'
    elif 'fabric.mod.json' in nset or 'quilt.mod.json' in nset:
        fname = 'fabric.mod.json' if 'fabric.mod.json' in nset else 'quilt.mod.json'
        info.loader = 'fabric' if fname == 'fabric.mod.json' else 'quilt'
        info.evidence.append(fname)
        try:
            fj = load_json_lenient(jar.text(fname))
        except (ValueError, JarError):
            fj = {}
        if fname == 'quilt.mod.json':
            fj = fj.get('quilt_loader', fj)
            meta = fj.get('metadata', {})
            fj = dict(fj, name=meta.get('name'), description=meta.get('description'))
        info.modid = fj.get('id')
        info.mods = [info.modid] if info.modid else []
        info.name = fj.get('name')
        info.version = fj.get('version')
        info.description = fj.get('description') or ''
        auth = fj.get('authors') or []
        info.authors = ', '.join(a if isinstance(a, str) else a.get('name', '') for a in auth) if isinstance(auth, list) else str(auth)
        dep = (fj.get('depends') or {}).get('minecraft')
        if isinstance(dep, list):
            dep = dep[0] if dep else None
        if dep:
            info.mc_range = str(dep)
            info.mc_version = _range_low(dep)
    if 'mcmod.info' in nset:
        info.evidence.append('mcmod.info')
        try:
            mi = load_json_lenient(jar.text('mcmod.info'))
        except (ValueError, JarError):
            mi = None
        lst = mi if isinstance(mi, list) else (mi or {}).get('modList') or (mi or {}).get('modlist') or []
        if lst and isinstance(lst[0], dict):
            m0 = lst[0]
            if info.loader == 'unknown':
                info.loader = 'forge-legacy'
                info.modid = m0.get('modid')
                info.mods = [x.get('modid') for x in lst if isinstance(x, dict) and x.get('modid')]
                info.name = m0.get('name')
                info.version = m0.get('version')
                info.description = m0.get('description') or ''
                auth = m0.get('authorList') or m0.get('authors') or []
                info.authors = ', '.join(auth) if isinstance(auth, list) else str(auth)
            if m0.get('mcversion') and not info.mc_version:
                info.mc_version = str(m0.get('mcversion'))
    if info.mc_version and '${' in info.mc_version:
        info.mc_version = None
    if (info.loader in ('unknown', 'forge', 'forge-legacy') and not info.mc_version) or (info.loader == 'forge' and 'mcmod.info' in nset):
        # undeclared version, or a 1.12 jar that ships both mcmod.info and a mods.toml: the bytecode decides
        v, ev = version_from_classes(jar)
        if v:
            mt = mc_tuple(v)
            if info.loader == 'forge' and mt < (1, 13):
                info.loader = 'forge-legacy'
                info.mc_range = None
                info.evidence.append(f'mods.toml ignored: bytecode targets Minecraft {v}')
            if not info.mc_version or info.loader == 'forge-legacy':
                info.mc_version = info.mc_version if info.mc_version and mc_tuple(info.mc_version) < (1, 13) else v
                info.evidence.append(ev)
    if info.loader == 'unknown':
        if any(n.startswith('cpw/mods/fml') for n in names) or 'META-INF/fml_cache_annotation.json' in nset:
            info.loader = 'forge-legacy'
        elif any(n.endswith('.class') for n in names) and any(n.startswith('assets/') for n in names):
            info.loader = 'forge-legacy' if info.mc_version and mc_tuple(info.mc_version) < (1, 13) else 'forge-legacy?'
    return info


def asset_namespaces(jar):
    ns = {}
    for n in jar.names():
        parts = n.split('/')
        if len(parts) >= 3 and parts[0] in ('assets', 'data') and parts[1] not in ('minecraft', 'c', 'forge', 'neoforge', 'fabric', 'realms'):
            ns[parts[1]] = ns.get(parts[1], 0) + 1
    return ns


def lce_id(ns, used=None):
    """Mod namespace -> valid LCE mod id ([a-z0-9_]{2,31}, not reserved)."""
    s = re.sub(r'(?<=[a-z0-9])(?=[A-Z])', '_', ns or 'mod')
    s = re.sub(r'[^a-z0-9_]', '_', s.lower()).strip('_') or 'mod'
    s = re.sub(r'_+', '_', s)[:31]
    if len(s) < 2:
        s = (s + '_mod')[:31]
    if s in ('minecraft', 'packs', 'lce', 'reservations'):
        s = (s + '_mod')[:31]
    return s
