#!/usr/bin/env bash
# worldgen195 host tests: fake-image unit test (ASan/UBSan), image pin audit,
# PS4 compile check. (The real-code OreFeature fixture is steel_ore_own_chunk_195/test_native.py.)
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build}
mkdir -p "$out"
g++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
    -Wall -Wextra -Werror -DLCE_WG195_TEST "$here/worldgen195.cpp" "$here/tests/test_worldgen195.cpp" -o "$out/test_worldgen195"
"$out/test_worldgen195"
PY=${LCE_RE_PYTHON:-/home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python}
if [ -x "$PY" ] && [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ]; then
  "$PY" "$here/tests/audit.py"
  if [ -f "$here/tests/test_native.py" ]; then "$PY" "$here/tests/test_native.py" "$out"; fi
else
  echo "SKIP audit/native fixture: analysis image or capstone venv missing"
fi
bash "$here/ps4check.sh" "$out/ps4"
