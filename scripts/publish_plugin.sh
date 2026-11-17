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
git add -A
git commit -q -m "Publish plugin build $build" || true
git push -q origin HEAD:main
cp "$prx" "/tmp/mc195_lce_session_${build}.prx"
gh release create "$build" "/tmp/mc195_lce_session_${build}.prx" --title "$build" \
  --notes "Official LCE Server plugin build $build (PRX sha256 $sha)" >/dev/null 2>&1 \
  || gh release upload "$build" "/tmp/mc195_lce_session_${build}.prx" --clobber
rm -f "/tmp/mc195_lce_session_${build}.prx"
echo "published $build ($sha)"
