# A38 Reversed Physical Result

Native491 bound files, same ELF on both targets:
`6f6ea95f06d287baacba28d06b8ddc80ebf875b765b389076979205a067daae3`.
Only moving mode selection/restoration in engine.cpp and generated physics
differs from V1. Both targets finish six moving3/2/3 trips, held2/3/2 unchanged,
36 exactly equal held main/env rasters each and14 rejecting reader controls.
Roads, three vehicles, live reflections, night lights and original head writer
remain qualified. Real VRAM relocation, pressure and Hybrid FINISH included.

## Physical Display Hand-Offs

| Trip | Buffers | EntryHz | DISPFB registerHz | Intervals >25ms |
|---|---|---|---|---|
| Day candidate before |3|58.841|58.818|10/524|
| Day control |2|48.733|48.836|98/431|
| Day candidate return |3|58.838|58.815|10/523|
| Night candidate before |3|55.171|55.104|43/490|
| Night control |2|40.681|40.832|168/359|
| Night candidate return |3|54.946|54.889|45/489|

Both triple endpoints beat the middle double control: day+9.980..9.982,
night+14.057..14.272 registerHz. Candidate day p9516.687ms, night33.366ms;
double references33.367ms. Both reverse net gates PASS, BOTH stable60 FAIL.
Non-pacing work means14.163/14.019/14.214 day,16.309/16.122/16.409 night:
the gain is presentation pacing, not a new faster packet-preparation routine.
DISPFB programming events are hardware hand-offs, NOT photon/input-latency
measurements; first/last two boundary intervals excluded equally.

## Evidence / Lifecycle

603-file immutable inventory. This README is outside the inventory. Copied
both-target readers replay reports byte-identically outside this archive.
Physical report SHA256
`111c18ec9677c10c1ae139fe1befc5a567664e8cc4246959b75c595e841a66cb`;
emulator `5993adf6de6e56cf055b7499298ba7fd5fe1c3fd98ae82ca1585cb8681ff3370`.
The archive omits full staged assets with their recorded hashes; not a runnable
asset mirror. First-order V1 is preserved unchanged.

Editor network reset succeeds, owned client6428 stopped after DRIVEDONE/export.
Owned hidden emulator26/Xvfb15 also stopped after completion. Devices stay ON.
An initial process-stop identity guard rejected localized DateTime reparsing;
direct typed DateTime comparison confirmed the same launch/PID before stopping.
No retry/reset or target change resulted. Both final readers use closed logs.

Triple pacing is now supported in BOTH moving orders, but no production default
is changed. Normal production integration/boot remains separate from this private
common A31/A30 baseline. Next audit/try-ready-present must preserve the unsubmitted
current prefix, previous DMA/GS FINISH and free Hybrid display ownership.
