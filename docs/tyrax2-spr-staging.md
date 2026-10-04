# TyraX2 scratchpad staging: current gates

The 2026-10-02 audit separates scratchpad staging from removing cache flushes
and from adding DMA CALL/RET. The implemented experimental 1.169.1 runtime
already owns one native CNT/REF prefix per ordinary frame. The next useful
hardware experiment stages a **finalized prefix** through SPR into frame-owned
RAM while retaining current publication of every other DMA-readable source.
No SPR runtime or performance improvement ships with this audit.

The [October 4 private runtime follow-up](tyrax2-spr-call-runtime-2026-10-04.md)
implemented direct SPR prefix construction and native CALL/RET replay. Guarded
SPR bytes and the physical images passed, but both candidates increased elapsed
time in both physical orders. Their [immutable source archive](tyrax2-spr-call-prototype-sources-2026-10-04/README.md)
preserves the actual implementation without adding a production renderer mode.
The finalized-prefix copy-only arm described below remains unpriced on hardware;
the direct-construction experiment does not close cache-flush removal gates.

## Host experiment completed

`tools/verify-spr-prefix-staging.cpp` uses the actual `FrameVifWriter` and current
VIF validator. It reconstructs finalized prefixes through host staging windows
of 8/16 KiB, with one or two slots. It covers 511/512/513 and 1023/1024/1025
qword boundaries, a short final chunk, the 8192-qword capacity, multi-chunk
DIRECT, NUM=0 UNPACK/MPG, a REF, and rollback through the writer API **before**
finalization. Reconstructed bytes compare exactly and canaries remain intact.
The test scaffolding delays copies, refuses premature publication and window
overwrite, and independently holds two destination banks until reader release.

Build from the repository root with a C++17 host compiler:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror tools/verify-spr-prefix-staging.cpp -o verify-spr-prefix-staging
./verify-spr-prefix-staging
```

On Windows the recorded MinGW GCC 16.1.0 run adds `-static` and uses an `.exe`
output. It passed **40 variants, 16,188 assertions, 124 host copy completions
and 449 expected refusals**. The original dynamically linked scratch executable
failed before main with an entry-point resolution error; static linking passed.
No sanitizer or Linux run was performed for this tool.

These checks validate byte preservation and the host model's bookkeeping.
The bank/window state machine is scaffolding, not the engine's implemented SPR
owner. It preserves external REF addresses, not REF payload publication or
lifetime. It cannot establish EE cache behavior, DMAC barriers, real transfer
completion or hardware performance. The
[audit and host record](tyrax2-spr-host-2026-10-02.json) preserves source hashes,
commands/results and the detailed feasibility report.

## Hardware prototype and publication requirements

SPR is 16 KiB, while a native prefix may occupy 128 KiB. Reserve the scratchpad
range and fromSPR channel explicitly; do not assume that custom project code
or libraries leave them unused. Copy bounded chunks of a finalized prefix to
owned RAM, keeping each source window until its transfer completes. Finish
every transfer before exposing the new VIF1 TADR. Keep the RAM destination
until its VIF1 reader completes; staging completion does not release the frame.

The destination's cached alias is a critical constraint: dirty cached lines
can overwrite the DMA-written prefix during a later global flush. Establish
clean/invalidate ownership before DMA, prohibit cached writes afterward, and
use a proven read path for byte comparison. Review the exact SDK/manual cache
operation and EE store barrier before running the candidate. Preserve the
1.169.1 post-presentation direct-channel wait and all existing VU/GIF/GS fences.

The first private SPR→RAM arm retains the RAM writer and `FlushCache`. It is
a correctness/overhead control. Replacing prefix construction with committed
SPR chunks follows only after that passes; rollback across chunks then needs
an explicit transactional contract. Removing the global flush is a later gate:
inventory publication of snapshots, copied mutable REFs, newly baked immutable
spans, program and texture uploads, HUD/DIRECT payloads and direct fallbacks.
Immutable reader leases establish lifetime, not cache visibility.

The older no-flush arm corrupted 1,271 pixels in a horizon band. It did not
distinguish an unflushed REF writer from a missing ordering barrier. Its
1.09 ms saving belongs to the old submission architecture; it is not a current
gain estimate. Keep flush removal gated by that unresolved source/publication
audit. A parked scene cannot replace cold rebuilds, moving routes, scene/mode
changes, overflow and adversarial dirty-alias checks on PS2.

## Object replay and CALL

Whole-bag baked replay already emits one REF to a VIF stream. That stream is
not a DMA subchain and cannot become a CALL target directly. Literal CALL/RET
would require a different stored format, bounded traversal and return depth,
validation, nested resource leases and TTE ordering support. Current validators
and arenas deliberately reject it. Measure uniform construction, native
conversion and replay misses before adding another format. A roughly
16-qword-plus-CALL reference-title scheme is an efficiency target, not an
established Tyra ABI or a demonstrated improvement over current REF replay.

Acceptance requires exact hardware bytes and pixels, preserved bank/source
ownership and ordinary single-chain behavior, then repeated physical work
**and full-period** measurements with diagnostic guards disabled. PCSX2 speed
does not measure PS2 staging performance or cache correctness.
