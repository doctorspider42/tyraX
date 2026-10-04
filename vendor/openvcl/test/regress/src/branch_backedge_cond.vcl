; A loop whose HEAD is a conditional branch on a register the back edge's DELAY
; SLOT rewrites. Reduced from main's #218 clip programs, which walk the frustum
; planes with a per-package bitmask:
;
;     planeLoop:  ibltz  buffer, planeActive        <- first row of the block
;     planeAdvance: ... ibne planePtr, end, planeLoop
;                       iadd buffer, buffer, buffer <- delay slot shifts the mask
;
; --emit-delay-fillers puts the shift in the delay slot (Sony's vcl does the same),
; and --branch-bubble-on-dependency then asks "does the row above the branch
; produce its condition?" in FILE order, where the answer is no. Along the taken
; edge the shift is the row immediately before the branch, so every iteration after
; the first tests the PREVIOUS plane's bit.
;
; Nothing about the values changes, which is why pa-dag, pb-dag and pd-cond all
; call this program correct. The assertion is a path DISTANCE.
.name BRBACKEDGECOND
.syntax new
.init_vf_all
.init_vi_all
--enter
--endenter
    iaddiu  mask,     vi00, 0x0f00
    iaddiu  planePtr, vi00, 0
    iaddiu  planeEnd, vi00, 6
    lq      total,    32(vi00)
planeLoop:
    ibltz   mask, planeActive
    b       planeAdvance
planeActive:
    lq      plane, 0(planePtr)
    add     total, total, plane
    sq      total, 40(vi00)
planeAdvance:
    iaddiu  planePtr, planePtr, 1
    iadd    mask,     mask,     mask
    ibne    planePtr, planeEnd, planeLoop
    sq      total, 41(vi00)
--exit
--endexit
