# Actual V22 / V5 TC artifact review — budget rejected

The root native command exited 0 and linked the exact 4096-byte / 512-word TC
image `46e21d2395ceff6d8fb91a40fb4b002d589584018301ec433e5f0eb79a0a34ab`.
Actual assembled object symbol bytes equal the linked ELF symbol bytes. Frozen
500 source files and 491 mirrored files were checked before preservation; source
TC and actual mirrored TC bytes match. Preserved `.o`, expanded `.vcl`, scheduled
`.vsm`, linked image, source/includes and root provenance/log have hashes in
`proof.json`. No build or shared-cache mutation was performed.

**REJECT:** VU1Clip main1840 + unchanged billboards206 = **2046**, exceeding
draw-finish2042 by4. EEClip main1640 + billboards206 =1846. Main-only fit does
not qualify this candidate. No image eviction, target execution or native target
acceptance is claimed.

## Concrete scheduled assembly observations

- Image entry initializes ADC mask VI02 to0x8000, loads real single-color mode
  VI03 and spot mode VI04. Persistent fog VF01, MVP VF02..05 and spot VF06..08
  are not scratch registers of the reduction. The postpass uses VI05/07/08 as
  scratch; buffer VI05 is reacquired by XT0P at the next package.
- At vertexLoopsDone, the marker is masked before reduction. The marker-absent
  branch goes directly to coronaReturn, with its delay-slot count-mask setup
  affecting scratch only. Marker-present count is reloaded/masked into VI01.
  Three times that count is subtracted from final output address VI14 to recover
  the original first output; kick VI13 stays unchanged.
- Each quad retains first final RGBA in VF16, final XYZF2 in VF19 and STQ in
  VF30. Six-corner loop VI12 starts6, decrements once and advances sourceVI11 by3
  in its branch delay slot. Package remaining count VI09 independently decreases
  by6 and sourceVI06 advances by18 in its branch delay slot.
- Full final RGBA equality uses subtraction VF18.xyzw and zero mask15; final
  Z/F/ADC equality uses VF18.zw and mask3; final Q equality uses VF18.z and mask2.
  Scheduled FMAND consumes the preceding subtraction's MAC flags with a nop
  upper instruction. Between subtraction and flag consumption, only lower
  integer mask setup occurs; no competing upper arithmetic overwrites MAC flags.
  This is a producer/consumer observation, not an independent formal proof of
  hardware MAC flag latency or exceptional floating-point behavior.
- Q bounds and rectangle-difference bounds likewise consume their final bound
  subtraction before other arithmetic. First-corner ADC rejection precedes the
  six final-word checks; equality to the first complete fog/ADC word rejects a
  differing later ADC bit. All original outputs remain before entire-package
  pass, so a rejection branch leaves their bytes intact.
- SourceVI06 advances18 per compacted quad while destinationVI03 advances6.
  Forward/reverseY select original corner pairs0/2 or5/1. Each endpoint's STQ,
  RGBA and XYZF2 are loaded before its corresponding forward stores can reach
  unread later source; the delay-slot forward pointer is replaced on reverseY.
  Only the original primitive tag is overwritten after all compaction; preceding
  material tags and kickVI13 are preserved.
- Every marker-present reject or success joins coronaDone, whose scheduled
  `ilw.x VI03,8(VI00)` restores the mesh-persistent real single-color option
  before XGKICK/end/branch-begin. This prevents reduction scratchVI03 from leaking
  into a subsequent MSCNT package. Marker-absent return never clobbered VI03.

The review found no additional definite register-lifetime or branch-merge source
blocker in this scope. V5 remains **budget rejected** and unexecuted. Any V6
shrink must preserve these gates and requires fresh actual assembler/register/
MAC schedule review, budget proof and output/raster acceptance. No whole-program
VU semantics, GS consumption, raster or performance PASS is inferred here.
