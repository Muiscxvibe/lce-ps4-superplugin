#!/usr/bin/env bash
# Compile Lua 5.4.8 + lce_script.c with the shipping PS4 flags. The decimal
# point is pinned to '.' so localeconv()/locale.h are not needed on the
# freestanding freebsd target (the vendor "decimal point fix" is in luaconf.h).
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build/ps4}; mkdir -p "$out"
tc=${OO_PS4_TOOLCHAIN:-/home/codey/ps4-drive-layout-Phw5N4/ps4-baseline/research/OpenOrbis/PS4Toolchain}
cc=${LCE_PS4_CLANG:-clang}
if ! command -v "$cc" >/dev/null 2>&1 || [ ! -d "$tc" ]; then echo "SKIP ps4check: clang or OpenOrbis toolchain missing"; exit 0; fi
cflags=(--target=x86_64-pc-freebsd12-elf -fPIC -mstackrealign -funwind-tables -fno-stack-protector -O2 -ffreestanding -fno-builtin
        -isysroot "$tc" -isystem "$tc/include" -DMC_LEGACY195 "-Dlua_getlocaledecpoint()='.'" -DLUA_32BITS=0)
for f in "$here"/lua/*.c; do "$cc" "${cflags[@]}" -std=c11 -w -c "$f" -o "$out/$(basename "${f%.c}").o"; done
"$cc" "${cflags[@]}" -std=c11 -Wall -Wextra -Werror -c "$here/lce_script.c" -o "$out/lce_script.o"
echo "script: PS4 objects built in $out (Lua core + lce_script)"
