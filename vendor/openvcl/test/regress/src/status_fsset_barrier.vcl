; FSSET is the only thing that retires an accumulated status contribution, so it
; is a hard barrier: `before` must stay ABOVE the fsset and `after` BELOW it, and
; the fsand must stay below both.
;
; If the barrier is missing, `before`'s sticky bits survive the clear and the
; fsand reports a judgement from the wrong side of it. The two muls are otherwise
; interchangeable - same shape, dead destinations, no register dependency between
; them - so nothing but the barrier keeps them apart.
;
; Before the fix: fsset/fsand/mul are declared as touching nothing at all, so the
; scheduler is free to emit them in any order and --drop-dead-writes deletes both
; muls.
.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq      a, 0(vi00)
    lq      b, 1(vi00)
    mul.x   before, a, b
    fsset   0x0000
    mul.y   after,  a, b
    fsand   sticky, 0x0C00
    isw.x   sticky, 3(vi00)
--exit
--endexit
