#!/usr/bin/env bash
# Build the SDK loader for the PS4 (CUSA00265 super plugin link) with the same
# target and flags as ps4-metadata-snapshot/crashlog-build/build-crashlog.sh.
# Output:
#   build/ps4/liblcemodloader.a     core + composite retail195 backend + every module + Lua + sdk_boot
#   build/ps4/lcemodloader.o        same, as one relocatable object (ld -r)
#   build/ps4/undefined.txt         symbols the shipping plugin / OpenOrbis libs must provide
set -euo pipefail
cd "$(dirname "$0")"
export PATH=/home/codey/minecraft-lce-work/toolchain/bin:$PATH
export LD_LIBRARY_PATH=/home/codey/minecraft-lce-work/toolchain/clang19/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
tc=${OO_PS4_TOOLCHAIN:-/home/codey/ps4-drive-layout-Phw5N4/ps4-baseline/research/OpenOrbis/PS4Toolchain}
out=build/ps4
mkdir -p "$out/lua"
python3 tools/gen_vanilla.py >/dev/null
flags=(--target=x86_64-pc-freebsd12-elf -fPIC -mstackrealign -funwind-tables -fno-stack-protector -O2 -ffreestanding -fno-builtin
       -isysroot "$tc" -isystem "$tc/include" -DMC_LEGACY195)
if [[ ${LCE_EXPERIMENTAL_FUEL:-0} == 1 ]]; then flags+=(-DLCE_EXPERIMENTAL_FUEL); fi
if [[ ${LCE_EXPERIMENTAL_VIEWPORT_TINT:-0} == 1 ]]; then flags+=(-DLCE_EXPERIMENTAL_VIEWPORT_TINT); fi
if [[ ${LCE_WAYPOINT_PACKAGES:-0} == 1 ]]; then flags+=(-DLCE_WAYPOINT_PACKAGES); fi
cxx=(-std=c++17 -fvisibility=hidden -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-function)
# events195 thunks must let a native game exception unwind through their frame guard (modules/events195/STATUS.md)
cxx_eh=(-std=c++17 -fvisibility=hidden -fexceptions -fno-rtti -Wall -Wextra -Werror)
cc=(-std=c11 -Wall -Wextra -Werror)
lua=(-std=c11 -w "-Dlua_getlocaledecpoint()='.'" -DLUA_32BITS=0 -include modules/script/ps4_stdio_shim.h)
objs=()
c_obj() { clang "${flags[@]}" "${cc[@]}" -c "$1" -o "$2"; objs+=("$2"); }
cxx_obj() { clang++ "${flags[@]}" "${cxx[@]}" -c "$1" -o "$2"; objs+=("$2"); }
c_obj loader/sha256.c "$out/sha256.o"
c_obj loader/retail195/oneshot195.c "$out/oneshot195.o"
# core loader, composite backend, PS4 host, startup owner
for src in loader/package.cpp loader/ledger.cpp loader/loader.cpp loader/ext.cpp \
           loader/retail195/backend195.cpp loader/retail195/services_glue195.cpp loader/retail195/host_ps4.cpp \
           integration/sdk_boot.cpp integration/r72_world_bind195.cpp; do
  cxx_obj "$src" "$out/$(basename "${src%.cpp}").o"
done
# Potion publication catches native registry unwinds and closes the Creative lease.
o="$out/composite195.o"
clang++ "${flags[@]}" "${cxx_eh[@]}" -c loader/retail195/composite195.cpp -o "$o"; objs+=("$o")
# modules
for src in modules/items195/items.cpp modules/items195/recipes.cpp modules/items195/furnace.cpp modules/items195/effects.cpp modules/items195/adapter.cpp \
           modules/blocks195/blocks195.cpp modules/entities195/entities195.cpp modules/worldgen195/worldgen195.cpp modules/ui195/ui195.cpp \
           modules/strings195/strings195.cpp modules/enchants195/enchants195.cpp modules/effects195/effects195.cpp \
           modules/biomes195/biomes195.cpp modules/audio195/audio195.cpp modules/projectiles195/projectiles195.cpp \
           modules/fuel195/fuel195.cpp modules/fuel195/adapter.cpp modules/fuel195/fuel195_ps4.cpp \
           modules/netsync195/netsync_client.cpp modules/netsync195/netsync_sdk.cpp modules/netsync195/netsync_plugin.cpp; do
  o="$out/m_$(basename "$(dirname "$src")")_$(basename "${src%.cpp}").o"
  cxx_obj "$src" "$o"
done
for src in modules/events195/ev195.cpp modules/events195/services195.cpp; do
  o="$out/m_events195_$(basename "${src%.cpp}").o"
  clang++ "${flags[@]}" "${cxx_eh[@]}" -c "$src" -o "$o"; objs+=("$o")
done
# Lua 5.4.8 (verbatim, modules/script/lua) + the sandbox. The sandbox opens only
# base/table/string/math/utf8/coroutine, so io/os/package/debug and linit are not
# linked at all (no fopen/system/tmpnam/getenv reach the plugin from them).
for f in modules/script/lua/*.c; do
  case "$(basename "$f")" in liolib.c|loslib.c|loadlib.c|ldblib.c|linit.c|lua.c|luac.c) continue;; esac
  o="$out/lua/$(basename "${f%.c}").o"
  clang "${flags[@]}" "${lua[@]}" -c "$f" -o "$o"; objs+=("$o")
done
clang "${flags[@]}" "${cc[@]}" "-Dlua_getlocaledecpoint()='.'" -DLUA_32BITS=0 -c modules/script/lce_script.c -o "$out/m_script_lce_script.o"; objs+=("$out/m_script_lce_script.o")
c_obj modules/script/ps4_stdio_shim.c "$out/m_script_ps4_stdio_shim.o"
rm -f "$out/liblcemodloader.a"
llvm-ar rcs "$out/liblcemodloader.a" "${objs[@]}" 2>/dev/null || ar rcs "$out/liblcemodloader.a" "${objs[@]}"
ld.lld -r -m elf_x86_64 "${objs[@]}" -o "$out/lcemodloader.o"
# Unresolved externals (provided by the shipping plugin objects and the OpenOrbis libc/libkernel)
{ llvm-nm "$out/lcemodloader.o" 2>/dev/null || nm "$out/lcemodloader.o"; } | awk '$1=="U"{print $2}' | sort -u > "$out/undefined.txt"
# Weak references (resolved if the plugin provides them, else NULL)
{ llvm-nm "$out/lcemodloader.o" 2>/dev/null || nm "$out/lcemodloader.o"; } | awk '$1=="w"||$1=="v"{print $2}' | sort -u > "$out/weak.txt"
echo "built $out/liblcemodloader.a and $out/lcemodloader.o (${#objs[@]} objects)"
sha256sum "$out/liblcemodloader.a" "$out/lcemodloader.o"
echo "undefined symbols (provided by the shipping plugin / OpenOrbis libs):"
cat "$out/undefined.txt"
echo "weak references:"
cat "$out/weak.txt"
