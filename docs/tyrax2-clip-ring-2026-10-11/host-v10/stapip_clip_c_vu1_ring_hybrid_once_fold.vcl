
.name StaPipVU1ClipC_ring_hybrid_once_fold
.syntax new
.name StaPipVU1Clip_C_ring_hybrid_once_fold
.vu
.init_vf_all
.init_vi_all
--enter
--endenter
    fcset      0x000000
    lq         mvp[0], 0(vi00)
    lq         mvp[1], 1(vi00)
    lq         mvp[2], 2(vi00)
    lq         mvp[3], 3(vi00)
    lq         singleColor, 7(vi00)
    lq         fogParams, 8(vi00)
    add.x      fogParams, vf00, fogParams[z]
    lq         spotPos, 12(vi00)
    lq         spotDirV, 13(vi00)
    lq         spotColV, 14(vi00)
    lq         cvec, 20(vi00)
    move       sjudge, cvec
begin:
    xtop       buffer
    lq.xyz     scale, 0(buffer)
    lq         primTag, 1(buffer)
    iaddiu     vertexData, buffer, 2
    ilw.w      vertexCount, 0(buffer)
    iaddiu     countMask, vi00, 1023
    iand       vertexCount, vertexCount, countMask
    iadd       colorData, vertexData, vertexCount
    ilw.y      sceFlag, 8(vi00)
    iblez      sceFlag, setDestAddrColor
    iadd       destAddress, colorData, vertexCount
    b          setDestAddr
setDestAddrColor:
    ilw.x      sceFlag, 8(vi00)
    iblez      sceFlag, setDestAddrMultiColor
    iadd       destAddress, vertexData, vertexCount
    b          setDestAddr
setDestAddrMultiColor:
    iadd       destAddress, colorData, vertexCount
setDestAddr:
    isw.x      destAddress, 1016(vi00)
    isw.y      destAddress, 1016(vi00)
    isw.z      buffer, 1016(vi00)
    ilw.w      sceFlag, 1(buffer)
    iaddiu     buffer, vi00, 1
    iand       buffer, sceFlag, buffer
    isw.w      buffer, 1016(vi00)
    iaddiu     buffer, vi00, 2
    iand       buffer, sceFlag, buffer
    iaddiu     sceFlag, vi00, 3
    isub       sceFlag, sceFlag, buffer
    isw.x      sceFlag, 1017(vi00)
    isw.y      vi00, 1017(vi00)
    iaddiu     outCount, vi00, 0
    ilw.x      sceFlag, 1017(vi00)
    iadd       vertexCounter, vertexCount, sceFlag
    iaddi      vertexCounter, vertexCounter, -3
triLoop:
    ilw.y      destAddress, 1016(vi00)
    ilw.z      sceFlag, 1016(vi00)
    lq         primTag, 1(sceFlag)
    sub.w      primTag, vf00, vf00
    lq         gifSetTag, 19(vi00)
    lq         lodGifTag, 9(vi00)
    lq         testsTag, 10(vi00)
    lq         alphaGifTag, 21(vi00)
    sq         gifSetTag, 0(destAddress)
    sq         testsTag, 1(destAddress)
    sq         gifSetTag, 2(destAddress)
    sq         lodGifTag, 3(destAddress)
    sq         gifSetTag, 4(destAddress)
    sq         alphaGifTag, 5(destAddress)
    sq         primTag, 6(destAddress)
    iaddiu     destAddress, destAddress, 7
    iaddiu     outCount, vi00, 0
    ilw.w      sceFlag, 1016(vi00)
    iadd       sceFlag, vertexData, sceFlag
    lq         vertex1, 0(sceFlag)
    ilw.w      sceFlag, 1016(vi00)
    isub       sceFlag, vertexData, sceFlag
    iaddiu     sceFlag, sceFlag, 1
    lq         vertex2, 0(sceFlag)
    lq         vertex3, 2(vertexData)
    ilw.y      sceFlag, 8(vi00)
    iblez      sceFlag, sharedColorMode
    ilw.w      sceFlag, 1016(vi00)
    iadd       sceFlag, colorData, sceFlag
    lq.xyz     normal1, 0(sceFlag)
    ilw.w      sceFlag, 1016(vi00)
    isub       sceFlag, colorData, sceFlag
    iaddiu     sceFlag, sceFlag, 1
    lq.xyz     normal2, 0(sceFlag)
    lq.xyz     normal3, 2(colorData)
    lq.xyz     lightMatrix[0], 4(vi00)
    lq.xyz     lightMatrix[1], 5(vi00)
    lq.xyz     lightMatrix[2], 6(vi00)
    lq.xyz     lightDirections[0], 12(vi00)
    lq.xyz     lightDirections[1], 13(vi00)
    lq.xyz     lightDirections[2], 14(vi00)
    lq.xyz     lightColors[0], 15(vi00)
    lq.xyz     lightColors[1], 16(vi00)
    lq.xyz     lightColors[2], 17(vi00)
    lq         ambientColor, 18(vi00)
    mula.xyz   acc, lightDirections[0], normal1[x]
    madda.xyz  acc, lightDirections[1], normal1[y]
    madd.xyz   color1, lightDirections[2], normal1[z]
    mini.xyz   color1, color1, vf00[w]
    max.xyz    color1, color1, ambientColor[w]
    mula.xyz   acc, lightColors[0], color1[x]
    madda.xyz  acc, lightColors[1], color1[y]
    madda.xyz  acc, lightColors[2], color1[z]
    madd.xyz   color1, ambientColor, vf00[w]
    loi        255
    mini.xyz   color1, color1, i
    max.xyz    color1, color1, vf00[x]
    loi        128
    add.w      color1, vf00, i
    mula.xyz   acc, lightDirections[0], normal2[x]
    madda.xyz  acc, lightDirections[1], normal2[y]
    madd.xyz   color2, lightDirections[2], normal2[z]
    mini.xyz   color2, color2, vf00[w]
    max.xyz    color2, color2, ambientColor[w]
    mula.xyz   acc, lightColors[0], color2[x]
    madda.xyz  acc, lightColors[1], color2[y]
    madda.xyz  acc, lightColors[2], color2[z]
    madd.xyz   color2, ambientColor, vf00[w]
    loi        255
    mini.xyz   color2, color2, i
    max.xyz    color2, color2, vf00[x]
    loi        128
    add.w      color2, vf00, i
    mula.xyz   acc, lightDirections[0], normal3[x]
    madda.xyz  acc, lightDirections[1], normal3[y]
    madd.xyz   color3, lightDirections[2], normal3[z]
    mini.xyz   color3, color3, vf00[w]
    max.xyz    color3, color3, ambientColor[w]
    mula.xyz   acc, lightColors[0], color3[x]
    madda.xyz  acc, lightColors[1], color3[y]
    madda.xyz  acc, lightColors[2], color3[z]
    madd.xyz   color3, ambientColor, vf00[w]
    loi        255
    mini.xyz   color3, color3, i
    max.xyz    color3, color3, vf00[x]
    loi        128
    add.w      color3, vf00, i
    b          sharedAttributesReady
sharedColorMode:
    ibltz      sceFlag, litColorMode
    ilw.x      sceFlag, 8(vi00)
    iblez      sceFlag, multiColorNoSpot
    add        color1, vf00, singleColor
    add        color2, vf00, singleColor
    add        color3, vf00, singleColor
    b          processingNoSpot
multiColorNoSpot:
    ilw.w      sceFlag, 1016(vi00)
    iadd       sceFlag, colorData, sceFlag
    lq         color1, 0(sceFlag)
    ilw.w      sceFlag, 1016(vi00)
    isub       sceFlag, colorData, sceFlag
    iaddiu     sceFlag, sceFlag, 1
    lq         color2, 0(sceFlag)
    lq         color3, 2(colorData)
processingNoSpot:
    loi        255
    mini.xyz   color1, color1, i
    loi        255
    mini.xyz   color2, color2, i
    loi        255
    mini.xyz   color3, color3, i
    b          sharedAttributesReady
litColorMode:
    ilw.x      sceFlag, 8(vi00)
    iblez      sceFlag, multiColor
    add        color1, vf00, singleColor
    add        color2, vf00, singleColor
    add        color3, vf00, singleColor
    b          processing
multiColor:
    ilw.w      sceFlag, 1016(vi00)
    iadd       sceFlag, colorData, sceFlag
    lq         color1, 0(sceFlag)
    ilw.w      sceFlag, 1016(vi00)
    isub       sceFlag, colorData, sceFlag
    iaddiu     sceFlag, sceFlag, 1
    lq         color2, 0(sceFlag)
    lq         color3, 2(colorData)
processing:
    sub.xyz    spotD, vertex1, spotPos
    mul.xyz    spotSq, spotD, spotD
    add.x      spotDist, spotSq, spotSq[y]
    add.x      spotDist, spotDist, spotSq[z]
    mul.xyz    spotTm, spotD, spotDirV
    add.x      spotT, spotTm, spotTm[y]
    add.x      spotT, spotT, spotTm[z]
    max.x      spotT, spotT, vf00[x]
    mul.x      spotT, spotT, spotT
    mul.x      spotC, spotDist, spotDirV[w]
    sub.x      spotC, spotT, spotC
    mul.x      spotC, spotC, spotColV[w]
    mini.x     spotC, spotC, vf00[w]
    max.x      spotC, spotC, vf00[x]
    add.x      acc, vf00, vf00[w]
    msub.x     spotA, spotDist, spotPos[w]
    mini.x     spotA, spotA, vf00[w]
    max.x      spotA, spotA, vf00[x]
    mul.x      spotC, spotC, spotA
    mul.xyz    spotD, spotColV, spotC[x]
    add.xyz    color1, color1, spotD
    sub.xyz    spotD, vertex2, spotPos
    mul.xyz    spotSq, spotD, spotD
    add.x      spotDist, spotSq, spotSq[y]
    add.x      spotDist, spotDist, spotSq[z]
    mul.xyz    spotTm, spotD, spotDirV
    add.x      spotT, spotTm, spotTm[y]
    add.x      spotT, spotT, spotTm[z]
    max.x      spotT, spotT, vf00[x]
    mul.x      spotT, spotT, spotT
    mul.x      spotC, spotDist, spotDirV[w]
    sub.x      spotC, spotT, spotC
    mul.x      spotC, spotC, spotColV[w]
    mini.x     spotC, spotC, vf00[w]
    max.x      spotC, spotC, vf00[x]
    add.x      acc, vf00, vf00[w]
    msub.x     spotA, spotDist, spotPos[w]
    mini.x     spotA, spotA, vf00[w]
    max.x      spotA, spotA, vf00[x]
    mul.x      spotC, spotC, spotA
    mul.xyz    spotD, spotColV, spotC[x]
    add.xyz    color2, color2, spotD
    sub.xyz    spotD, vertex3, spotPos
    mul.xyz    spotSq, spotD, spotD
    add.x      spotDist, spotSq, spotSq[y]
    add.x      spotDist, spotDist, spotSq[z]
    mul.xyz    spotTm, spotD, spotDirV
    add.x      spotT, spotTm, spotTm[y]
    add.x      spotT, spotT, spotTm[z]
    max.x      spotT, spotT, vf00[x]
    mul.x      spotT, spotT, spotT
    mul.x      spotC, spotDist, spotDirV[w]
    sub.x      spotC, spotT, spotC
    mul.x      spotC, spotC, spotColV[w]
    mini.x     spotC, spotC, vf00[w]
    max.x      spotC, spotC, vf00[x]
    add.x      acc, vf00, vf00[w]
    msub.x     spotA, spotDist, spotPos[w]
    mini.x     spotA, spotA, vf00[w]
    max.x      spotA, spotA, vf00[x]
    mul.x      spotC, spotC, spotA
    mul.xyz    spotD, spotColV, spotC[x]
    add.xyz    color3, color3, spotD
    loi        255
    mini.xyz   color1, color1, i
    loi        255
    mini.xyz   color2, color2, i
    loi        255
    mini.xyz   color3, color3, i
sharedAttributesReady:
    mula       acc, mvp[0], vertex1[x]
    madda      acc, mvp[1], vertex1[y]
    madda      acc, mvp[2], vertex1[z]
    madd       vertex1, mvp[3], vertex1[w]
    mula       acc, mvp[0], vertex2[x]
    madda      acc, mvp[1], vertex2[y]
    madda      acc, mvp[2], vertex2[z]
    madd       vertex2, mvp[3], vertex2[w]
    mula       acc, mvp[0], vertex3[x]
    madda      acc, mvp[1], vertex3[y]
    madda      acc, mvp[2], vertex3[z]
    madd       vertex3, mvp[3], vertex3[w]
    clipw.xyz  vertex1, vertex1
    sub.x      sjudge, cvec, vertex1[z]
    add.y      sjudge, cvec, vertex1[z]
    clipw.xyz  sjudge, sjudge
    clipw.xyz  vertex2, vertex2
    sub.x      sjudge, cvec, vertex2[z]
    add.y      sjudge, cvec, vertex2[z]
    clipw.xyz  sjudge, sjudge
    fcand      VI01, 0x3CA3CA
    iadd       triOr, VI01, vi00
    clipw.xyz  vertex3, vertex3
    sub.x      sjudge, cvec, vertex3[z]
    add.y      sjudge, cvec, vertex3[z]
    clipw.xyz  sjudge, sjudge
    fcand      VI01, 0x3CA
    ior        triOr, triOr, VI01
    sq         vertex1, 956(vi00)
    sq         color1, 957(vi00)
    sq         vertex2, 958(vi00)
    sq         color2, 959(vi00)
    sq         vertex3, 960(vi00)
    sq         color3, 961(vi00)
    iaddiu     srcBase, vi00, 956
    iaddiu     srcEnd, srcBase, 6
    ibeq       triOr, vi00, fanStart
    xtop       buffer
    ilw.w      buffer, 0(buffer)
    iaddiu     planePtr, vi00, 944
    iaddiu     dstBase, vi00, 986
planeLoop:
    ibltz      buffer, planeActive
    b          planeAdvance
planeActive:
    lq         planeV, 0(planePtr)
    lq         planeE, 1(planePtr)
    iaddi      prevPtr, srcEnd, -2
    lq         ppos, 0(prevPtr)
    lq         pcol, 1(prevPtr)
    mul        dtmp, ppos, planeV
    add.x      dtmp, dtmp, dtmp[y]
    add.x      dtmp, dtmp, dtmp[z]
    add.x      dtmp, dtmp, dtmp[w]
    add.x      pd, dtmp, planeE[x]
    iadd       curPtr, srcBase, vi00
    iadd       dstPtr, dstBase, vi00
edgeLoop:
    lq         cpos, 0(curPtr)
    lq         ccol, 1(curPtr)
    mul        dtmp, cpos, planeV
    add.x      dtmp, dtmp, dtmp[y]
    add.x      dtmp, dtmp, dtmp[z]
    add.x      dtmp, dtmp, dtmp[w]
    add.x      cd, dtmp, planeE[x]
    add.x      sjudge, vf00, pd[x]
    add.y      sjudge, vf00, cd[x]
    sub.xy     sjudge, sjudge, cvec[w]
    clipw.xyz  sjudge, sjudge
    fcand      VI01, 0x2
    iadd       vertMask, VI01, vi00
    fcand      VI01, 0x8
    ibne       vertMask, VI01, edgeCross
    ibne       VI01, vi00, edgeAdvance
    b          edgeEmitCur
edgeCross:
    sub.x      dtmp, pd, cd[x]
    div        q, pd[x], dtmp[x]
    sub        lerpP, cpos, ppos
    mul        lerpP, lerpP, q
    add        lerpP, lerpP, ppos
    sub        lerpC, ccol, pcol
    mul        lerpC, lerpC, q
    add        lerpC, lerpC, pcol
    sq         lerpP, 0(dstPtr)
    sq         lerpC, 1(dstPtr)
    iaddiu     dstPtr, dstPtr, 2
    ibne       VI01, vi00, edgeAdvance
edgeEmitCur:
    sq         cpos, 0(dstPtr)
    sq         ccol, 1(dstPtr)
    iaddiu     dstPtr, dstPtr, 2
edgeAdvance:
    move       ppos, cpos
    move       pcol, ccol
    add.x      pd, vf00, cd[x]
    iaddiu     curPtr, curPtr, 2
    ibne       curPtr, srcEnd, edgeLoop
    ibeq       dstPtr, dstBase, triNext
    iadd       srcEnd, dstPtr, vi00
    iadd       vertMask, srcBase, vi00
    iadd       srcBase, dstBase, vi00
    iadd       dstBase, vertMask, vi00
planeAdvance:
    iaddiu     planePtr, planePtr, 2
    iadd       buffer, buffer, buffer
    iaddiu     vertMask, vi00, 956
    ibne       planePtr, vertMask, planeLoop
fanStart:
    iaddiu     vertMask, srcBase, 6
    isub       vertMask, srcEnd, vertMask
    ibltz      vertMask, triNext
    iaddiu     fanPtr, srcBase, 2
fanLoop:
    iaddiu     fanNext, fanPtr, 2
    ibeq       fanNext, srcEnd, triNext
    iadd       emitPtr, srcBase, vi00
    iadd       emitNxt, fanPtr, vi00
    iaddiu     emitCnt, vi00, 3
fanEmitLoop:
    lq         ecPos, 0(emitPtr)
    lq         ecCol, 1(emitPtr)
    mul.x      ecFogF, fogParams, ecPos[w]
    add.x      ecFogF, ecFogF, fogParams[w]
    loi        255
    mini.x     ecFogF, ecFogF, i
    max.x      ecFogF, ecFogF, vf00[x]
    ftoi4.x    ecFogF, ecFogF
    mtir       ecFog, ecFogF[x]
    isw.w      ecFog, 1(destAddress)
    div        q, vf00[w], ecPos[w]
    mul.xyz    ecPos, ecPos, q
    mula.xyz   acc, scale, vf00[w]
    madd.xyz   ecPos, ecPos, scale
    ftoi4.xyz  ecPos, ecPos
    max.xyz    ecCol, ecCol, vf00[x]
    ftoi0      ecCol, ecCol
    sq         ecCol, 0(destAddress)
    sq.xyz     ecPos, 1(destAddress)
    iaddiu     destAddress, destAddress, 2
    iadd       emitPtr, emitNxt, vi00
    iadd       emitNxt, fanNext, vi00
    iaddi      emitCnt, emitCnt, -1
    ibgtz      emitCnt, fanEmitLoop
    iaddiu     outCount, outCount, 3
    iadd       fanPtr, fanNext, vi00
    b          fanLoop
triNext:
    ilw.y      sceFlag, 1016(vi00)
    iaddiu     buffer, outCount, 16384
    iaddiu     buffer, buffer, 16384
    isw.x      buffer, 6(sceFlag)
    ilw.y      buffer, 1017(vi00)
    iadd       buffer, sceFlag, buffer
    xgkick     buffer
--barrier
    iaddiu     buffer, vi00, 6
    isw.y      buffer, 1017(vi00)
    ilw.x      buffer, 1016(vi00)
    iadd       buffer, buffer, buffer
    iaddiu     buffer, buffer, 49
    isub       buffer, buffer, sceFlag
    isw.y      buffer, 1016(vi00)
    ilw.x      sceFlag, 1017(vi00)
    iadd       vertexData, vertexData, sceFlag
    iadd       colorData, colorData, sceFlag
    ilw.x      buffer, 1017(vi00)
    iaddi      buffer, buffer, -1
    ibgtz      buffer, keepListParity
    ilw.w      buffer, 1016(vi00)
    iaddiu     sceFlag, vi00, 1
    isub       sceFlag, sceFlag, buffer
    isw.w      sceFlag, 1016(vi00)
keepListParity:
    ilw.x      sceFlag, 1017(vi00)
    isub       vertexCounter, vertexCounter, sceFlag
    ibgtz      vertexCounter, triLoop
--cont
    b          begin
--exit
--endexit
