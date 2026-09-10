# openvcl minimal repros

Tiny `.vcl` shaders that isolate known openvcl bugs.  Each one
compiles in well under a second, produces text output that's small
enough to read at a glance, and exercises a specific suspect code
path without dragging in a full game engine.

These are *diagnostic tools*, not regression tests.  They live here
so investigators have somewhere to start instead of having to
re-derive the input pattern from a 600-line preprocessed `.vcl`.

## quad_adc_bug.vcl

Historical minimal repro for the ps2gl GL_QUADS rendering bug that was
formerly tracked in the root roadmap.

What it exercises:

- One main loop iteration handling 4 "vertices"
- 4 `clipw.xyz` operations
- The back-face-cull cross product (`mul + opmula + opmsub`)
- The `fmand` reading MAC sign of the cross product
- The `fcand` reading aggregated clip flags (24-bit mask)
- The `ior + iaddiu + mfir.w + sq` chain that writes the ADC bit

Build:

    cd .. && make openvcl
    ./openvcl test/repro/quad_adc_bug.vcl -o /tmp/quad_repro.vsm

Inspect the emitted VSM.  In the full ps2gl build, this pattern
ends up writing `0xFFFF8000` (ADC=1, skip drawing) into the W field
of vertices 3 and 4 of every quad, even when those vertices are in
the view frustum -- making the entire quad invisible.  The expected
W is `0x00007FFF` (ADC=0, draw).

Confirmed via PCSX2 memory inspection at `0x1100D420` and
`0x1100D450` (the v3 / v4 position quadwords in the GIF DMA chain);
see /tmp/vu1_trace/SMOKING_GUN.md.

## Historical investigation questions

1. Is `fcand` returning non-zero because openvcl's clipw chain
   actually produces non-zero clip flags for in-frustum vertices?
   (Compare to Sony's reference, which produces zero clip flags
   for the same input.)
2. Is the order of `clipw.xyz` ops openvcl emits causing the
   CLIP-register history to contain a stale non-zero entry that
   fcand then OR's in?
3. Is the input data (VF07.w threshold, VF08 transformed position)
   correct at the time clipw runs?
4. Would forcing `fcand` to use a 6-bit mask (only the *most
   recent* clipw, not aggregated) work around the bug?  If yes,
   the fix is in how the source's `clip_vert` macro is being
   expanded vs how openvcl schedules it.
