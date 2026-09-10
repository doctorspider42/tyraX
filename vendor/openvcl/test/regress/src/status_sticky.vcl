.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq      a, 0(vi00)
    lq      b, 1(vi00)
    fsset   0x0000
    mul.x   probe, a, b
    fsand   overflowed, 0x0C00
    isw.x   overflowed, 2(vi00)
--exit
--endexit
