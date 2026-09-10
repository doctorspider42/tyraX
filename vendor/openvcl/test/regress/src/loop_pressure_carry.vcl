; A --LoopCS loop with THIRTY-FOUR independent float temporaries in the body and
; one carried name: `carry` is written at the bottom of the body (abs.xyzw) and
; read at the top (mul.y out, b, carry[z]), so nothing but the loop-carried range
; extension keeps its register away from a temporary.
;
; extendLoopDirectiveRange used to give up on the whole loop when the number of
; aliases with a range OVERLAPPING it exceeded the register file - here 41 floats
; against 31 - and the set it counted is not the set it extends: the temporaries
; are never extended and cost nothing, while the carried name is. So the guard
; fired because of the temporaries and dropped the extension for `carry`.
;
; Compiled WITHOUT --loop-liveness-always, because that flag is what skips the
; guard: with it on, this program was correct all along. Without it, and without
; a --LoopCS on the target, extendLoopDirectiveRange is not reached at all, which
; is why no corpus in this effort ever exercised the guard - it fired 387 times
; on the 70 real microprograms WITH the flag, where it is disabled, and zero
; times without it.
.syntax new
.name LoopPressureCarry
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    iaddiu     n0, vi00, 8
    iaddiu     n1, vi00, 16
    ilw.x      cnt, 0(vi00)
    iaddiu     cnt, cnt, 3
    lq         a, 0(vi00)
    lq         b, 1(vi00)
    lq         c, 2(vi00)
    lq         out, 3(vi00)
    ftoi4.xyzw carry, a
loop:
    --LoopCS   1, 1
    ibltz      n1, skip
    mul.y      out, b, carry[z]
    sq         out, 300(vi00)
    mul.xyzw   f00, a, b
    sq         f00, 400(vi00)
    mul.xyzw   f01, a, b
    sq         f01, 401(vi00)
    mul.xyzw   f02, a, b
    sq         f02, 402(vi00)
    mul.xyzw   f03, a, b
    sq         f03, 403(vi00)
    mul.xyzw   f04, a, b
    sq         f04, 404(vi00)
    mul.xyzw   f05, a, b
    sq         f05, 405(vi00)
    mul.xyzw   f06, a, b
    sq         f06, 406(vi00)
    mul.xyzw   f07, a, b
    sq         f07, 407(vi00)
    mul.xyzw   f08, a, b
    sq         f08, 408(vi00)
    mul.xyzw   f09, a, b
    sq         f09, 409(vi00)
    mul.xyzw   f10, a, b
    sq         f10, 410(vi00)
    mul.xyzw   f11, a, b
    sq         f11, 411(vi00)
    mul.xyzw   f12, a, b
    sq         f12, 412(vi00)
    mul.xyzw   f13, a, b
    sq         f13, 413(vi00)
    mul.xyzw   f14, a, b
    sq         f14, 414(vi00)
    mul.xyzw   f15, a, b
    sq         f15, 415(vi00)
    mul.xyzw   f16, a, b
    sq         f16, 416(vi00)
    mul.xyzw   f17, a, b
    sq         f17, 417(vi00)
    mul.xyzw   f18, a, b
    sq         f18, 418(vi00)
    mul.xyzw   f19, a, b
    sq         f19, 419(vi00)
    mul.xyzw   f20, a, b
    sq         f20, 420(vi00)
    mul.xyzw   f21, a, b
    sq         f21, 421(vi00)
    mul.xyzw   f22, a, b
    sq         f22, 422(vi00)
    mul.xyzw   f23, a, b
    sq         f23, 423(vi00)
    mul.xyzw   f24, a, b
    sq         f24, 424(vi00)
    mul.xyzw   f25, a, b
    sq         f25, 425(vi00)
    mul.xyzw   f26, a, b
    sq         f26, 426(vi00)
    mul.xyzw   f27, a, b
    sq         f27, 427(vi00)
    mul.xyzw   f28, a, b
    sq         f28, 428(vi00)
    mul.xyzw   f29, a, b
    sq         f29, 429(vi00)
    mul.xyzw   f30, a, b
    sq         f30, 430(vi00)
    mul.xyzw   f31, a, b
    sq         f31, 431(vi00)
    mul.xyzw   f32, a, b
    sq         f32, 432(vi00)
    mul.xyzw   f33, a, b
    sq         f33, 433(vi00)
    abs.xyzw   carry, c
    sq         carry, 301(vi00)
skip:
    mul.xyzw   b, b, a
    sq         b, 302(vi00)
    iaddi      cnt, cnt, -1
    ibne       cnt, vi00, loop
--exit
--endexit
