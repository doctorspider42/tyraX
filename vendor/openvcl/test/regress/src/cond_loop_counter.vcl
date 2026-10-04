.syntax new
.name Fz710305
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    ilw.x      k0, 1(vi00)
    ilw.x      k1, 2(vi00)
    ilw.x      k2, 3(vi00)
    ilw.x      k3, 4(vi00)
Llcnt1:
    ibne       k1, vi00, Lclipcond2
Lclipcond2:
    iaddi      k0, k0, -1
    ibgtz      k0, Llcnt1
    ibne       k3, k0, Lconly5
    iadd       k0, k0, k3
    iaddiu     k0, k0, 22
Lconly5:
    iadd       k2, k2, k0
--exit
--endexit
