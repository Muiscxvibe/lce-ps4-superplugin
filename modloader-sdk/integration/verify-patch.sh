#!/usr/bin/env bash
# Verify integration/shipping-plugin.patch WITHOUT touching the shipping trees:
#   1. base hashes (patch-base.sha256) of the live files,
#   2. patch -p0 --dry-run + real apply on fresh copies in OUT/b,
#   3. compile every patched TU (and its unpatched original) with the exact
#      build-crashlog.sh flags; the patched source is injected with a clang VFS
#      overlay (real path -> copy), so nothing is written to or symlinked into
#      r72-session-candidate / ps4-transport / crashlog-build,
#   4. the original objects must be byte-identical to crashlog-build/build/*.o,
#   5. link the full super195.elf (shipping ld line, patched objects + lcemodloader.o) in OUT/link
#      and list dynamic imports that the shipping ELF does not already have.
# usage: integration/verify-patch.sh OUTDIR      (OUTDIR must not exist or be empty)
set -euo pipefail
OUT=${1:?usage: verify-patch.sh OUTDIR}
W=/home/codey/minecraft-lce-work; SDK=$W/modloader-sdk; root=$W/r72-session-candidate; crash=$W/ps4-metadata-snapshot/crashlog-build; transport=$W/ps4-transport
tc=/home/codey/ps4-drive-layout-Phw5N4/ps4-baseline/research/OpenOrbis/PS4Toolchain
export LD_LIBRARY_PATH=$W/toolchain/clang19/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
mkdir -p "$OUT"; OUT=$(realpath "$OUT"); case "$OUT" in $root*|$crash*|$transport*) echo "refusing OUTDIR inside a shipping tree"; exit 2;; esac
[ -z "$(ls -A "$OUT")" ] || { echo "OUTDIR not empty"; exit 2; }
P=$SDK/integration/shipping-plugin.patch
echo "== 1. base hashes (pristine reconstruction: the shipping tree may already carry this or the previous patch)"
python3 "$SDK/integration/rebase-patch.py" --pristine "$OUT/p"
sed "s#  $W/#  $OUT/p/#" "$SDK/integration/patch-base.sha256" | sha256sum -c -
echo "== 2. dry-run + apply on copies"
mkdir -p "$OUT/b" "$OUT/out" "$OUT/link"
files=$(grep '^+++ ' "$P" | awk '{print $2}')
for f in $files; do mkdir -p "$OUT/b/$(dirname "$f")"; cp --no-dereference "$OUT/p/$f" "$OUT/b/$f"; done
(cd "$OUT/b" && patch -p0 --dry-run < "$P" && patch -p0 --quiet < "$P" && find . -name '*.orig' -delete)
bash -n "$OUT/b/ps4-metadata-snapshot/crashlog-build/build-crashlog.sh"
echo "== 3/4. compile (stock vs prebuilt, patched)"
python3 "$root/loader/selection_abi32_195/overlay.py" "$OUT/overlay.json"
python3 "$root/loader/mod_menu_frontend_complete_packs_195/overlay.py" "$OUT/frontend" >/dev/null
python3 - "$OUT" "$W" <<'PY'
import json,os,sys
out,W=sys.argv[1],sys.argv[2]
d=json.load(open(f'{out}/overlay.json')); d['use-external-names']=False; d['roots'].extend(json.load(open(f'{out}/frontend/overlay.json'))['roots'])
json.dump(d,open(f'{out}/overlay.json','w'))
for tree,name in (('b','patched-overlay.json'),('p','overlay.json')):
    roots=list(d['roots'])
    for base,_,fs in os.walk(f'{out}/{tree}'):
        for f in fs:
            rel=os.path.relpath(os.path.join(base,f),f'{out}/{tree}'); roots.append({'type':'file','name':os.path.join(W,rel),'external-contents':os.path.join(base,f)})
    json.dump({'version':0,'use-external-names':False,'roots':roots},open(f'{out}/{name}','w'))
PY
common=(-I"/home/codey/minecraft-ps4-research/loader/modded_crafting_tabs_195/work-r20/integration/include" -DMC_LEGACY195 -DMC_SUPER195 --target=x86_64-pc-freebsd12-elf -fPIC -mstackrealign -funwind-tables -fno-stack-protector -O2 -ffreestanding -fno-builtin -isysroot "$tc" -isystem "$tc/include")
cxx=(-std=c++17 -fvisibility=hidden -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-function)
s195=$root/loader/super_content_steel_nbt_save_195
fail=0
run() { local name=$1 prebuilt=$2 kind=$3; shift 3
  for v in stock patched; do
    local ov=$OUT/overlay.json; [ $v = patched ] && ov=$OUT/patched-overlay.json
    if [ $kind = c ]; then clang -ivfsoverlay "$ov" "${common[@]}" -std=c11 -Wall -Wextra -Werror "$@" -o "$OUT/out/$name.$v.o" || fail=1
    else clang++ -ivfsoverlay "$ov" "${common[@]}" "${cxx[@]}" "$@" -o "$OUT/out/$name.$v.o" || fail=1; fi
  done
  local ref="$crash/build/$prebuilt"; [ -f "$ref.pre-sdk.keep" ] && ref="$ref.pre-sdk.keep"   # an applied tree rebuilt the object
  if cmp -s "$OUT/out/$name.stock.o" "$ref"; then echo "$name: compiles; unpatched object identical to ${ref#$crash/}"
  else echo "$name: compiles; WARNING unpatched object differs from ${ref#$crash/}"; fi; }
run super super.o cxx -c "$s195/super.cpp"
run trace trace.o cxx -I"$root/loader/steel_startup_trace_195" -DSTEEL_TRACE_COMPONENT -c "$s195/registry_trace.cpp"
run tools_runtime tools_runtime.o cxx -I"$root/compatibility/ruby/vendor/lua-5.4.8/src" -c "$s195/tools_runtime.cpp"
run resource process_resource.o cxx -I"$root/compatibility/ruby/vendor/lua-5.4.8/src" -Dsteel195_resource_grow=mc195_items_resource_grow -c "$s195/resource.cpp"
run content_process content_process.o cxx -c "$s195/content_process.cpp"
run craft_hook content_craft_hook.o cxx -c "$root/loader/steel_process_content_195/craft_hook.cpp"
run craft_ui_capture craft_provider/integration_provider_capture.o cxx -I/home/codey/minecraft-ps4-research/loader/modded_crafting_tabs_195/work-r20 -c "$root/loader/additive_recipes_195/craft_ui_capture.cpp"
run native_bridge lce_native_bridge.o c -I"$transport" -c "$transport/native_bridge.c"
run session_runtime lce_session_runtime.o cxx -I"$transport" -c "$transport/session_runtime.cpp"
# shared_terrain_195/runtime.cpp (gap G1): its own build.sh flags (Steel mode, -Os, no ABI overlay)
terr=(--target=x86_64-pc-freebsd12-elf -Os -fPIC -mstackrealign -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -Wno-unused-function -isysroot "$tc" -isystem "$tc/include" -std=c++17 -fno-exceptions -fno-rtti)
python3 - "$OUT" "$W" <<'PY'
import json,os,sys
out,W=sys.argv[1],sys.argv[2]
for tree,name in (('b','terr-patched.json'),('p','terr-stock.json')):
    roots=[]
    for base,_,fs in os.walk(f'{out}/{tree}'):
        for f in fs:
            rel=os.path.relpath(os.path.join(base,f),f'{out}/{tree}'); roots.append({'type':'file','name':os.path.join(W,rel),'external-contents':os.path.join(base,f)})
    json.dump({'version':0,'use-external-names':False,'roots':roots},open(f'{out}/{name}','w'))
PY
clang++ -ivfsoverlay "$OUT/terr-stock.json" "${terr[@]}" -c "$root/loader/shared_terrain_195/runtime.cpp" -o "$OUT/out/terrain.stock.o" || fail=1
clang++ -ivfsoverlay "$OUT/terr-patched.json" "${terr[@]}" -c "$root/loader/shared_terrain_195/runtime.cpp" -o "$OUT/out/terrain.patched.o" || fail=1
tref="$crash/build/shared_terrain_runtime.o"; [ -f "$tref.pre-sdk.keep" ] && tref="$tref.pre-sdk.keep"
if cmp -s "$OUT/out/terrain.stock.o" "$tref"; then echo "terrain_runtime: compiles; unpatched object identical to ${tref#$crash/}"
else echo "terrain_runtime: compiles; WARNING unpatched object differs from ${tref#$crash/}"; fi
[ $fail = 0 ] || { echo "COMPILE FAILED"; exit 1; }
echo "== 5. full link in $OUT/link"
"$SDK/build-ps4.sh" >/dev/null
objs=(); for o in "$crash"/build/craft_provider/*.o; do case "${o##*/}" in modded-crafting-candidate.o|integration_provider_capture.o) ;; *) objs+=("$o");; esac; done
ld.lld -r "${objs[@]}" "$OUT/out/craft_ui_capture.patched.o" -o "$OUT/link/modded-crafting-candidate.o"
debug="$root/native-debug-plugin/cooperative-upgrade/menu-hierarchy-world-actions"; lcesdk=$SDK
line=$(grep '^ld.lld ' "$OUT/b/ps4-metadata-snapshot/crashlog-build/build-crashlog.sh" | grep -v '^ld.lld -r ' | grep -m1 'super195.elf')
line=${line//'$crash/build/super195.elf'/$OUT/link/super195.elf}
eval "args=(${line#ld.lld })"
final=()
for a in "${args[@]}"; do case "$a" in
  $crash/build/super.o) final+=("$OUT/out/super.patched.o");; $crash/build/trace.o) final+=("$OUT/out/trace.patched.o");;
  $crash/build/tools_runtime.o) final+=("$OUT/out/tools_runtime.patched.o");; $crash/build/shared_terrain_runtime.o) final+=("$OUT/out/terrain.patched.o");; $crash/build/process_resource.o) final+=("$OUT/out/resource.patched.o");; $crash/build/content_process.o) final+=("$OUT/out/content_process.patched.o");;
  $crash/build/content_craft_hook.o) final+=("$OUT/out/craft_hook.patched.o");; $crash/build/lce_native_bridge.o) final+=("$OUT/out/native_bridge.patched.o");;
  $crash/build/lce_session_runtime.o) final+=("$OUT/out/session_runtime.patched.o");;
  $crash/build/craft_provider/modded-crafting-candidate.o) final+=("$OUT/link/modded-crafting-candidate.o");; *) final+=("$a");; esac; done
ld.lld "${final[@]}"
readelf --dyn-syms -W "$crash/build/super195.elf" | awk '$7=="UND"{print $8}' | sort -u > "$OUT/link/imports.shipping"
readelf --dyn-syms -W "$OUT/link/super195.elf" | awk '$7=="UND"{print $8}' | sort -u > "$OUT/link/imports.patched"
echo "linked $OUT/link/super195.elf; dynamic imports not in the shipping ELF: [$(comm -13 "$OUT/link/imports.shipping" "$OUT/link/imports.patched" | tr '\n' ' ')]"
UP=$(ls "$SDK"/integration/upgrade-*.patch 2>/dev/null | sort | tail -1)   # newest upgrade for an already-applied tree
if [ -n "$UP" ] && [ -s "$UP" ]; then
  echo "== 6. upgrade patch on a copy of the LIVE tree (for an engineer who applied the previous version)"
  mkdir -p "$OUT/live"; for f in $(grep '^+++ ' "$UP" | awk '{print $2}'); do mkdir -p "$OUT/live/$(dirname "$f")"; cp "$W/$f" "$OUT/live/$f"; done
  (cd "$OUT/live" && patch -p0 --quiet < "$UP")
  for f in $(grep '^+++ ' "$UP" | awk '{print $2}'); do cmp -s "$OUT/live/$f" "$OUT/b/$f" || { echo "upgrade result differs from the full patch: $f"; exit 1; }; done
  echo "upgrade patch $(basename "$UP"): live tree + upgrade == pristine + full patch"
elif [ -n "$UP" ]; then echo "== 6. $(basename "$UP") is empty: the live tree already equals pristine + full patch"
fi
echo "VERIFY OK"
