--enter
--endenter
    add      tmp,  vf00, vf00
    mul      out1, tmp,  tmp
    sq       out1, 0(vi00)
--exit
--endexit
.init_vf vf01-vf04
.init_vi vi01-vi04
