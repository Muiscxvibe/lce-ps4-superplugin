#!/usr/bin/env bash
set -euo pipefail
crash="${LCE_CRASHLOG_BUILD:-/home/codey/minecraft-lce-work/ps4-metadata-snapshot/crashlog-build}"
r72="${LCE_R72:-/home/codey/minecraft-lce-work/r72-session-candidate}"
cd "$r72/loader/super_content_steel_nbt_save_195"
root=$(cd ../.. && pwd)
debug="$root/native-debug-plugin/cooperative-upgrade/menu-hierarchy-world-actions"
legacy="$root/loader/native_threeway_repeat_candidate/source/legacy195"
loader="$root/loader/native_threeway_repeat_candidate/source/loader"
store="$root/loader/store_button_195"
tc=/home/codey/ps4-drive-layout-Phw5N4/ps4-baseline/research/OpenOrbis/PS4Toolchain
sdk=/home/codey/cod-ps4-network/vendor/GoldHEN_Plugins_SDK
mkdir -p "$crash/build"
# Links verified r23 baseline objects plus every rebuilt Lucky component and r24 addition.
catalog_tmp=$(mktemp -d)
trap 'rm -rf "$catalog_tmp"' EXIT
cp "$root/catalog_snapshot.pinned.bin" "$catalog_tmp/catalog_snapshot.bin"
cp "$catalog_tmp/catalog_snapshot.bin" $crash/build/catalog_snapshot.bin
python3 "$root/loader/selection_abi32_195/overlay.py" "$catalog_tmp/overlay.json"
python3 "$root/loader/mod_menu_frontend_complete_packs_195/overlay.py" "$catalog_tmp/frontend" >/dev/null
python3 - "$catalog_tmp/overlay.json" "$catalog_tmp/frontend/overlay.json" <<'PYMERGE'
import json,sys
from pathlib import Path
path=Path(sys.argv[1]);data=json.loads(path.read_text());extra=json.loads(Path(sys.argv[2]).read_text())
data['use-external-names']=False;data['roots'].extend(extra['roots']);path.write_text(json.dumps(data))
PYMERGE
export MC195_ABI_OVERLAY="$catalog_tmp/overlay.json"
flags=(-I"/home/codey/minecraft-ps4-research/loader/modded_crafting_tabs_195/work-r20/integration/include" -ivfsoverlay "$MC195_ABI_OVERLAY" -DMC_LEGACY195 -DMC_SUPER195 --target=x86_64-pc-freebsd12-elf -fPIC -mstackrealign -funwind-tables -fno-stack-protector -O2 -ffreestanding -fno-builtin -isysroot "$tc" -isystem "$tc/include")
cxx=(-std=c++17 -fvisibility=hidden -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wno-unused-function)
transport="${LCE_TRANSPORT:-/home/codey/minecraft-lce-work/ps4-transport}"
# LCE_RELEASE=1: public release build. Compiles out diagnostics (netmgr vtable
# tracer, dialog/disconnect/packet traces, metadata snapshot and native-tabs
# reports, verbose GoldHEN logging) and reports per-server pseudonymous console
# ids. Default (unset/0): the dev build, unchanged.
debug_diag=(-DMC_METADATA_SNAPSHOT -DMC_NATIVE_DEBUG_DIAGNOSTICS)
plugin_build="r8-anticheat-$(date +%Y%m%d%H%M%S)"
if [ "${LCE_RELEASE:-0}" = 1 ]; then
 flags+=(-DLCE_RELEASE=1 -DLCE_HW_PSEUDONYMS=1)
 debug_diag=()
 plugin_build="r9-release-$(date +%Y%m%d%H%M%S)"
fi
if [ "${LCE_OFFLINE_PREVIEW:-0}" = 1 ]; then
 if [ "${LCE_RELEASE:-0}" != 1 ]; then
  echo "LCE_OFFLINE_PREVIEW=1 requires LCE_RELEASE=1" >&2
  exit 1
 fi
 flags+=(-DLCE_OFFLINE_PREVIEW=1)
 plugin_build="r9-offline-$(date +%Y%m%d%H%M%S)"
fi
clang++ "${flags[@]}" "${cxx[@]}" -include "$PWD/abi_guard.hpp" -Wno-missing-field-initializers -I"$root/native-debug-plugin" -I"$root/shader-re/opengnm/include" -c "$root/loader/mod_menu_frontend_complete_packs_195/frontend.cpp" -o "$crash/build/tabs_modmenu_frontend.o"
echo "$plugin_build" > $crash/build/plugin_build.txt
for name in session_runtime session_coordinator platform_hook_install join_native_ops join_list join_binding join_adapter join_vtable session_query_dispatch external_players external_players_native external_session_state native_start_transition player_channel player_native_factory player_vtable sqr_native_factory socket_native_factory native_identity native_keyboard account_flow account_join_route login_gui orbis_scheduler; do
 clang++ "${flags[@]}" "${cxx[@]}" -I"$transport" -c "$transport/$name.cpp" -o "$crash/build/lce_$name.o"
done
for name in orbis_socket orbis_binding orbis_session session_transport connection framing send_queue tex_choice native_bridge player_send_hook auth_client auth_https orbis_auth gateway_handshake; do
 clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I"$transport" -c "$transport/$name.c" -o "$crash/build/lce_$name.o"
done
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -c $crash/vtable_trace.c -o $crash/build/lce_vtable_trace.o
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -c $crash/goldhen_ids.c -o $crash/build/lce_goldhen_ids.o
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I"$transport" -c "$transport/skin_registry.c" -o $crash/build/lce_skin_registry.o
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I"$transport" -c "$transport/tex_registry.c" -o $crash/build/lce_tex_registry.o
# Downloaded skin-pack layout validator (64x64 skins, BOX/OFFSET 3D geometry): rebuilt from source; replaces the
# prebuilt gre_generic_flat_format.o (host test: r72 loader/additive_pack_import_195/run_test_flat_format.sh).
[ -f "$crash/build/gre_generic_flat_format.o.pre-3dskin.keep" ] || cp "$crash/build/gre_generic_flat_format.o" "$crash/build/gre_generic_flat_format.o.pre-3dskin.keep"
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -c "$root/loader/additive_pack_import_195/flat_format.c" -o "$crash/build/gre_generic_flat_format.o"
# Java texture-pack runtime: rebuilt here (extra slots for downloaded packs, see dynamic.hpp); replaces the prebuilt gre_java_* objects.
for name in runtime component source_loader staged_verify adapter bind kernel_file_ops; do
 clang++ "${flags[@]}" "${cxx[@]}" -c "$root/loader/java_texture_invictus_runtime_195/$name.cpp" -o "$crash/build/gre_java_$name.o"
done
# mods_content.cpp addresses mc195_mods::Model fields by the offsets used by the prebuilt object; refuse to build if it changed.
model_dis=$(objdump -d --no-show-raw-insn "$crash/build/tabs_modmenu_model.o")
for pattern in 'cmp    0x68(%r12),%ecx' 'cmpb   $0x0,0x1327c(%rdi)' 'mov    %rax,0x13288(%r12)' 'cmp    0x11ec(%r12),%eax' 'cmp    0x11f0(%r12),%edx' 'imul   $0x8c,%rdi,%rdi' 'cmpl   $0x0,0x13258(%rdi)' 'lea    0x1a58(%rbx),%rax'; do
 grep -qF -- "$pattern" <<<"$model_dis" || { echo "tabs_modmenu_model.o layout changed (pattern $pattern); update kSectionOffset/kActiveOffset/kGenerationOffset in mods_content.cpp" >&2; exit 1; }
done
mono="${LCE_MONOCYPHER:-/home/codey/minecraft-lce-work/third_party/monocypher}"
for name in content_catalog content_download; do
 clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I"$transport" -I"$mono" -c "$transport/$name.c" -o "$crash/build/lce_$name.o"
done
for name in monocypher monocypher-ed25519; do
 clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I"$mono" -c "$mono/$name.c" -o "$crash/build/lce_$name.o"
done
clang++ "${flags[@]}" "${cxx[@]}" -I"$transport" -c "$transport/mods_menu.cpp" -o "$crash/build/lce_mods_menu.o"
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I"$transport" -c "$transport/plugin_update.c" -o "$crash/build/lce_plugin_update.o"
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I"$transport" -c "$transport/install_key_store.c" -o "$crash/build/lce_install_key_store.o"
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -I"$mono" -c $crash/plugin_update.cpp -o "$crash/build/lce_plugin_update_glue.o"
# First-run self-setup: /data/lce-mods folders, LAN discovery (UDP 25564) -> lce-servers.conf, account.conf/CA when missing.
# Pure logic + host tests: ps4-transport/{lan_discovery,auto_config}.c, run_test_selfsetup.sh.
for name in lan_discovery auto_config; do
 clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I"$transport" -c "$transport/$name.c" -o "$crash/build/lce_$name.o"
done
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -c $crash/selfsetup.cpp -o "$crash/build/lce_selfsetup.o"
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -c $crash/mods_content.cpp -o "$crash/build/lce_mods_content.o"
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -c $crash/dyn_skins.cpp -o $crash/build/lce_dyn_skins.o
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -I"$r72" -c $crash/dyn_textures.cpp -o $crash/build/lce_dyn_textures.o
# remote_actions.cpp: debug menu on a server (debug_all.hpp; host test run_debug_all_test.sh)
clang++ "${flags[@]}" "${cxx[@]}" -I$crash -c $crash/remote_actions.cpp -o $crash/build/lce_remote_actions.o
# mod_gate.cpp: runtime gating of bundled-mod GUI/content while on a server + anticheat canonical hash
clang++ "${flags[@]}" "${cxx[@]}" -I$crash -c $crash/mod_gate.cpp -o $crash/build/lce_mod_gate.o
# mods_tabs*.cpp: downloaded/installed content (SDK mods, packs, maps) appended to the native Mods menu tabs + SDK enable list
clang++ "${flags[@]}" "${cxx[@]}" -I$crash -c $crash/mods_tabs.cpp -o $crash/build/lce_mods_tabs.o
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I$crash -I"$transport" -c $crash/mods_tabs_lce.cpp -o $crash/build/lce_mods_tabs_lce.o
# perf_boot.cpp (agent-perfmods): boot timing, pristine-RX verification cache (--wrap=mc195_verify_pristine_rx),
# retail add-on mount timers and frame-rate options (perf.ini; host tests: run_perf_boot_test.sh).
clang++ "${flags[@]}" "${cxx[@]}" -I$crash -c $crash/perf_boot.cpp -o $crash/build/lce_perf_boot.o
# worldsize195 (agent-worldsize): "Huge" 1024-chunk local worlds; game-code words armed only off LCE servers
# and recorded with mod_gate (host tests: agent-worldsize/test.sh).
clang++ "${flags[@]}" "${cxx[@]}" -I$crash -c $crash/worldsize195.cpp -o $crash/build/lce_worldsize195.o
clang++ "${flags[@]}" "${cxx[@]}" -I$crash -c $crash/worldsize_lce.cpp -o $crash/build/lce_worldsize_lce.o
clang++ "${flags[@]}" "${cxx[@]}" -I$crash -c $crash/remote_nbt.cpp -o $crash/build/lce_remote_nbt.o
clang++ "${flags[@]}" "${cxx[@]}" -c $crash/remote_input.cpp -o $crash/build/lce_remote_input.o
# Full NBT editor for the NBT tab while on a server (host tests: run_nbt_editor_test.sh). Its frame
# hook is called from cmd_overlay.cpp's Navigation::begin wrap; it wraps mc195_mods_pad_sample_data.
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I$crash -c $crash/nbt_editor.cpp -o $crash/build/lce_nbt_editor.o
# Local-world backend for the NBT editor (host tests: run_nbt_editor_test.sh test_local_backend); super.cpp subscribes it to the shared scope.
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I$crash -c $crash/local_nbt.cpp -o $crash/build/lce_local_nbt.o
# Debug menu "Commands" / "WorldEdit" tabs: controller palette for the server's in-game commands.
clang++ "${flags[@]}" "${cxx[@]}" -I"$transport" -c "$transport/cmd_menu.cpp" -o "$crash/build/lce_cmd_menu.o"
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -c $crash/cmd_overlay.cpp -o "$crash/build/lce_cmd_overlay.o"
# In-game chat (touch pad -> on-screen keyboard -> chat packet; chat log) and the server F3 overlay.
clang++ "${flags[@]}" "${cxx[@]}" -I"$transport" -c "$transport/chat_log.cpp" -o "$crash/build/lce_chat_log.o"
clang++ "${flags[@]}" "${cxx[@]}" -I"$transport" -c "$transport/chat_keyboard.cpp" -o "$crash/build/lce_chat_keyboard.o"
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -c $crash/chat_overlay.cpp -o "$crash/build/lce_chat_overlay.o"
# Map import (downloaded maps -> savedata): pure logic + console glue (replaces the removed legacy staged-zip importer),
# plus the generic, reviewed archive parser and PS4 region validation from loader/map_import_195 (no pinned index, no provider.cpp/worker.cpp).
mapimp="$root/loader/map_import_195"
clang++ "${flags[@]}" "${cxx[@]}" -I"$transport" -c "$transport/map_import.cpp" -o "$crash/build/lce_map_import.o"
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -I"$root/loader" -c $crash/map_import_lce.cpp -o "$crash/build/lce_map_import_lce.o"
clang++ "${flags[@]}" "${cxx[@]}" -c "$mapimp/archive.cpp" -o "$crash/build/lce_map_archive.o"
clang++ "${flags[@]}" "${cxx[@]}" -c "$mapimp/region_validation.cpp" -o "$crash/build/lce_map_region_validation.o"
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -DLCE_PLUGIN_BUILD="\"$plugin_build\"" -I"$transport" -c "$transport/client_report.c" -o "$crash/build/lce_client_report.o"
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I$crash -c $crash/logger.crashdiag.c -o $crash/build/frontend_crash_logger.o
clang++ "${flags[@]}" "${cxx[@]}" -I"$debug" -I"$transport" "${debug_diag[@]}" -DMC_COOPERATIVE_TABS -Dplugin_load=mc195_debug_load -Dplugin_unload=mc195_debug_unload -Dmodule_start=mc195_debug_module_start -Dmodule_stop=mc195_debug_module_stop -Dg_pluginName=mc195_debug_name -Dg_pluginDesc=mc195_debug_desc -Dg_pluginAuth=mc195_debug_auth -Dg_pluginVersion=mc195_debug_version -c $crash/debug_main.item_diag.cpp -o $crash/build/debug_main.o
# Rebuild the current NBT presenter rather than using a stale object.
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers "${debug_diag[@]}" -I"$root/shader-re/opengnm/include" -c "$root/menu-render-bridge/nuklear_presenter.cpp" -o "$crash/build/tabs_114.o"
lcesdk=${LCE_SDK_ROOT:-/home/codey/minecraft-lce-work/modloader-sdk}
viewport_flags=()
waypoint_objects=()
if [[ ${LCE_EXPERIMENTAL_VIEWPORT_TINT:-0} == 1 ]]; then
  export LCE_EXPERIMENTAL_VIEWPORT_TINT
  viewport_flags+=(-DLCE_EXPERIMENTAL_VIEWPORT_TINT)
fi
if [[ ${LCE_F3_WAYPOINTS:-0} == 1 ]]; then
  [[ ${LCE_OFFLINE_PREVIEW:-0} == 1 ]] || { echo "LCE_F3_WAYPOINTS=1 requires LCE_OFFLINE_PREVIEW=1" >&2; exit 1; }
  waypoint_root=${LCE_WAYPOINTS_ROOT:?Set LCE_WAYPOINTS_ROOT to the waypoint source directory}
  viewport_flags+=(-DLCE_F3_WAYPOINTS=1 -I"$waypoint_root/include" -I"$waypoint_root/integration" -I"$root/loader/f3_server_195")
  mkdir -p "$crash/build/waypoints"
  waypoint_objects=("$crash/build/waypoints/model.o" "$crash/build/waypoints/draw.o")
  clang "${flags[@]}" -std=c99 -Wall -Wextra -Werror -I"$waypoint_root/include" \
    -c "$waypoint_root/src/lce_waypoints.c" -o "${waypoint_objects[0]}"
  clang++ "${flags[@]}" "${cxx[@]}" "${viewport_flags[@]}" \
    -I"$root/shader-re/opengnm/include" \
    -c "$waypoint_root/integration/f3_waypoint_draw.cpp" -o "${waypoint_objects[1]}"
fi
if [[ ${LCE_WAYPOINT_PACKAGES:-0} == 1 ]]; then
  [[ ${LCE_F3_WAYPOINTS:-0} == 1 ]] || { echo "LCE_WAYPOINT_PACKAGES=1 requires LCE_F3_WAYPOINTS=1" >&2; exit 1; }
  export LCE_WAYPOINT_PACKAGES
  viewport_flags+=(-DLCE_WAYPOINT_PACKAGES=1 -I"$waypoint_root/activation")
  waypoint_objects+=("$crash/build/waypoints/packages.o")
  clang++ "${flags[@]}" "${cxx[@]}" "${viewport_flags[@]}" -include "$PWD/abi_guard.hpp" \
    -I"$root/shader-re/opengnm/include" -I"$lcesdk/include" \
    -c "$waypoint_root/activation/f3_package_waypoints.cpp" -o "$crash/build/waypoints/packages.o"
fi
[ -f $crash/tabs_29.orig.keep ] || cp $crash/build/tabs_29.o $crash/tabs_29.orig.keep
nav_catalog_input="$crash/tabs_29.orig.keep"
if [[ ${LCE_WAYPOINT_PACKAGES:-0} == 1 ]]; then
  nav_catalog_input="$crash/build/waypoints/navigation.o"
  clang++ "${flags[@]}" "${cxx[@]}" "${debug_diag[@]}" "${viewport_flags[@]}" -include "$PWD/abi_guard.hpp" \
    -Wno-missing-field-initializers -I"$root/native-debug-plugin" -I"$root/nbt-dropped-item" \
    -I"$root/menu-render-bridge/gnm-submit" -I"$root/shader-re/opengnm/include" -I"$lcesdk/include" \
    -c "$root/loader/nbt_status_wait_195/navigation.cpp" -o "$nav_catalog_input"
fi
# Debug menu catalogue: give the "(unavailable)" rows the server / plugin can serve real
# command ids, re-encode baby / villager rows (patch_debug_catalog.py checks every row first).
python3 "$crash/patch_debug_catalog.py" "$nav_catalog_input" "$crash/build/tabs29_catalog.obj"
# remote_actions.cpp (debug_all.hpp) reads the trade-level page state from Navigation; refuse
# to build if the Navigation layout moved.
nav_dis=$(objdump -d --no-show-raw-insn "$nav_catalog_input")
for pattern in 'lea    0x3210(%rax),%rdi' 'cmpb   $0x0,0x3340(%rax)' 'cmpb   $0x0,0x3341(%rax)' 'mov    0x3338(%rax),%ecx' 'mov    0x333c(%rax),%eax' 'cmpb   $0x0,0x60(%rdi)'; do
 grep -qF -- "$pattern" <<<"$nav_dis" || { echo "tabs_29.o Navigation layout changed (pattern $pattern); update debug_all.hpp kNav* offsets" >&2; exit 1; }
done
objcopy --add-symbol lce_body_submit_sheep=.text._ZN11tab_actions10Dispatcher12submit_sheepEjj:0,function,global --add-symbol lce_body_submit_animal_variant=.text._ZN11tab_actions10Dispatcher21submit_animal_variantEjj:0,function,global --add-symbol lce_body_available_sheep=.text._ZNK11tab_actions10Dispatcher15available_sheepEjj:0,function,global --add-symbol lce_body_available_animal_variant=.text._ZNK11tab_actions10Dispatcher24available_animal_variantEjj:0,function,global --redefine-sym _ZN11tab_actions10Dispatcher12submit_sheepEjj=lce_hook_submit_sheep --redefine-sym _ZN11tab_actions10Dispatcher21submit_animal_variantEjj=lce_hook_submit_animal_variant --redefine-sym _ZNK11tab_actions10Dispatcher15available_sheepEjj=lce_hook_available_sheep --redefine-sym _ZNK11tab_actions10Dispatcher24available_animal_variantEjj=lce_hook_available_animal_variant --add-symbol lce_body_available=.text._ZNK11tab_actions10Dispatcher9availableEjj:0,function,global --add-symbol lce_body_submit=.text._ZN11tab_actions10Dispatcher6submitEjj:0,function,global --redefine-sym _ZNK11tab_actions10Dispatcher9availableEjj=lce_hook_available --redefine-sym _ZN11tab_actions10Dispatcher6submitEjj=lce_hook_submit --add-symbol lce_body_submit_item_quantity=.text._ZN11tab_actions10Dispatcher20submit_item_quantityEjjj:0,function,global --add-symbol lce_body_submit_mob=.text._ZN11tab_actions10Dispatcher10submit_mobEjj:0,function,global --redefine-sym _ZN11tab_actions10Dispatcher20submit_item_quantityEjjj=lce_hook_submit_item_quantity --redefine-sym _ZN11tab_actions10Dispatcher10submit_mobEjj=lce_hook_submit_mob --add-symbol lce_body_available_mob=.text._ZNK11tab_actions10Dispatcher13available_mobEjj:0,function,global --redefine-sym _ZNK11tab_actions10Dispatcher13available_mobEjj=lce_hook_available_mob --add-symbol lce_body_available_item_quantity=.text._ZNK11tab_actions10Dispatcher23available_item_quantityEjjj:0,function,global --redefine-sym _ZNK11tab_actions10Dispatcher23available_item_quantityEjjj=lce_hook_available_item_quantity $crash/build/tabs29_catalog.obj $crash/build/tabs_29.o
# LCE mod loader SDK (modloader-sdk/integration/shipping-plugin.patch, INTEGRATION.md).
# 1. Rebuild the r72 translation units this patch changes, with the flags above.
#    The prebuilt objects are kept once as *.pre-sdk.keep (rollback: copy them back).
for entry in "debug_tabs_viewport_195/multi_bootstrap.cpp:tabs_view_multi_bootstrap" \
             "debug_tabs_view_diagnostics_195/bindings.cpp:tabs_view_bindings" \
             "f3_server_195/hud.cpp:lucky_f3_hud"; do
  clang++ "${flags[@]}" "${cxx[@]}" "${debug_diag[@]}" "${viewport_flags[@]}" \
    -Wno-missing-field-initializers -include "$PWD/abi_guard.hpp" -I"$root/native-debug-plugin" \
    -I"$root/menu-render-bridge/gnm-submit" -I"$root/shader-re/opengnm/include" -I"$lcesdk/include" \
    -c "$root/loader/${entry%:*}" -o "$crash/build/${entry#*:}.o"
done
if (( ${#waypoint_objects[@]} )); then
  ld.lld -r "$crash/build/lucky_f3_hud.o" "${waypoint_objects[@]}" -o "$crash/build/waypoints/hud.o"
  mv "$crash/build/waypoints/hud.o" "$crash/build/lucky_f3_hud.o"
fi
# The SDK and the stock r72 world services share one native entity adapter.
clang++ "${flags[@]}" "${cxx[@]}" -fexceptions \
  -c "$root/loader/lucky_block_world_events_195/entity.cpp" -o "$crash/build/lucky_world_entity.o"
s195="$root/loader/super_content_steel_nbt_save_195"
for o in super trace tools_runtime process_resource content_process content_craft_hook creative_mods_category creative_mods_grid tools_generic_creative tools_generic_named_stack lucky_r30_lucky_items_providers lucky_integration craft_provider/integration_provider_capture craft_provider/modded-crafting-candidate; do
 [ -f "$crash/build/$o.o.pre-sdk.keep" ] || cp "$crash/build/$o.o" "$crash/build/$o.o.pre-sdk.keep"
done
clang++ "${flags[@]}" "${cxx[@]}" -c "$s195/super.cpp" -o "$crash/build/super.o"
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$root/native-debug-plugin" -I"$root/menu-render-bridge/gnm-submit" -I"$root/shader-re/opengnm/include" -c "$s195/nbt_integration.cpp" -o "$crash/build/nbt_lifecycle_integration.o"
clang++ "${flags[@]}" "${cxx[@]}" "${debug_diag[@]}" -Wno-missing-field-initializers -I"$root/native-debug-plugin" -I"$root/menu-render-bridge/gnm-submit" -I"$root/shader-re/opengnm/include" -c "$root/loader/debug_pad_close_195/raw_pad_owner.cpp" -o "$crash/build/tabs_122.o"
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -c "$root/loader/content_startup_notice_195/notice.c" -o "$crash/build/content_notice_reporter.o"
for component in pump request; do
 clang++ "${flags[@]}" "${cxx[@]}" -c "$root/loader/additive_skin_pump_repair_195/telemetry_$component.cpp" -o "$crash/build/gre_telemetry_$component.o"
done
[ -f "$crash/build/nbt_lifecycle_shared_scope_mux.o.pre-scope8.keep" ] || cp "$crash/build/nbt_lifecycle_shared_scope_mux.o" "$crash/build/nbt_lifecycle_shared_scope_mux.o.pre-scope8.keep"
clang++ "${flags[@]}" "${cxx[@]}" -fexceptions -c "$root/loader/shared_server_scope_195/mux.cpp" -o "$crash/build/nbt_lifecycle_shared_scope_mux.o"   # shared scope capacity 8 (room for the LCE SDK)
clang++ "${flags[@]}" "${cxx[@]}" -I"$root/loader/steel_startup_trace_195" -DSTEEL_TRACE_COMPONENT -c "$s195/registry_trace.cpp" -o "$crash/build/trace.o"
clang++ "${flags[@]}" "${cxx[@]}" -I"$root/compatibility/ruby/vendor/lua-5.4.8/src" -c "$s195/tools_runtime.cpp" -o "$crash/build/tools_runtime.o"
clang++ "${flags[@]}" "${cxx[@]}" -c "$s195/category.cpp" -o "$crash/build/creative_mods_category.o"
clang++ "${flags[@]}" "${cxx[@]}" -c "$s195/grid.cpp" -o "$crash/build/creative_mods_grid.o"
clang++ "${flags[@]}" "${cxx[@]}" -c "$root/loader/additive_items_195/creative.cpp" -o "$crash/build/tools_generic_creative.o"
clang++ "${flags[@]}" "${cxx[@]}" -fexceptions -I"$lcesdk/integration" -c "$root/loader/additive_items_195/named_stack.cpp" -o "$crash/build/tools_generic_named_stack.o"
clang++ "${flags[@]}" "${cxx[@]}" -fexceptions -c "$s195/lucky_items_providers.cpp" -o "$crash/build/lucky_r30_lucky_items_providers.o"
clang++ "${flags[@]}" "${cxx[@]}" -c "$s195/lucky_integration.cpp" -o "$crash/build/lucky_integration.o"
clang++ "${flags[@]}" "${cxx[@]}" -I"$root/compatibility/ruby/vendor/lua-5.4.8/src" -Dsteel195_resource_grow=mc195_items_resource_grow -c "$s195/resource.cpp" -o "$crash/build/process_resource.o"
clang++ "${flags[@]}" "${cxx[@]}" -c "$s195/content_process.cpp" -o "$crash/build/content_process.o"
clang++ "${flags[@]}" "${cxx[@]}" -c "$root/loader/steel_process_content_195/craft_hook.cpp" -o "$crash/build/content_craft_hook.o"
clang++ "${flags[@]}" "${cxx[@]}" -I/home/codey/minecraft-ps4-research/loader/modded_crafting_tabs_195/work-r20 -c "$root/loader/additive_recipes_195/craft_ui_capture.cpp" -o "$crash/build/craft_provider/integration_provider_capture.o"
craft_objs=(); for o in "$crash"/build/craft_provider/*.o; do [ "${o##*/}" = modded-crafting-candidate.o ] || craft_objs+=("$o"); done
ld.lld -r "${craft_objs[@]}" -o "$crash/build/craft_provider/modded-crafting-candidate.o"
# Terrain atlas owner with SDK block tiles (gap G1): shared_terrain_195/runtime.cpp built with its own build.sh flags
# (Steel mode, -Os, no ABI overlay); the unpatched source reproduces the prebuilt object byte for byte (md5 84d1a800...).
[ -f "$crash/build/shared_terrain_runtime.o.pre-sdk.keep" ] || cp "$crash/build/shared_terrain_runtime.o" "$crash/build/shared_terrain_runtime.o.pre-sdk.keep"
clang++ --target=x86_64-pc-freebsd12-elf -Os -fPIC -mstackrealign -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -Wno-unused-function -isysroot "$tc" -isystem "$tc/include" -std=c++17 -fno-exceptions -fno-rtti -c "$root/loader/shared_terrain_195/runtime.cpp" -o "$crash/build/shared_terrain_runtime.o"
# 2. Build the SDK (core, composite retail195 backend, modules, Lua, netsync) as one object.
if [[ -z ${LCE_SDK_OBJECT:-} ]]; then
  "$lcesdk/build-ps4.sh" >/dev/null
  LCE_SDK_OBJECT="$lcesdk/build/ps4/lcemodloader.o"
fi
ld.lld --wrap=mc195_verify_pristine_rx --wrap=mods195_category_status --wrap=mods195_category_count --wrap=mods195_category_wrapper --wrap=lce_sdk_boot --wrap=mc195_craft_select_embedded --wrap=mc195_craft_filter_decide --wrap=steel195_creative_hook_start --wrap=steel195_craft_hook_start --wrap=steel195_resource_hooks_start --wrap=steel195_retirement_hook_start --wrap=mc195_ore_probe_hook_start --wrap=mc195_lucky_hook_start --wrap=mc195_steel_ore_prepare --wrap=steel195_ore_block_hook_start --wrap=mc195_craft_candidate_start --wrap=mc195_lucky_prepare --wrap=mc195_terrain_install --wrap=mc195_giant_texture_install --wrap=mc195_rainbow_install --wrap=mc195_lucky_chat_colour_install --wrap=mods195_private_movie_start --wrap=mods195_nav_start --wrap=mc195_custom_startup_prepare --wrap=mc195_lucky_native_prepare --wrap=_ZN9tab_modal10Navigation4pollEv --wrap=mc195_mods_pad_sample_data --wrap=_Z13mc195_f3_bindP21mc195_natural_process --wrap=_ZN9tab_modal10Navigation5beginEiiiihhPNS_5FrameE --wrap=_ZN9tab_modal10Navigation10invalidateEv --wrap=_ZN11menu_render16NuklearPresenter5startERN9tab_modal10NavigationERKNS_11DirectLeaseEiiiihhPNS_13DirectFrameIdE --wrap=lce_remote_inbound_frame --wrap=lce_remote_pump --wrap=lce_plugin_update_tick --wrap=_ZN12mc195_f3_hud9availableEjb --wrap=_ZN12mc195_f3_hud7enabledEj --wrap=_ZN12mc195_f3_hud5stateEj --wrap=_ZN12mc195_f3_hud6toggleEjb --wrap=_ZN12mc195_f3_hud4tickEj --wrap=_ZN12mc195_f3_hud6renderEjRN11menu_render16PostUiCompositorEPv --wrap=_ZN12mc195_f3_hud10invalidateEj -m elf_x86_64 -pie --wrap=mc195_gre_set_skin_enabled --wrap=mc195_selection_worker_startup_choice --wrap=_ZN9tab_modal10Navigation5inputEiNS_3KeyEbb --wrap=mc195_debug_action_available --wrap=_ZN10mc195_mods5Model4openEv --wrap=_ZN10mc195_mods5Model7refreshEv --wrap=_ZN10mc195_mods5Model5inputEN9tab_modal3KeyEb --wrap=_ZN10mc195_mods5Model4drawERKN11menu_render10DirectSinkEiiiihh --wrap=_ZN9tab_modal10Navigation30configure_visual_world_actionsERKN11tab_actions14WeatherBackendE --version-script "$debug/exports.map" --script "$tc/link.x" -e _init --eh-frame-hdr -L "$tc/lib" $crash/build/crtprx.o $crash/build/super.o $crash/build/frontend_diagnostics.o $crash/build/frontend_crash_logger.o $crash/build/debug_main.o $crash/build/native_pad.o $crash/build/weather.o $crash/build/runtime.o $crash/build/action_trace.o $crash/build/runtime_attest.o $crash/build/shared_tick_195.o $crash/build/dialog_gate_195.o $crash/build/title_detour.o $crash/build/ps4_attest.o $crash/build/store_bridge.o $crash/build/probe.o $crash/build/menu_action.o $crash/build/menu_label.o $crash/build/title_branch.o $crash/build/title_thunk.o $crash/build/offline_observe.o $crash/build/offline_observe_asm.o $crash/build/offline_once_component.o $crash/build/offline_prearm.o $crash/build/offline_repeat.o $crash/build/trace.o $crash/build/registry_component.o $crash/build/trace_*.o $crash/build/store_route.o $crash/build/label_adapter.o $crash/build/catalog_bridge.o $crash/build/catalog_builtin.o $crash/build/catalog_snapshot.o $crash/build/catalog.o $crash/build/selection_selection.o $crash/build/selection_store.o $crash/build/selection_worker.o $crash/build/local_store_ps4.o $crash/build/manifest.o $crash/build/legacy_*.o $crash/build/steel_*.o $crash/build/process_*.o $crash/build/creative_*.o $crash/build/content_*.o $crash/build/gre_*.o $crash/build/sha.o $crash/build/map_save_service.o $crash/build/compat.o $crash/build/tabs_*.o $crash/build/nbt_lifecycle_*.o $crash/build/tools_*.o $crash/build/lucky_*.o $crash/build/shared_terrain_*.o $crash/build/craft_provider/modded-crafting-candidate.o $crash/build/lce_*.o "$LCE_SDK_OBJECT" --start-group $crash/build/tabs_libopengnm.a -lc -lkernel -lSceNet -lSceGnmDriver -lSceVideoOut -lSceImeDialog -lSceSaveData -lSceUserService -lSceLibcInternal --end-group -Map="$crash/build/super195.map" -o $crash/build/super195.elf
OO_PS4_TOOLCHAIN="$tc" "$tc/bin/linux/create-fself" -in=$crash/build/super195.elf -out=$crash/build/super195.oelf --lib=$crash/build/super195.prx --paid 0x3800000000000011
sha256sum $crash/build/super195.prx
