.syntax new
.name DelaySlotLive
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    iaddiu     ptr,  vi00, 24
    iaddiu     alt,  vi00, 72
    iaddiu     one,  vi00, 8
    ilw.x      gate, 4(vi00)
    ilw.x      cnt,  0(vi00)
    iaddiu     cnt,  cnt, 3
    lq         v,    0(vi00)
loop:
    isw.x      one,  2(ptr)
    ibltz      gate, skip
    ior        ptr,  alt, one
skip:
    mul.xyzw   v, v, v
    sq         v, 300(vi00)
    iaddi      cnt, cnt, -1
    ibne       cnt, vi00, loop
--exit
--endexit
