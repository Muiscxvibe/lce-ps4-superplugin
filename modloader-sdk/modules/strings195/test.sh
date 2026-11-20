#!/usr/bin/env bash
# Host tests for modules/strings195: fake-image ASan/UBSan suite, image/arc
# pins, and the Unicorn suite that runs the module against the REAL retail
# Localizer code (insert + lookup + rehash).
set -euo pipefail
cd "$(dirname "$0")"
out=${OUT:-build/host}
mkdir -p "$out"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
    -Wall -Wextra -Werror tests/test_strings195.cpp strings195.cpp -o "$out/test_strings195"
"$out/test_strings195"
py=/home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python
if [ -x "$py" ] && [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ]; then
  g++ -std=c++17 -O1 -g -fPIC -shared -Wall -Wextra -Werror strings195.cpp tests/bridge.cpp -o "$out/libs195bridge.so"
  "$py" -I tests/test_image_pins.py
  "$py" -I tests/test_unicorn_retail.py "$out/libs195bridge.so"
else
  echo "retail image or analysis venv absent: skipping image pins and Unicorn"
fi
echo "strings195: ALL TESTS PASSED"
