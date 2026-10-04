.syntax new
.name QBackEdgeStale
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq         f0, 0(vi00)
    lq         f1, 1(vi00)
    lq         f2, 2(vi00)
    lq         f3, 3(vi00)
    ilw.x      cnt, 4(vi00)
    div        q, f0[x], f1[x]
    mulq.xyzw  f4, f1, q
    sq         f4, 299(vi00)
    add.xyzw   f5, f0, f1
    add.xyzw   f6, f5, f0
    add.xyzw   f7, f6, f0
    add.xyzw   f8, f7, f0
    add.xyzw   f9, f8, f0
    add.xyzw   f10, f9, f0
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
    sq         f22, 298(vi00)
Lloop:
    mulq.zw    f3, f1, q
    sq         f3, 300(vi00)
    add.xyzw   f2, f2, f1
    rsqrt      q, f0[x], f1[x]
    iaddi      cnt, cnt, -1
    ibne       cnt, vi00, Lloop
    sq         f2, 301(vi00)
--exit
--endexit
