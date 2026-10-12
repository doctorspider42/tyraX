# Reflection turn evidence, 2026-10-08

See [the report](../tyrax2-reflection-turn.md).

- `attribution`: five reflection interventions, completed on PS2 and PCSX2.
- `scatter-copy`: rejected three-phase memcpy scatter candidate.
- `vu-paint`: private EE/VU/EE prepass prototype, PS2 and PCSX2.
- `vu-paint-reverse`: reversed VU/EE/VU physical qualification.
- `night-roads`: full seven-road main scene at midnight, EE/VU/EE sweep.
- `roads-day`, `night-no-roads`, `day-no-roads`: same-source factorial controls.
- `rejected-night-no-roads-v2`: missing-shadow-pin failure, NOT price evidence.
- `tools/factorial`: updated generators and context/factorial analyzers.
- `factorial-analysis.json`: natural road intervention contrasts; includes
  the validated .12-unit car/camera height shift, NOT strict world-pose equality.
- `road-refine`: road-only refinement off/on/off, reversed order and separate
  unpriced vertex/plane oracle. All keep VU paint enabled. Private, not promoted.
- `road-vu/day`: constant no-kick versus VU vertex/output bypass; no useful
  PS2 gain. Includes shader controls/negative tests and resident symbol ranges.
- `road-nokick/day`: retained road submissions with XYZF3 suppressing primitive
  kicks; no useful physical gain, keeping the next inquiry upstream of raster.
- `road-submit`: same-height road drawing omission with physical road storage
  and receivers retained; day/night fixtures and frozen analyzer/generator.
- `road-single`: colour-only and two completed capacity-aware packing trials,
  rejected v1 build log, host topology checks and frozen diagnostic tools.
  Final packing saves only 0.159-0.192 ms on PS2; no production promotion.
- `tools`: generators, analyzers and shared support used by these fixtures.

`tools/vu-paint` freezes the updated generator/analyzer and numerical harness
separately from the earlier attribution tooling. The host oracle compares
source-level VCL, including complete GIF output; it does not model the target
assembler's physical-register allocation. No paired GS raster equality is
claimed for the paint prototype, and no production codegen change is promoted.

Each fixture contains 467-file source/native/ELF binding, private modified
sources, deployment inputs, runtime CSV/logs and analysis. ELFs remain in the
external scratch directories named in the report; hashes are archived here.
PINE screenshots witness moving camera phases, not exact pixel equivalence.
The original road-free project manifest SHA-256 remains
`5e09e8db4d57e0d01bc0b6d5850ac6e9bc03ce461aa7bfc5204332f89d2eb95a`.
All 303 physical launch inputs per fixture were rechecked after execution.
Owned host/emulator processes were stopped, followed by a network reset with
ten-second settle; the physical screen was not independently inspected.
