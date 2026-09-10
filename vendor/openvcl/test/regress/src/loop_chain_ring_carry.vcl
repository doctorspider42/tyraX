.syntax new
.name LoopChainRingCarry
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    ilw.x      cnt, 0(vi00)
    ilw.x      k1, 2(vi00)
    ilw.x      k2, 3(vi00)
    ilw.x      k3, 4(vi00)
Lloop1:
    iadd       k1, k1, k3
    ilw.x      k3, 2(vi00)
    iaddiu     k3, k3, 2
Lno3:
Lni4:
    iaddi      k1, k1, -1
    ibgtz      k1, Lni4
    iaddi      k3, k3, -1
    ibgtz      k3, Lno3
    iaddi      cnt, cnt, -1
    ibne       cnt, vi00, Lloop1
    isub       k2, k2, k3
    isw.x      k2, 0(vi00)
--exit
--endexit
