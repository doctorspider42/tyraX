.syntax new
.name MacLatency
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    fcset      0x000000
    fsset      0x0000
    iaddiu     out, vi00, 8
    lq         a, 0(vi00)
    lq         b, 1(vi00)
    lq         c, 2(vi00)
    lq         d, 3(vi00)
    iaddiu     msk, vi00, 15
    mul.xyzw   probe1, a, b
    mul.xyzw   probe2, c, d
    fmand      flags, msk
    isw.y      flags, 0(out)
    sq         probe1, 1(out)
    sq         probe2, 2(out)
--exit
--endexit
