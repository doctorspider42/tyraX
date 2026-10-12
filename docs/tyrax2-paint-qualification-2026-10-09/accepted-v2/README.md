# A13 V2 lean qualification build

The integrated public paint path is ready for physical pricing after six complete
emulator control trips. This archive is build/workload qualification, NOT a PS2
performance result or proof of sustained 60 Hz.

ELF `996b459f73ca13173e741c373cef57b9fd459262e92dd38ab82b3cfb40b46162`;
470 native source files agree with the staged build. Hardware trace and frame
profiling are compiled out. PaintLife diagnostics, custom override toggles and
forced LOD changes are absent; normal VU clipping and authored LOD are restored.
The paint block equals production except for declared control/counter hooks.

Modes are EE/VU/EE at noon, then EE/VU/EE at midnight. All six trips cross z=108
from z=-74.303497 with full throttle, zero steer, three vehicles, live reflection
capture, 66 road chunks and camera sweeps about +/-60 degrees. The run has
3,799 rows, including 3,079 driving frames. During driving the two VU arms have
576/561 VU draws, exactly matching their paint draws; all EE arms have zero.
Captures remain active in all arms. Nine malformed controls reject wrong paths,
missing completion/phase, frozen capture, missing cars, observer leakage and
wrong mood. The incomplete live run was explicitly rejected before completion.

Six parked screenshots are visual smoke checks, not paired GS readbacks. No
emulator timing prices are emitted. Both arms use PAINT images, so a future
same-ELF contrast excludes whole-image tax versus the flag-OFF executable.
Light per-draw counters and deferred CSV writes remain; common observer cost
is not isolated. Reverse order is supported by the generator but not built or
physically qualified here. A standalone parked-turn physical test is also pending.

The ELF and 303 input files are staged at:
`F:/Projects/tyrax2-paint-qualification-20261009-v2/physical/bin`.
Their hashes are preserved, and no editor-deploy.json exists: nothing was sent
to the wedged console. After physical recovery, use the editor-equivalent
`tools/tyrax2-deploy-ps2.ps1` with this BinDir. Revalidate any old owned client's
PID AND command before handing it to -OwnedClientId; never trust a recorded PID
without checking for reuse. Retain its logs. Analyze a complete run with:

```powershell
python tools/tyrax2-paint-qualification-analyze.py F:/Projects/tyrax2-paint-qualification-20261009-v2 --target physical
```

The physical analyzer requires deployment identity and pinned inputs. It reports
work/pacing tails and common 20-unit distance bins, not frame-index comparisons.
Partial runs fail. No automatic promotion or shutdown follows from average FPS.

Log provenance: the final analysis was rerun after stopping the owned emulator.
A12's immutable archive had gained a post-completion log tail between analysis
and shutdown. The reanalysis under host/ uses that archived final log, passes all
checks and is identical except for its log hash; no source/CSV/result changed.
The archive writer now rejects a result whose log hash differs from the final log.

Bound sources, ELF, CSV, final logs, screenshots, manifests, selected body assets
and physical pins are preserved. Remaining assets stay in the local fixture and
are identified by asset-hashes.json. PS2 still has the A06 freepad DMA Busy stall.
