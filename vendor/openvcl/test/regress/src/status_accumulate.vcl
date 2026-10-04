; Two contributors and one STICKY-ONLY reader (0x0C00 = IS|DS).
;
; Both `mul` results are dead as VF values, so --drop-dead-writes will offer to
; delete them; both contribute to the sticky half the fsand reads, so neither may
; go, and both must be scheduled ABOVE the fsand. Their order relative to each
; other is free - that is what "accumulating" buys.
;
; Before the fix: no edge and no observability check, so BOTH muls are deleted.
.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq      a, 0(vi00)
    lq      b, 1(vi00)
    lq      c, 2(vi00)
    fsset   0x0000
    mul.x   deadA, a, b
    mul.y   deadB, b, c
    fsand   sticky, 0x0C00
    isw.x   sticky, 3(vi00)
--exit
--endexit
