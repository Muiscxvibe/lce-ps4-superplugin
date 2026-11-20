#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build/ps4}; mkdir -p "$out"
tc=${OO_PS4_TOOLCHAIN:-/home/codey/ps4-drive-layout-Phw5N4/ps4-baseline/research/OpenOrbis/PS4Toolchain}
cc=${LCE_PS4_CLANG:-clang}
if ! command -v "$cc" >/dev/null 2>&1 || [ ! -d "$tc" ]; then echo "SKIP ps4check: clang or OpenOrbis toolchain missing"; exit 0; fi
flags=(--target=x86_64-pc-freebsd12-elf -fPIC -mstackrealign -funwind-tables -fno-stack-protector -O2 -ffreestanding -fno-builtin
       -isysroot "$tc" -isystem "$tc/include" -DMC_LEGACY195)
cxx=(-std=c++17 -fvisibility=hidden -fno-exceptions -fno-rtti -Wall -Wextra -Werror)
"$cc" "${flags[@]}" "${cxx[@]}" -c "$here/ui195.cpp" -o "$out/ui195.o"
echo "ui195: PS4 object built in $out"
