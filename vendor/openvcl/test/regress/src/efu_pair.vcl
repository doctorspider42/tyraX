.syntax new
.name EfuPair
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    fcset      0x000000
    iaddiu     out, vi00, 8
    lq         a, 0(vi00)
    lq         b, 1(vi00)
    lq         lenA, 2(vi00)
    lq         lenB, 3(vi00)
    eleng      p, a
    mfp.x      lenA, p
    sq         lenA, 0(out)
    erleng     p, b
    mfp.x      lenB, p
    sq         lenB, 1(out)
--exit
--endexit
