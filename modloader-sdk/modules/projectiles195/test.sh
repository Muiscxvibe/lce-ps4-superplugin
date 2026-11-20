#!/usr/bin/env bash
# projectiles195 host tests: fake-image unit test (ASan/UBSan), image pins, PS4 compile.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build}
mkdir -p "$out"
g++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
    -Wall -Wextra -Werror -DP195_TEST "$here/projectiles195.cpp" "$here/tests/test_projectiles195.cpp" -o "$out/test_projectiles195"
"$out/test_projectiles195"
PY=${LCE_RE_PYTHON:-/home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python}
if [ -x "$PY" ] && [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ]; then
  "$PY" -I "$here/tests/test_image_pins.py"
else
  echo "SKIP image pins: analysis image or venv missing"
fi
bash "$here/ps4check.sh" "$out/ps4"
echo "projectiles195: ALL TESTS PASSED"
