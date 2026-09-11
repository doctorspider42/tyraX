.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    fcset   0x000000
    lq      v1, 0(vi00)
    lq      v2, 1(vi00)
    lq      s,  2(vi00)
    lq      m,  3(vi00)
    mul     v1, v1, m
    clipw.xyz v1, v1
    mul     s, s, m
    clipw.xyz s, s
    mul     v2, v2, m
    clipw.xyz v2, v2
    add     s, s, m
    clipw.xyz s, s
    fcand   VI01, 0x3CA3CA
    isw.x   VI01, 4(vi00)
--exit
--endexit
