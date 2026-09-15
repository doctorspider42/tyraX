# VU1 arithmetic and DMA cache-flush cost, physical PS2, September 15 2026

Raw evidence for [the report](../../../../docs/vu1-and-dma-cache-cost.md). Seven
boots of one fixture on a physical console; two probes, both reverted afterwards.

## Arms

All arms share one fixture and differ only as noted. `perf-probe.cfg` in `bin/`
carries `<extraFlushes> <suppressFlush>`; an absent file means both zero.

| arm | ELF | what differs |
| --- | --- | --- |
| `control-a`, `control-b` | first probe ELF | nothing — two fresh boots, the noise floor |
| `flush1` | first probe ELF | one extra `FlushCache(0)` before each static VIF1 submission |
| `flush3` | first probe ELF | three extra |
| `control-c` | second probe ELF | nothing; re-baselines after the flush-suppression knob was added |
| `vuheavy` | third ELF | `cull_td` main loop 107 → 119 cycles per triangle |
| `vuheavy-tc` | fourth ELF | `cull_tc` 133 → 145 and `cull_c` 130 → 142 cycles per triangle |

A `noflush` arm (`suppressFlush = 1`, so `dma_channel_send_packet2` is called
with `flush_cache` false) is **not** in this archive because it produced no
measurement: it hung the console during boot and wedged ps2link until a physical
Reset. That outcome is the finding — see the report.

## Reproduction

The original working root was `D:/tyra-flush-0915`; `run-arm.ps1` preserves that
provenance and needs its paths adapted elsewhere. Build the fixture like this,
from the repository root:

```bash
python examples/vehicle-playground/authoring/benchmark-district.py D:/tyra-flush-0915/probe
cp -r examples/vehicle-playground/.res-baked D:/tyra-flush-0915/probe/.res-baked
python examples/vehicle-playground/authoring/instrument-frame-cost.py D:/tyra-flush-0915/probe
```

`benchmark-district.py` deliberately excludes `.res-baked`, and `bin/aoatlas` and
`bin/aomap` are written by an editor build rather than by the make resources
phase — copy all three in from the example, or the deployment is incomplete and
its timings mean nothing (docs/performance-hardware-recheck.md). Then compile
with `tools/toolchain/native-build.ps1` **directly**: an editor `--build` would
regenerate the instrumentation away.

Check the deployment before believing any arm:

```bash
python check_assets.py D:/tyra-flush-0915/probe/bin asset-manifest.json
```

All 105 reference PNG/TMDL/MTL files must match; `aoatlas/scene0.png` and
`aomap/scene0.png` are expected extras that the September 14 manifest predates.

Each arm is one `run-arm.ps1` invocation, which rewrites `perf-probe.cfg` and
`ps2link.run`, verifies both, resets the console, stops the previous file server
and starts `execee` from the fixture's own `bin`. Wait for `bin/frame-cost.csv`
to stop growing — it is written a line at a time over `host:` and a mid-write
read parses as a short file — then copy it out before the next arm. Keep other
builds, emulators and bulk transfers off the machine during a run.

`summarize_arm.py name=path ...` validates each CSV (960 unique rows, 240 per
pose, every field finite and non-negative) and prints the per-pose table in
`summary.txt`, plus `arm-summary.json`.

## Probe sources

`probe-source/` holds everything the arms added to the engine, all of it reverted
before the commit:

- `perf_probe.hpp` / `perf_probe.cpp` — the boot-time `perf-probe.cfg` reader.
- `engine-probe.patch` — the two call sites: `PerfProbe::configure()` beside
  `HardwareTrace::configure()`, and the extra-flush loop plus the `flush_cache`
  argument in `StaPipQBufferRenderer::sendPacket`.
- `stapip_cull_tc_vu1.vclpp`, `stapip_cull_c_vu1.vclpp` — the VU1 sensitivity
  probe: one extra `MatrixMultiplyVertex` per vertex stored into free VU1 data
  memory at quadwords 1016-1018, so the assembler cannot eliminate it and the
  GIF packet stays bit-identical.

Read the probe's real cost out of the generated assembler rather than assuming
it: count the lines between the loop label and the loop branch in the native
cache's `.../programs/cull/stapip_cull_tc_vu1.o.vsm`. That count is cycles per
triangle, and it is what the report's arithmetic uses.

## Traps this run paid for

- **Aim a VU1 experiment at the program the scene actually runs.** The first
  arm instrumented `cull_td` and measured exactly zero, because the generated
  game attaches a lighting bag only to dynamically lit objects and everything
  else selects `cull_tc` / `cull_c`. A wrong-program probe is indistinguishable
  from "VU1 is free".
- **A flush-suppression arm can wedge the console.** Budget a physical Reset,
  and do not queue arms behind one.
- **`frame-cost.csv` appearing is not `frame-cost.csv` being finished.** Poll
  until the size stops changing.
