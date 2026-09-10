.name ClipLoopWindow
.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    xtop    base
    fcset   0x000000
    lq      va, 0(base)
    lq      vb, 1(base)
    lq      vc, 2(base)
    lq      vd, 3(base)
    ilw.x   sel, 30(base)
    iaddiu  cnt, vi00, 3
loop:
    clipw.xyz vc, vc
    fceq    VI01, 0x3CA3CA
    iaddiu  adc, VI01, 1
    isw.x   adc, 20(base)
    ibeq    sel, vi00, tail
    add     vd, vd, vd
    sq      vd, 21(base)
tail:
    mul     va, va, vd
    clipw.xyz va, va
    clipw.xyz vb, vb
    iaddi   cnt, cnt, -1
    ibne    cnt, vi00, loop
--exit
--endexit
