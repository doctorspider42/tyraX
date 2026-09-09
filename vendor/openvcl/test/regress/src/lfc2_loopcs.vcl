.syntax new
.name LoopFieldCarry2
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
    mul.w      carry, c, b[w]
    mul.y      out, b, carry[z]
    sq         out, 300(vi00)
    abs.xyzw   carry, c
    sq         carry, 301(vi00)
skip:
    mul.xyzw   b, b, a
    sq         b, 302(vi00)
    iaddi      cnt, cnt, -1
    ibne       cnt, vi00, loop
--exit
--endexit
