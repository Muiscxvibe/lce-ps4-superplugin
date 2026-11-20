#!/usr/bin/env bash
# Host tests for modules/enchants195: fake-image ASan/UBSan suite, image pins,
# and the Unicorn suite (real registry + stock bootstrap + the module's publish
# + table/name/behaviour virtuals on the published objects).
set -euo pipefail
cd "$(dirname "$0")"
out=${OUT:-build/host}
mkdir -p "$out"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
    -Wall -Wextra -Werror tests/test_enchants195.cpp enchants195.cpp ../strings195/strings195.cpp -o "$out/test_enchants195"
"$out/test_enchants195"
py=/home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python
if [ -x "$py" ] && [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ]; then
  g++ -std=c++17 -O1 -g -fPIC -shared -Wall -Wextra -Werror enchants195.cpp ../strings195/strings195.cpp \
      ../strings195/tests/bridge.cpp tests/bridge_e.cpp -o "$out/libe195bridge.so"
  "$py" -I tests/test_image_pins.py
  "$py" -I tests/test_unicorn_retail.py "$out/libe195bridge.so"
else
  echo "retail image or analysis venv absent: skipping image pins and Unicorn"
fi
echo "enchants195: ALL TESTS PASSED"
