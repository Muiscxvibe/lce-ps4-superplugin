#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build}; mkdir -p "$out"
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
    "$here/ui195.cpp" "$here/tests/test_ui195.cpp" -o "$out/test_ui195"
"$out/test_ui195"
bash "$here/ps4check.sh" "$out/ps4"
