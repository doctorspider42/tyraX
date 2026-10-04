; A NON-STICKY reader: 0x0002 is the S bit, which describes the last operation and
; is replaced by every FMAC. So this reader does not just need every contributor
; behind it, it needs `last` to be the one immediately behind it - the accumulating
; class's one concession to the fact that the status register is two registers in
; one.
;
; `probe` and `last` have no register dependency and identical shape, so only the
; last-writer pinning keeps `last` after `probe`.
.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq      a, 0(vi00)
    lq      b, 1(vi00)
    fsset   0x0000
    mul.x   probe, a, b
    mul.y   last,  b, a
    fsand   signBit, 0x0002
    isw.x   signBit, 3(vi00)
--exit
--endexit
