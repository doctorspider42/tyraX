; A SECOND reader of the same implicit resource gets no edge to the writers the
; FIRST one consumed, because addPreciseImplicitFlagDependencies() flushes its
; pending-writer list at every reader. Here that is the engine's environment-map
; seed, reduced: two partial ACC writes feed three madd readers, and only the
; first madd is edged to them.
;
; `madd.z` reads nothing but ACC (vf00 is zero), so it is ready immediately and a
; list scheduler puts it early - above the seed, where it picks up whatever the
; PREVIOUS chain left in ACC.z. The `mula.xyz`/`madd.xyz` pair above is that
; previous chain, and it writes z, so the corruption is silent and numeric.
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
    lq       scale,     3(vi00)
    mula.xyz acc, scale, vf00[w]
    madd.xyz probe, scale, scale
    add.w    probe, vf00, vf00[x]
    add.xy   acc, vf00, envConsts[w]
    add.z    acc, vf00, envConsts[z]
    madd.x   stq, envConsts, dotR[x]
    madd.y   stq, envConsts, dotU[x]
    madd.z   stq, vf00, vf00[x]
    add.w    stq, vf00, vf00[x]
    sq       stq,   4(vi00)
    sq       probe, 5(vi00)
--exit
--endexit
