.syntax new
.name Fvb
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    fcset      0x000000
    lq         f2, 2(vi00)
    lq         f9, 9(vi00)
    iaddiu     out, vi00, 40
    ilw.x      k1, 2(vi00)
    clipw.xyz  f9, f9
    fcor       VI01, 0x2
    ibne       VI01, vi00, Lskip
    mul.xyzw   f2, f2, f9
    sq         f2, 0(out)
Lskip:
    isw.x      k1, 2(out)
    ilw.x      k1, 3(vi00)
Lloop:
    ibgtz      k1, Lloop
    sq         f9, 1(out)
--exit
--endexit
