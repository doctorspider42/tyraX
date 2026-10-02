; The control for q_branch_gap: the same labelled Q reader entered by a forward
; branch, with enough independent work between the division and the BRANCH that
; no edge shortens the distance below FDIV's latency.  Nothing here needs a wait
; on any path, and the emitter emits none.
;
; It is asserted as MAXCOUNT waitq:0, not as a value check, because a value
; check cannot fail on a compiler that has become too conservative - and "insert
; a wait at every branch target that has a division outstanding" satisfies
; q_branch_gap while costing an instruction in every loop the engine has.  This
; is the case that says the fix looked at the distance.
.syntax new
.name QBranchGapOk
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
    lq         f5, 5(vi00)
    div        q, f0[x], f1[y]
    sq         f0, 310(vi00)
    sq         f1, 311(vi00)
    sq         f2, 312(vi00)
    sq         f3, 313(vi00)
    sq         f4, 314(vi00)
    sq         f5, 315(vi00)
    sq         f0, 316(vi00)
    sq         f1, 317(vi00)
    sq         f2, 318(vi00)
    sq         f3, 319(vi00)
    ibgez      n0, Lskip
    sq         f4, 320(vi00)
Lskip:
    mulq.w     f2, f1, q
    sq         f2, 302(vi00)
--exit
--endexit
