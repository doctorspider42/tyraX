.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq       v1, 0(vi00)
    lq       v2, 1(vi00)
    lq       v3, 2(vi00)
    clipw.xyz v1, v1
    clipw.xyz v2, v2
    clipw.xyz v3, v3
--exit
    out_hw_clip clip
--endexit
