; The other half of the same defect, and the one that was silent. `vertex` here
; is a FLOAT alias in argument positions the operand pattern marks :dest, so the
; trailing letter IS a field selection: Sony's vcl compiles this to `lq.x` and
; `sq.x`, one component. The trigram list made openvcl read the name whole and
; emit a full-quadword `lq`, so the same source compiled to a different program
; with no diagnostic on either side.
;
; Asserted as COUNT lq.x, because the divergence is exactly the field suffix - and
; because the value oracles in lib/ parse the source themselves and have no
; old-syntax mode, so they read `vertex` whole and flag this program whichever
; way it is compiled. That is the oracles' model, not the emitter's answer: Sony's
; vcl emits lq.x / sq.x here, which is what says the emitter is right.
		.syntax old
		.name OldAliasField
		.vu
		.init_vf_all
		.init_vi_all
		--enter
		--endenter
		lq		vertex, 0(vi00)
		sq		vertex, 300(vi00)
		--exit
		--endexit
