.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq      seed, 0(vi00)
    lq      salt, 1(vi00)
    lq      pepper, 2(vi00)
    rinit   R, seed[x]
    rxor    R, salt[x]
    rxor    R, pepper[x]
    rget.xyzw out, R
    sq      out, 3(vi00)
--exit
--endexit
