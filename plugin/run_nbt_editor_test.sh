#!/usr/bin/env bash
# Host tests for the NBT editor core (ASan/UBSan) and the existing remote NBT bridge test.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); out=$(mktemp -d); trap 'rm -rf "$out"' EXIT
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined -fno-sanitize-recover=undefined -I"$here" "$here/nbt_editor_test.cpp" -o "$out/nbt"
"$out/nbt"
g++ -std=c++17 -Wall -Wextra -Werror -I"$here" "$here/remote_nbt_core_test.cpp" -o "$out/core"
"$out/core"
