#!/usr/bin/env bash
set -euo pipefail
sdk=$(cd "$(dirname "$0")/.." && pwd)
root=$(cd "${1:-"$sdk/../r72-session-candidate"}" && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
flags=(-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
mkdir -p "$out/include/orbis"
cat > "$out/include/orbis/libkernel.h" <<'EOF'
#pragma once
#include <stdint.h>
extern "C" uint64_t sceKernelGetProcessTime(void);
EOF

python3 "$sdk/tools/lce_modpack.py" build "$sdk/examples/viewport-tint" "$out/packages"
"${CC:-clang}" -std=c11 "${flags[@]}" -c "$sdk/loader/sha256.c" -o "$out/sha256.o"
"${CXX:-clang++}" -std=c++17 "${flags[@]}" \
    "$sdk/tests/test_viewport_tint_config.cpp" "$sdk/tests/mock_backend.cpp" \
    "$sdk/loader/"{package,ledger,loader,ext}.cpp "$out/sha256.o" -o "$out/config_test"
"$out/config_test" "$out/packages/viewtint/mod.lcm" "$out/packages/viewtint/config"
"${CXX:-clang++}" -std=c++17 "${flags[@]}" \
    -I"$out/include" -I"$sdk/include" -I"$root/loader/debug_tabs_viewport_195" \
    -isystem "$root/shader-re/opengnm/include" \
    "$sdk/tests/test_viewport_tint_render.cpp" -o "$out/render_test"
"$out/render_test"

gnm=(-isystem "$root/shader-re/opengnm/include")
for component in viewport player_view; do
    "${CC:-clang}" -std=c11 "${flags[@]}" "${gnm[@]}" \
        -c "$root/loader/debug_tabs_viewport_195/$component.c" -o "$out/$component.o"
done
"${CXX:-clang++}" -std=c++17 "${flags[@]}" "${gnm[@]}" \
    -Wno-unused-function -Wno-missing-field-initializers \
    "$root/loader/debug_pad_sessions_195/adapter.cpp" "$root/loader/debug_tabs_view_diagnostics_195/bindings.cpp" \
    "$root/loader/f3_server_195/test_watch_view.cpp" "$out/viewport.o" "$out/player_view.o" -o "$out/watch_test"
"$out/watch_test"
