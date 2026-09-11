; The control for loop_pressure_carry: the same loop with two temporaries
; instead of thirty-four, so the alias count stays under the register file and
; the guard is never reached. This program was correct before the fix and must
; stay correct after it - if it ever fails, the change has broken the ordinary
; carried-value path rather than the overflow one.
.syntax new
.name LoopPressureCarryOk
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
    abs.xyzw   carry, c
    sq         carry, 301(vi00)
skip:
    mul.xyzw   b, b, a
    sq         b, 302(vi00)
    iaddi      cnt, cnt, -1
    ibne       cnt, vi00, loop
--exit
--endexit
