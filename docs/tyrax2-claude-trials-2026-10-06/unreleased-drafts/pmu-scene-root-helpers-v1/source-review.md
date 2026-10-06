# Private whole-scene I/D PMU observer proposal

This is a source-review proposal only. No fixture, native build, emulator or hardware execution is authorized by this document. Root reviews the boundaries before the isolated draft is created.

## Fixture and contrast

Use new `pmu-scene-physical-v1`, kind27 (subject to root reservation), cloning the qualified object-route fixture's501 source files and authored resources. Restore production `stapip_core.cpp` and `stapip_qbuffer_renderer.cpp`; remove every kind22 route scope. Keep production fixed-hour mood/paused API, sky retint, assertions and ENV_NORMALIZED untouched. No assertion-bypass candidate. Source manifest and root freeze start `frozen=false`.

Off/On/Off and reverse use one ELF and the existing ordinary full-night sampler. All effect masks stay0 and compact table staysOn. Both arms keep identical PCCR0x800340D0 configuration. On adds only two ordered counter reads at Scene entry and two at exit, each with `sync.p` and a compiler memory clobber, plus bounded record writes/guards. Off does not read counters at Scene boundaries. Common ring/code/phase guards/configuration remain unpriced against a binary without the observer.

## Exact Scene boundary

Place a private RAII bracket at `TerrainGame::renderScene()` entry in `game/src/gen/game_physics.gen.cpp`, immediately after the existing disabled HardwareTrace Scene scope. Destructor records the end when the existing function returns, including any early return. It excludes pre-Scene simulation, outer begin/endFrame, HUD and postfx; it includes all existing work and waits reached inside Scene. No new waits, FINISH events, DMA drains, cache flushes or changes to completion ownership.

Record only128 existing sample frames900..1027 in enabled phases. Four PMU reads per recorded Scene call. Require one entered Scene call per recorded engine-loop frame, no nested bracket, exact phase/frame ownership; otherwise reject. Ring128 records is shared across arms, reset outside tax window and exported after existing sampler export1327. Each entry retains `frame`, ordered `I0/D0/I1/D1`, PMU-read count4 and invocation ordinal. No new Count read; correlate each row with the already captured loop sample of that frame. PMU endpoints are not an atomic pair and measured misses include observer instructions, data, preemption and other user threads.

## Setup and configuration ownership

Private wrappers append to existing `night_ablation.hpp` to retain501 sources. Use inline native `mtps`/`mtpc` setup matching the pinned installed archive sequence: stop PCCR, sync.p, zero counters, sync.p, write0x800340D0, sync.p. Never call PerfTest::StartSampling or ZeroCount; no COP0Count writes. `mfps` for PCCR readback is an implementation gate: root must verify actual opcode/toolchain support and linked placement before release.

Configure/reset once at each phase offset799, outside the800..1119 price window; Off and On both perform exactly the same setup. Read/check PCCR at750 before setup, immediately after setup799, and1156 after the whole price window, outside priced intervals. The pre-setup read records prior PCCR without asserting it equals our configuration. Post-setup and end require exactly0x800340D0, not just selector bits. This is observed global configuration stability, not reservation or proof that no transient owner changed it. Do not disable interrupts/audio or synthesize per-thread ownership.

## Counter overflow gate

Pinned primary evidence establishes selectors and reads, not overflow semantics. Do not borrow Count's modulo-wrap rule or silently unsigned-subtract decreasing counters. Preserve raw endpoints. Initially reject any decreasing endpoint or endpoint with high bit set, and label extracted non-wrapping deltas as provisional until counter width/event bounds are primary-qualified. A low endpoint alone cannot rule out one or more wraps. Root must resolve this semantic gate (or explicitly accept only raw endpoint/activation evidence) before reporting qualified miss totals. Misses are event counts, not elapsed stall cycles, rates, or recoverable milliseconds.

## Exact draft patch list

1. `tyra/engine/inc/debug/night_plan.hpp`: add isolated kind27 zerojoint/restored/fullnight/tableOn dialect.
2. `tyra/engine/inc/debug/night_runtime.hpp`: phase/window gating; no producer scoped clocks; common PMU setup at799 and outside-window PCCR guards; deferred raw PMU ring export; validity propagates into NIGHTDONE.
3. `tyra/engine/inc/debug/night_ablation.hpp`: namespace-local ring/state, configuration wrappers and Scene RAII observer; no new file or debug macro changes.
4. `game/src/gen/game_physics.gen.cpp`: one private Scene RAII instance; existing rendering code untouched.
5. Production core/qbuffer restoration removes inherited route scopes; no production checkout edits.
6. Isolated strict kind27 parser and helpers: retain source/native/ELF/symbol/assets/runtime closure, exact PMU phase/control/row schema, complete inherited loop capture, Off zero rows, On128 rows/four reads each, bounds/nesting/configuration guards. Host controls include both orders and rejected missing/duplicate/wrong-frame/off-active/wrong-PCCR/decreasing/high-bit/extra-read records. Host PMU stubs test ordered reads and all return paths; no emulator event accounting claim.

## Native and physical review gates

Root must audit actual linked setup/read opcodes and placement, no Count write, retained assertions/ENV operation, VU residency/ABI unchanged and all501 mirrored source identity. Source/parser controls are not native activation. Price observer net enabled tax using both physical orders before interpreting miss endpoints. Emulator may qualify protocol/raster only. Successful configuration readback and positive I/D deltas on hardware establish sampled activation, not instruction stall attribution or60FPS gain.

## Draft implementation and ownership review

The isolated source draft is now prepared and remains unfrozen. PMU opcode syntax was independently pinned in pmu-scene-primary-v1 from ps2dev/binutils-gdb commit530530b9df4ca59bb66fa01f31a6fc5600f67c97. Unknown enabled PCCR immediately before setup is rejected without writes, even matching selector bits without a prior successful local setup. The exact current scene pointer is checked at scope exit. Guarded cleanup at final5399 reads PCCR and stops only the known locally configured value; unknown changed owner is rejected and never overwritten. Setup, controls, export and cleanup are outside pricing. No overflow or physical miss accounting is qualified.
