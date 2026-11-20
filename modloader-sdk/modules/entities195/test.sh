#!/usr/bin/env bash
# entities195: fake-image host test (ASan/UBSan), static image audit, PS4 compile check.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build}
mkdir -p "$out"
g++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
    -Wall -Wextra -Werror -DLE195_TEST "$here/entities195.cpp" "$here/tests/test_entities195.cpp" -o "$out/test_entities195"
"$out/test_entities195"
PY=${LCE_RE_PYTHON:-/home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python}
if [ -x "$PY" ] && [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ]; then
  "$PY" "$here/tests/audit.py"
else
  echo "SKIP audit: analysis image or capstone venv missing"
fi
bash "$here/ps4check.sh" "$out/ps4"
