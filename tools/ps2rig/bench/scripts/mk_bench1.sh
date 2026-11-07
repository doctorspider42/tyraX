#!/bin/bash
# mk_r1b.sh NAME SRC MODE - R1-B physics PMU arm (architecture loop R1-B).
#   MODE=awake : SRC=P1 - the 72 lifted crates resting awake for the whole run
#                (physSleep 100000, PHYS_HOLD_AFTER 1000000 = never held),
#                scripted drive removed, car parked.
#   MODE=pile  : SRC=U5src - the user's crate field with the patch_kf scripted
#                drive (frames 250..750 full throttle) ploughing into it;
#                defaults kept (holding on, as shipped).
# Probes: kf/kf2n/kf3 (frame ring), laps + phys2 (PHYSLAP stage laps),
# patch_r1b_pmu (R1BPMU: cycles + I/D misses per stage). Optional env
# SOA=1 applies patch_r1b_soa.py (needs ARM switch, adds patch_arm.py).
# Code = the physics-release editor (PR #311 src), rig engine read-only.
set -e
A=$1; SRC=$2; MODE=$3
cd /c/tx/qp
[ -e "$A" ] && { echo "arm $A exists - pick a new name"; exit 1; }
cp -r $SRC $A && rm -f $A/bin/log.txt && rm -rf $A/obj
/f/Projects/tyra-editor/.tmp/worktrees/dod-submission/build/tyrax-editor.exe --refresh-gen C:/tx/qp/$A > regen-$A.log 2>&1
python patch_kf.py C:/tx/qp/$A
if [ "$MODE" = awake ]; then
  sed -i 's/if (g_frameStamp > 250 \&\& g_frameStamp < 750) { inThrottle = 1.0F; inBrake = 0.0F; inNos = 1; }//' $A/src/gen/game_vehicles.gen.cpp
  sed -i 's/inline constexpr int PHYS_HOLD_AFTER = 10;/inline constexpr int PHYS_HOLD_AFTER = 1000000;/' $A/inc/game_runtime.gen.hpp
  grep -c "PHYS_HOLD_AFTER = 1000000;" $A/inc/game_runtime.gen.hpp
fi
if [ -n "${SOA:-}" ]; then
  python patch_arm.py C:/tx/qp/$A
  python patch_r1b_soa.py C:/tx/qp/$A
fi
cd /f/Projects/tyra-editor/.tmp/worktrees/dod-submission
timeout 1500 powershell -NoProfile -File tools/toolchain/native-build.ps1 -Project "C:/tx/qp/$A" -Engine "${ENGINE:-C:/tx/qp/tyra}" -Cache "${ECACHE:-C:/tx/qp/engine-cache}" -Toolchain "$LOCALAPPDATA/tyra-editor/toolchain/ps2dev" > /c/tx/qp/build-$A.log 2>&1 || true
tail -1 /c/tx/qp/build-$A.log; grep -i "error" /c/tx/qp/build-$A.log | head -5 || true
ls -la /c/tx/qp/$A/bin/vehicle-playground.elf
