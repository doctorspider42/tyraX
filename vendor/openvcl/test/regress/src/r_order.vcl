.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq      seed, 0(vi00)
    lq      m0,   4(vi00)
    lq      m1,   5(vi00)
    rinit   R, seed[x]
    mul     heavy, m0, m1
    rnext.xyzw first,  R
    rget.xyzw  second, R
    add     heavy, heavy, m0
    sq      first,  3(vi00)
    sq      second, 6(vi00)
    sq      heavy,  7(vi00)
--exit
--endexit
