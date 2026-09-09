; The control for acc_fields.vcl: the later ACC write DOES cover the earlier one's
; fields, so the earlier one really is dead and --drop-dead-writes must still
; delete it. Without this, "stop killing ACC writes" would look like a fix while
; being nothing but the optimisation turned off.
;
; Expect: `mula.x acc, a, b` gone, `mula.xyzw acc, b, c` and the madd kept.
.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    lq       a,   0(vi00)
    lq       b,   1(vi00)
    lq       c,   2(vi00)
    mula.x    acc, a, b
    mula.xyzw acc, b, c
    madd     out, a, c
    sq       out, 3(vi00)
--exit
--endexit
