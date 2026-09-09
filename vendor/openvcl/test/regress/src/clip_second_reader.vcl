; The same flush, on CLIP. `fcand outer` is the second reader of the window, so
; it has no edge to the clipw at all and may be scheduled above it - where it
; judges the PREVIOUS vertex.
;
; The two readers deliberately write DIFFERENT integer registers. In the engine's
; clip programs the pair `fcand VI01,0x2` / `fcand VI01,0x8` share one, and the
; register WAW between them is the only thing that has been holding the second
; one down: an accident, not a model.
;
; PASS: both fcand sit below the clipw.
.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq       v, 0(vi00)
    lq       m, 1(vi00)
    mul.xyzw scaled, v, m
    clipw.xyz scaled, scaled
    fcand    inner, 0x2
    fcand    outer, 0x8
    isw.x    inner, 4(vi00)
    isw.x    outer, 5(vi00)
--exit
--endexit
