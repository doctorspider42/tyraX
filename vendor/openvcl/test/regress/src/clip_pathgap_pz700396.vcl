.syntax new
.name Fz700396
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    fcset      0x000000
    fsset      0x0000
    iaddiu     n0, vi00, 8
    iaddiu     n1, vi00, 24
    iaddiu     n2, vi00, 40
    iaddiu     n3, vi00, 56
    iaddiu     n4, vi00, 72
    iaddiu     n5, vi00, 88
    ilw.x      cnt, 0(vi00)
    iaddiu     cnt, cnt, 3
    lq         f0, 0(vi00)
    lq         f1, 1(vi00)
    lq         f2, 2(vi00)
    lq         f3, 3(vi00)
    lq         f4, 4(vi00)
    lq         f5, 5(vi00)
    lq         f6, 6(vi00)
    lq         f7, 7(vi00)
    lq         f8, 8(vi00)
    lq         f9, 9(vi00)
    lq         f10, 10(vi00)
    lq         f11, 11(vi00)
    lq         f12, 12(vi00)
    lq         f13, 13(vi00)
    rinit      R, f1[w]
    rget.xyzw  f10, R
    rnext.xyzw f5, R
    sq         f10, 301(vi00)
    sq         f5, 302(vi00)
    adda.xz   acc, f13, f5
    ibgtz      n5, Laccskip1
    madda.x    acc, f0, f7
Laccskip1:
    madd.xz   f10, f4, f9
    madd.x    f7, f9, f3
    rinit      R, f1[w]
    rget.xyzw  f7, R
    sq         f7, 303(vi00)
    clipw.xyz  f11, f11
    clipw.xyz  f12, f12
    iblez      n5, Lclipskip2
    clipw.xyz  f11, f11
    clipw.xyz  f8, f8
    clipw.xyz  f7, f7
    fcand      VI01, 0xFFF
    iaddiu     adc, VI01, 0x7FFF
    isw.w      adc, 1(n2)
    sq         f8, 304(vi00)
    sq         f7, 305(vi00)
Lclipskip2:
    fcand      VI01, 0x3F
    iaddiu     adc, VI01, 0x7FFF
    isw.w      adc, 0(n2)
--exit
--endexit
