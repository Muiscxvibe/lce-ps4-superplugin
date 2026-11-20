#!/usr/bin/env bash
# Host tests for modules/audio195 (fake natives, ASan/UBSan) + image pins + Unicorn runs of the real retail code.
set -euo pipefail
cd "$(dirname "$0")"
out=${OUT:-build/host}
mkdir -p "$out"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined \
    -Wall -Wextra -Werror tests/test_audio195.cpp audio195.cpp -o "$out/test_audio195" -lm
A195_WAV_OUT="$out/a195.wav" "$out/test_audio195"
py=/home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python
if [ -x "$py" ] && [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ]; then
  "$py" -I tests/test_image_pins.py
  "$py" -I tests/test_unicorn_retail.py "$out/a195.wav"
else
  echo "retail image or analysis venv absent: skipping image pin / Unicorn tests"
fi
echo "audio195: ALL TESTS PASSED"
