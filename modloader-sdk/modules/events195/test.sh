#!/usr/bin/env bash
# events195 host tests (ASan/UBSan) + sound-id audit + PS4 compile check.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
out=${1:-$here/build}; mkdir -p "$out"
python3 -I "$here/tools/gen_sounds.py" >/dev/null 2>&1 || true
g++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror -DLCE195_HOST_TEST \
    "$here/ev195.cpp" "$here/services195.cpp" "$here/tests/test_ev195.cpp" -o "$out/test_ev195"
"$out/test_ev195"
PY=${LCE_RE_PYTHON:-python3}
if [ -f "$here/tests/audit.py" ] && [ -f /home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf ]; then
  "$PY" -I "$here/tests/audit.py"
  VENV=/home/codey/minecraft-ps4-research/private-direct-legacy-capture/venv-analysis/bin/python
  if [ -x "$VENV" ]; then "$VENV" -I "$here/tests/test_unicorn_events195.py"; else echo "SKIP Unicorn: analysis venv missing"; fi
else
  echo "SKIP RVA audit: analysis image missing"
fi
bash "$here/ps4check.sh" "$out/ps4"
