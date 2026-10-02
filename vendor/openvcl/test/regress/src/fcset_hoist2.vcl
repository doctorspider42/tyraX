.syntax new
.name FcsetHoist2
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    fcset      0x000000
    iaddiu     out, vi00, 8
    lq         v1, 0(vi00)
    lq         v2, 1(vi00)
    lq         v3, 2(vi00)
    lq         v4, 3(vi00)
    lq         v5, 4(vi00)
    clipw.xyz  v1, v1
    clipw.xyz  v2, v2
    clipw.xyz  v3, v3
    fcor       VI01, 0x3CA3CA
    iaddiu     judgeA, VI01, 0x7FFF
    isw.w      judgeA, 0(out)
    fcset      0x00000000
    fcand      VI01, 0x3FFFF
    isw.x      VI01, 1(out)
    clipw.xyz  v4, v4
    clipw.xyz  v5, v5
    fcor       VI01, 0x3F
    iaddiu     judgeB, VI01, 0x7FFF
    isw.w      judgeB, 2(out)
--exit
--endexit
