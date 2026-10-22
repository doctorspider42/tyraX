# Linked VBlank Wait and Submission Dependency Audit

This audit binds the current A40 ELF's actual SDK VSync instructions and ordered
frame source dependencies; it is not a new physical timing experiment.

The prior goal turn made progress: A40 closes both correctness targets and its
first physical order, while Burnout metadata corroborates a Hybrid2-like layout.
This step tests whether a naive freshness change is a valid next candidate.
Review A38, rejected A39, existing frame-pacing.md and Burnout buffer recheck first.

`tools/tyrax2-vblank-sdk-audit.py` validates all492 bound sources and exact `.text`
equality between stripped ELF and `.elf.sym`. `graph_wait_vsync` at0x1017d8 has
14 exact linked instructions: CSR is read, its VSINT bit combined with a second
CSR read and written back, then VSINT is polled until newly set. The SDK starts
a fresh wait rather than consuming an arbitrary previously latched edge.
Four changed-instruction controls reject. Thirty-two host low-event-register
cases model write-one-to-clear behavior; these are NOT target timing samples.
The OR/write also acknowledges other low event bits already seen; existing
completion consumes the exclusive old FINISH first. Do not move the wait across
that handshake or assume CSR OR is a selective write of VSINT alone.

Two histories expose identical VSINT=8: one is still inside blank, one is already
scanning. The bit alone cannot tell them apart. Therefore replacing the SDK wait
with `if (!(CSR & 8)) wait` is NOT a safe candidate: it may copy into the displayed
target during scanout, reuse an old event or skip more than one field. No proof
here establishes active blank or a sufficient remaining conversion-copy budget.

## Bound Current Frame Ordering

1. N is finalized with its sole chain FINISH and submitted. EE starts N+1 using
   another arena/native bank while N runs on VU/GS.
2. N+1 recording can remain deferred through its frame end, or reach an earlier
   real barrier/fallback. Before its next real submission, clear its reserved
   nativePendingSequence and call completeBeforeSubmission with recording/bank/
   captured-HUD protection. The callback must not submit its own current prefix.
3. Complete N's VIF sequence, consume N's exclusive GS FINISH. In Hybrid2 wait
   for NEW VBlank, send the CT32-to-CT16 copy/restore and keep the display address
   fixed. The following submission's GIF fence protects this asynchronous copy.
4. Submit N+1 only after prior presentation protection. It may clear/reuse the
   fixed draw surface, so it cannot simply overtake N's source copy.
5. Hybrid3 copies to an unscanned output, completes its copy/restore FINISH and
   arms ISR publication. The extra output target decouples this queue from scanout.

This confirms overlapping EE preparation already exists. It does NOT prove that
all CPU time is avoidable, or that the display-copy dependency alone explains
A38. An old job may become ready before EE reaches its callback; source shows
the possibility, not the number of physically avoidable missed deadlines.
Mean non-pacing work mixes owners and cannot supply that number either.

## Next Architectural Candidate Boundary

Different from A39's rejected per-Core early-ready hook under Hybrid3: investigate
independent VBlank-start submission of a PREBUILT, immutable Hybrid2 output copy,
with no renderer/callback/allocation/cache construction in interrupt context.
Same two colour targets and existing conversion quality; no extra texture reserve.
This is a proposed intervention, NOT implemented or performance-qualified here.

The subsequent [protocol component](../tyrax2-hybrid-copy-protocol-2026-10-11/README.md)
passes host schedule/guard/lifecycle controls and EE object compilation. It does
not yet implement the actual Core/queue/MMIO adapter or prove physical timing.

Before target implementation, prove an explicit single-old-job protocol:
arm only after the exact old chain has actually started and its FINISH is owned;
ISR may claim only when old VIF DMA/final FINISH AND GIF channel are ready;
withdraw unclaimed work atomically before the main fallback consumes FINISH;
never issue a duplicate copy, wait in ISR or let N+1 clear a copy source early.
Keep the immutable packet/cache/source alive through actual copy DMA/GS completion.
ISR must not touch the current EE recording context, pending prefix, arena or HUD
fence. Main completion must preserve those owners and serialize N+1 at the SAME
copy completion fence. Do not reuse A39's renderer-state-mutating callback in ISR.

Require positive old-job/real-copy activation, ordinary fallback/lifecycle cases,
both exact source/main/env gates and same-ELF Hybrid2-only Off/On/Off plus reverse
physical net. Also qualify display-copy completion within blank: completed drawing
readbacks alone cannot detect scanout tearing. Failure must preserve baseline,
not remove a fence or call a partial frame60. No production change/default yet.

Reproduce source/binary audit against the original private bound fixture:

```powershell
python tools/tyrax2-vblank-sdk-audit.py F:/Projects/tyrax2-paint-triple-20261011-v1 --output F:/Projects/vblank-sdk-audit-replay.json
```

Initial helper selected the start marker as its own function end, failing before
acceptance; repaired end search begins after the start marker. The passing JSON
pins the tool/ELF and all model limits. No console launch/reset in this step.
