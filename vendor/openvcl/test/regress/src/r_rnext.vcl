.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq      seed, 0(vi00)
    rinit   R, seed[x]
    rnext.xyzw skipped, R
    rnext.xyzw wanted,  R
    sq      wanted, 3(vi00)
--exit
--endexit
