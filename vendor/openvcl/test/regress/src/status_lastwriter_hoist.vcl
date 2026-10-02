; The last-writer pinning, on a shape where the scheduler WANTS to break it.
;
; `probe` sits at the bottom of a four-deep chain and is the last thing ready;
; `last` depends on nothing but two loads and is ready immediately. A list
; scheduler with no reason to keep them apart puts `last` early, where it fills
; the chain's latency, and then the fsand reads `probe`'s flags.
;
; The mask is 0x0002 - the S bit, non-sticky, "the sign of the LAST operation" -
; so `last` has to be the instruction immediately in front of the reader.
;
; Compare with the same program at mask 0x0C00 (sticky only), where the scheduler
; is free to hoist `last` and the accumulating class lets it: that pair is what
; the last-writer pinning costs, measured.
.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq      a, 0(vi00)
    lq      b, 1(vi00)
    fsset   0x0000
    mul.xyzw c1, a, b
    mul.xyzw c2, c1, b
    mul.xyzw c3, c2, b
    mul.xyzw probe, c3, b
    mul.xyzw last, b, a
    fsand   signBit, 0x0002
    isw.x   signBit, 3(vi00)
    sq      probe, 4(vi00)
    sq      last,  5(vi00)
--exit
--endexit
