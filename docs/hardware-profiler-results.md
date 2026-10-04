# Hardware profiler: physical PS2 findings

The bounded hardware timeline and seven controlled PS2 runs identify expensive EE-side render submission and devkit host I/O as the next optimization targets. GS pixel rasterization alone does not explain this scene's low frame rate.

## Method

Native PAL 512x512, 32bpp except the explicit 16bpp probe, no BLSS/adaptation/extrapolation, parked traffic, identical garage/outer-road day/night views. Every arm records 240 warmed frame rows per view (960 total), then captures a separate timeline after frame 1500. Trace export and GS screenshots occur after the timing window. The repeat uses the exact control ELF. All 105 PNG/TMDL/MTL assets match the complete baked reference in all seven deployments.

Work is update + submit + finish, excluding presentation waits and outer engine pad/info. The hardware timeline also includes those outer scopes. Rolling FPS medians are retained in the raw summary but are not used to rank small gains. Included timing buckets overlap.

## Work per frame (milliseconds; lower is better)

| Probe | Garage day | Garage night | Outer day | Outer night |
|---|---:|---:|---:|---:|
| control | 46.43 | 53.56 | 24.92 | 28.48 |
| no-host | 41.74 | 48.44 | 20.04 | 23.07 |
| 16bpp | 46.79 | 54.43 | 25.28 | 28.57 |
| no-extra-passes | 41.41 | 44.69 | 23.53 | 25.54 |
| scissor/game | 44.78 | 50.94 | 24.25 | 26.94 |
| unlit/game | 46.28 | 53.46 | 24.96 | 28.87 |
| control-repeat | 46.80 | 53.64 | 25.09 | 28.58 |

## What the probes establish

- **Host I/O:** replacing only live-tool fopen calls with failure returns saves 4.69–5.41 ms/frame; update falls to approximately 2.9 ms. This is a diagnostic suppression, not a shipped debugger behavior change. Render submission and geometry remain effectively unchanged.

- **GS raster area:** masking engine scissor writes to one pixel retains identical triangle counts and program/geometry submission. Work improves only 1.66 / 2.62 / 0.68 / 1.53 ms. The GS capture retains the old loading image because subsequent scene rendering and clears are clipped; fresh frame IDs, complete output and matching geometry establish that the game is running. This rules out pixel fill as the dominant cost in these views, not primitive setup, bandwidth or every kind of GS backpressure.

- **Framebuffer depth:** 16bpp does not establish a gain. It changes framebuffer/z bandwidth, not the full texture workload.

- **Additional passes:** freezing the shared environment target after its first valid capture and suppressing projected shadows, light pools, blob shadows and light beams saves 5.02 / 8.86 / 1.39 / 2.94 ms. This is an intentionally degraded compound probe; its component savings must not be added to other probes.

- **Unlit bags:** no useful gain; garage RGB and triangle counts are identical to control. This is not evidence that all VU work is cheap or eliminated. Most visible paths evidently do not change under this override; it does not test a replacement transform/clip microprogram.

## Timeline evidence

The first four-frame garage trace recorded 8,132 events without overflow. Representative inclusive sections: Objects 16.82 ms, Procedural 5.08 ms, Terrain 4.91 ms, Wheels 3.81 ms, shared reflection capture averaged across its alternating frames 2.69 ms, Projected_shadows 2.75 ms. Individual object scopes include coupe 2.23 ms, Tristar 2.19 ms, Tower blocks 03/04 approximately 1.85 ms each. These include waits and overlap parent scopes.

The complete control has approximately 263.5 static-bag calls and 156 packet submissions per garage-day frame. The timeline records about 7.85 ms in VIF1 DMA waits, 0.04 ms in GIF DMA waits, and 0.22 ms in existing GS FINISH waits. Small FINISH tails do not measure total GS work. VIF1 wait-entry snapshots show several states; they are observations at boundaries, not state-residency percentages. The scene uses VU1 clipping; this is not evidence of falling back to the EE clipper.

## Instrumentation overhead and limits

Render submission inferred from traced Game - Update - EndFrame is approximately 0.83 ms above the unarmed timing-window mean in the control; the 12-frame repeat without register snapshots is approximately 0.76 ms above its unarmed mean. These are same-view estimates from short captures, not universal correction factors. Use traces for attribution and the separate unarmed windows for comparisons. Unarmed compiled hooks still add code and branches; a build-without-hooks A/B was not performed, so zero disabled overhead is not claimed.

The baseline repeat varies by 0.09–0.37 ms of work across views. The unlit and 16bpp results do not justify integration. The profiler does not expose exact EE/VU1/GS utilization percentages and cannot separate VU arithmetic from all transfer or downstream stalls. No Linux editor build was run; Windows Release editor compilation, fresh no-terrain code generation, PS2 native compilation and physical captures passed. Truncated, overflowed, wrong-count and out-of-frame CSVs were rejected, and HTML frame selection/tables were verified in the browser.

## Next engine work

Prioritize the static submission path: retained per-mesh/package metadata and command data with explicit DMA buffer ownership, fewer repeated per-bag setup operations, and larger useful batches where materials and visibility permit. The current scene spends tens of milliseconds outside the marked VIF/GIF/FINISH waits; treat that as EE-side preparation/dispatch work plus any uninstrumented stalls, not as a precise CPU ALU measurement. Break Dispatch into package classification, data copies and packet assembly before choosing the retained representation.

Keep host I/O quiet during normal play and enable channels on demand; the measured cost is large enough to deserve explicit profiles. Preserve graphical quality by scheduling expensive reflection/shadow work selectively, not by shipping the diagnostic disable switches. None of the visual probe changes is integrated. Roads are unchanged.

[Raw timings, traces, summaries, asset audit, ELF hashes, screenshots and exact experiment scripts](../examples/vehicle-playground/authoring/hardware-profiler-2026-09-14/) retain the evidence. Use [the profiler tools](hardware-profiler.md) to export any raw trace to standalone HTML or Perfetto JSON. The console was left running the complete control-repeat fixture with the normal garage image.


## Detailed dispatch and native viewer (1.92)

The editor now renders the same CSV directly in **Debugger > Hardware timeline**,
with boot arming/disarming, frame selection, horizontal zoom, raw tooltips and
inclusive totals. The Windows Release build and scripted load/next/previous/zoom/
arm/disarm passed. A native parser harness accepted the real capture and rejected
seven malformed/incomplete/overflow cases. The parser implementation was extracted
unchanged into the temporary harness; no separate parser was substituted. Linux
was not run; this addition uses the shared C++/ImGui implementation without OS APIs.

A two-frame garage trace recorded 4,517 events with no overflow. Inclusive means:

| Scope | ms/frame |
|---|---:|
| Dispatch | 23.130 |
| Package_create (includes classification) | 3.947 |
| Package_classify | 2.908 |
| Packet_build | 2.929 |
| VIF1_submit | 3.011 |
| VIF1_DMA_wait | 7.531 |

`QBuffer_copy` was never entered in these two frames. Copy optimization is therefore
not supported as the priority for this view. Package metadata construction is the
approximately 1.04 ms difference between the nested package scopes, including
instrumentation overhead. These scopes do not assign continuous hardware utilization.

A bounded candidate reused the most recent identical bbox-range classification
inside each package-creation call. It changed no DMA ownership, packet ordering,
assets or renderer quality. It was **rejected and reverted**: package creation in
the short trace changed 3.947 → 3.923 ms, classification 2.908 → 2.948 ms and Dispatch
23.130 → 23.122 ms. This is not a useful frame-time improvement.

Three physical runs recorded 960 warmed samples each (240 per view), with detailed
capture deferred until engine frame 1500, after the timing window. The repeated
control ELF is byte-identical. Render submission, excluding update/presentation:

| View | Detail control | Reuse candidate | Control repeat |
|---|---:|---:|---:|
| Garage day | 37.979 ms | 38.191 ms | 37.961 ms |
| Garage night | 44.393 ms | 44.570 ms | 44.357 ms |
| Outer day | 16.329 ms | 16.206 ms | 16.333 ms |
| Outer night | 18.993 ms | 18.894 ms | 18.975 ms |

The small outer-view reductions accompany regressions in the heavier garage views;
this does not justify retaining the candidate. Update timing also varied (host I/O
and concurrent editor validation), so its total-work differences are not attributed
to classification. Both variants retain identical triangle means and all 105 asset
hashes. Their physical garage screenshots are pixel-identical. The console was left
running the **dispatch-detail-repeat** control, without the rejected cache.

No engine speedup is claimed from this iteration. The shipped changes are the
native viewer and finer instrumentation. The next substantial hypothesis is to
reduce repeated submission/setup and improve overlap across bags while preserving
texture and DMA lifetimes; that requires a separate measured implementation, not
more speculative cache layers. Roads and graphical quality remain unchanged.

[Raw captures, all 2,880 samples, hashes, UI validation and rejected source](../examples/vehicle-playground/authoring/hardware-profiler-2026-09-14/dispatch-detail/)
retain this follow-up evidence. The initial control and repeat render submission
stay within 0.04 ms per view; unarmed hooks are still not claimed to have zero cost.
