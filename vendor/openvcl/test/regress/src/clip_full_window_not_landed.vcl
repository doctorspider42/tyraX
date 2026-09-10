; A full-window CLIP read taken before its own clipw has landed.
;
; 0x3FFFF is "is any of the last three vertices outside". The mask is
; position-INDEPENDENT - it gets the same answer whichever entry each judgement
; sits in - and both emitters have used that to let the reader sit adjacent to
; its clipw. Position-independence is not latency-independence: the window is
; three entries wide, so a read taken before the newest push has landed is not a
; partly-settled answer, it is the window SHIFTED BY A WHOLE VERTEX.
;
; This is the shape of every Tyra cull program: transform, clipw, ask "is
; anything outside", and turn that into the ADC bit of the vertex that completes
; the triangle - so the triangle that gets drawn is judged by the wrong vertex.
;
; PASS: at least four emitted rows between the clipw and the fcand.
.syntax new
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    fcset      0x000000
    iaddiu     dst, vi00, 16
    lq         mvp[0], 1(vi00)
    lq         mvp[1], 2(vi00)
    lq         mvp[2], 3(vi00)
    lq         mvp[3], 4(vi00)
    lq         scale,  5(vi00)
    lq         v,      0(vi00)
    mul        acc,    mvp[0], v[x]
    madd       acc,    mvp[1], v[y]
    madd       acc,    mvp[2], v[z]
    madd       v,      mvp[3], v[w]
    clipw.xyz  v, v
    fcand      VI01, 0x3FFFF
    iaddiu     adc,  VI01, 0x7FFF
    isw.w      adc,  2(dst)
    div        q, vf00[w], v[w]
    mul.xyz    v, v, q
    mula.xyz   acc, scale, vf00[w]
    madd.xyz   v, v, scale
    ftoi4.xyz  v, v
    sq.xyz     v, 2(dst)
--exit
--endexit
