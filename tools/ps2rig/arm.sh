#!/bin/bash
# Build one rig arm: a fresh copy of the base project and of the engine,
# instrumented by make_arm.py, built with the native toolchain.
# usage: arm.sh <name> [make_arm flags...]
#   -> $RIG_ROOT/vpn<name> (project) + $RIG_ROOT/tyra<name> (engine), log in $RIG_ROOT/build<name>.log
# env: RIG_ROOT (default /c/tx), BASE (base project dir name inside RIG_ROOT, default vpn),
#      MAKE_ARM (instrumentation script, default make_arm.py; make_engine_probe.py for any project)
# Never reuse an arm name: a stale run log fools the summaries.
set -e
N="$1"; shift
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
RIG_ROOT="${RIG_ROOT:-/c/tx}"
cd "$RIG_ROOT"
if [ -e "vpn$N" ] || [ -e "run$N.log" ]; then echo "arm $N already used"; exit 2; fi
cp -r "${BASE:-vpn}" "vpn$N"
mkdir -p "tyra$N"
WREPO="$(cygpath -w "$REPO")"; WRIG="$(cygpath -w "$RIG_ROOT")"
cmd.exe //c "robocopy $WREPO\vendor\tyra $WRIG\tyra$N /MIR /XD obj bin .git /NFL /NDL /NJH /NJS /NP" > /dev/null || true
python "$HERE/${MAKE_ARM:-make_arm.py}" "$RIG_ROOT/vpn$N" "$RIG_ROOT/tyra$N" "$@"
powershell -NoProfile -ExecutionPolicy Bypass -File "$REPO/tools/toolchain/native-build.ps1" \
  -Project "$(cygpath -m "$RIG_ROOT/vpn$N")" -Engine "$(cygpath -m "$RIG_ROOT/tyra$N")" \
  -Cache "$LOCALAPPDATA/tyra-editor/native-build/cld-$N" \
  -Toolchain "$LOCALAPPDATA/tyra-editor/toolchain/ps2dev" > "$RIG_ROOT/build$N.log" 2>&1 || true
if grep -q -E "error:|Error [0-9]" "$RIG_ROOT/build$N.log"; then grep -E "error:|Error [0-9]" "$RIG_ROOT/build$N.log" | head; echo BUILD FAILED; exit 1; fi
touch "$RIG_ROOT/vpn$N/bin/ps2link.run"
ls -la "$RIG_ROOT/vpn$N"/bin/*.elf; echo BUILD OK
