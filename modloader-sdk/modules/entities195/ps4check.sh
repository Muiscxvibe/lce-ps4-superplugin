#!/usr/bin/env bash
# Compile entities195 with the PS4 target and flags of ../../build-ps4.sh (no link).
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build/ps4}
mkdir -p "$out"
export PATH=/home/codey/minecraft-lce-work/toolchain/bin:$PATH
export LD_LIBRARY_PATH=/home/codey/minecraft-lce-work/toolchain/clang19/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
tc=${OO_PS4_TOOLCHAIN:-/home/codey/ps4-drive-layout-Phw5N4/ps4-baseline/research/OpenOrbis/PS4Toolchain}
flags=(--target=x86_64-pc-freebsd12-elf -fPIC -mstackrealign -funwind-tables -fno-stack-protector -O2 -ffreestanding -fno-builtin
       -isysroot "$tc" -isystem "$tc/include" -DMC_LEGACY195)
cxx=(-std=c++17 -fvisibility=hidden -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-function)
clang++ "${flags[@]}" "${cxx[@]}" -c "$here/entities195.cpp" -o "$out/entities195.o"
{ llvm-nm "$out/entities195.o" 2>/dev/null || nm "$out/entities195.o"; } | awk '$1=="U"{print $2}' | sort -u > "$out/undefined.txt"
echo "ps4check: built $out/entities195.o; undefined: $(tr '\n' ' ' < "$out/undefined.txt")"
