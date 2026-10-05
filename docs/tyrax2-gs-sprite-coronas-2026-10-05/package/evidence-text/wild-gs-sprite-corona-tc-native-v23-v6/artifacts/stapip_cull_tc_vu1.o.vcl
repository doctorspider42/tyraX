
.name StaPipVU1CullTC
.syntax new
.name StaPipVU1Cull_TC
.vu
.init_vf_all
.init_vi_all
--enter
--endenter

   fcset   0x000000


   lq             mvp[0], 0+0(vi00)
   lq             mvp[1], 0+1(vi00)
   lq             mvp[2], 0+2(vi00)
   lq             mvp[3], 0+3(vi00)

    ilw.x   singleColorEnabled, 8(vi00)

   lq          fogParams,   8(vi00)
   add.x       fogParams,   vf00,          fogParams[z]


   iaddiu      adcMask,     vi00,          0x4000
   iadd        adcMask,     adcMask,     adcMask


   lq          spotPos,     12+0(vi00)
   lq          spotDirV,     12+1(vi00)
   lq          spotColV,     12+2(vi00)
   ilw.y       spotEnabled,    8(vi00)

begin:
    xtop buffer

   lq.xyz  scale,                  0(buffer)
   lq      primTag,                1(buffer)

    iaddiu  vertexData,         buffer,         2
    ilw.w   stateFlag,          0(buffer)
    iaddiu  poolMarkerMask,      vi00,            0x0400
    iand    poolColorEnabled,    stateFlag,       poolMarkerMask
    iadd    vertexCount,        stateFlag,      vi00
    iaddiu  countMask,          vi00,            0x03FF
    iand    vertexCount,        vertexCount,     countMask
    iadd    stqData,            vertexData,     vertexCount
    iaddiu  colorRunRemaining,  vi00,            0
    iaddiu  colorStride,        vi00,            0
    iblez   poolColorEnabled,   poolLegacyLayout
    iaddi   colorStride,        vi00,            -1
    iadd    colorDescriptor,    stqData,        vertexCount
    ilw.x   colorRunRemaining,  0(colorDescriptor)
    iaddiu  colorData,          colorDescriptor, 1
    iaddiu  kickAddress,        colorData,      2
    b       setDestAddr
poolLegacyLayout:
    iadd    colorData,          stqData,        vertexCount
    iblez   singleColorEnabled, setDestAddrMultiColor
    iadd    kickAddress,        stqData,        vertexCount
    lq      singleColor,        7(vi00)
    add     singleColor,        vf00,           singleColor
    sq      singleColor,        1016(vi00)
    sq      singleColor,        1016+1(vi00)
    sq      singleColor,        1016+2(vi00)
    iaddiu  colorData,          vi00,           1016
    iaddiu  colorStride,        vi00,           0
    b       setDestAddr
setDestAddrMultiColor:
    iadd    kickAddress,        colorData,      vertexCount
    iaddiu  colorStride,        vi00,           3
setDestAddr:
    iaddiu  vertexCounter, vi00, 0x0800
    iand    vertexCounter, stateFlag, vertexCounter
    ibeq    vertexCounter, vi00, coronaInputReady
    iaddiu  kickAddress, kickAddress, 1
coronaInputReady:
    iaddiu  destAddress,        kickAddress,    0
    ibgez   stateFlag,          reuseMaterialState

   lq             gifSetTag, 19(vi00)


   lq      lodGifTag,             9(vi00)
   lq      testsTag,           10(vi00)
   lq      texBufferClutGifTag,   11(vi00)


   lq      alphaGifTag,           21(vi00)


   sq gifSetTag,            0(destAddress)
   sq testsTag,             1(destAddress)
   sq gifSetTag,            2(destAddress)
   sq lodGifTag,            3(destAddress)
   sq gifSetTag,            4(destAddress)
   sq texBufferClutGifTag,  5(destAddress)
   sq gifSetTag,            6(destAddress)
   sq alphaGifTag,          7(destAddress)
   sq primTag,              8(destAddress)
   iaddiu                     destAddress,    destAddress,    9

    b       materialStateReady
reuseMaterialState:
    sq      primTag,            0(destAddress)
    iaddiu  destAddress,        destAddress,     1
materialStateReady:
    iadd vertexCounter, buffer, vertexCount
    ibgez   spotEnabled,    unlitSelect
vertexLoop:
        ibgez   colorStride,       poolLegacyLoad0
        lq      color1,   (colorData)
        lq      color2,   (colorData)
        lq      color3,   (colorData)
        b       poolColorsReady0
poolLegacyLoad0:
        lq      color1,   (colorData)
        lq      color2,   1(colorData)
        lq      color3,   2(colorData)
poolColorsReady0:
        lq      vertex1,  (vertexData)
        lq      stq1,     (stqData)
        lq      vertex2,  1(vertexData)
        lq      stq2,     1(stqData)
        lq      vertex3,  2(vertexData)
        lq      stq3,     2(stqData)

   sub.xyz     spotD,         vertex1,      spotPos
   mul.xyz     spotSq,        spotD,         spotD
   add.x       spotDist,      spotSq,        spotSq[y]
   add.x       spotDist,      spotDist,      spotSq[z]
   mul.xyz     spotTm,        spotD,         spotDirV
   add.x       spotT,         spotTm,        spotTm[y]
   add.x       spotT,         spotT,         spotTm[z]
   max.x       spotT,         spotT,         vf00[x]
   mul.x       spotT,         spotT,         spotT
   mul.x       spotC,         spotDist,      spotDirV[w]
   sub.x       spotC,         spotT,         spotC
   mul.x       spotC,         spotC,         spotColV[w]
   mini.x      spotC,         spotC,         vf00[w]
   max.x       spotC,         spotC,         vf00[x]
   adda.x      acc,           vf00,          vf00[w]
   msub.x      spotA,         spotDist,      spotPos[w]
   mini.x      spotA,         spotA,         vf00[w]
   max.x       spotA,         spotA,         vf00[x]
   mul.x       spotC,         spotC,         spotA
   mul.xyz     spotAdd,       spotColV,     spotC[x]
   add.xyz     color1,       color1,       spotAdd


   mul            acc,           mvp[0], vertex1[x]
   madd           acc,           mvp[1], vertex1[y]
   madd           acc,           mvp[2], vertex1[z]
   madd           vertex1, mvp[3], vertex1[w]


   mul.x       fogAccum,      fogParams,   vertex1[w]
   add.x       fogAccum,      fogAccum,      fogParams[w]
   loi         255
   mini.x      fogAccum,      fogAccum,      i
   max.x       fogAccum,      fogAccum,      vf00[x]
   ftoi4.x     fogAccum,      fogAccum
   mtir        fogInt,      fogAccum[x]


   clipw.xyz   vertex1,      vertex1
   fcand       VI01,          0x3FFFF
   iaddiu      adcBit,        VI01,          0x7FFF
   iand        adcBit,        adcBit,        adcMask
   ior         adcBit,        adcBit,        fogInt
   isw.w       adcBit,        2(destAddress)


   div            q, vf00[w], vertex1[w]
   mul.xyz        vertex1, vertex1, q


   mula.xyz    acc,        scale,    vf00[w]
   madd.xyz    vertex1,   vertex1,   scale
   ftoi4.xyz   vertex1,   vertex1


   mulq  outputStq1,  stq1,  q


   loi         255
   mini.xyz    color1, color1, i
   max.xyz     color1, color1, vf00[x]
   ftoi0       color1, color1


   sub.xyz     spotD,         vertex2,      spotPos
   mul.xyz     spotSq,        spotD,         spotD
   add.x       spotDist,      spotSq,        spotSq[y]
   add.x       spotDist,      spotDist,      spotSq[z]
   mul.xyz     spotTm,        spotD,         spotDirV
   add.x       spotT,         spotTm,        spotTm[y]
   add.x       spotT,         spotT,         spotTm[z]
   max.x       spotT,         spotT,         vf00[x]
   mul.x       spotT,         spotT,         spotT
   mul.x       spotC,         spotDist,      spotDirV[w]
   sub.x       spotC,         spotT,         spotC
   mul.x       spotC,         spotC,         spotColV[w]
   mini.x      spotC,         spotC,         vf00[w]
   max.x       spotC,         spotC,         vf00[x]
   adda.x      acc,           vf00,          vf00[w]
   msub.x      spotA,         spotDist,      spotPos[w]
   mini.x      spotA,         spotA,         vf00[w]
   max.x       spotA,         spotA,         vf00[x]
   mul.x       spotC,         spotC,         spotA
   mul.xyz     spotAdd,       spotColV,     spotC[x]
   add.xyz     color2,       color2,       spotAdd


   mul            acc,           mvp[0], vertex2[x]
   madd           acc,           mvp[1], vertex2[y]
   madd           acc,           mvp[2], vertex2[z]
   madd           vertex2, mvp[3], vertex2[w]


   mul.x       fogAccum,      fogParams,   vertex2[w]
   add.x       fogAccum,      fogAccum,      fogParams[w]
   loi         255
   mini.x      fogAccum,      fogAccum,      i
   max.x       fogAccum,      fogAccum,      vf00[x]
   ftoi4.x     fogAccum,      fogAccum
   mtir        fogInt,      fogAccum[x]


   clipw.xyz   vertex2,      vertex2
   fcand       VI01,          0x3FFFF
   iaddiu      adcBit,        VI01,          0x7FFF
   iand        adcBit,        adcBit,        adcMask
   ior         adcBit,        adcBit,        fogInt
   isw.w       adcBit,        2+3(destAddress)


   div            q, vf00[w], vertex2[w]
   mul.xyz        vertex2, vertex2, q


   mula.xyz    acc,        scale,    vf00[w]
   madd.xyz    vertex2,   vertex2,   scale
   ftoi4.xyz   vertex2,   vertex2


   mulq  outputStq2,  stq2,  q


   loi         255
   mini.xyz    color2, color2, i
   max.xyz     color2, color2, vf00[x]
   ftoi0       color2, color2


   sub.xyz     spotD,         vertex3,      spotPos
   mul.xyz     spotSq,        spotD,         spotD
   add.x       spotDist,      spotSq,        spotSq[y]
   add.x       spotDist,      spotDist,      spotSq[z]
   mul.xyz     spotTm,        spotD,         spotDirV
   add.x       spotT,         spotTm,        spotTm[y]
   add.x       spotT,         spotT,         spotTm[z]
   max.x       spotT,         spotT,         vf00[x]
   mul.x       spotT,         spotT,         spotT
   mul.x       spotC,         spotDist,      spotDirV[w]
   sub.x       spotC,         spotT,         spotC
   mul.x       spotC,         spotC,         spotColV[w]
   mini.x      spotC,         spotC,         vf00[w]
   max.x       spotC,         spotC,         vf00[x]
   adda.x      acc,           vf00,          vf00[w]
   msub.x      spotA,         spotDist,      spotPos[w]
   mini.x      spotA,         spotA,         vf00[w]
   max.x       spotA,         spotA,         vf00[x]
   mul.x       spotC,         spotC,         spotA
   mul.xyz     spotAdd,       spotColV,     spotC[x]
   add.xyz     color3,       color3,       spotAdd


   mul            acc,           mvp[0], vertex3[x]
   madd           acc,           mvp[1], vertex3[y]
   madd           acc,           mvp[2], vertex3[z]
   madd           vertex3, mvp[3], vertex3[w]


   mul.x       fogAccum,      fogParams,   vertex3[w]
   add.x       fogAccum,      fogAccum,      fogParams[w]
   loi         255
   mini.x      fogAccum,      fogAccum,      i
   max.x       fogAccum,      fogAccum,      vf00[x]
   ftoi4.x     fogAccum,      fogAccum
   mtir        fogInt,      fogAccum[x]


   clipw.xyz   vertex3,      vertex3
   fcand       VI01,          0x3FFFF
   iaddiu      adcBit,        VI01,          0x7FFF
   iand        adcBit,        adcBit,        adcMask
   ior         adcBit,        adcBit,        fogInt
   isw.w       adcBit,        2+6(destAddress)


   div            q, vf00[w], vertex3[w]
   mul.xyz        vertex3, vertex3, q


   mula.xyz    acc,        scale,    vf00[w]
   madd.xyz    vertex3,   vertex3,   scale
   ftoi4.xyz   vertex3,   vertex3


   mulq  outputStq3,  stq3,  q


   loi         255
   mini.xyz    color3, color3, i
   max.xyz     color3, color3, vf00[x]
   ftoi0       color3, color3

        sq      outputStq1,     0(destAddress)
        sq      color1,         1(destAddress)
        sq.xyz  vertex1,        2(destAddress)
        sq      outputStq2,     0+3(destAddress)
        sq      color2,         1+3(destAddress)
        sq.xyz  vertex2,        2+3(destAddress)
        sq      outputStq3,     0+6(destAddress)
        sq      color3,         1+6(destAddress)
        sq.xyz  vertex3,        2+6(destAddress)
        iaddiu  vertexData,     vertexData,     3                         
        iaddiu  stqData,        stqData,        3  
        ibgez   colorStride,       poolLegacyAdvance0
        iaddi   colorRunRemaining, colorRunRemaining, -3
        ibne    colorRunRemaining, vi00, poolAdvanceDone0
        iaddiu  colorData, colorData, 1
        b       poolAdvanceDone0
poolLegacyAdvance0:
        iadd    colorData,      colorData,      colorStride
poolAdvanceDone0:
        iaddiu  destAddress,    destAddress,    9
        iaddi   vertexCounter,  vertexCounter,  -3
        ibne    vertexCounter,  buffer, vertexLoop
    b       vertexLoopsDone
unlitSelect:
    ilw.z   fogScaleBits,   8(vi00)
    ibeq    fogScaleBits,   vi00,   noFogSetup
unlitVertexLoop:
        ibgez   colorStride,       poolLegacyLoad1
        lq      color1,   (colorData)
        lq      color2,   (colorData)
        lq      color3,   (colorData)
        b       poolColorsReady1
poolLegacyLoad1:
        lq      color1,   (colorData)
        lq      color2,   1(colorData)
        lq      color3,   2(colorData)
poolColorsReady1:
        lq      vertex1,  (vertexData)
        lq      stq1,     (stqData)
        lq      vertex2,  1(vertexData)
        lq      stq2,     1(stqData)
        lq      vertex3,  2(vertexData)
        lq      stq3,     2(stqData)

   mul            acc,           mvp[0], vertex1[x]
   madd           acc,           mvp[1], vertex1[y]
   madd           acc,           mvp[2], vertex1[z]
   madd           vertex1, mvp[3], vertex1[w]


   mul.x       fogAccum,      fogParams,   vertex1[w]
   add.x       fogAccum,      fogAccum,      fogParams[w]
   loi         255
   mini.x      fogAccum,      fogAccum,      i
   max.x       fogAccum,      fogAccum,      vf00[x]
   ftoi4.x     fogAccum,      fogAccum
   mtir        fogInt,      fogAccum[x]


   clipw.xyz   vertex1,      vertex1
   fcand       VI01,          0x3FFFF
   iaddiu      adcBit,        VI01,          0x7FFF
   iand        adcBit,        adcBit,        adcMask
   ior         adcBit,        adcBit,        fogInt
   isw.w       adcBit,        2(destAddress)


   div            q, vf00[w], vertex1[w]
   mul.xyz        vertex1, vertex1, q


   mula.xyz    acc,        scale,    vf00[w]
   madd.xyz    vertex1,   vertex1,   scale
   ftoi4.xyz   vertex1,   vertex1


   mulq  outputStq1,  stq1,  q


   loi         255
   mini.xyz    color1, color1, i
   max.xyz     color1, color1, vf00[x]
   ftoi0       color1, color1


   mul            acc,           mvp[0], vertex2[x]
   madd           acc,           mvp[1], vertex2[y]
   madd           acc,           mvp[2], vertex2[z]
   madd           vertex2, mvp[3], vertex2[w]


   mul.x       fogAccum,      fogParams,   vertex2[w]
   add.x       fogAccum,      fogAccum,      fogParams[w]
   loi         255
   mini.x      fogAccum,      fogAccum,      i
   max.x       fogAccum,      fogAccum,      vf00[x]
   ftoi4.x     fogAccum,      fogAccum
   mtir        fogInt,      fogAccum[x]


   clipw.xyz   vertex2,      vertex2
   fcand       VI01,          0x3FFFF
   iaddiu      adcBit,        VI01,          0x7FFF
   iand        adcBit,        adcBit,        adcMask
   ior         adcBit,        adcBit,        fogInt
   isw.w       adcBit,        2+3(destAddress)


   div            q, vf00[w], vertex2[w]
   mul.xyz        vertex2, vertex2, q


   mula.xyz    acc,        scale,    vf00[w]
   madd.xyz    vertex2,   vertex2,   scale
   ftoi4.xyz   vertex2,   vertex2


   mulq  outputStq2,  stq2,  q


   loi         255
   mini.xyz    color2, color2, i
   max.xyz     color2, color2, vf00[x]
   ftoi0       color2, color2


   mul            acc,           mvp[0], vertex3[x]
   madd           acc,           mvp[1], vertex3[y]
   madd           acc,           mvp[2], vertex3[z]
   madd           vertex3, mvp[3], vertex3[w]


   mul.x       fogAccum,      fogParams,   vertex3[w]
   add.x       fogAccum,      fogAccum,      fogParams[w]
   loi         255
   mini.x      fogAccum,      fogAccum,      i
   max.x       fogAccum,      fogAccum,      vf00[x]
   ftoi4.x     fogAccum,      fogAccum
   mtir        fogInt,      fogAccum[x]


   clipw.xyz   vertex3,      vertex3
   fcand       VI01,          0x3FFFF
   iaddiu      adcBit,        VI01,          0x7FFF
   iand        adcBit,        adcBit,        adcMask
   ior         adcBit,        adcBit,        fogInt
   isw.w       adcBit,        2+6(destAddress)


   div            q, vf00[w], vertex3[w]
   mul.xyz        vertex3, vertex3, q


   mula.xyz    acc,        scale,    vf00[w]
   madd.xyz    vertex3,   vertex3,   scale
   ftoi4.xyz   vertex3,   vertex3


   mulq  outputStq3,  stq3,  q


   loi         255
   mini.xyz    color3, color3, i
   max.xyz     color3, color3, vf00[x]
   ftoi0       color3, color3

        sq      outputStq1,     0(destAddress)
        sq      color1,         1(destAddress)
        sq.xyz  vertex1,        2(destAddress)
        sq      outputStq2,     0+3(destAddress)
        sq      color2,         1+3(destAddress)
        sq.xyz  vertex2,        2+3(destAddress)
        sq      outputStq3,     0+6(destAddress)
        sq      color3,         1+6(destAddress)
        sq.xyz  vertex3,        2+6(destAddress)
        iaddiu  vertexData,     vertexData,     3                         
        iaddiu  stqData,        stqData,        3  
        ibgez   colorStride,       poolLegacyAdvance1
        iaddi   colorRunRemaining, colorRunRemaining, -3
        ibne    colorRunRemaining, vi00, poolAdvanceDone1
        iaddiu  colorData, colorData, 1
        b       poolAdvanceDone1
poolLegacyAdvance1:
        iadd    colorData,      colorData,      colorStride
poolAdvanceDone1:
        iaddiu  destAddress,    destAddress,    9
        iaddi   vertexCounter,  vertexCounter,  -3
        ibne    vertexCounter,  buffer, unlitVertexLoop
    b       vertexLoopsDone
noFogSetup:
    iaddiu  fogConstInt,    vi00,   0xFF0
noFogVertexLoop:
        ibgez   colorStride,       poolLegacyLoad2
        lq      color1,   (colorData)
        lq      color2,   (colorData)
        lq      color3,   (colorData)
        b       poolColorsReady2
poolLegacyLoad2:
        lq      color1,   (colorData)
        lq      color2,   1(colorData)
        lq      color3,   2(colorData)
poolColorsReady2:
        lq      vertex1,  (vertexData)
        lq      stq1,     (stqData)
        lq      vertex2,  1(vertexData)
        lq      stq2,     1(stqData)
        lq      vertex3,  2(vertexData)
        lq      stq3,     2(stqData)

   mul            acc,           mvp[0], vertex1[x]
   madd           acc,           mvp[1], vertex1[y]
   madd           acc,           mvp[2], vertex1[z]
   madd           vertex1, mvp[3], vertex1[w]


   clipw.xyz   vertex1,      vertex1
   fcand       VI01,          0x3FFFF
   iaddiu      adcBit,        VI01,          0x7FFF
   iand        adcBit,        adcBit,        adcMask
   ior         adcBit,        adcBit,        fogConstInt
   isw.w       adcBit,        2(destAddress)


   div            q, vf00[w], vertex1[w]
   mul.xyz        vertex1, vertex1, q


   mula.xyz    acc,        scale,    vf00[w]
   madd.xyz    vertex1,   vertex1,   scale
   ftoi4.xyz   vertex1,   vertex1


   mulq  outputStq1,  stq1,  q


   loi         255
   mini.xyz    color1, color1, i
   max.xyz     color1, color1, vf00[x]
   ftoi0       color1, color1


   mul            acc,           mvp[0], vertex2[x]
   madd           acc,           mvp[1], vertex2[y]
   madd           acc,           mvp[2], vertex2[z]
   madd           vertex2, mvp[3], vertex2[w]


   clipw.xyz   vertex2,      vertex2
   fcand       VI01,          0x3FFFF
   iaddiu      adcBit,        VI01,          0x7FFF
   iand        adcBit,        adcBit,        adcMask
   ior         adcBit,        adcBit,        fogConstInt
   isw.w       adcBit,        2+3(destAddress)


   div            q, vf00[w], vertex2[w]
   mul.xyz        vertex2, vertex2, q


   mula.xyz    acc,        scale,    vf00[w]
   madd.xyz    vertex2,   vertex2,   scale
   ftoi4.xyz   vertex2,   vertex2


   mulq  outputStq2,  stq2,  q


   loi         255
   mini.xyz    color2, color2, i
   max.xyz     color2, color2, vf00[x]
   ftoi0       color2, color2


   mul            acc,           mvp[0], vertex3[x]
   madd           acc,           mvp[1], vertex3[y]
   madd           acc,           mvp[2], vertex3[z]
   madd           vertex3, mvp[3], vertex3[w]


   clipw.xyz   vertex3,      vertex3
   fcand       VI01,          0x3FFFF
   iaddiu      adcBit,        VI01,          0x7FFF
   iand        adcBit,        adcBit,        adcMask
   ior         adcBit,        adcBit,        fogConstInt
   isw.w       adcBit,        2+6(destAddress)


   div            q, vf00[w], vertex3[w]
   mul.xyz        vertex3, vertex3, q


   mula.xyz    acc,        scale,    vf00[w]
   madd.xyz    vertex3,   vertex3,   scale
   ftoi4.xyz   vertex3,   vertex3


   mulq  outputStq3,  stq3,  q


   loi         255
   mini.xyz    color3, color3, i
   max.xyz     color3, color3, vf00[x]
   ftoi0       color3, color3

        sq      outputStq1,     0(destAddress)
        sq      color1,         1(destAddress)
        sq.xyz  vertex1,        2(destAddress)
        sq      outputStq2,     0+3(destAddress)
        sq      color2,         1+3(destAddress)
        sq.xyz  vertex2,        2+3(destAddress)
        sq      outputStq3,     0+6(destAddress)
        sq      color3,         1+6(destAddress)
        sq.xyz  vertex3,        2+6(destAddress)
        iaddiu  vertexData,     vertexData,     3                         
        iaddiu  stqData,        stqData,        3  
        ibgez   colorStride,       poolLegacyAdvance2
        iaddi   colorRunRemaining, colorRunRemaining, -3
        ibne    colorRunRemaining, vi00, poolAdvanceDone2
        iaddiu  colorData, colorData, 1
        b       poolAdvanceDone2
poolLegacyAdvance2:
        iadd    colorData,      colorData,      colorStride
poolAdvanceDone2:
        iaddiu  destAddress,    destAddress,    9
        iaddi   vertexCounter,  vertexCounter,  -3
        ibne    vertexCounter,  buffer, noFogVertexLoop
vertexLoopsDone:
    ilw.w   vertexCount, 0(buffer)
    iaddiu  vertexCounter, vi00, 0x0800
    iand    stateFlag, vertexCount, vertexCounter
    ibeq    stateFlag, vi00, coronaReturn
    iaddiu  vertexCounter, vi00, 0x03FF
    iand    vertexCount, vertexCount, vertexCounter
    iadd    vertexCounter, vertexCount, vertexCount
    iadd    vertexCounter, vertexCounter, vertexCount
    isub    destAddress, destAddress, vertexCounter
    iadd    vertexData, destAddress, vi00
    iadd    stqData, vertexCount, vi00
coronaQuadCheck:
    lq      color1, 1(vertexData)
    itof0   color1, color1
    lq      vertex1, 2(vertexData)
    itof0   vertex1, vertex1
    lq      outputStq1, 0(vertexData)
    iaddiu  fogInt, vi00, 0x20
    loi     0.0001
    sub.z   vf00, outputStq1, i
    fmand   singleColorEnabled, fogInt
    ibne    singleColorEnabled, vi00, coronaDone
    loi     10000
    add.z   vertex3, vf00, i
    sub.z   vf00, vertex3, outputStq1
    fmand   singleColorEnabled, fogInt
    ibne    singleColorEnabled, vi00, coronaDone
    ilw.w   singleColorEnabled, 2(vertexData)
    iand    singleColorEnabled, singleColorEnabled, adcMask
    ibne    singleColorEnabled, vi00, coronaDone
    iadd    colorData, vertexData, vi00
    iaddiu  colorRunRemaining, vi00, 6
coronaVertexCheck:
    lq      color2, 1(colorData)
    itof0   color2, color2
    sub     color3, color2, color1
    iaddiu  fogInt, vi00, 15
    fmand   singleColorEnabled, fogInt
    ibne    singleColorEnabled, fogInt, coronaDone
    lq      vertex2, 2(colorData)
    itof0   vertex2, vertex2
    sub.zw  color3, vertex2, vertex1
    iaddiu  fogInt, vi00, 3
    fmand   singleColorEnabled, fogInt
    ibne    singleColorEnabled, fogInt, coronaDone
    lq      outputStq2, 0(colorData)
    sub.z   color3, outputStq2, outputStq1
    iaddiu  fogInt, vi00, 2
    fmand   singleColorEnabled, fogInt
    ibne    singleColorEnabled, fogInt, coronaDone
    iaddiu  colorData, colorData, 3
    iaddi   colorRunRemaining, colorRunRemaining, -1
    ibne    colorRunRemaining, vi00, coronaVertexCheck
    lq      outputStq3, 8(vertexData)
    itof0   outputStq3, outputStq3
    sub.xy  vertex3, outputStq3, vertex1
    abs.xy  vertex3, vertex3
    loi     32767
    add.xy  outputStq3, vf00, i
    sub.xy  vf00, outputStq3, vertex3
    iaddiu  singleColorEnabled, vi00, 0xC0
    fmand   singleColorEnabled, singleColorEnabled
    ibne    singleColorEnabled, vi00, coronaDone
    ilw.x   fogConstInt, 2(vertexData)
    ilw.x   fogInt, 5(vertexData)
    ilw.x   vertexCounter, 8(vertexData)
    ibne    fogInt, vertexCounter, coronaDone
    ilw.x   vertexCounter, 17(vertexData)
    ibne    fogConstInt, vertexCounter, coronaDone
    isub    vertexCounter, fogInt, fogConstInt
    iblez   vertexCounter, coronaDone
    ilw.y   fogConstInt, 2(vertexData)
    ilw.y   vertexCounter, 5(vertexData)
    ibne    fogConstInt, vertexCounter, coronaDone
    ilw.y   fogInt, 8(vertexData)
    ilw.y   vertexCounter, 17(vertexData)
    ibne    fogInt, vertexCounter, coronaDone
    ibeq    fogConstInt, fogInt, coronaDone
    iaddiu  vertexData, vertexData, 18
    iaddi   stqData, stqData, -6
    ibne    stqData, vi00, coronaQuadCheck
    iadd    vertexData, destAddress, vi00
    iadd    singleColorEnabled, destAddress, vi00
    iadd    stqData, vertexCount, vi00
coronaCompact:
    ilw.y   fogConstInt, 2(vertexData)
    ilw.y   fogInt, 8(vertexData)
    isub    vertexCounter, fogInt, fogConstInt
    ibltz   vertexCounter, coronaReverseY
    iadd    colorData, vertexData, vi00
    iaddiu  colorRunRemaining, vertexData, 6
    b       coronaEndpointsReady
coronaReverseY:
    iaddiu  colorData, vertexData, 15
    iaddiu  colorRunRemaining, vertexData, 3
coronaEndpointsReady:
    lq      outputStq2, 0(colorData)
    lq      color2, 1(colorData)
    lq      vertex2, 2(colorData)
    sq      outputStq2, 0(singleColorEnabled)
    sq      color2, 1(singleColorEnabled)
    sq      vertex2, 2(singleColorEnabled)
    lq      outputStq2, 0(colorRunRemaining)
    lq      color2, 1(colorRunRemaining)
    lq      vertex2, 2(colorRunRemaining)
    sq      outputStq2, 3(singleColorEnabled)
    sq      color2, 4(singleColorEnabled)
    sq      vertex2, 5(singleColorEnabled)
    iaddiu  vertexData, vertexData, 18
    iaddiu  singleColorEnabled, singleColorEnabled, 6
    iaddi   stqData, stqData, -6
    ibne    stqData, vi00, coronaCompact
    iaddi   vertexCounter, kickAddress, -1
    lq      primTag, 0(vertexCounter)
    iaddi   vertexCounter, destAddress, -1
    sq      primTag, 0(vertexCounter)
coronaDone:
    ilw.x   singleColorEnabled, 8(vi00)
coronaReturn:
    xgkick kickAddress
--barrier
--cont
    b   begin
--exit
--endexit
