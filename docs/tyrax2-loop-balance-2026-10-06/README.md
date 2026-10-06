# Full-city daylight loop balance

The private PS2 observer separates pre-render game update, the render phase,
existing VIF1/GIF/GS waits, pacing and engine remainder in the same daytime
Vehicle Playground drive used by the preceding isolation trials.

The completed hardware run places most observed heavy-loop elapsed time inside
the render phase after its explicit waits and pacing have been removed. It does
not identify a particular rendering function or measure retired EE instructions.
No production renderer or physics change is introduced.

## Physical result

Native ELF SHA-256:
`49fbb9d2bcec09a5ff48e09373492b553e67c379a48277c9a2e78a009af7a245`.
Source branch head at launch: `5d7f6df7e`. See the
[strict result](physical/strict-result.json) and
[primary-agent qualification](physical/root-qualification.json).

| Disjoint non-pacing category | All 128 driving rows, mean ms | 20 over-budget driving rows, mean ms |
| --- | ---: | ---: |
| Game update before renderer.beginFrame | 1.292801 | 1.295176 |
| Render phase excluding leaf waits/pacing | 13.210619 | 17.410194 |
| VIF1 queue/channel/FIFO waits | 0.005034 | 0.005573 |
| GIF DMA waits | 0.001743 | 0.001738 |
| GS FINISH waits | 0.000811 | 0.000810 |
| Engine remainder | 0.034621 | 0.036315 |
| Total non-pacing loop | 14.545630 | 18.749806 |

Here, over budget means non-pacing work above16.68333 ms; it is not a direct
count of missed displays. Render accounts for92.86% of heavy-row non-pacing
elapsed time. The worst work row is frame931:21.996857 ms total,
20.789358 ms render,1.168901 ms pre-render update and0.006178 ms explicit waits.
Its completion belongs to EE loop930. The raw maximum VIF wait sum is0.044030 ms;
no long explicit completion wait explains the observed multi-millisecond spikes.

Pacing is separate: all-driving mean4.737039 ms, making the ordinary measured
whole-loop mean19.282669 ms. Existing vsync/buffer availability remains inside
this pacing category. It must not be reclassified as more render preparation or
treated as an independently removable GPU cost.

The existing320-loop sampler records driving non-pacing work13.833547 ms and
entry period18.352116 ms. Its slowest64-loop moving chunk has21.115091 ms entry
period. Later representative-view windows have13.638140/13.640658 ms work and
16.683237/16.683333 ms entry period, approximately59.94 Hz. Each window has320
rendered completions and zero synthetic completions. The128-row partition covers
only offsets900..1027; it is not a partition of all320 chunk loops.

## Observer calibration

All phases use the same ELF, ordinary sampler, allocated buffers and full
gameplay. Phase0 observes every driving raw row. Parked phase1 uses Off40/On48/
Off40; phase2 uses On40/Off48/On40. ON-minus-OFF work contrasts are:

| Parked order | Contrast ms | Outer-cohort spread ms |
| --- | ---: | ---: |
| Off/On/Off | -0.083311 | 0.000713 |
| On/Off/On | +0.093763 | 0.000745 |

These opposite signs do not resolve a stable scalar observer tax. No uniform
subtraction is accepted. They report observed perturbation in these controls,
not a proven error bound for every moving sample. The fixed camera matches the
selected drive midpoint at both cold witnesses; normal physics continues with
small parked-car drift. Pixel-identical state and the cause of the order effect
are not established. The several-millisecond render spikes remain a useful
attribution lead, rather than an exact uninstrumented instruction bill.

Every enabled row makes17 additional Count reads:3 phase markers and2 reads
for each of4 VIF,2 GIF and1 GS leaf wait. There are256 enabled and128 disabled
rows, totalling4352 extra reads. Disabled rows make zero new Count reads but
retain branches, ownership metadata, scratch/reset/copy work and data footprint.
The new128-row buffer is15872 bytes; the ordinary sample/chunk arrays are6144/
420 bytes and the balance scratch row124 bytes. Common footprint is unpriced.
The inherited `CLOSEOUTPHASE countReads=262` describes the ordinary sampler only.

## Scope and coverage

The inherited sampler's existing entry/end stamps enclose each measured loop.
Three additional stamps mark entry to `game->loop`, the call immediately before
`renderer.beginFrame`, and return from `game->loop`. Render therefore includes
scene drawing and its own CPU preparation, `dayNightTick`, secondary views when
active, HUD updates/drawing, post effects and endFrame. It is not exclusively
geometry preparation. Pre-render update is not all game logic everywhere.

Wait clocks wrap existing leaf loops/calls only: queue waitFor, drain spin and
VIF SDK wait, ring backpressure, VIF FIFO polls, central direct VIF/GIF DMA
adapters, draw_wait_finish and the GS check loop. Queue flush, arena reset and
GS clear remain outside those spans. In particular, the GS span ends before
clear can complete another job. Existing flip/vsync t0/t1 stamps supply pacing
without extra clock reads. Raytracer VU0 waiting is not an active renderer in
this fixture. FrameProfile and HardwareTrace compile to zero; there is no added
fairness drain, FINISH tag, wait, polling loop or physics/controller change.

An outer-only guard prevents double accounting and invalidates mixed-kind or
phase-crossing waits. Runtime additionally requires zero nested waits. All384
raw rows have matched balance rows; enabled rows satisfy monotonic phase stamps,
nonnegative exclusive categories, exact whole-loop sum and agreement with the
independent cumulative pacing delta. Submission records the actual EE owner,
sequence and context; every enabled completion in this run belongs to the
preceding EE loop, not the current producer. Source coverage is reviewed
separately: algebraic closure alone cannot prove that every possible hardware
stall was instrumented.

Elapsed categories include thread/interrupt preemption, SDK/queue servicing,
memory stalls and observer work. Short explicit waits do not prove that VU/GS
are universally idle or exclude DMA/memory contention inside producer code.
The result narrows this driving diagnosis to the render phase; it does not
settle full-night60 fps, all cameras/maps, or a pure EE versus GPU throughput bill.

## Verification and evidence

- Native sources match actual cache mirrors, dependencies, objects, archive
  members and linked ELF/symbols; the balance buffer has one linked instance.
  Build-v1 diagnostic format/indentation warnings were corrected in build-v2,
  whose observer has no such warnings. Only build-v2 was released to devices.
- A primary-agent-owned software PCSX2 run completes5400 loops. Completed
  raster was inspected; emulator time is not hardware pricing. Exact process
  exit was independently confirmed after its stop request.
- Eight damaged artifacts are rejected: missing/duplicate row, marker order,
  pacing disagreement, clock-read count, nesting, job owner and negative
  exclusive time. The scripts and rejection outputs are preserved.
- The same qualified ELF completes5400 PS2 loops after the user's physical
  reset. Original host bytes, newline-only normalized cold records, direct
  sample file and recursive launch input pins are preserved. Physical TV
  inspection remains pending; the client is left serving the completed game.
- Earlier empty-car, day-drive and vehicle-scope inventories are verified
  unchanged. New evidence has its own [inventory](inventory.json).

The next useful cut is inside the render phase: frame setup/day-night evaluation,
renderScene and its scene/secondary-view preparation, HUD/effects, and endFrame.
Then split the dominant region into visibility/bag preparation, clipping and
packet construction/submission while retaining this enclosing balance and the
same wait attribution. Removing more vehicle physics or guessing at long DMA
waits is not supported by this observed window.
