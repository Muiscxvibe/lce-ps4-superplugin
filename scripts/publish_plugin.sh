#!/bin/bash
# Publish an official plugin build: GitHub release + official-builds.json update.
# Usage: publish_plugin.sh <super195.prx> <build id> [game text sha256 ...]
set -euo pipefail
prx=$1; build=$2; shift 2
repo_dir=$(cd "$(dirname "$0")/.." && pwd)
sha=$(sha256sum "$prx" | cut -d' ' -f1)
cd "$repo_dir"
git pull --ff-only -q || true
python3 - "$build" "$sha" "$@" <<'PY'
import json, sys
build, sha, texts = sys.argv[1], sys.argv[2], sys.argv[3:]
m = json.load(open('official-builds.json'))
prev = m.get('current')
if prev and prev != build:
    m['outdated_plugin_builds'] = sorted(set(m.get('outdated_plugin_builds', [])) | {prev})
    m['builds'].pop(prev, None)
entry = m['builds'].setdefault(build, {})
entry['prx_sha256'] = sha
if texts:
    entry['game_text_sha256'] = texts
m['current'] = build
json.dump(m, open('official-builds.json', 'w'), indent=2)
PY
# Signed auto-update channel (served by the server's content mirror as repo "plugin"):
# index.json + index.json.sig at the repo root, PRX attached to release "session-plugin-v<version>".
version=$(echo "$build" | grep -oE '[0-9]{14}$')
python3 - "$prx" "$version" "$build" <<'PY2'
import hashlib, json, sys
from pathlib import Path
sys.path.insert(0, 'tools')
import lce_content
prx, version, build = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
data = prx.read_bytes()
index = {'schema': 1, 'repo': 'plugin', 'type': 'plugin', 'items': [{
    'id': 'session-plugin', 'name': 'LCE Server plugin', 'version': version,
    'description': 'Official build ' + build,
    'files': [{'path': 'mc195_lce_session.prx', 'asset': 'mc195_lce_session.prx', 'size': len(data),
               'sha256': hashlib.sha256(data).hexdigest(),
               'url': f'https://github.com/Muiscxvibe/lce-ps4-superplugin/releases/download/session-plugin-v{version}/mc195_lce_session.prx'}]}]}
lce_content.write_signed(Path('.'), index)
PY2
git add -A
git commit -q -m "Publish plugin build $build" || true
git push -q origin HEAD:main
cp "$prx" "/tmp/mc195_lce_session_${build}.prx"
gh release create "$build" "/tmp/mc195_lce_session_${build}.prx" --title "$build" \
  --notes "Official LCE Server plugin build $build (PRX sha256 $sha)" >/dev/null 2>&1 \
  || gh release upload "$build" "/tmp/mc195_lce_session_${build}.prx" --clobber
rm -f "/tmp/mc195_lce_session_${build}.prx"
stage=$(mktemp -d); cp "$prx" "$stage/mc195_lce_session.prx"
gh release create "session-plugin-v$version" "$stage/mc195_lce_session.prx" --title "Auto-update $build" \
  --notes "Signed auto-update channel for $build" >/dev/null 2>&1 \
  || gh release upload "session-plugin-v$version" "$stage/mc195_lce_session.prx" --clobber
rm -rf "$stage"
echo "published $build ($sha)"
