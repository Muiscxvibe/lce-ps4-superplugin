#!/usr/bin/env bash
# Host tests for modules/items195 (fake 1.95 image, ASan/UBSan).
set -euo pipefail
cd "$(dirname "$0")"
out=${OUT:-build/host}
mkdir -p "$out"
SRC="items.cpp recipes.cpp furnace.cpp effects.cpp adapter.cpp"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
    -Wall -Wextra -Werror tests/test_items195.cpp $SRC -o "$out/test_items195"
"$out/test_items195"
if [ -x /home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python ] && \
   [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ]; then
  py=/home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python
  "$py" -I tests/test_image_pins.py
  "$py" -I tests/test_unicorn_retail.py
else
  echo "retail image or analysis venv absent: skipping image pin test"
fi
echo "items195: ALL TESTS PASSED"
