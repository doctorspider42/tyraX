; The control for old_alias_int, and the shape upstream's trigram list existed
; for: ps2gl's `indexed.vcl` names its integer aliases `next_index`,
; `first_index` and `last_index`. Those compiled before and must still compile -
; a fix that answers old_alias_int by stripping every trailing letter would
; reject these instead, and swapping which spellings work is not a fix.
		.syntax old
		.name OldAliasIntOk
		.vu
		.init_vf_all
		.init_vi_all
		--enter
		--endenter
		iaddiu		next_index, vi00, 8
		iaddiu		last_index, next_index, 4
		isw		last_index, 300(vi00)x
		--exit
		--endexit
