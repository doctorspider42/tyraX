.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq       a,   0(vi00)
    lq       b,   1(vi00)
    lq       c,   2(vi00)
    mula.x   acc, a, b
    mula.yzw acc, b, c
    madd     out, a, c
    sq       out, 3(vi00)
--exit
--endexit
