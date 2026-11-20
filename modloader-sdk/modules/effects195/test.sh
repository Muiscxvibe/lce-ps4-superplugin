#!/usr/bin/env bash
# Host tests for modules/effects195 (ASan/UBSan), then image pins and Unicorn runs of the real retail code.
set -euo pipefail
cd "$(dirname "$0")"
out=${OUT:-build/host}
mkdir -p "$out"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
    -Wall -Wextra -Werror -DLCE195E_HOST tests/test_effects195.cpp effects195.cpp -o "$out/test_effects195"
ASAN_OPTIONS=detect_leaks=0 "$out/test_effects195"
py=/home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python
if [ -x "$py" ] && [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ]; then
  "$py" -I tests/test_image_pins.py
  "$py" -I tests/test_unicorn_retail.py
else
  echo "retail image or analysis venv absent: skipping image pin and Unicorn tests"
fi
echo "effects195: ALL TESTS PASSED"
