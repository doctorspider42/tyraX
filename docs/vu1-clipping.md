# VU1 clipping and the guard band

Clipping on the PlayStation 2 is software. The GS rasterises whatever the GIF
hands it and offers exactly one hardware facility for geometry that leaves the
picture: a 2D **scissor** rectangle. Everything else — deciding that a triangle
crosses the near plane, cutting it, interpolating the new vertices — is work
somebody has to do, and in TyraX that somebody is VU1.

This page is about how the static pipeline decides which of its three routes a
piece of geometry takes, why most edge-of-screen geometry needs no clipping at
all, and what the routing costs when it gets that decision wrong.

## The three routes

`StaPipCore::render` splits a bag into **packages** — as many vertices as fit in
half a VU1 double buffer — classifies each against the camera frustum, and sends
it down one of three paths:

| classification | route | program family | cost |
|---|---|---|---|
| `OUTSIDE_FRUSTUM` | dropped on the EE | — | nothing |
| `IN_FRUSTUM` | **cull** | `stapip_cull_*` | DMA by reference, one kick |
| `PARTIALLY_IN_FRUSTUM` | **clip** | `stapip_clip_*` | split into thirds, memcpy per stream, one kick each |

The cull programs transform, light and project, and mark a triangle that fails
their `clipw` judgement as **not drawn** by setting its ADC bit. They never cut
anything. The clip programs run a full Sutherland–Hodgman cut against up to six
planes on VU1, then fan-triangulate whatever polygon comes out and patch the
prim giftag's NLOOP with the vertex count they actually produced.

The clip route is the expensive one, and not mainly because of the cut. A
crossing package is split into **thirds** so the scratch polygon fits, which
triples the number of DMA chains and VU1 kicks; each third is filled with
`StaPipQBuffer::fillByCopy1By3`, a `memcpy` of the positions, STs, colours and
normals, where the cull route hands VU1 a **pointer** and lets the DMA
controller read the vertex array in place.

## The guard band

The frustum a package is *classified* against and the planes VU1 *cuts* against
are deliberately not the same thing.

The projection divides by `RendererSettings::projectionScale`, which is **4096**,
and `StaPipVU1Program` scales the result by **2048** before `ftoi4`. So clip-space
`x/w = ±1` maps onto the GS raster window's full 0…4096 range, while the visible
picture sits at `width / 4096` of `w` — **0.125** across a 512-pixel raster and
0.109 down a 448-line one. The screen is a small box in the middle of the
coordinate space the GS can actually address.

`VU1_CLIP_XY_BAND` (0.9) is the X/Y plane the clip programs cut to, and it is
about **seven times** the screen's half extent. In pixels: a triangle may hang
roughly **1590 px past either edge** of a 512×448 picture before anything is cut,
and the GS scissor — which acts during rasterisation, so unseen pixels cost no
fill — crops the raster instead. That is guard-band clipping, and it is why the
band is not 1.0: a vertex at exactly `|x| = w` scales to GS coordinate 4096.0,
one past the 12.4 XYZ2 maximum, wraps to the far side of the raster window and
smears a wedge across the screen.

Near and far are different. The scissor cannot reconstruct the intersection of
an edge with the near plane — it only discards the pixels a bad projection
produced, and a vertex with `w ≈ 0` projects to infinity or flips sign and hands
the GS a monstrous inverted triangle. **Near-plane crossings still need a real
cut**, which is what the Sutherland–Hodgman loop is for, and the plane table
puts near and far first for the reason the GDC 2002 *PlayStation 2 Clipping*
talk gives: the X/Y judgement has valid-looking regions behind the camera, so Z
has to be resolved before them.

## Routing: a package that leaves the screen usually needs no clipping

Because the two frustums differ, `PARTIALLY_IN_FRUSTUM` does **not** mean "needs
clipping". It means "leaves the screen". A package straddling the screen border
but sitting comfortably inside the guard band crosses no VU clip plane at all,
and the plane loop would run with nothing active.

`StaPipBagPackager::checkFrustum` answers both questions in one pass over the
package's AABB. `CoreBBox::activePlaneMaskAABB` sets a bit when the box crosses
**or lies outside** a plane, so an all-clear means the box is inside every one of
them, and the packager sets `StaPipBagPackage::guardBandOnly`. `StaPipCore` then
routes such a package to **cull**, whole and by pointer — no split, no copy, no
clipper. `StaPipCore::isGuardBandOnly` is the predicate; `renderPkgs` and
`renderSubpkgs` are the two sites.

Three things make that safe, and each is load-bearing:

- **`w > 0` is implied.** For a negative `w` the two side half-spaces
  (`x ≤ 0.9w` and `x ≥ −0.9w`) are contradictory, so a box behind the camera
  always sets at least one bit and can never come out clear.
- **The mask is tested over EIGHT planes, not six.** The cull programs'
  `PerformTyraFogClipCheck` masks `fcand` with `0x3FFFF` — all six clip flags of
  all three vertices, `z` against `±w` included — while the guard band's own
  near constant is deliberately looser (`PlanesClipAlgorithm::clipMargin`). That
  leaves a thin shell in front of the near plane where the clipper draws a
  triangle the cull program would ADC away: a hole at point-blank range on a
  dense surface the camera has walked into. `StaPipCore::computeClipObjectSpacePlanes`
  therefore builds the six VU planes **plus** the exact near (`z ≤ w`) and far
  (`z ≥ −w`) pair. Entries 6 and 7 exist on the EE only — they are never
  uploaded, and `clipPlaneMask` is masked back to six bits before VU1 sees it.
- **`guardBandOnly` is only ever set with VU1 clipping on.** With the legacy EE
  clipper the packager has no clip planes to transform and fills `clipPlaneMask`
  with the view-plane crossing mask for telemetry instead, which is never zero
  for a partial package.

The trade is real and it is worth stating: the whole package is now submitted
where the split would have dropped some of its thirds as `OUTSIDE_FRUSTUM`. Arm
B below submits about **7 % more triangles in 54 % fewer packages** — the cost on
this pipeline is per package (a DMA chain, a kick, a copy), not per triangle, and
the extra triangles land off-screen where the scissor discards them during
rasterisation.

## What it measured

Fixture: `examples/large-terrain` (2048×2048 terrain, 1181 scattered props, the
`fpp` template) copied to a short path, PCSX2 2.3.x **software renderer**, PAL
progressive, debug profile, Live Link / Live Debugger / Live Logic / Remote Pad /
Time Machine **off**, camera driven from a **frame index** in a global script
(four legs of 250 frames: a full 360° pan, a 240-unit dolly, a half orbit, the
return) so frame *k* of one run shows exactly what frame *k* of the other does.
The arms differ by **one line** — `isGuardBandOnly` returning the flag or
returning false — and carry the identical instrument.

Per-frame `work` (`FRAMETIME`, the frame-timing rig — see
[profiling.md](profiling.md)), paired by frame index, first 150 frames discarded,
**n = 2922 frames**:

| | mean | median | p95 | max |
|---|---|---|---|---|
| routed to the clipper (before) | 6.887 ms | 6.639 | 9.108 | 15.866 |
| guard-band routing (after) | **4.670 ms** | 4.428 | 6.275 | 14.108 |

**d = −2.217 ms, 95 % CI [−2.258, −2.175], a 1.475× speedup**, with 2864 of 2922
frames faster. Where it went, from the `FTCLIP` line (mean per 50-frame window,
64 common windows):

| | before | after |
|---|---|---|
| packages sent to the clipper | 11 164 | **2 127** |
| triangles clipped | 68 456 | **13 264** |
| packages taking cull via the guard band | 0 | 2 696 |
| qbuffer flushes | 1 287 | **756** |
| total packages drawn | 15 731 | 7 193 |
| total triangles drawn | 153 752 | 164 906 |

Five sixths of the clipper's load was geometry that needed no clipping.

**The picture is unchanged, and the control is what proves it.** Four parked
poses were captured through the game's own self-screenshot in two boots of each
arm. Two boots of the *same* build differ (this fixture streams terrain chunks,
so a capture lands on a slightly different LOD state): A vs A2 = 2222 differing
pixels of 802 816, B vs B2 = 1268. Across the arms the numbers are the same
size — and **A2 vs B2 is byte-identical over all four poses**. Three distinct
images from four runs, and the arm is not what sorts them. Had the routing
changed what is drawn, no cross-arm pair could have come back at zero.

Caveats, stated rather than implied: one fixture, one machine, PCSX2. The
measurement is of **EE work** (packaging, copies, DMA chains, VU1 kicks), which
the emulator reports honestly; nothing here is a claim about GS fill, which PCSX2
under-reports by 76× (see [profiling.md](profiling.md), "The calibration gate").

## The FTCLIP line

The routing counters are `StaPipTelemetry`, opt-in via
`StaPipCore::setTelemetryEnabled`. The generated game turns them on and prints
them **only** under `TYRA_FRAME_PROFILE` (`inc/debug/frame_profile.hpp`, default
0), once a second beside `FRAMETIME`:

```
FTCLIP f=1200 cull=3451/110733 clip=1501/9409 guard=2094/68672 out=10416 flush=519 vuwait=0.01
```

`cull` / `clip` / `guard` are `packages/triangles` over the window, `guard` being
the subset of `cull` that took that route because of the guard band — what the
clipper no longer sees. `out` is packages dropped on the EE, `flush` the qbuffer
flushes, `vuwait` the milliseconds the EE spent waiting on VIF1/VU1.

Read it as the *why* behind a `work` figure, not as a metric of its own: `clip`
falling while `guard` rises by the same amount is what a routing change looks
like, and the totals must stay comparable between two arms of an A/B, or the
arms are not looking at the same scene.

## What the guard band does not buy

From the same GDC talk, evaluated against this pipeline and **not** adopted:

- **Rejecting degenerate triangles before the cut.** Real meshes produce few of
  them and micro memory is the scarce resource here — the clip program set sits
  at 1676 of 2042 slots.
- **Per-triangle plane masks.** The talk reuses each vertex's clip codes to pick
  the planes to cut against. Here the mask is already per *package*, and the
  `clipw` flags cannot be reused for it: they describe `±w`, while the planes
  actually cut are the 0.9 band and a near with a margin.
- **Four-way unrolling to hide the 7-cycle divide.** The cull and as_is loops
  already process a whole triangle per iteration with three independent chains,
  which is what VCL needs to interleave the divides.

One thing from it that TyraX *did* pay for the hard way is worth repeating: the
talk's closing slide says that when clipped and unclipped geometry take different
transform paths, the seam tears, and *"the only solution is to unify the maths"*.
That is exactly the coplanar-package defect recorded in the `tyra-engine-dev`
skill, and it is an argument for keeping VU1 clipping the default — both routes
then run the same MVP multiply and the same perspective divide on the same unit.

## Real hardware: the slot-pool race (and what it is not)

On 2026-09-10/11 the Aster showcase ran on a real PS2 and a lamp's corona
(6 vertices, textured, additive, `fullClipChecks`) came out wrong in some
frames: a bright wedge or a dark hairline from the lamp to the top-left corner
of the screen, once in a while the quad missing. PCSX2 never showed it. Two
days of measurement went into the VU1 clipper and its assembler before the
fixture that finally worked was built, so the conclusion and the method are
recorded here in full.

**The fixture.** An input replay (`bin/replay.in`, pins `dt`) plus a script that
parks the camera from frame 1400 on, so the same pose renders for the rest of
the run; then N free-running `--capture-frame`s scored against one reference
of that pose with the HUD rows masked. A build's *failure rate* over 24-30
frames is the number - a single frame says nothing, because the corruption is
intermittent, and two builds photographed once each can differ for no reason
at all.

**What the rates said.** Sony's `vcl` build: 4 of 24 frames wrong. openvcl's
production flag set: 2 of 2, 3 of 9, 19 of 30 in successive arms. openvcl
without `--fmac-interlock`: 1 of 2. Every openvcl variant that changed
`stapip_clip_c`'s schedule changed the rate - and the corona is not even drawn
by `clip_c` (it is a textured single-colour bag, `clip_tc`). The EE clipper
(`"clipping": "precise"`) was worse than any of them: giant textured slabs
across the whole screen, different in every frame, at 18 FPS. PCSX2 rendered
both modes pixel-stable. So: a hardware timing race whose window the
microprogram's length merely moves, not a defect in any microprogram or in
the assembler. The wrong pictures being a small discrete set (the same 1286-
or 1388-pixel sliver again and again) said the same thing - a handful of
interleavings, not random garbage.

**Bisecting the window on the console** (each arm = one deploy, 24-30 frames):

| arm | result |
|---|---|
| a GS FINISH handshake after every bag (`sync.align3D()`) | 24/24 clean, **-4 FPS** |
| a VIF1 `FLUSH` packet after every bag, the EE waiting for it | 24/24 clean, -4 FPS |
| `FLUSHE` at the head of the per-mesh uniform chain (kept - see below) | no change |
| `FLUSH` at the head of the uniform chain (VIF stalls, EE runs on) | no change |
| a VIF1 wait before the beams rewrite their billboard arrays | no change |

Only the arms where the **EE** waited fixed it, and a VIF-side barrier that let
the EE run on did not. So the EE was overwriting something the previous bag's
DMA was still reading, and it was not the bag's own arrays. It was the
renderer's: `StaPipQBuffer::fillByCopyMax` / `fillByCopy1By2` merge small
in-frustum packages by **copying** them into a per-slot pool (in both clipping
modes; `fillByCopy1By3` and the EE clipper's `writeChunk` copy too), the
packet's REF tags point at that pool, and `flushBuffers()` resets the slot
indices the moment the packet is *sent*. The next bag - the next of thirteen
coronas rendered back to back - copies into slot 0 while slot 0 is still being
read: the DMA picks up a vertex of the *next* lamp, which is why every sliver
ended at a different lamp than the one it started from. The victim is always
a small bag followed quickly by another small bag; the window grows with
whatever keeps the previous transfer alive, and a big translucent fill near
the camera keeps the GS, hence the GIF, hence the kick, hence the VIF1 chain
behind it, busy for a long time. The EE clipper copies *everything* into the
slots, so there it broke everything.

**The fix** (`stapip_qbuffer.cpp`, `stapip_qbuffer_renderer.cpp`): the pool has
two sides, and `sendPacket()` flips the side together with the packet double
buffer it already had. That send waits for the previous DMA before it sends,
so the side being written is always the one whose transfer finished - the
guarantee the packet buffers had, extended to the data they reference. It is
the same guarantee the 24/24 EE-wait probe gave, without the EE ever idling
(that probe cost 4 FPS, which is why it is not the fix). The EE clipper needs
no wait of its own any more either. **Measured on the console with the pool
fix**, openvcl production set: `"clipping": "vu1"` **30 of 30 frames at 0
pixels** against Sony's reference at 22.5-23.5 FPS (23.3 before); `"precise"`
**24 of 24 frames identical**, at a constant 223 edge pixels from the VU1
reference (the EE clipper's own rounding; PCSX2 puts the two 291 apart) and
the same 17.9 FPS it had while drawing slabs. PCSX2 is unchanged in both
modes.

Two more barriers went in with it, both cheap and both real even though
neither was *the* window here: a `FLUSHE` at the head of the StaPip and DynPip
uniform chains (they unpack the MVP, OPTIONS and the six clip planes at
absolute VU1 addresses, and the clip programs read the planes and OPTIONS
inside their loops - the previous batch may still be running, parked on its
`xgkick`), and a VIF1 wait before the projected-shadow pass rewrites its shared
`projClamp` buffer between two parts of one caster (the runtime's one other
by-reference array that is rewritten between submissions; every per-view
rewrite - beams, sky bodies - sits inside `align3D()` brackets already).

### One native first packet; rejected linked-DMA experiments (1.86.3-1.86.4)

Do not combine already-finished uniform and geometry chains by byte-appending
them or by rewriting the uniform packet's empty `END` as a zero-QWC DMA `NEXT`.
The byte-appended version stalled in PCSX2. The proper-tag `NEXT` version ran and
profiled faster in PCSX2, but froze a physical PS2 on the first gameplay frame
in three fresh boots. An otherwise identical baseline ran past 600 frames and
returned five valid profiles.

The safe 1.86.4 implementation constructs one chain from the beginning instead:
the current double-buffered packet receives the leading `FLUSHE`, uniform
unpacks and geometry commands before a single final `END` is written. There is
no cross-allocation jump and no finished chain embedded as data. It survived
more than 2100 frames on a freshly booted physical PS2 and returned a correct GS
capture. Five settled Aster captures reduced median DMA submit by 1.005 ms,
Dispatch by 1.283 ms and VU1 wait by 0.717 ms versus the two-submission path;
serialized Total remained fill-bound at about 34.3 ms.

The independent safe optimization is at the qbuffer input: when a bag-level box
is wholly inside the frustum, qbuffers reference each contiguous source range
directly. Package descriptors and copied pools remain mandatory for partial
classification and EE clipping, where data can be split or rewritten.

**What it is not.** The assembler. openvcl's production output puts a store
one row behind the FMAC write it reads at 88 sites over the 25 programs where
Sony's `vcl` never goes below two rows, and a patched openvcl that kept two
rows (+14 words over the resident set) was built and tested: it changed the
failure *mode* and not the fact, and the EE-wait probe rendered that very
production set pixel-identically to Sony's for 24 of 24 frames - so that
distance is not a hardware hazard and the patch was not kept. The lesson for
the next hardware-only bug: measure a **rate**, on a **parked
pose**, and bisect with **barriers** before reading microcode.

## See also

- [profiling.md](profiling.md) — the frame-timing rig, the measurement protocol
  and the calibration gate.
- [vu-framework.md](vu-framework.md) — `--vu-check`, the host VU1 simulator, and
  the C++ descriptions that generate the clip program family.
- [vu-authoring.md](vu-authoring.md) — writing a project's own VU1 program.
