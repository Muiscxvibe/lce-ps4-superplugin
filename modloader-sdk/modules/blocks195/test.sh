#!/usr/bin/env bash
# Host tests for modules/blocks195 (g++ + ASan/UBSan, warnings are errors).
# The fake image is built from the real retail 1.95 image (pass another path as $1).
set -euo pipefail
cd "$(dirname "$0")"
out=build/host
mkdir -p "$out"
img=${1:-/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf}
want=c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f
if [ -r "$img" ] && [ "$(sha256sum "$img" | cut -d' ' -f1)" != "$want" ]; then echo "wrong image (sha256 != $want)"; exit 1; fi
g++ -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Werror \
    -DB195_HOST_TEST tests/test_blocks195.cpp blocks195.cpp -o "$out/test_blocks195"
# also prove the public header is valid C
echo '#include "blocks195.h"' | gcc -std=c11 -Wall -Wextra -Werror -x c -fsyntax-only -I. -
set +e; "$out/test_blocks195" "$img"; rc=$?; set -e
if [ $rc -eq 77 ]; then echo "SKIPPED: retail image not available"; exit 0; fi
[ $rc -eq 0 ] || exit $rc
py=/home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python
if [ -x "$py" ] && [ -r "$img" ]; then "$py" -I tests/test_unicorn_boxes.py; else echo "analysis venv or image absent: skipping the multi-box Unicorn test"; fi
