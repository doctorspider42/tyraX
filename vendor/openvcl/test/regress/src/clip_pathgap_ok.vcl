; CONTROL for clip_pathgap.vcl, and the one that decides whether the pass is a
; measurement or a blanket.  Same labelled CLIP reader, same branch over the same
; block - but the branch is ABOVE the clipw group, so every path into `Ljudge:`
; already runs at least three rows since the newest push.  A pass that pads every
; labelled reader, or every reader it cannot walk back to, costs words here.
.syntax new
.name ClipPathGapOk
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    iaddiu     VI02, vi00, 8
    ilw.x      VI05, 4(vi00)
    lq         VF07, 0(vi00)
    lq         VF08, 1(vi00)
    lq         VF11, 2(vi00)
    lq         VF12, 3(vi00)
    iblez      VI05, Ljudge
    clipw.xyz  VF11, VF11
    clipw.xyz  VF08, VF08
    clipw.xyz  VF07, VF07
    fcand      VI01, 0xFFF
    iaddiu     VI03, VI01, 0x7FFF
    isw.w      VI03, 1(VI02)
Ljudge:
    fcand      VI01, 0x3F
    iaddiu     VI03, VI01, 0x7FFF
    isw.w      VI03, 0(VI02)
--exit
--endexit
