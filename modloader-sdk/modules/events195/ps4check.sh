#!/usr/bin/env bash
# Compile events195 + services195 with the shipping PS4 flags (build-ps4.sh).
# Exceptions stay ENABLED: the item/block/entity thunks must let a native game
# exception unwind through the frame guard (the r72 server scope requires the
# same, see loader/lucky_block_world_events_195/server_scope.cpp). The SDK core
# loader stays -fno-exceptions; only this behaviour layer needs unwinding.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build/ps4}; mkdir -p "$out"
tc=${OO_PS4_TOOLCHAIN:-/home/codey/ps4-drive-layout-Phw5N4/ps4-baseline/research/OpenOrbis/PS4Toolchain}
cc=${LCE_PS4_CLANG:-clang}
if ! command -v "$cc" >/dev/null 2>&1 || [ ! -d "$tc" ]; then echo "SKIP ps4check: clang or OpenOrbis toolchain missing"; exit 0; fi
flags=(--target=x86_64-pc-freebsd12-elf -fPIC -mstackrealign -funwind-tables -fno-stack-protector -O2 -ffreestanding -fno-builtin
       -isysroot "$tc" -isystem "$tc/include" -DMC_LEGACY195)
cxx=(-std=c++17 -fvisibility=hidden -fexceptions -fno-rtti -Wall -Wextra -Werror)
for src in ev195.cpp services195.cpp; do "$cc" "${flags[@]}" "${cxx[@]}" -c "$here/$src" -o "$out/${src%.cpp}.o"; done
echo "events195: PS4 objects built in $out"
