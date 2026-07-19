;//--------------------------------------------------------------------------------
;// Tyra's standard macros library
;//--------------------------------------------------------------------------------

;//---------------------------------------------------------
;// LoadTyraStaticData - Load "Set" gif tag
;//---------------------------------------------------------
#macro LoadTyraStaticData: t_gifSetTagName
   lq             t_gifSetTagName, VU1_SET_GIFTAG_ADDR(vi00)
#endmacro

;//---------------------------------------------------------
;// LoadTyraLightMatrix - Loads single color. Color is placed in 4th slot of Lights matrix
;//---------------------------------------------------------
#macro LoadTyraSingleColor: t_singleColor, t_singleColorEnabled, t_singleColorAddr, t_optionsAddr
   lq       t_singleColor,          t_singleColorAddr(vi00)
   ilw.x    t_singleColorEnabled,   t_optionsAddr(vi00)
#endmacro

;//---------------------------------------------------------
;// LoadTyraLerpValue - Loads interpolation value.
;//---------------------------------------------------------
#macro LoadTyraLerpValue: t_lerpValue, t_optionsAddr
   lq.y  t_lerpValue, t_optionsAddr(vi00)
#endmacro

;//---------------------------------------------------------
;// LoadTyraScaleValue - Loads screen scales.
;//---------------------------------------------------------
#macro LoadTyraScaleValue: t_scale, t_buffer
   lq.xyz  t_scale,  0(t_buffer)
#endmacro

;//---------------------------------------------------------
;// LoadTyraTags - Load lod, texture buffer and clut
;// 1 - GIF tag - texture LOD
;// 2 - GIF tag - Z buffer tests method
;// 3 - GIF tag - texture buffer & CLUT
;//---------------------------------------------------------
#macro LoadTyraTagsTexture: t_lodGifTag, t_testsGifTag, t_texBufferClutGifTag, t_lodAddr, t_testsAddr, t_ClutAddr
   lq      t_lodGifTag,             t_lodAddr(vi00)
   lq      t_testsGifTag,           t_testsAddr(vi00)
   lq      t_texBufferClutGifTag,   t_ClutAddr(vi00)
#endmacro

;//---------------------------------------------------------
;// LoadTyraTags - Load lod, texture buffer and clut
;// 1 - GIF tag - texture LOD
;// 2 - GIF tag - Z buffer tests method
;//---------------------------------------------------------
#macro LoadTyraTags: t_lodGifTag, t_testsGifTag, t_lodAddr, t_testsAddr
   lq      t_lodGifTag,             t_lodAddr(vi00)
   lq      t_testsGifTag,           t_testsAddr(vi00)
#endmacro

;//---------------------------------------------------------
;// LoadTyraPrimTag - Load prim tag
;// 2 - GIF tag - tell GS how many data we will send
;//---------------------------------------------------------
#macro LoadTyraPrimTag: t_primTag, t_buffer
   lq      t_primTag,   1(t_buffer)
#endmacro

;//---------------------------------------------------------
;// LoadTyraBufferTags - Load scales and prim tag
;// 1 - float : X, Y, Z - scale vector that we will use to scale the verts after projecting them, float : W - vert count.
;// 2 - GIF tag - tell GS how many data we will send
;//---------------------------------------------------------
#macro LoadTyraBufferTags: t_scale, t_primTag, t_buffer
   lq.xyz  t_scale,                  0(t_buffer)
   lq      t_primTag,                1(t_buffer)
#endmacro

;//---------------------------------------------------------
;// LoadTyraDirectionalLights - Load directions and colors. All are always present
;// - 3 directional lights directions
;// - 3 directional lights colors + ambient color
;//---------------------------------------------------------
#macro LoadTyraDirectionalLights: t_lightMatrix, t_lightDirections, t_lightsColors, t_ambientColor, t_dirOffset, t_colorOffset, t_matrixOffset
   lq.xyz      t_lightMatrix[0],       t_matrixOffset+0(vi00)
   lq.xyz      t_lightMatrix[1],       t_matrixOffset+1(vi00)
   lq.xyz      t_lightMatrix[2],       t_matrixOffset+2(vi00)
   lq.xyz      t_lightDirections[0],   t_dirOffset(vi00)
   lq.xyz      t_lightDirections[1],   t_dirOffset+1(vi00)
   lq.xyz      t_lightDirections[2],   t_dirOffset+2(vi00)
   lq.xyz      t_lightsColors[0],      t_colorOffset(vi00)
   lq.xyz      t_lightsColors[1],      t_colorOffset+1(vi00)
   lq.xyz      t_lightsColors[2],      t_colorOffset+2(vi00)
   lq.xyz      t_ambientColor,         t_colorOffset+3(vi00)
#endmacro

;//---------------------------------------------------------
;// StoreTyraGifTagsTexture - Store gif tags. 
;// Not using sqi instruction, because VCL cannot optimize it.
;// Primtag contains information about how many polys we will send
;//---------------------------------------------------------
#macro StoreTyraGifTagsTexture: t_gifSetTag, t_lodGifTag, t_texBufferClutGifTag, t_primTag, t_testsTag, t_destAddress
   sq t_gifSetTag,            0(t_destAddress)
   sq t_testsTag,             1(t_destAddress)
   sq t_gifSetTag,            2(t_destAddress)
   sq t_lodGifTag,            3(t_destAddress)
   sq t_gifSetTag,            4(t_destAddress)
   sq t_texBufferClutGifTag,  5(t_destAddress)
   sq t_primTag,              6(t_destAddress)
   iaddiu                     t_destAddress,    t_destAddress,    7
#endmacro

;//---------------------------------------------------------
;// StoreTyraGifTags - Store gif tags. 
;// Not using sqi instruction, because VCL cannot optimize it.
;// Primtag contains information about how many polys we will send
;//---------------------------------------------------------
#macro StoreTyraGifTags: t_gifSetTag, t_lodGifTag, t_primTag, t_testsTag, t_destAddress
   sq t_gifSetTag,            0(t_destAddress)
   sq t_testsTag,             1(t_destAddress)
   sq t_gifSetTag,            2(t_destAddress)
   sq t_lodGifTag,            3(t_destAddress)
   sq t_primTag,              4(t_destAddress)
   iaddiu                     t_destAddress,    t_destAddress,    5
#endmacro

;//---------------------------------------------------------
;// Modified by TyraX: StoreTyraGifTags*Alpha - the StaPip variants of the
;// store macros above with one more (set, A+D) pair: the GS ALPHA register
;// (blend equation), uploaded per mesh to VU1_ALPHA_ADDR. Every StaPip
;// program emits it, so the equation is part of the ordered GIF stream
;// (default alpha-over; additive for the reflective-material env pass) and
;// no CPU-side FINISH barrier is needed to switch it. The dynamic pipeline
;// keeps the original 7/5-qword macros - its C++ side neither uploads the
;// ALPHA qword nor accounts for the bigger tag block in maxVertCount.
;// stapip_vu1_program.cpp getMaxVertCount reserves 9 qwords for this block;
;// the clip programs patch the prim NLOOP at offset 8 (texture) / 6 (plain).
;//---------------------------------------------------------
#macro StoreTyraGifTagsTextureAlpha: t_gifSetTag, t_lodGifTag, t_texBufferClutGifTag, t_alphaGifTag, t_primTag, t_testsTag, t_destAddress
   sq t_gifSetTag,            0(t_destAddress)
   sq t_testsTag,             1(t_destAddress)
   sq t_gifSetTag,            2(t_destAddress)
   sq t_lodGifTag,            3(t_destAddress)
   sq t_gifSetTag,            4(t_destAddress)
   sq t_texBufferClutGifTag,  5(t_destAddress)
   sq t_gifSetTag,            6(t_destAddress)
   sq t_alphaGifTag,          7(t_destAddress)
   sq t_primTag,              8(t_destAddress)
   iaddiu                     t_destAddress,    t_destAddress,    9
#endmacro

#macro StoreTyraGifTagsAlpha: t_gifSetTag, t_lodGifTag, t_alphaGifTag, t_primTag, t_testsTag, t_destAddress
   sq t_gifSetTag,            0(t_destAddress)
   sq t_testsTag,             1(t_destAddress)
   sq t_gifSetTag,            2(t_destAddress)
   sq t_lodGifTag,            3(t_destAddress)
   sq t_gifSetTag,            4(t_destAddress)
   sq t_alphaGifTag,          5(t_destAddress)
   sq t_primTag,              6(t_destAddress)
   iaddiu                     t_destAddress,    t_destAddress,    7
#endmacro

;// Modified by TyraX: loads the per-mesh ALPHA A+D qword (see above).
#macro LoadTyraAlphaTag: t_alphaGifTag, t_alphaAddr
   lq      t_alphaGifTag,           t_alphaAddr(vi00)
#endmacro

;//---------------------------------------------------------
;// Modified by TyraX: env (matcap) programs - sphere-mapped reflective
;// materials. The vertex stream's ST slot carries the OBJECT-SPACE NORMAL;
;// the ST is computed on VU1 from the per-mesh camera basis uploaded at
;// VU1_ENV_BASIS_ADDR (3 qwords, reusing the lights matrix area - env bags
;// never carry lighting):
;//   qw+0  camera right (xyz)
;//   qw+1  camera up    (xyz)
;//   qw+2  constants (0.5, -0.5, 1.0, 0.5)
;//---------------------------------------------------------
#macro LoadTyraEnvBasis: t_envRight, t_envUp, t_envConsts
   lq      t_envRight,     VU1_ENV_BASIS_ADDR(vi00)
   lq      t_envUp,        VU1_ENV_BASIS_ADDR+1(vi00)
   lq      t_envConsts,    VU1_ENV_BASIS_ADDR+2(vi00)
#endmacro

;// Turns the normal in t_stq into a matcap STQ, in place:
;//   s = 0.5 + 0.5 * dot(normalize(n), right)
;//   t = 0.5 - 0.5 * dot(normalize(n), up)   (image space, v = 0 at the top)
;//   q = 1.0                                 (the standard mulq-by-1/w follows)
;// The editor viewport shader and the generated game's EE fallback mirror
;// this formula - keep the three in sync.
;// The normal is RE-NORMALIZED first: the EE clipper lerps it across clip
;// cuts, and a lerped normal is SHORT - without this, every clipped chunk's
;// ST collapsed toward the sphere-map center (dark smudges on screen-edge
;// geometry). Costs an rsqrt; also covers scaled models' normals.
;// USES THE Q REGISTER (rsqrt): call this BEFORE the position's div q /
;// VertexPersCorr, or the later mulq picks up the wrong Q.
;// Structure: inlined normalize + two dot products (vclpp does not expand
;// nested macros), then the bias+scale assembled through the accumulator.
;// WARNING: no ";" comment lines inside a #macro body - vclpp silently
;// swallows the whole expansion of such a macro (drops every call site).
#macro CalculateTyraEnvStq: t_stq, t_envRight, t_envUp, t_envConsts
   mul.xyz  tyraEnvLen,  t_stq,       t_stq
   add.x    tyraEnvLen,  tyraEnvLen,  tyraEnvLen[y]
   add.x    tyraEnvLen,  tyraEnvLen,  tyraEnvLen[z]
   rsqrt    q,           vf00[w],     tyraEnvLen[x]
   mul.xyz  tyraEnvNrm,  t_stq,       q
   mul.xyz  tyraEnvDotR, tyraEnvNrm,  t_envRight
   add.x    tyraEnvDotR, tyraEnvDotR, tyraEnvDotR[y]
   add.x    tyraEnvDotR, tyraEnvDotR, tyraEnvDotR[z]
   mul.xyz  tyraEnvDotU, tyraEnvNrm,  t_envUp
   add.x    tyraEnvDotU, tyraEnvDotU, tyraEnvDotU[y]
   add.x    tyraEnvDotU, tyraEnvDotU, tyraEnvDotU[z]
   add.xy   acc,     vf00,          t_envConsts[w]
   add.z    acc,     vf00,          t_envConsts[z]
   madd.x   t_stq,   t_envConsts,   tyraEnvDotR[x]
   madd.y   t_stq,   t_envConsts,   tyraEnvDotU[x]
   madd.z   t_stq,   vf00,          vf00[x]
#endmacro

;//---------------------------------------------------------
;// CalculateLights - Based on Dr Fortuna's work
;//
;// 1. Transform by the rotation part of the world matrix
;// 2. "Transform" the normal by the light direction matrix
;// 3. Four intensities, one for each light.
;// 4. Clamp the intensity to 0..1
;// 5. Transform the intensities by the light colour matrix
;// 6. Load 128 and put it into the alpha value
;// 7. Clamp result to 0-128 values
;// 8. And write to the output buffer
;//---------------------------------------------------------
#macro CalculateTyraDirectionalLights: t_outputColor, t_normal, t_lightDirections, t_lightColors, t_lightMatrix, t_ambientColor
   mul.xyz     acc,              t_lightMatrix[0],       t_normal[x]	
   madd.xyz    acc,              t_lightMatrix[1],       t_normal[y]
   madd.xyz    t_normal,         t_lightMatrix[2],       t_normal[z]
   mula.xyz    acc,              t_lightDirections[0],   t_normal[x]
   madd.xyz    acc,              t_lightDirections[1],   t_normal[y]
   madd.xyz    t_outputColor,    t_lightDirections[2],   t_normal[z]
   mini.xyz    t_outputColor,    t_outputColor,          vf00[w]
   max.xyz     t_outputColor,    t_outputColor,          vf00[x]
   mula.xyz    acc,              t_lightColors[0],       t_outputColor[x]
   madda.xyz   acc,              t_lightColors[1],       t_outputColor[y]
   madda.xyz   acc,              t_lightColors[2],       t_outputColor[z]
   madd.xyz    t_outputColor,    t_ambientColor,         vf00[w]
   loi         128
	addi.w      t_outputColor,    vf00,    i
#endmacro

;//---------------------------------------------------------
;// LerpXYZ - Linear interpolation between two points
;//---------------------------------------------------------
#macro LerpXYZ: t_output, t_from, t_to, t_interp
   sub.xyz   temp1,      t_to,    t_from
   mul.xyz   temp2,      temp1,   t_interp[y]
   add.xyz   t_output,   temp2,   t_from
#endmacro

;//---------------------------------------------------------
;// Lerp - Linear interpolation between two points
;//---------------------------------------------------------
#macro Lerp: t_output, t_from, t_to, t_interp
   sub   temp1,      t_to,    t_from
   mul   temp2,      temp1,   t_interp[y]
   add   t_output,   temp2,   t_from
#endmacro

;//---------------------------------------------------------
;// Modified by TyraX: GS hardware distance fog.
;//
;// LoadTyraFogParams - Load the options quadword. Fog uses:
;//   z - fogScale  = -255 / (fogEnd - fogStart)
;//   w - fogOffset = 255 * fogEnd / (fogEnd - fogStart)
;// (x holds singleColorEnabled, y holds dynpip interpolation)
;//---------------------------------------------------------
#macro LoadTyraFogParams: t_fogParams, t_optionsAddr
   lq          t_fogParams,   t_optionsAddr(vi00)
#endmacro

;//---------------------------------------------------------
;// MakeTyraAdcMask - Build the 0x8000 ADC bit mask once per
;// program run (iaddiu immediates are 15-bit, so add twice).
;//---------------------------------------------------------
#macro MakeTyraAdcMask: t_adcMask
   iaddiu      t_adcMask,     vi00,          0x4000
   iadd        t_adcMask,     t_adcMask,     t_adcMask
#endmacro

;//---------------------------------------------------------
;// CalculateTyraFog - Per-vertex GS fog coefficient.
;// t_vertex.w must still hold the clip-space W (view distance);
;// every macro in the pipeline leaves W untouched, so this can
;// run right before the vertex store.
;// F = clamp(w * fogScale + fogOffset, 0, 255). ftoi4 yields
;// F<<4, which is exactly the F field position of packed XYZF2
;// (word3 bits 4-11; the 4 fraction bits fall into ignored
;// bits 0-3). GS blends Cout = (F*Cin + (255-F)*FOGCOL) >> 8,
;// so F=255 means no fog.
;//---------------------------------------------------------
#macro CalculateTyraFog: t_fogInt, t_vertex, t_fogParams
   add.x       fogAccum,      vf00,          t_vertex[w]
   mul.x       fogAccum,      fogAccum,      t_fogParams[z]
   add.x       fogAccum,      fogAccum,      t_fogParams[w]
   loi         255
   mini.x      fogAccum,      fogAccum,      i
   max.x       fogAccum,      fogAccum,      vf00[x]
   ftoi4.x     fogAccum,      fogAccum
   mtir        t_fogInt,      fogAccum[x]
#endmacro

;//---------------------------------------------------------
;// PerformTyraFogClipCheck - PerformClipCheck variant that also
;// stores the fog coefficient. Upstream stores 0x7FFF/0x8000 in
;// the W word (only bit 15 = ADC matters for XYZ2), but packed
;// XYZF2 reads F from bits 4-11, so the ADC decision is masked
;// down to bit 15 before OR-ing the fog bits in.
;//---------------------------------------------------------
#macro PerformTyraFogClipCheck: t_vertex, t_destAddress, t_destAddressOffset, t_fogInt, t_adcMask
   clipw.xyz   t_vertex,      t_vertex
   fcand       VI01,          0x3FFFF
   iaddiu      adcBit,        VI01,          0x7FFF
   iand        adcBit,        adcBit,        t_adcMask
   ior         adcBit,        adcBit,        t_fogInt
   isw.w       adcBit,        t_destAddressOffset(t_destAddress)
#endmacro

;//---------------------------------------------------------
;// StoreTyraFog - Store the fog coefficient with ADC = 0, for
;// the as_is programs (geometry already clipped on the EE).
;//---------------------------------------------------------
#macro StoreTyraFog: t_fogInt, t_destAddress, t_destAddressOffset
   isw.w       t_fogInt,      t_destAddressOffset(t_destAddress)
#endmacro

;//---------------------------------------------------------
;// Modified by TyraX: dynamic spot light (flashlight).
;//
;// LoadTyraSpotLight - Load the three spot light quads. The
;// dir-lights addresses are reused - they are free in the
;// color (C/TC) programs. Layout (built on the EE, in mesh
;// object space - see StaPipQBufferRenderer::sendObjectData):
;//   quad0: position.xyz,  w = 1/objRange^2
;//   quad1: direction.xyz, w = cos^2(halfAngle)
;//   quad2: color.rgb,     w = softness/(objRange^2*(1-cos^2))
;//---------------------------------------------------------
#macro LoadTyraSpotLight: t_spotPos, t_spotDir, t_spotCol, t_addr
   lq          t_spotPos,     t_addr+0(vi00)
   lq          t_spotDir,     t_addr+1(vi00)
   lq          t_spotCol,     t_addr+2(vi00)
#endmacro

;//---------------------------------------------------------
;// CalculateTyraSpotLight - additive cone + distance falloff
;// on top of the baked vertex color, no N.L (the color paths
;// carry no normals). Works on the OBJECT-space vertex, so it
;// must run before MatrixMultiplyVertex overwrites it.
;// Mirrors addSpotToColor in stapip_clipper.cpp (the EE bakes
;// the same formula into colors for EE-clipped triangles) -
;// keep the two in sync.
;//   d      = vertex - spotPos
;//   dist2  = d.d
;//   t      = max(0, d.spotDir)
;//   cone   = clamp((t^2 - cos^2 * dist2) * invSoft, 0, 1)
;//   axial  = clamp(1 - dist2 * invRange2, 0, 1)
;//   color += spotColor.rgb * cone * axial
;//---------------------------------------------------------
#macro CalculateTyraSpotLight: t_color, t_vertex, t_spotPos, t_spotDir, t_spotCol
   sub.xyz     spotD,         t_vertex,      t_spotPos
   mul.xyz     spotSq,        spotD,         spotD
   add.x       spotDist,      spotSq,        spotSq[y]
   add.x       spotDist,      spotDist,      spotSq[z]
   mul.xyz     spotTm,        spotD,         t_spotDir
   add.x       spotT,         spotTm,        spotTm[y]
   add.x       spotT,         spotT,         spotTm[z]
   max.x       spotT,         spotT,         vf00[x]
   mul.x       spotT,         spotT,         spotT
   mul.x       spotC,         spotDist,      t_spotDir[w]
   sub.x       spotC,         spotT,         spotC
   mul.x       spotC,         spotC,         t_spotCol[w]
   mini.x      spotC,         spotC,         vf00[w]
   max.x       spotC,         spotC,         vf00[x]
   adda.x      acc,           vf00,          vf00[w]
   msub.x      spotA,         spotDist,      t_spotPos[w]
   mini.x      spotA,         spotA,         vf00[w]
   max.x       spotA,         spotA,         vf00[x]
   mul.x       spotC,         spotC,         spotA
   mul.xyz     spotAdd,       t_spotCol,     spotC[x]
   add.xyz     t_color,       t_color,       spotAdd
#endmacro