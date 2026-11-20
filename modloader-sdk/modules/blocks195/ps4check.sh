#!/usr/bin/env bash
# Compile modules/blocks195 with the exact PS4 target/flags of ../../build-ps4.sh
# (OpenOrbis sysroot, freestanding, no exceptions/RTTI). Output: modules/blocks195/build/ps4/blocks195.o
set -euo pipefail
cd "$(dirname "$0")"
export PATH=/home/codey/minecraft-lce-work/toolchain/bin:$PATH
export LD_LIBRARY_PATH=/home/codey/minecraft-lce-work/toolchain/clang19/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
tc=${OO_PS4_TOOLCHAIN:-/home/codey/ps4-drive-layout-Phw5N4/ps4-baseline/research/OpenOrbis/PS4Toolchain}
out=build/ps4
mkdir -p "$out"
flags=(--target=x86_64-pc-freebsd12-elf -fPIC -mstackrealign -funwind-tables -fno-stack-protector -O2 -ffreestanding -fno-builtin
       -isysroot "$tc" -isystem "$tc/include" -DMC_LEGACY195)
cxx=(-std=c++17 -fvisibility=hidden -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-function)
clang++ "${flags[@]}" "${cxx[@]}" -c blocks195.cpp -o "$out/blocks195.o"
echo "built $out/blocks195.o"
echo "undefined symbols:"
(llvm-nm "$out/blocks195.o" 2>/dev/null || nm "$out/blocks195.o") | awk '$1=="U"{print "  "$2}'
