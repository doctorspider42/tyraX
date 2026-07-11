//
// ______       ____   ___
//   |     \/   ____| |___|
//   |     |   |   \  |   |
//-----------------------------------------------------------------------
// Copyright 2022, tyra - https://github.com/h4570/tyra
// Licensed under Apache License 2.0
// Sandro Sobczyński <sandro.sobczynski@gmail.com>
//

// Updated once per mesh
#define VU1_MVP_MATRIX_ADDR 0
#define VU1_LIGHTS_MATRIX_ADDR 4
#define VU1_SINGLE_COLOR_ADDR 7
#define VU1_OPTIONS_ADDR 8
#define VU1_LOD_ADDR 9
#define VU1_Z_TESTS_ADDR 10
#define VU1_CLUT_ADDR 11
#define VU1_LIGHTS_DIRS_ADDR 12
#define VU1_LIGHTS_COLORS_ADDR 15
#define VU1_SET_GIFTAG_ADDR 19
// Modified by tyra-editor: VU1 clipping (clip program family).
// One quad of clip-space constants for the crossing test:
//   x = nearZ - GUARD  (nearZ = clip-space near plane, see PlanesClipAlgorithm)
//   y = -farZ - GUARD  (farZ  = -settings.getFar())
//   z = 0
//   w = GUARD          (bias so one clipw judgement tests both const planes)
#define VU1_CLIP_CONSTS_ADDR 20
#define VU1_STAPIP_LAST_ITEM_ADDR 20

// Bias used to turn the constant near/far plane tests into clipw judgements
// (x < -GUARD bits). Only affects rounding: values within GUARD * 2^-24 of a
// plane may classify either way, which is harmless. C++ side only.
#define VU1_CLIP_GUARD 4096.0F

// Buffer data (xtop)
#define VU1_STAPIP_VERT_DATA_ADDR 2

// Modified by tyra-editor: VU1 clipping scratch at the top of VU1 data memory
// (1024 qwords total). The double buffer is capped at VU1_STAPIP_DBUFFER_END
// so this area is never part of an xtop half. Contents are transient within
// one clip-program run (other pipelines may clobber them between meshes):
//   944..955  six clip planes, 2 qw each: (A,B,C,D) then (E,0,0,0);
//             inside test is dot4(v, ABCD) + E >= 0 (uploaded per mesh)
//   956..985  Sutherland-Hodgman polygon A (10 verts x 3 qw: pos, attr1, attr2)
//   986..1015 Sutherland-Hodgman polygon B
#define VU1_STAPIP_DBUFFER_END 944
#define VU1_CLIP_PLANES_ADDR 944
#define VU1_CLIP_POLY_A_ADDR 956
#define VU1_CLIP_POLY_B_ADDR 986
