.syntax new
.name QInterlockWords
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq         f0, 0(vi00)
    lq         f2, 2(vi00)
    lq         f5, 5(vi00)
    lq         f6, 6(vi00)
    lq         f7, 7(vi00)
    lq         f9, 9(vi00)
    lq         f10, 10(vi00)
    lq         f11, 11(vi00)
    lq         f13, 13(vi00)
    ilw.x      k1, 2(vi00)
    add.xyzw   f8, f5, f0
--barrier
    sqrt       q, f6[x]
    mul.xw   f11, f13, f10
    sub.xw   f0, f7, q
    iblez      k1, Lmtc3
    rsqrt      q, f9[x], f13[x]
    mini.xz   f6, f13, f0[x]
    add.xzw  f11, f6, f8
    mini.y    f6, f7, f2
    mulq.zw   f0, f7, q
    sq         f6, 307(vi00)
    sq         f11, 308(vi00)
    sq         f0, 309(vi00)
Lmtc3:
--exit
--endexit
