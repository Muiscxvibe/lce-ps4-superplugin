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
    index['items'] = [i for i in index['items'] if i['id'] != a.id]
    index['items'].append({'id': a.id, 'name': a.name, 'version': a.version, 'description': a.desc or '',
                           'files': files})
    index['items'].sort(key=lambda i: i['id'])
    write_signed(d, index)
    commit_push(d, f'Publish {a.id} {a.version}')
    print(f'{a.repo}: published {a.id} {a.version} ({len(files)} files)')


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
    s.add_argument('files', nargs='+'); s.add_argument('--desc', default=''); s.set_defaults(fn=cmd_add)
    s = sub.add_parser('remove'); s.add_argument('repo'); s.add_argument('id'); s.set_defaults(fn=cmd_remove)
    s = sub.add_parser('verify'); s.add_argument('repo'); s.set_defaults(fn=cmd_verify)
    a = p.parse_args()
    a.fn(a)


if __name__ == '__main__':
    main()
