; THE CONTROL for branch_backedge_cond. Same loop, same delay-slot integer op,
; same conditional branch as the first row of the loop head - but the branch tests
; a register nothing in the loop writes, so no path brings a producer next to it
; and no bubble is owed.
;
; It is a MAXCOUNT case, and that is the point: every value oracle in this suite is
; satisfied by an emitter that waits for everything, so a fix that padded the head
; of every loop would pass the reproducer and pass all four value kinds. This is
; the assertion that costs it something. The bound is the emitted nop count of the
; build that carries the fix; the shipped build emits the same number, so this case
; passes on both and only a compiler that started padding unconditionally fails it.
.name BRBACKEDGECONDOK
.syntax new
.init_vf_all
.init_vi_all
--enter
--endenter
    ilw.x   enabled,  8(vi00)
    iaddiu  planePtr, vi00, 0
    iaddiu  planeEnd, vi00, 6
    iaddiu  mask,     vi00, 0x0f00
    lq      total,    32(vi00)
planeLoop:
    ibltz   enabled, planeActive
    b       planeAdvance
planeActive:
    lq      plane, 0(planePtr)
    add     total, total, plane
    sq      total, 40(vi00)
planeAdvance:
    iaddiu  planePtr, planePtr, 1
    iadd    mask,     mask,     mask
    ibne    planePtr, planeEnd, planeLoop
    sq      total, 41(vi00)
    isw.x   mask,  42(vi00)
--exit
--endexit
