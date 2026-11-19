#!/usr/bin/env python3
"""Publish content (mods, texture packs, maps, skin packs) to the LCE content repos.

Each content repo (github.com/Muiscxvibe/<repo>) holds:
  index.json      catalog the PS4 mod menu downloads
  index.json.sig  Ed25519 signature of index.json (hex) by the content signing key
  README.md
Item files are uploaded as GitHub release assets (tag "<id>-v<version>") and
listed in the catalog with their download URL, size and SHA-256. The plugin
only installs an item when the catalog signature verifies against its built-in
public key and every downloaded file matches its SHA-256.

Usage:
  lce_content.py init  <repo>
  lce_content.py add   <repo> <id> <version> "<name>" <file> [<file> ...] [--desc "..."]
  lce_content.py remove <repo> <id>
  lce_content.py verify <repo>
Repos: mods, texture-packs, maps, skin-packs. Local clones live in
/home/codey/minecraft-lce-work/content-repos/<repo>.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

import nacl.signing

AUDIT = Path('/home/codey/minecraft-ps4-research/skin-import/addon-audit/audit.py')


def skin_meta(pck_path):
    """LCE registration fields for a SkinPack.pck: DLC pack id (PACKID) and skin count."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('lce_skin_audit', AUDIT)
    audit = importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)
    data = Path(pck_path).read_bytes()
    mappings, records, _, _ = audit.parse(data)
    skins = [r for r in records if r['type'] == 0]
    id_types = {m['type'] for m in mappings if m['name'].casefold() == 'packid'}
    ids = {int(prm['name'].strip()) & 0xffffff for r in records for prm in r['parameters']
           if prm['type'] in id_types and prm['name'].strip().isdecimal()}
    if not skins or len(skins) > 256:
        raise ValueError('skin pack has no skins or too many')
    return {'skin_count': len(skins), 'pack_ids': sorted(ids)}


def lce_block(repo, item_id, files, dlc_id=None):
    if repo != 'skin-packs':
        return None
    pck = next((f for f in files if Path(f[0]).name == 'SkinPack.pck'), None)
    if pck is None:
        raise SystemExit('skin pack needs a SkinPack.pck')
    meta = skin_meta(pck[0])
    if dlc_id is None:
        tail = item_id.rsplit('-', 1)[-1]
        dlc_id = int(tail) if tail.isdecimal() else (meta['pack_ids'][0] if len(meta['pack_ids']) == 1 else None)
    if not dlc_id or not 1 <= dlc_id <= 0xffffff:
        raise SystemExit(f'cannot determine DLC pack id for {item_id}; pass --dlc-id')
    return {'dlc_id': dlc_id, 'skin_count': meta['skin_count']}

OWNER = 'Muiscxvibe'
REPOS = {'mods': 'mod', 'texture-packs': 'texture-pack', 'maps': 'map', 'skin-packs': 'skin-pack'}
ROOT = Path('/home/codey/minecraft-lce-work/content-repos')
KEY = Path('/home/codey/minecraft-lce-work/signing/content-signing.key')
ID_RE = re.compile(r'^[a-z0-9][a-z0-9-]{1,47}$')
VERSION_RE = re.compile(r'^[0-9]+(\.[0-9]+){0,3}$')
TRAILER = None  # no co-author trailer (user request)
README = """# LCE {title}

{kind_title}s for the LCE PS4 superplugin. The in-game **Mods** menu downloads
from this repository.

- `index.json`: catalog (id, name, version, files with URL, size, SHA-256).
- `index.json.sig`: Ed25519 signature of `index.json`. The plugin rejects the
  catalog if the signature does not verify, and rejects any file whose SHA-256
  does not match.
- Files are attached to GitHub releases (one release per item version).

Publish with `lce_content.py` from the lce-ps4-superplugin repository; do not
edit `index.json` by hand (the signature would no longer match).
"""


def run(cmd, cwd=None, check=True):
    return subprocess.run(cmd, cwd=cwd, check=check, capture_output=True, text=True).stdout


def repo_dir(repo):
    if repo not in REPOS:
        sys.exit(f'unknown repo {repo!r}; choose from {", ".join(REPOS)}')
    d = ROOT / repo
    if not (d / '.git').exists():
        ROOT.mkdir(parents=True, exist_ok=True)
        run(['git', 'clone', '-q', f'https://github.com/{OWNER}/{repo}.git', str(d)])
    else:
        run(['git', 'pull', '-q', '--ff-only'], cwd=d, check=False)
    return d


def canonical_bytes(index):
    return (json.dumps(index, indent=2, sort_keys=True) + '\n').encode()


def write_signed(d, index):
    data = canonical_bytes(index)
    sk = nacl.signing.SigningKey(KEY.read_bytes())
    (d / 'index.json').write_bytes(data)
    (d / 'index.json.sig').write_text(sk.sign(data).signature.hex() + '\n')


def load_index(d, repo):
    p = d / 'index.json'
    if p.exists():
        return json.loads(p.read_text())
    return {'schema': 1, 'repo': repo, 'type': REPOS[repo], 'items': []}


def commit_push(d, message):
    run(['git', 'add', '-A'], cwd=d)
    if run(['git', 'status', '--porcelain'], cwd=d).strip():
        run(['git', 'commit', '-q', '-m', message], cwd=d)
        run(['git', 'push', '-q', 'origin', 'HEAD:main'], cwd=d)


def cmd_init(a):
    d = repo_dir(a.repo)
    if run(['git', 'branch', '--show-current'], cwd=d).strip() != 'main':
        run(['git', 'checkout', '-q', '-B', 'main'], cwd=d)
    kind = REPOS[a.repo]
    (d / 'README.md').write_text(README.format(title=a.repo.replace('-', ' ').title(),
                                               kind_title=kind.replace('-', ' ').capitalize()))
    write_signed(d, load_index(d, a.repo))
    commit_push(d, f'Initialise signed {a.repo} catalog')
    print(f'{a.repo}: initialised')


def cmd_add(a):
    if not ID_RE.match(a.id) or not VERSION_RE.match(a.version):
        sys.exit('id must be lowercase letters/digits/dashes; version like 1 or 1.2.3')
    if len(a.desc or '') > 127 or len(a.name) > 63 or not all(32 <= ord(c) < 127 for c in (a.desc or '') + a.name):
        sys.exit('name must be <= 63 and description <= 127 printable ASCII characters (plugin catalog limits)')
    d = repo_dir(a.repo)
    index = load_index(d, a.repo)
    tag = f'{a.id}-v{a.version}'
    files, uploads = [], []
    for f in a.files:
        p = Path(f)
        # A directory publishes every file inside it, keeping relative paths
        # (e.g. Data/media.arc); a file publishes just that file.
        pairs = [(q, q.relative_to(p).as_posix()) for q in sorted(p.rglob('*')) if q.is_file()] if p.is_dir() else [(p, p.name)]
        if not pairs:
            sys.exit(f'nothing to publish in {f}')
        for q, rel in pairs:
            if any(part in ('', '.', '..') or part.startswith('.') for part in rel.split('/')) or not re.fullmatch(r'[A-Za-z0-9._/-]{1,120}', rel):
                sys.exit(f'unsafe file path {rel!r}')
            asset = rel.replace('/', '__')
            files.append({'path': rel, 'asset': asset, 'size': q.stat().st_size,
                          'sha256': hashlib.sha256(q.read_bytes()).hexdigest(),
                          'url': f'https://github.com/{OWNER}/{a.repo}/releases/download/{tag}/{asset}'})
            uploads.append(f'{q}#{asset}' if asset != q.name else str(q))
    if len({f['asset'] for f in files}) != len(files):
        sys.exit('two files map to the same asset name')
    if subprocess.run(['gh', 'release', 'view', tag, '--repo', f'{OWNER}/{a.repo}'], capture_output=True).returncode != 0:
        run(['gh', 'release', 'create', tag, '--repo', f'{OWNER}/{a.repo}', '--title', f'{a.name} {a.version}',
             '--notes', a.desc or a.name])
    # Upload under the asset name (gh "file#label" sets only the display label,
    # so copy files whose asset name differs into a staging dir first).
    import shutil, tempfile
    with tempfile.TemporaryDirectory() as stage:
        paths = []
        for spec, f in zip(uploads, files):
            src = Path(spec.split('#')[0])
            dst = Path(stage) / f['asset']
            shutil.copyfile(src, dst)
            paths.append(str(dst))
        run(['gh', 'release', 'upload', tag, *paths, '--clobber', '--repo', f'{OWNER}/{a.repo}'])
    lce = lce_block(a.repo, a.id, [(spec.split('#')[0], f['path']) for spec, f in zip(uploads, files)], a.dlc_id)
    if lce and any(i.get('lce', {}).get('dlc_id') == lce['dlc_id'] and i['id'] != a.id for i in index['items']):
        sys.exit(f"DLC pack id {lce['dlc_id']} already used in this catalog")
    index['items'] = [i for i in index['items'] if i['id'] != a.id]
    item = {'id': a.id, 'name': a.name, 'version': a.version, 'description': a.desc or '', 'files': files}
    if lce:
        item['lce'] = lce
    index['items'].append(item)
    index['items'].sort(key=lambda i: i['id'])
    write_signed(d, index)
    commit_push(d, f'Publish {a.id} {a.version}')
    print(f'{a.repo}: published {a.id} {a.version} ({len(files)} files)')


def cmd_meta(a):
    """Add/refresh the lce block of an existing skin-pack item from a local copy of its SkinPack.pck."""
    d = repo_dir(a.repo)
    index = load_index(d, a.repo)
    item = next((i for i in index['items'] if i['id'] == a.id), None)
    if item is None:
        sys.exit('no such item')
    f = next((f for f in item['files'] if f['path'] == 'SkinPack.pck'), None)
    if f is None or hashlib.sha256(Path(a.pck).read_bytes()).hexdigest() != f['sha256']:
        sys.exit('local SkinPack.pck does not match the published file')
    lce = lce_block(a.repo, a.id, [(a.pck, 'SkinPack.pck')], a.dlc_id)
    if any(i.get('lce', {}).get('dlc_id') == lce['dlc_id'] and i['id'] != a.id for i in index['items']):
        sys.exit(f"DLC pack id {lce['dlc_id']} already used")
    item['lce'] = lce
    write_signed(d, index)
    commit_push(d, f'Registration metadata for {a.id}')
    print(f"{a.repo}: {a.id} dlc_id={lce['dlc_id']} skins={lce['skin_count']}")


PS4_FTP = ('192.168.1.175', 2121)
PS4_PACKS = '/data/lce-mods/packs'


def cmd_install_ps4(a):
    """Download a published item, verify it against the signed catalog, upload it to the PS4."""
    import ftplib, io, tempfile
    d = repo_dir(a.repo)
    data = (d / 'index.json').read_bytes()
    pub = bytes.fromhex(Path(str(KEY).replace('.key', '.pub')).read_text().strip())
    nacl.signing.VerifyKey(pub).verify(data, bytes.fromhex((d / 'index.json.sig').read_text().strip()))
    item = next((i for i in json.loads(data)['items'] if i['id'] == a.id), None)
    if item is None:
        sys.exit('no such item')
    if a.repo not in ('skin-packs', 'texture-packs'):
        sys.exit('install-ps4 supports skin-packs and texture-packs')
    tag = f"{item['id']}-v{item['version']}"
    with tempfile.TemporaryDirectory() as work:
        for f in item['files']:
            asset = f.get('asset', f['path'])
            run(['gh', 'release', 'download', tag, '--repo', f'{OWNER}/{a.repo}', '--pattern', asset, '--dir', work])
            got = Path(work) / asset
            if got.stat().st_size != f['size'] or hashlib.sha256(got.read_bytes()).hexdigest() != f['sha256']:
                sys.exit(f'{asset}: size/hash does not match the signed catalog')
        ftp = ftplib.FTP(); ftp.connect(*PS4_FTP, timeout=120); ftp.login()
        def mkdirs(path):
            cur = ''
            for part in path.strip('/').split('/'):
                cur += '/' + part
                try: ftp.mkd(cur)
                except ftplib.error_perm: pass
        base = f"{PS4_PACKS}/{item['id']}"
        for f in item['files']:
            rel = f['path']; dest = f'{base}/{rel}'
            mkdirs(dest.rsplit('/', 1)[0])
            with open(Path(work) / f.get('asset', rel), 'rb') as src:
                ftp.storbinary(f'STOR {dest}.part', src)
            try: ftp.delete(dest)
            except ftplib.error_perm: pass
            ftp.rename(f'{dest}.part', dest)
        lce = item.get('lce')
        if lce:
            pck = next(f for f in item['files'] if f['path'] == 'SkinPack.pck')
            line = f"{lce['dlc_id']} {lce['skin_count']} {pck['size']} {pck['sha256']}\n".encode()
            ftp.storbinary(f'STOR {base}/lce.desc.part', io.BytesIO(line))
            try: ftp.delete(f'{base}/lce.desc')
            except ftplib.error_perm: pass
            ftp.rename(f'{base}/lce.desc.part', f'{base}/lce.desc')
        ftp.quit()
    print(f"installed {a.repo}/{item['id']} to the PS4 ({len(item['files'])} files{', registered at next launch' if item.get('lce') else ''})")


def cmd_remove(a):
    d = repo_dir(a.repo)
    index = load_index(d, a.repo)
    index['items'] = [i for i in index['items'] if i['id'] != a.id]
    write_signed(d, index)
    commit_push(d, f'Remove {a.id}')
    print(f'{a.repo}: removed {a.id}')


def cmd_verify(a):
    d = repo_dir(a.repo)
    pub = bytes.fromhex(Path(str(KEY).replace('.key', '.pub')).read_text().strip())
    data = (d / 'index.json').read_bytes()
    sig = bytes.fromhex((d / 'index.json.sig').read_text().strip())
    nacl.signing.VerifyKey(pub).verify(data, sig)
    print(f'{a.repo}: signature OK, {len(json.loads(data)["items"])} items')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('init'); s.add_argument('repo'); s.set_defaults(fn=cmd_init)
    s = sub.add_parser('add'); s.add_argument('repo'); s.add_argument('id'); s.add_argument('version'); s.add_argument('name')
    s.add_argument('files', nargs='+'); s.add_argument('--desc', default='')
    s.add_argument('--dlc-id', type=int, default=None, help='skin packs: DLC pack id (default: numeric id suffix)')
    s.set_defaults(fn=cmd_add)
    s = sub.add_parser('meta'); s.add_argument('repo'); s.add_argument('id'); s.add_argument('pck')
    s.add_argument('--dlc-id', type=int, default=None); s.set_defaults(fn=cmd_meta)
    s = sub.add_parser('install-ps4'); s.add_argument('repo'); s.add_argument('id'); s.set_defaults(fn=cmd_install_ps4)
    s = sub.add_parser('remove'); s.add_argument('repo'); s.add_argument('id'); s.set_defaults(fn=cmd_remove)
    s = sub.add_parser('verify'); s.add_argument('repo'); s.set_defaults(fn=cmd_verify)
    a = p.parse_args()
    a.fn(a)


if __name__ == '__main__':
    main()
