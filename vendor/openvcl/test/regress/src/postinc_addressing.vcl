.name PostincAddressing
.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    xtop    base
    iaddiu  ptr, base, 4
    lqi     v0, (ptr++)
    lqi     v1, (ptr++)
    sub     v2, v0, v1
    sq      v2, 8(base)
    isw.x   ptr, 9(base)
--exit
--endexit
