# Motor District packed submission

This capture measures four draw paths in the same Motor District debug ELF
on a physical PS2 in NTSC progressive mode. ELF SHA-256:

`8604bcb51d97bec68bc5be8faeeecf896841dabda51bfd6d984c8484a4197ec2`

The fixture repeats a camera sweep from the parked start. The sweep moves
the camera and its target up to 44 world units along the starting heading
and returns both to the start over 120 frames. A scratch copy of the game
keeps quantized copies of eligible authored mesh draws, indexed by source
addresses and stream versions, alongside the original streams. It warms
up for 480 frames, then skips the first 20 frames of each block. The
fixture uses the game's existing submission scopes for eligible draws.

Arm numbers are 0 for ordinary draws, 1 for quantized EE decoding, 2 for
the original VU1 submission, and 3 for VU1 submission with batching and
retained commands. The last two arms use the same quantized meshes and
decoder. `StaPipCore::setPackedSubmissionOptimizations` switches between
them at the frame boundary.

[motor-district.tsv](motor-district.tsv) contains 100 rows per block in
frame order. The interval is measured between consecutive loop samples
with COP0 Count at 294,912 ticks per millisecond. Subtract `stall_ticks`,
the renderer's recorded stalls in that interval, to compute frame work.
`selected_vertices` counts draws for which the fixture has an eligible
quantized copy, before package culling. `candidate_vertices` counts the
draw requests examined by the fixture after its initial stream checks.

[Results and interpretation](../../../../../docs/procedural-runtime.md#packed-submission-batching-and-retention)
