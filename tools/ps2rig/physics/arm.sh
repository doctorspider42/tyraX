#!/bin/bash
# Build one physics-rig arm: copy a source project, regenerate its game code
# with the current editor, instrument it (game/apply.py) and build it against
# the rig engine (engine/setup.py).
# usage: arm.sh <name> <source project> [optional probes...]
#   -> $RIG_ROOT/<name> (project), log in $RIG_ROOT/build-<name>.log
# env: RIG_ROOT (default /c/tx), RIG_ENGINE (default $RIG_ROOT/tyra-phys),
#      EDITOR (default <repo>/build/tyrax-editor.exe)
# Keep RIG_ROOT short: ps2link's loadelf crashes on long host: ELF paths.
# Never reuse an arm name: a stale run log fools the summaries.
set -e
N="$1"; SRC="$2"; shift 2
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RIG_ROOT="${RIG_ROOT:-/c/tx}"
RIG_ENGINE="${RIG_ENGINE:-$RIG_ROOT/tyra-phys}"
EDITOR="${EDITOR:-$REPO/build/tyrax-editor.exe}"
A="$RIG_ROOT/$N"
if [ -e "$A" ] || [ -e "$RIG_ROOT/run-$N.log" ]; then echo "arm $N already used"; exit 2; fi
[ -d "$RIG_ENGINE/engine" ] || { echo "no rig engine at $RIG_ENGINE - run engine/setup.py first"; exit 2; }
cp -r "$SRC" "$A"
rm -rf "$A/bin" && mkdir -p "$A/bin"
"$EDITOR" --refresh-gen "$(cygpath -m "$A")" > "$RIG_ROOT/regen-$N.log" 2>&1
python "$HERE/game/apply.py" "$(cygpath -m "$A")" "$@"
powershell -NoProfile -ExecutionPolicy Bypass -File "$REPO/tools/toolchain/native-build.ps1" \
  -Project "$(cygpath -m "$A")" -Engine "$(cygpath -m "$RIG_ENGINE")" \
  -Cache "$(cygpath -m "$RIG_ROOT/phys-engine-cache")" \
  -Toolchain "$LOCALAPPDATA/tyra-editor/toolchain/ps2dev" > "$RIG_ROOT/build-$N.log" 2>&1 || true
if grep -q -E "error:|Error [0-9]" "$RIG_ROOT/build-$N.log"; then grep -E "error:|Error [0-9]" "$RIG_ROOT/build-$N.log" | head; echo BUILD FAILED; exit 1; fi
ls "$A"/bin/*.elf > /dev/null 2>&1 || { tail -5 "$RIG_ROOT/build-$N.log"; echo BUILD FAILED; exit 1; }
touch "$A/bin/ps2link.run"
ls -la "$A"/bin/*.elf; echo BUILD OK
