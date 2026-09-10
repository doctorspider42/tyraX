; A division whose consumer sits at a LABEL, with a forward branch that jumps
; over every row between them.
;
; The emitter clocks blocks in file order and carries its latency tracker along
; that order, so it measures the distance from `div` to `mulq` down the file -
; twelve rows, comfortably past FDIV's seven-cycle latency - and emits no wait.
; Along the taken edge the only rows between them are the branch and its delay
; slot, six cycles, and the `mulq` reads the PREVIOUS quotient.  Nothing on the
; hardware interlocks Q; `waitq` is the instruction that would, and there is
; none.
;
; Sony's vcl puts the reader exactly seven cycles after the div on the taken
; edge, which is what says seven is the number and not a modelling choice.
.syntax new
.name QBranchGap
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    iaddiu     n0, vi00, 8
    lq         f0, 0(vi00)
    lq         f1, 1(vi00)
    lq         f2, 2(vi00)
    lq         f3, 3(vi00)
    lq         f4, 4(vi00)
    div        q, f0[x], f1[y]
    ibgez      n0, Lskip
    mul.xyzw   f3, f3, f4
    add.xyzw   f4, f3, f0
    sub.xyzw   f3, f4, f1
    mul.xyzw   f4, f3, f2
    add.xyzw   f3, f4, f0
    sq         f3, 300(vi00)
    sq         f4, 301(vi00)
Lskip:
    mulq.w     f2, f1, q
    sq         f2, 302(vi00)
--exit
--endexit
