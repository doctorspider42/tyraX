; The same missing edge, shaped so that STOCK openvcl with no flags at all takes
; the bait: the two ACC seeds sit at the bottom of an rsqrt chain, and `madd.z`
; depends on nothing but ACC, so it is the only thing ready while the chain runs.
;
; PASS: every madd sits below both adda writes.
.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq       envConsts, 0(vi00)
    lq       dotR,      1(vi00)
    lq       dotU,      2(vi00)
    mul.xyz  len, envConsts, envConsts
    add.x    len, len, len[y]
    add.x    len, len, len[z]
    rsqrt    q, vf00[w], len[x]
    mul.xyzw nrm, envConsts, q
    add.xy   acc, vf00, nrm[w]
    add.z    acc, vf00, nrm[z]
    madd.x   stq, envConsts, dotR[x]
    madd.y   stq, envConsts, dotU[x]
    madd.z   stq, vf00, vf00[x]
    add.w    stq, vf00, vf00[x]
    sq       stq, 4(vi00)
--exit
--endexit
