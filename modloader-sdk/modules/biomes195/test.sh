#!/usr/bin/env bash
# biomes195 host tests: fake-image unit test (ASan/UBSan), image pins, real retail code in Unicorn, PS4 compile.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build}
mkdir -p "$out"
g++ -std=c++17 -g -O1 -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
    -Wall -Wextra -Werror -DLCE_B195_TEST "$here/biomes195.cpp" "$here/tests/test_biomes195.cpp" -o "$out/test_biomes195"
"$out/test_biomes195"
PY=${LCE_RE_PYTHON:-/home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python}
if [ -x "$PY" ] && [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ]; then
  "$PY" -I "$here/tests/test_image_pins.py"
  "$PY" -I "$here/tests/test_unicorn_retail.py"
else
  echo "SKIP image pins / Unicorn: analysis image or venv missing"
fi
bash "$here/ps4check.sh" "$out/ps4"
echo "biomes195: ALL TESTS PASSED"
