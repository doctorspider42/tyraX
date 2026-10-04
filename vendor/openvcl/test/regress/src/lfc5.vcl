; Adjacent to lfc2, and a different question: the first in-loop access to `carry`
; is a FULL write, but it sits inside a conditional block, so along the taken
; path the read below the join still sees the PREVIOUS iteration's value.
; File-order live-in classification calls that "defined in the loop" too.
.syntax new
.name LoopCondFullWrite
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
    ibltz      n1, skip
    abs.xyzw   carry, c
    sq         carry, 301(vi00)
skip:
    mul.y      out, b, carry[z]
    sq         out, 300(vi00)
    mul.xyzw   b, b, a
    sq         b, 302(vi00)
    iaddi      cnt, cnt, -1
    ibne       cnt, vi00, loop
--exit
--endexit
