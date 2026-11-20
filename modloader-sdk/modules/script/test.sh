#!/usr/bin/env bash
# script (Lua sandbox) host tests: builds Lua 5.4.8 + lce_script, runs the
# sandbox-escape / budget tests (ASan/UBSan), then the PS4 compile check.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build}; mkdir -p "$out/obj"
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
for f in "$here"/lua/*.c; do gcc -std=c11 -O1 -g $SAN -DLUA_USE_POSIX -w -c "$f" -o "$out/obj/$(basename "${f%.c}").o"; done
gcc -std=c11 -O1 -g $SAN -DLUA_USE_POSIX -Wall -Wextra -Werror -c "$here/lce_script.c" -o "$out/obj/lce_script.o"
g++ -std=c++17 -O1 -g $SAN -Wall -Wextra -Werror "$here/tests/test_script.cpp" "$out"/obj/*.o -lm -o "$out/test_script"
"$out/test_script"
bash "$here/ps4check.sh" "$out/ps4"
