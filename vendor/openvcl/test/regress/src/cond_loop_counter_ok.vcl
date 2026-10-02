.syntax new
.name Fya
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    fcset      0x000000
    fsset      0x0000
    lq         f0, 0(vi00)
    lq         f1, 1(vi00)
    iaddiu     out, vi00, 40
    iaddiu     n1, vi00, 8
    iaddiu     n2, vi00, 24
    iaddiu     n3, vi00, 40
    iaddiu     k2, vi00, 4
    ilw.x      cnt, 0(vi00)
    iaddiu     cnt, cnt, 3
    ilw.x      k0, 4(vi00)
    iaddiu     k0, k0, 2
Lloop:
    clipw.xyz  f0, f0
    fcand      VI01, 0x3FFFF
    iaddiu     k1, VI01, 2
    ibne       k1, vi00, Ljoin
    iand       n1, n1, n2
    iand       n2, n1, n3
    mtir       n1, f0[w]
Ljoin:
    iaddi      k0, k0, -1
    mul.xyzw   f2, f1, f1
    sq         f2, 301(vi00)
    iaddi      cnt, cnt, -1
    ibgtz      k0, Lloop
    iadd       k2, k2, k0
    isw.x      k2, 1(out)
--exit
--endexit
