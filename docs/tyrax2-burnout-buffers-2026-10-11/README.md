# Burnout Buffer Metadata Recheck

Offline reanalysis of the user's six existing Burnout3 states and two GS dumps
checks output architecture, not physical timing or an equal-workload comparison.
Raw proprietary states/dumps remain outside the repo; metadata pins their hashes.

`tools/tyrax2-burnout-buffers.py` uses the pinned ZIP/CRC reader and existing
GS freeze-v9/GIF parser. All six slots have both FRAME contexts CT32, width64=10,
block2240, DTHE0. Both dumps (four VSync events each) show active DISPFB2 CT16S,
block0, width64=10. No sampled decoded PMODE/DISPFB/DISPLAY field change occurs.
Main drawing targets CT32/block2240; a640x448 CT24-textured SPRITE copy writes
CT16S/block0 with DTHE1. Other reflection/postprocess targets exist: this is not
a claim that the game uses only two allocations throughout VRAM.

This CORROBORATES the earlier emulator-captures.md architecture reference rather
than discovering a new copy-free renderer. Burnout's observed layout resembles
TyraX Hybrid2. Ordinary CT32 double is still a useful alternative diagnostic,
but is NOT Burnout's observed solution and changes VRAM/output format.

Snapshots drain consumers; dump event order has no EE wait durations. These
files cannot establish when Burnout waits, whether a copy stays within VBlank,
the complete EE schedule or physical EE/GS cost. Next debugger work should bind
actual synchronization/copy submission stops and distinguish source preparation
from consumer waits; no hardware timing follows from those pauses either.

Reproduce with the user's original private capture directories:

```powershell
python tools/tyrax2-burnout-buffers.py --state-reader docs/tyrax2-burnout-buffers-2026-10-11/state-reader-pinned.py --states C:/Users/pawel/OneDrive/Documents/PCSX2/sstates --dumps C:/Users/pawel/OneDrive/Documents/PCSX2/snaps --output F:/Projects/burnout-buffers-replay.json
```

Initial audit attempt fails on the existing GsWalker Counter API (format keys
are decoded strings, fill is a scalar); repair uses existing format/fill/draw
fields, no raw register-layout change. The successful metadata report is retained.
Reanalysis with the copied pinned state reader reproduces the complete metadata
report byte-identically, SHA256243f5ee59d5d7c80148c09d389627dc6a06c62ea17219e00cd6b9bce5c95031e.
All four paint-trial Python tools and the buffer auditor compile; git diff --check
passes. No live emulator was launched or changed for this recheck.
