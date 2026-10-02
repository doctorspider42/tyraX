; Minimal repro for the openvcl quad-renderer ADC-bit bug.
;
; The pattern under test is the new_adc_bit computation:
;
;     ior new_adc_bit, vi01, z_sign
;     iaddiu new_adc_bit, new_adc_bit, 0x7fff
;     mfir.w vf15, new_adc_bit
;     sq vf15, 0(vi04)
;
; where vi01 comes from fcand (clip flags) and z_sign comes from
; fmand (MAC sign of the back-face cross product opmsub.xyz).
;
; Expected output for an unclipped, front-facing vertex:
;     fcand result = 0 (no clip bits set after clipw of in-frustum vertex)
;     fmand result = 0 (well, depends on the FMAC sign, but for a
;       deliberately-positive cross product we want 0)
;     ior  = 0
;     iaddiu = 0x7fff  (ADC bit clear)
;     vf15.W = 0x00007FFF
;
; The bug is openvcl producing 0x8000 here for a vertex that should
; pass.

	.init_vf_all
	.init_vi_all
	.name vsmReproAdc

	--enter
	--endenter

main_loop_lid:
	xtop         vi04
	iaddiu       vi05, vi00, 0xffff
	lq           vf07, 0(vi00)
	lq           vf08, 1(vi00)
	lq           vf09, 2(vi00)
	lq           vf15, 3(vi00)

	; 4 clipw.xyz, one per "vertex"
	clipw.xyz    vf08, vf08w
	clipw.xyz    vf08, vf08w
	clipw.xyz    vf08, vf08w
	clipw.xyz    vf08, vf08w

	; back-face cull cross product -- opmsub's MAC sign is z_sign
	mul.xyz      vf09, vf08, vf07
	opmula.xyz   ACC, vf08, vf09
	opmsub.xyz   vf12, vf09, vf08

	; the suspect chain
	fmand        vi06, vi05            ; z_sign  <- MAC sign
	fcand        vi01, 0x0ffffff       ; vi01    <- clip flags
	iand         vi01, vi01, vi01
	ior          vi08, vi01, vi06
	iaddiu       vi06, vi08, 0x7fff

	mfir.w       vf15, vi06
	sq           vf15, 0(vi04)

	--cont
	b            main_loop_lid
