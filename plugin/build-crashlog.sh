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
plugin_build="r8-anticheat-$(date +%Y%m%d%H%M%S)"
echo "$plugin_build" > $crash/build/plugin_build.txt
for name in session_runtime session_coordinator platform_hook_install join_native_ops join_list join_binding join_adapter join_vtable session_query_dispatch external_players external_players_native external_session_state native_start_transition player_channel player_native_factory player_vtable sqr_native_factory socket_native_factory native_identity native_keyboard account_flow account_join_route login_gui orbis_scheduler; do
 clang++ "${flags[@]}" "${cxx[@]}" -I"$transport" -c "$transport/$name.cpp" -o "$crash/build/lce_$name.o"
done
for name in orbis_socket orbis_binding orbis_session session_transport connection framing send_queue native_bridge player_send_hook auth_client auth_https orbis_auth gateway_handshake; do
 clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I"$transport" -c "$transport/$name.c" -o "$crash/build/lce_$name.o"
done
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -c $crash/vtable_trace.c -o $crash/build/lce_vtable_trace.o
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -c $crash/goldhen_ids.c -o $crash/build/lce_goldhen_ids.o
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I"$transport" -c "$transport/skin_registry.c" -o $crash/build/lce_skin_registry.o
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I"$transport" -c "$transport/tex_registry.c" -o $crash/build/lce_tex_registry.o
# Java texture-pack runtime: rebuilt here (extra slots for downloaded packs, see dynamic.hpp); replaces the prebuilt gre_java_* objects.
for name in runtime component source_loader staged_verify adapter bind kernel_file_ops; do
 clang++ "${flags[@]}" "${cxx[@]}" -c "$root/loader/java_texture_invictus_runtime_195/$name.cpp" -o "$crash/build/gre_java_$name.o"
done
# mods_content.cpp addresses mc195_mods::Model fields by the offsets used by the prebuilt object; refuse to build if it changed.
model_dis=$(objdump -d --no-show-raw-insn "$crash/build/tabs_modmenu_model.o")
for pattern in 'cmp    0x68(%r12),%ecx' 'cmpb   $0x0,0x1327c(%rdi)' 'mov    %rax,0x13288(%r12)'; do
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
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -I"$mono" -c $crash/plugin_update.cpp -o "$crash/build/lce_plugin_update_glue.o"
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -c $crash/mods_content.cpp -o "$crash/build/lce_mods_content.o"
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -c $crash/dyn_skins.cpp -o $crash/build/lce_dyn_skins.o
clang++ "${flags[@]}" "${cxx[@]}" -Wno-missing-field-initializers -I"$transport" -I"$r72" -c $crash/dyn_textures.cpp -o $crash/build/lce_dyn_textures.o
clang++ "${flags[@]}" "${cxx[@]}" -I$crash -c $crash/remote_actions.cpp -o $crash/build/lce_remote_actions.o
clang++ "${flags[@]}" "${cxx[@]}" -I$crash -c $crash/remote_nbt.cpp -o $crash/build/lce_remote_nbt.o
clang++ "${flags[@]}" "${cxx[@]}" -c $crash/remote_input.cpp -o $crash/build/lce_remote_input.o
clang++ "${flags[@]}" "${cxx[@]}" -Wno-unused-parameter -c $crash/map_import_removed.cpp -o $crash/build/lce_map_import_removed.o
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -DLCE_PLUGIN_BUILD="\"$plugin_build\"" -I"$transport" -c "$transport/client_report.c" -o "$crash/build/lce_client_report.o"
clang "${flags[@]}" -std=c11 -Wall -Wextra -Werror -I$crash -c $crash/logger.crashdiag.c -o $crash/build/frontend_crash_logger.o
clang++ "${flags[@]}" "${cxx[@]}" -I"$debug" -I"$transport" -DMC_METADATA_SNAPSHOT -DMC_COOPERATIVE_TABS -DMC_NATIVE_DEBUG_DIAGNOSTICS -Dplugin_load=mc195_debug_load -Dplugin_unload=mc195_debug_unload -Dmodule_start=mc195_debug_module_start -Dmodule_stop=mc195_debug_module_stop -Dg_pluginName=mc195_debug_name -Dg_pluginDesc=mc195_debug_desc -Dg_pluginAuth=mc195_debug_auth -Dg_pluginVersion=mc195_debug_version -c $crash/debug_main.item_diag.cpp -o $crash/build/debug_main.o
[ -f $crash/tabs_29.orig.keep ] || cp $crash/build/tabs_29.o $crash/tabs_29.orig.keep
objcopy --add-symbol lce_body_submit_sheep=.text._ZN11tab_actions10Dispatcher12submit_sheepEjj:0,function,global --add-symbol lce_body_submit_animal_variant=.text._ZN11tab_actions10Dispatcher21submit_animal_variantEjj:0,function,global --add-symbol lce_body_available_sheep=.text._ZNK11tab_actions10Dispatcher15available_sheepEjj:0,function,global --add-symbol lce_body_available_animal_variant=.text._ZNK11tab_actions10Dispatcher24available_animal_variantEjj:0,function,global --redefine-sym _ZN11tab_actions10Dispatcher12submit_sheepEjj=lce_hook_submit_sheep --redefine-sym _ZN11tab_actions10Dispatcher21submit_animal_variantEjj=lce_hook_submit_animal_variant --redefine-sym _ZNK11tab_actions10Dispatcher15available_sheepEjj=lce_hook_available_sheep --redefine-sym _ZNK11tab_actions10Dispatcher24available_animal_variantEjj=lce_hook_available_animal_variant --add-symbol lce_body_available=.text._ZNK11tab_actions10Dispatcher9availableEjj:0,function,global --add-symbol lce_body_submit=.text._ZN11tab_actions10Dispatcher6submitEjj:0,function,global --redefine-sym _ZNK11tab_actions10Dispatcher9availableEjj=lce_hook_available --redefine-sym _ZN11tab_actions10Dispatcher6submitEjj=lce_hook_submit --add-symbol lce_body_submit_item_quantity=.text._ZN11tab_actions10Dispatcher20submit_item_quantityEjjj:0,function,global --add-symbol lce_body_submit_mob=.text._ZN11tab_actions10Dispatcher10submit_mobEjj:0,function,global --redefine-sym _ZN11tab_actions10Dispatcher20submit_item_quantityEjjj=lce_hook_submit_item_quantity --redefine-sym _ZN11tab_actions10Dispatcher10submit_mobEjj=lce_hook_submit_mob $crash/tabs_29.orig.keep $crash/build/tabs_29.o
ld.lld --wrap=_Z13mc195_f3_bindP21mc195_natural_process -m elf_x86_64 -pie --wrap=mc195_gre_set_skin_enabled --wrap=mc195_selection_worker_startup_choice --wrap=_ZN9tab_modal10Navigation5inputEiNS_3KeyEbb --wrap=mc195_debug_action_available --wrap=_ZN10mc195_mods5Model4openEv --wrap=_ZN10mc195_mods5Model7refreshEv --wrap=_ZN10mc195_mods5Model5inputEN9tab_modal3KeyEb --wrap=_ZN10mc195_mods5Model4drawERKN11menu_render10DirectSinkEiiiihh --wrap=_ZN9tab_modal10Navigation30configure_visual_world_actionsERKN11tab_actions14WeatherBackendE --version-script "$debug/exports.map" --script "$tc/link.x" -e _init --eh-frame-hdr -L "$tc/lib" $crash/build/crtprx.o $crash/build/super.o $crash/build/frontend_diagnostics.o $crash/build/frontend_crash_logger.o $crash/build/debug_main.o $crash/build/native_pad.o $crash/build/weather.o $crash/build/runtime.o $crash/build/action_trace.o $crash/build/runtime_attest.o $crash/build/shared_tick_195.o $crash/build/dialog_gate_195.o $crash/build/title_detour.o $crash/build/ps4_attest.o $crash/build/store_bridge.o $crash/build/probe.o $crash/build/menu_action.o $crash/build/menu_label.o $crash/build/title_branch.o $crash/build/title_thunk.o $crash/build/offline_observe.o $crash/build/offline_observe_asm.o $crash/build/offline_once_component.o $crash/build/offline_prearm.o $crash/build/offline_repeat.o $crash/build/trace.o $crash/build/registry_component.o $crash/build/trace_*.o $crash/build/store_route.o $crash/build/label_adapter.o $crash/build/catalog_bridge.o $crash/build/catalog_builtin.o $crash/build/catalog_snapshot.o $crash/build/catalog.o $crash/build/selection_selection.o $crash/build/selection_store.o $crash/build/selection_worker.o $crash/build/local_store_ps4.o $crash/build/manifest.o $crash/build/legacy_*.o $crash/build/steel_*.o $crash/build/process_*.o $crash/build/creative_*.o $crash/build/content_*.o $crash/build/gre_*.o $crash/build/sha.o $crash/build/map_save_service.o $crash/build/compat.o $crash/build/tabs_*.o $crash/build/nbt_lifecycle_*.o $crash/build/tools_*.o $crash/build/lucky_*.o $crash/build/shared_terrain_*.o $crash/build/craft_provider/modded-crafting-candidate.o $crash/build/lce_*.o --start-group $crash/build/tabs_libopengnm.a -lc -lkernel -lSceNet -lSceGnmDriver -lSceVideoOut -lSceImeDialog -lSceSaveData -lSceUserService -lSceLibcInternal --end-group -o $crash/build/super195.elf
OO_PS4_TOOLCHAIN="$tc" "$tc/bin/linux/create-fself" -in=$crash/build/super195.elf -out=$crash/build/super195.oelf --lib=$crash/build/super195.prx --paid 0x3800000000000011
sha256sum $crash/build/super195.prx
