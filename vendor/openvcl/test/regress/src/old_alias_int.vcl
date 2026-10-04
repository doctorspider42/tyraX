; OLD SYNTAX. Four integer aliases whose names end in x, y, z and w. None of
; those arguments can carry a field at all - `iaddiu`'s destination is
; `vi:write` and `isw`'s value operand is a plain `vi`, neither marked :dest,
; :bc or :flag - so the letters are part of the name, and Sony's vcl reads them
; that way: its output for this program is the same three rows whatever the
; names are spelled.
;
; openvcl stripped the letter first and rejected the WHOLE argument afterwards
; when the modifier turned out to be missing, so every one of these was
; "Invalid argument" and the program did not compile. What survived it was a
; hardcoded list of English trigrams - "dex", "tex", "lex", "rex", "sex" -
; which is how ps2gl's `next_index` compiled and `next_matrix` did not.
		.syntax old
		.name OldAliasInt
		.vu
		.init_vf_all
		.init_vi_all
		--enter
		--endenter
		iaddiu		next_matrix, vi00, 8
		iaddiu		array, next_matrix, 4
		iaddiu		fuzz, array, 2
		iaddiu		draw, fuzz, 1
		isw		draw, 300(vi00)x
		--exit
		--endexit
