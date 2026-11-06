#!/bin/bash
# Headless PCSX2 under Xvfb inside WSL - counts and correctness only, never timings.
# Run from Windows as: wsl -e bash /mnt/<repo>/tools/ps2rig/emu.sh <bin dir> <out dir> [seconds] [display]
# (paths as WSL paths). EE console goes to <out>/emulator.log.
# env: PCSX2_APPRUN (extracted PCSX2 AppImage AppRun; required)
BIN="$1"; OUT="$2"; SECS="${3:-240}"; DISP="${4:-87}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
: "${PCSX2_APPRUN:?set PCSX2_APPRUN to the extracted PCSX2 AppRun}"
mkdir -p "$OUT/profile/PCSX2/inis"
sed -e 's/^PINESlot\s*=.*$/PINESlot = 29187/' -e 's/^\(StartPaused\|ShowOnStartup\)\s*=.*$/\1 = false/' \
  "$REPO/docs/tyrax2-loop-balance-2026-10-06/emulator/PCSX2.ini" > "$OUT/profile/PCSX2/inis/PCSX2.ini"
rm -f "$BIN/ps2link.run"
Xvfb ":$DISP" -screen 0 1280x800x24 > "$OUT/xvfb.log" 2>&1 &
XP=$!
sleep 1
cd "$BIN"
ELF=$(ls *.elf | head -1)
DISPLAY=":$DISP" QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 timeout "$SECS" \
  "$PCSX2_APPRUN" -datapath "$OUT/profile" -logfile "$OUT/emulator.log" -batch -nogui -elf "$BIN/$ELF" \
  > "$OUT/stdout.log" 2>&1
kill $XP 2>/dev/null
echo EMUDONE
