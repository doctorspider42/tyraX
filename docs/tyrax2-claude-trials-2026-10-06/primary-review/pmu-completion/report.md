# Read-only PMU and completion audit, 2026-10-06

This audit inspects source and an installed archive. No renderer changes, native builds, emulator execution or console operations were performed. Documentation capture is the only write. It establishes implementable measurement boundaries, not current cache/wait cost, observer overhead or optimization savings. Companion-pass activation and the assertion trial take priority; PMU requires independent activation and overhead qualification first.

## Primary PMU evidence

The installed `ps2s/core.h` and `ps2s/perfmon.h` are byte-identical to ps2dev/ps2stuff commit `c8541baad8f1ad5462bf5955c66f51a15fcf20e2`. Copies, upstream URLs and hashes are recorded in `source-pins.json`. Upstream `src/perfmon.cpp` assigns I-cache misses to counter 0 with selector 6 and D-cache misses to counter 1 with selector 6. These events can therefore be selected simultaneously.

Upstream `Core::SetupPerfCounters` and the actual installed `libps2stuff.a` disassembly use: stop PCCR; `sync.p`; zero both counters through `mtpc`; `sync.p`; shift event 0 by 5 and event 1 by 15; set user enables at bits 4/14 and master enable at bit 31; write PCCR through `mtps`; `sync.p`. The derived user-mode I/D pair is **0x800340D0**. Counter reads use `mfpc` and the installed inline wrappers follow each read with `sync.p`.

Do not reuse `PerfTest::StartSampling`: it calls `Core::ZeroCount()`, which writes COP0 Count and breaks the renderer's shared timeline. It also rotates event selections between samples. Use a narrow private observer that never writes Count.

No PMU invocation was found by source search in editor, engine or tools C++/headers. This is not proof of global hardware ownership across BIOS and other threads. Counters are global, not per-thread; user enable does not exclude another user thread during preemption. The primary API supplies no reservation or context ownership mechanism. Preserve interrupts/audio scheduling rather than manufacturing a different workload by disabling them.

## Minimal PMU proposal

1. Configure the I/D pair outside the priced block. Keep the same PMU configuration in both observer Off and On phases; avoid per-bag counter setup/reset.
2. Start with one whole Scene bracket: read each counter at entry and exit, retaining ordered unsigned endpoint differences and existing elapsed Count samples. Paired sequential reads are not an atomic snapshot.
3. Check control/configuration before and after the window outside priced samples. Audit actual linked `mfpc`/`mtps`/`mtpc` instructions and reject ownership/configuration changes.
4. Preserve ordinary rendering, DMA submission and consumer waits. Count the four extra counter reads and their `sync.p` instructions, ring writes and guards. Price the same-ELF observer with Off/On/Off and reverse order. Bound intervals and qualify counter overflow semantics before relying on unsigned wrap handling; do not assume the Count clock's semantics transfer automatically to PMU events.
5. Only after activation and a useful signal, relocate the observer to one selected region in a separately qualified fixture. Four extra PMU reads at each of five regions times 134 calls per frame are not a minimal first experiment.

Misses are events, not elapsed stall ticks. High miss counts establish neither a constant per-miss latency nor recoverable frame savings. Low I/D counts do not prove that instruction count is the sole remaining bottleneck: other stalls and events remain possible. Without matching access counts, do not call the values cache miss rates. Emulator counter behavior cannot qualify physical event accounting.

## Existing completion ownership

At `vendor/tyra/engine/src/renderer/core/renderer_core.cpp:452`, `completePipelineFrame` returns immediately without a pending frame. For a pending frame it clears `pipelineFramePending` before existing VIF sequence completion, sets the GS context, waits/clears the already-recorded FINISH, and executes its existing presentation/pacing bracket. That bracket alone increments `stallTotal`.

`RendererCore::getStallTotal()` at `renderer_core.hpp:334` is a memory-only, non-destructive accessor. Do not consume `takeStallTicks()`: other readers own its reset semantics. `RendererCoreSync::waitAndClear()` at `renderer_core_sync.cpp:79` spins on the existing GS FINISH and clears its event. The queue callback at `paths/path1/vif1_queue.cpp:237` protects the new bank and temporarily detaches its pending PATH3 fence while completing the prior job. The measured presentation belongs to the pending job, not automatically to the new recording frame.

## Minimal completion refresh

Observe only an actually pending completion. Capture the pending sequence/context and the existing stall total, then take the first additional Count read. Execute the unchanged completion body. Take the second additional Count read, then read the stall total again. Record:

- inclusive completion = end Count minus start Count;
- contained pacing = ending stall total minus starting stall total;
- inclusive remainder = inclusive completion minus contained pacing.

The two additional Count reads need compiler memory clobbers and actual native placement verification. Use unsigned deltas within qualified short intervals. Retain exactly one owning rendered presentation, zero synthetic presentation, stable sequence/context, no nesting and pacing no larger than the inclusive duration. Keep pending/no-op call counts distinct. Export a bounded ring outside priced windows and price Off/On/Off plus reverse-order observer tax. Metadata/reset/layout work shared by both arms remains unpriced against a build without the observer.

This adds no FINISH, no DMA drains and no new consumer waits. October 4 V3 already qualified completion minus its sole contained pacing increment: combined 384-sample remainder **0.003120 ms**. The raw roughly 13.8 ms completion duration included pacing and was not a new large GPU-wait discovery. A current-fixture refresh can test whether this changed; it cannot erase that earlier result.

If the fresh remainder stays at that small scale, stop. If it materially increases beyond observer tax/control variation, use a subsequent fixture with **four Count reads** around the existing VIF and FINISH wait calls (two endpoints for each). Keep context assignment outside those child brackets, retain the parent containment proof, and compare only within that fixture. Do not add barriers or GPU-hold, which changes pipeline availability. These are inclusive wait-call durations, including queue bookkeeping/preemption/observer seams, not pure VU or GS utilization. A first-DMA-kick timestamp is unnecessary for this initial remainder question and requires separate job attribution if introduced later.

## Evidence limitations

Local header copies and the complete installed archive disassembly are captured. The disassembly record is normalized from captured tool stdout to UTF-8 with LF; its hash describes that record, not raw pipe bytes. The original binary archive is identified by SHA256 and is not copied. Primary upstream source is pinned and copied. Root source files and the prior completion page are identified by path/hash, but the next fixture must freeze its own full source/native provenance. No runtime cost or successful hardware PMU activation is claimed here.
