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
// Modified by TyraX: VU1 clipping (clip program family).
// One quad of clip-space constants for the crossing test:
//   x = nearZ - GUARD  (nearZ = clip-space near plane, see PlanesClipAlgorithm)
//   y = -farZ - GUARD  (farZ  = -settings.getFar())
//   z = 0
//   w = GUARD          (bias so one clipw judgement tests both const planes)
#define VU1_CLIP_CONSTS_ADDR 20
// Modified by TyraX: per-mesh GS ALPHA register (A+D pair), emitted in-band
// with the other tags by every StaPip program (StoreTyraGifTags*Alpha) - the
// blend equation follows the mesh in the ordered GIF stream, so switching to
// the reflective materials' additive equation needs no FINISH barriers.
#define VU1_ALPHA_ADDR 21
// Modified by TyraX: camera basis (right, up, consts - 3 qwords) for the env
// (matcap) programs. Reuses the lights-matrix area (= VU1_LIGHTS_MATRIX_ADDR;
// env bags never carry lighting). Kept as a LITERAL: vclpp expands #defines
// only one level, an alias reaches dvp-as unresolved.
#define VU1_ENV_BASIS_ADDR 4
// Modified by TyraX: particle billboard basis (right, up - 2 qwords) for the
// billboard program family. Same lights-matrix area as the env basis
// (billboard bags never carry lighting); kept as a LITERAL for the same
// vclpp one-level-#define reason.
#define VU1_BILLBOARD_BASIS_ADDR 4
// Modified by TyraX: two quadwords a project's OWN VU1 program reads
// (docs/vu-authoring.md). They sit in the directional-lights colour block,
// which is free in exactly the programs that can carry a custom stage list -
// the colour ones - so the engine only uploads them for a bag with no
// lighting. A lit bag fills 15..18 with light colours and must never see these.
//   VU1_CUSTOM_PARAMS_ADDR  the mesh's four numbers (StaPipCore::setVuParams)
//   VU1_CUSTOM_TIME_ADDR    (time, sin time, cos time, 1.0)
// Kept as LITERALS for the same reason VU1_ENV_BASIS_ADDR is: vclpp expands
// #defines only one level and an alias reaches dvp-as unresolved.
// The mesh's four numbers (StaPipCore::setVuParams). All zero means "this mesh
// wants nothing", which every stage is required to render bit-identically to
// the untouched program.
#define VU1_CUSTOM_PARAMS_ADDR 15
// (time, sin time, cos time, 1.0) - StaPipCore::setVuTime. WRAP the seconds:
// the generated sine's range reduction folds through a 2^23 add and loses the
// fraction long before a float would.
#define VU1_CUSTOM_TIME_ADDR 16
#define VU1_STAPIP_LAST_ITEM_ADDR 21

// Bias used to turn the constant near/far plane tests into clipw judgements
// (x < -GUARD bits). Only affects rounding: values within GUARD * 2^-24 of a
// plane may classify either way, which is harmless. C++ side only.
#define VU1_CLIP_GUARD 4096.0F

// X/Y band the clip programs cut to, as a fraction of w. Must be < 1.0:
// a vertex at exactly |x| = w scales to GS coordinate 4096.0, one past the
// 12.4 XYZ2 maximum - it wraps to the far side of the raster window and
// smears a wedge across the screen. 0.9 stays well inside (coord 3891)
// while still covering the whole visible frustum (screen edge = 0.5 w),
// so the GS scissor produces pixel-identical output. C++ side only.
#define VU1_CLIP_XY_BAND 0.9F

// Buffer data (xtop)
#define VU1_STAPIP_VERT_DATA_ADDR 2

// Modified by TyraX: VU1 clipping scratch at the top of VU1 data memory
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
