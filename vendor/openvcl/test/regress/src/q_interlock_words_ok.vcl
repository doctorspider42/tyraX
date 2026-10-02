.syntax new
.name QInterlockWordsOk
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq         f0, 0(vi00)
    lq         f1, 1(vi00)
    lq         f6, 6(vi00)
    lq         f9, 9(vi00)
--barrier
    rsqrt      q, f9[x], f6[x]
    add.xyzw   f10, f1, f0
    add.xyzw   f11, f10, f0
    add.xyzw   f12, f11, f0
    add.xyzw   f13, f12, f0
    add.xyzw   f14, f13, f0
    add.xyzw   f15, f14, f0
    add.xyzw   f16, f15, f0
    add.xyzw   f17, f16, f0
    add.xyzw   f18, f17, f0
    add.xyzw   f19, f18, f0
    add.xyzw   f20, f19, f0
    add.xyzw   f21, f20, f0
    add.xyzw   f22, f21, f0
    add.xyzw   f23, f22, f0
    add.xyzw   f24, f23, f0
    add.xyzw   f25, f24, f0
    add.xyzw   f26, f25, f0
    add.xyzw   f27, f26, f0
    mulq.xyzw  f28, f27, q
    sq         f28, 307(vi00)
--exit
--endexit
