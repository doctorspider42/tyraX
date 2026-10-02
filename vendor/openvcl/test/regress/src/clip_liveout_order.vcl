; clip_liveout.vcl showed that `out_hw_clip` keeps the three clipw alive. This
; shows the other half: that keeping them is not enough, because the SCHEDULER
; decided CLIP was dead from in-program readers alone and was therefore free to
; reorder them.
;
; The CLIP register is a 24-bit window of the last four 6-bit judgements, so which
; push landed in which entry is the whole content of the value this program hands
; on. `v1` is at the bottom of a four-deep chain and is the last thing ready; `v2`
; and `v3` are ready immediately. A list scheduler with no edge between the pushes
; issues them in readiness order - v2, v3, then v1 - and the successor reads the
; three vertices' judgements in the wrong window positions.
;
; No `fcand` anywhere on purpose: the declaration is the only thing saying CLIP is
; live, which is exactly the case vuIgnoredFlagWawResourcesForRemaining() got
; wrong.
;
; Expect after the fix: clipw for v1, then v2, then v3, in source order.
.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq       v1, 0(vi00)
    lq       v2, 1(vi00)
    lq       v3, 2(vi00)
    lq       m,  3(vi00)
    mul.xyzw v1, v1, m
    mul.xyzw v1, v1, m
    mul.xyzw v1, v1, m
    mul.xyzw v1, v1, m
    clipw.xyz v1, v1
    clipw.xyz v2, v2
    clipw.xyz v3, v3
--exit
    out_hw_clip clip
--endexit
