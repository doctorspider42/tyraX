# Road-free reflection isolation

This diagnostic separates shared environment capture from the vehicle body
reflection pass on the user's road-free Vehicle Playground copy. It changes
only scratch fixtures, not the source project or production rendering.

The supplied project is `F:/Tyra-Projects/vehicles-test/vehicle-playground - No roads`.
Main has zero roads and three vehicles; the other scenes still contain roads
and are not visited. The saved manifest has all body shine values absent (zero),
but Live Debugger, Remote Pad, memory and frame-profiler overlays enabled.
That does not establish the configuration used in the user's preceding run.

The diagnostic disables those channels, restores body shine 0.45 on the three
vehicle definitions in the scratch copy and runs the vehicle bake once. All
runtime arms therefore use identical model parts, materials and base geometry.
Both geometry caches and terrain strip refinement keep their production defaults.

Nine full-throttle drives use modes 0,1,2,3,0,3,2,1,0:

- 0: normal shared capture and normal body environment pass.
- 1: retained valid capture frozen while driving; body pass unchanged.
- 2: capture frozen and vehicle body environment pass skipped while driving.
- 3: normal capture, body environment pass skipped while driving.

Warmup and 120 parked samples run normally before every drive. The body-pass
gate follows ordinary eligibility/distance/shine-budget checks, before basis
and color preparation. It does not remove base vehicle draws or rewrite baked
materials. Capture reuse/content scans remain active. Thus mode 2 is not exactly
an authored `Body shine = 0` rebuild. Frozen imagery is deliberately incorrect;
this is attribution, not a proposed quality setting or production fix.

Real dt, physics, camera, effects and off-road handling remain enabled. No roads
means a different driving workload from older experiments; do not subtract
their timings. Warmup is 1200 loops, throttle begins at 1320, endpoint z >=108.
Only capture, paint and body-pass eligibility/draw/skip counts remain enabled;
there are no per-object clocks. Whole-loop clocks and shared diagnostic memory
still have unpriced overhead. Use PS2, not emulator timing, for conclusions.

Generate with `tools/tyrax2-reflection-isolation.py <new-fixture> <source-project>`.
Build natively, bind sources/ELF before another native build, then verify in an
owned hidden emulator. Physical deployment must mirror the editor: release the
owned host server, send one network reset (silence is not failure), wait through
its listener grace and ten-second settle, stage `ps2link.run`, and launch the
host server from `bin`. Never kill another project's server without approval.

The earlier full-map capture-only freeze saved 0.638-0.660 ms
but did not restore sustained 60 Hz; see [the previous trial](tyrax2-full-throttle.md#shared-capture-freeze-follow-up).

`tyrax2-strip-capture.py <fixture> --drive <phases...>` takes read-only-PINE
selected moving smoke images on the owned Xvfb session; the default remains
parked rotation captures. Images of frozen/skipped reflections are not a
pixel-equivalence test.

## Physical results (2026-10-08)

All nine phases passed on PS2 (7495 samples) and PCSX2 (7506 samples), including
activation, parked pose, lane/damage/endpoint, timing balance and sparse-counter
checks. The same ELF is bound to 467 native source files:
`30542d50974de21dff9efcea0b872d868b6ecf700c5b96e5f2cef4fa17cbb668`.
Seven deliberately corrupted inputs are rejected by the analyzer on both runs.
The owned emulator was stopped after smoke captures; phase 4's requested moving
capture was missed, but phases 5-8 were recorded with PINE phase/offset witnesses.

| Phase | Mode | Mean work ms | Loop Hz | Work p95 ms | Entry periods >25 ms |
| --- | --- | ---: | ---: | ---: | ---: |
| 0 | Normal | 9.812 | 59.77 | 12.655 | 1 |
| 1 | Frozen capture | 9.554 | 59.77 | 11.988 | 1 |
| 2 | Frozen + body pass off | 9.402 | 59.77 | 11.733 | 1 |
| 3 | Normal capture + body pass off | 9.745 | 59.69 | 12.545 | 1 |
| 4 | Normal | 9.891 | 59.69 | 12.563 | 2 |
| 5 | Normal capture + body pass off | 9.704 | 59.77 | 12.536 | 1 |
| 6 | Frozen + body pass off | 9.404 | 59.77 | 11.709 | 1 |
| 7 | Frozen capture | 9.542 | 59.77 | 11.946 | 1 |
| 8 | Normal | 9.842 | 59.77 | 12.664 | 1 |

Work is elapsed minus explicit pacing, not pure EE time. Each drive has 712-713
sampled loops and exactly as many recorded presentations; this is not a hardware
scanout trace. All parked controls are 59.94 Hz. Normal capture arms perform
85-86 captures; frozen arms perform zero with positive blocked eligibility.
Body draws are 774-775 when enabled, zero when disabled, with matching skip
counts. Both expensive geometry caches and terrain refinement remain active.

Across bracketing normal controls, freezing capture saves **0.258-0.349 ms**.
Skipping the body pass on the frozen capture saves another **0.137-0.152 ms**;
skipping it with live capture saves **0.066-0.187 ms**. Disabling both saves
**0.410-0.489 ms**, but does not remove the rare hitch. These are inclusive
interventions, not independently additive producer timings.

### The residual hitch is not shared capture or body reflection drawing

Every arm's worst work occurs at **z approximately -53.593**, offset 1491/1492.
Ordinarily it is **40.842-41.730 ms**, with one 62.662 ms outlier in phase 3.
That loop has **zero captures and zero paint-color rebuilds in all nine arms**,
including mode 2 where both operations and body draws are disabled. Update takes
only 1.065-1.108 ms there; the long elapsed interval is in rendering (including
its waits). Entry periods peak around 50.05 ms, or 66.74 ms in phase 3. The
entry period belongs to the preceding loop, so do not join its capture flag as
though it described the same interval.

This contradicts a *sustained* lack of frame budget on this specific clean,
daytime straight drive. It does not explain which render-side operation causes
the location-specific hitch. Cold geometry, terrain residency and synchronous
asset work are candidates, not proven causes. Instrument that one location,
rather than another full-renderer timing census, before proposing a fix.

**Paint-color rebuild count is zero throughout every moving arm.** Straight
driving does not exercise the costly view-dependent color rewrite. Turning the
car/camera remains a separate, necessary reflection test; this trial cannot
clear that path or reproduce every user-driven route. The user's matte rebuild
also changes material partitioning, unlike these same-bake interventions.

### Deployment and source preservation

The editor-equivalent network reset succeeded on the first attempt, followed by
a normal game boot; no manual console reset was needed. The original source
manifest is byte-identical to the pre-generation copy after the test. No source
project settings or production engine behavior were changed.

The saved devkit flags are a plausible contributor to the difference from the
user's observed FPS, **not an isolated causal measurement**. This fixture also
compiles hardware tracing and legacy profiling off, pauses noon, and drives a
fixed route. Do not subtract its absolute times from a differently configured
editor build or call this proof that the devkit caused all previous slowdown.

[Evidence archive](tyrax2-road-free-reflections-2026-10-08/README.md) retains
CSV, analyses, logs, launch/input identities, source binding and relevant sources.
