; Two Q stages inside a --LoopCS loop, a carried FLOAT read at the top and
; written at the bottom, and 12 short-lived float temporaries. The guard in
; extendMultiQStageRange counts every float alias OVERLAPPING the loop against
; the allocatable pool but extends only the Q-stage aliases, so the temporaries
; can push the count over and cost the carried name its range.
.syntax new
.name qstage_bail
.vu
.init_vf vf01-vf08
.init_vi_all
--enter
--endenter
    iaddiu     ctr, vi00, 4
    iaddiu     out, vi00, 300
    add        carried, vf00, vf00[w]
    add        den1, vf00, vf00[w]
    add        den2, vf00, vf00[w]
Lloop:
    --LoopCS   1, 1
    sq         carried, 40(out)
    div        q, vf00[w], den1[w]
    mulq       stage1, carried, q
    div        q, vf00[w], den2[w]
    mulq       stage2, stage1, q
    sq         stage2, 41(out)
    add        f00, vf00, vf00[w]
    sq         f00, 0(out)
    add        f01, vf00, vf00[w]
    sq         f01, 1(out)
    add        f02, vf00, vf00[w]
    sq         f02, 2(out)
    add        f03, vf00, vf00[w]
    sq         f03, 3(out)
    add        f04, vf00, vf00[w]
    sq         f04, 4(out)
    add        f05, vf00, vf00[w]
    sq         f05, 5(out)
    add        f06, vf00, vf00[w]
    sq         f06, 6(out)
    add        f07, vf00, vf00[w]
    sq         f07, 7(out)
    add        f08, vf00, vf00[w]
    sq         f08, 8(out)
    add        f09, vf00, vf00[w]
    sq         f09, 9(out)
    add        f10, vf00, vf00[w]
    sq         f10, 10(out)
    add        f11, vf00, vf00[w]
    sq         f11, 11(out)
    add        carried, stage2, stage2
    iaddi      ctr, ctr, -1
    ibne       ctr, vi00, Lloop
--exit
--endexit
