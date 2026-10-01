# TyraX2 renderer foundations

TyraX2 is the staged migration toward preparing frame N on the EE while VU1/GS
execute frame N-1, with explicit ownership of every submitted resource.
The current work establishes validation and a reproducible seated Motor District
fixture. Frame arenas and cross-frame execution are not implemented yet.

## Acceptance order

1. Archive current physical-PS2 day/night seated-start controls. Preserve the
   authored camera, video mode and quality; turn off host polling. Record mean,
   median, p95, over-budget frames and complete frame period separately.
2. Validate complete DMA/VIF chains before submission, independently of timing.
3. Introduce frame-owned packets/copy pools while retaining current submission.
   Establish RAM ceilings, reclaim fences and a safe overflow fallback first.
4. Give uploads, GS state and auxiliary render targets an explicit ordered
   representation, covering static/dynamic pipelines, HUD and screen effects.
5. Enable N/N-1 execution experimentally. Compare input latency, RAM/VRAM,
   pixels, frame tails and streaming/scene transitions against the controls.

## Ownership seams for the arena stage

| Resource | Current protection | Required frame-arena contract |
| --- | --- | --- |
| StaPip packets | `packetSequence[context]` is waited before rotating into a reused buffer in `sendPacket` | Immutable packet slices until their final DMA reader completes; retain current send ordering first |
| QBuffer copied vertices/ST/colour/normals | `stapip_qbuffer.cpp` rotates one pool side per packet buffer | Allocate slices from the same submission owner as the packet; reclaim packet and payload together |
| MVP/light/colour uniforms | Queue mode copies transient values inline | Snapshot at recording time; never borrow mutable renderer state |
| Retained/baked command arenas | Content/version keys plus delayed destruction | Pin referenced entries to real completion, including replacement and scene unload |
| VIF1 HUD chains | `chainSeq[chainSide]` protects two alternating buffers | Keep DIRECT data alive and ordered behind the relevant 3D passes |
| Texture allocations/render targets | Mid-frame fences and GS state caches | Pin VRAM until the final GS reader, order upload/eviction and raster redirects explicitly |

DMA completion permits reuse of an EE source buffer; it is not proof that GS
has stopped sampling a VRAM texture. Conversely, counting two frame ticks is
not a completion fence. The arena stage must keep those lifetimes separate.
Do not broaden an allocation's lifetime merely by increasing queue depth:
depth 8 already regressed, and copy-pool memory scales with that depth.

The previous [architecture investigation](ee-submission-rearchitecture.md#a-frame-pipelined-engine-tyrax2---considered-2026-09-24-not-now)
records the hazards. Its older sweep's overlap estimate does not quantify the
current seated start. Fresh GPU-hold results are diagnostic segment measurements,
not ordinary frame times or an exact prediction of pipeline gain.

## Pre-submit chain guard

`TYRA_VIF1_CHAIN_CHECK=1` enables the guard in `Vif1Queue::submit`. Both StaPip
and the VIF1 HUD supply their complete packet size, including the terminator.
The default is 0: ordinary builds do not scan submitted payloads.

The bounded decoder accepts linear TTE chains containing CNT, REF, REFE and END.
It rejects missing/early terminators, inline overruns, unsafe reference ranges,
unknown VIF commands and unfinished command payloads before starting the chain.
Unexpected DMA tag IRQ/PCE controls are rejected too: TIE is enabled
on this channel, so a stray IRQ bit could terminate a transfer early.
Reserved bits 16–25 are ignored, matching the hardware/SDK contract:
`packet2_chain_set_dma_tag` assigns named bitfields without clearing padding.
Requiring zero padding falsely rejected a real night-scene packet during the
first control-bit guard boot; the host harness includes that regression case.
Traversal is bounded to 65,536 packet quadwords and 1,048,576 payload quadwords
(16 MiB including repeated REF reads), so malformed chains cannot expand into
unbounded diagnostic work.
It handles UNPACK widths, NUM=0, STCYCL fill/skip counts, IRQ command bits, MPG,
DIRECT/DIRECTHL and fixed-size register payloads. Each chain starts decoding at
CL=WL=1; current UNPACK writers establish STCYCL explicitly. A future writer
depending on inherited non-default cycle state needs an explicit initial-state
contract before using this validator.

NEXT/CALL/RET, REFS stall control and scratchpad references are deliberately
unsupported. Adding them requires bounded traversal and explicit resource
ownership; a validator must not follow arbitrary addresses in a broken chain.
The EE resolver permits only aligned ordinary RAM spans below 32 MiB. This is
a range check, **not an allocation/lifetime proof**. The caller still guarantees
the packet's readable extent; frame arenas must eventually establish ownership.
The guard does not validate GIF semantics or GS image correctness, and cannot
detect later writes, stale cache data or missing inter-path barriers.

Accepted-chain counts appear as `VIFCHECK accepted`. Rejection reports an error
enum, packet tag offset, last VIF command and pending word count, then parks
the EE thread without submitting the rejected chain. This is a correctness arm,
never a performance arm, even when all chains pass.

Run the portable malformed-input harness from the repository root:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tools/verify-vif1-chain.cpp -o /tmp/vifcheck
/tmp/vifcheck
```

On Windows use the same compiler command with an absolute `.exe` output outside
the checkout, then run that executable. The harness includes truncated and
out-of-range chains, real command payload layouts and 10,000 malformed streams.

## Reproducible physical-console fixtures

```sh
python tools/tyrax2-fixture.py /absolute/scratch/day --pose day --mode timing
python tools/tyrax2-fixture.py /absolute/scratch/night --pose night --mode timing
```

The script copies the current example and engine outside the repository, leaves
the seated start intact, selects the authored district-night default and disables
Remote Pad, Live Debugger, Live Link/Logic, Time Machine, input recording and the
memory HUD. It records settings, source asset/engine hashes and source revision.
It refuses existing destinations. Run `--refresh-gen` on the copied `game`, then
build it through `tools/toolchain/native-build.ps1`/`.sh` using the copied `tyra`
and a dedicated cache. Use the usual [resident-IOP marker and deployment rules](ps2link-setup.md).

`--mode plain` is the uninstrumented transparency control; `timing` selects
`TYRA_FRAME_PROFILE=2`; `check` selects the chain guard; `hold` enables the
existing GPU-hold probe at queue depth 80. Hold deliberately serializes segments,
can alter memory pressure and omits some intermediate GS tails; its frame work
must never be quoted as shipped performance. A held-frame overflow invalidates
its measurement. Keep correctness, hold and timing captures separate.

Archive stdout, final ELF/resource hashes and the refreshed manifest after each
build. Compare the same warmed frame range and camera, with repeat boots; source
revision alone does not capture uncommitted changes. NTSC and PAL have different
budgets (16.667 and 20 ms); `FRAMETIME over20` is not an NTSC acceptance count.
No gain is claimed until a production candidate has passed these gates.

For an exact 512-frame warmed window from the current timing producer:

```sh
python tools/tyrax2-timing.py /scratch/day-boot1.log --first 1100 --frames 512 --hz 60
```

The parser joins ps2client's inserted tty-packet newlines, including splits
inside hexadecimal words, but preserves original token spaces. It requires
complete 64-value FTRAW records, rejects duplicate frames and refuses missing
samples in the requested window. The raw buffer dumps at a 50-frame summary
boundary after filling 512 slots; subsequent blocks start at frame 550, 1100,
1650, etc. Do not treat the gaps between blocks as measured data. Summaries
use nearest-rank p95 and the authored Hz budget. Full period and missed-field
counts still come from FRAMETIME, not from FTRAW active work.

## Initial physical-PS2 controls (2026-10-01)

The current seated garage start was booted twice per pose, with original NTSC
progressive/hybrid video settings, ordinary quality and host polling off.
Both boots use the same ELF within each pose. The warmed raw window is
frames 1100–1611 (512 frames):

| Pose | Mean work, boot 1 / 2 | p95, boot 1 / 2 | Work above 16.667 ms, boot 1 / 2 |
| --- | --- | --- | --- |
| Day | 15.707 / 15.721 ms | 16.389 / 16.304 ms | 8 / 7 of 512 |
| Night | 19.918 / 19.912 ms | 20.525 / 20.519 ms | 512 / 512 |

The matching FRAMETIME windows put the night period at 33.37 ms; the day
also misses many NTSC fields despite most render-work samples fitting 16.667 ms.
Simulation/input (`pre`), presentation and vsync are part of the full period.
These are baseline controls, not an optimization result. No plain-build
transparency or pixel-equivalence acceptance is claimed yet.

[The machine-readable record](tyrax2-baseline-2026-10-01.json) includes ELF hashes,
per-window periods and repeats; complete stdout and scratch builds remain in
the recorded local artifact directory. A later candidate must preserve the
same pose and include full-frame and visual gates as well as these work samples.

The first hardware guard build accepted 110,592 chains without rejection.
Subsequent payload-budget/control checks passed 32 host cases and 10,000
malformed streams on Windows and Linux ASan/UBSan. After correcting the
reserved-bit false rejection, the final guard reached the physical-PS2 night
scene and accepted at least 94,208 chains without rejection; the
archived stdout snapshot and ELF hashes are in the record. The GPU-hold launch stopped before
gameplay with `freepad: DMA Busy`, twice including after a user-confirmed
physical power cycle. It produced no usable GPU measurement. The pad initially
reported ready; no root cause is established. Do not infer pipeline performance
or classify this as a render-chain defect from the tty symptom alone.
