/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: compile-time toggles for the VU1-audit experiments that
# only a physical-PS2 run can judge (docs/vu1-clipping.md, "Measured-only
# experiments"). All three default to 0, which is exactly the shipping
# engine: the experiment microprograms are still assembled (so a toggle
# flip needs no Makefile change) but, like the unlinked clip references,
# nothing references their symbols and they stay out of the ELF.
#
# Flip ONE to 1 per A/B arm. vclpp has no #if, so a toggle selects a whole
# alternative program IMAGE: each wrapper below picks its symbols with it.
*/

#pragma once

/** (a) cull_c / cull_tc: keep 255 in a VF register instead of reloading it
 * with `loi 255` before every clamp, and take the ADC bit from a two-entry
 * table at VU1_ADC_TABLE_ADDR (indexed by fcand's 0/1) instead of
 * `iaddiu 0x7FFF; iand adcMask`. BIT-IDENTICAL output (--vu-check proves
 * it); the question is only cycles. Images: *_k255_vu1.vclpp. */
#ifndef TYRA_VU1_EXP_K255_ADC_TABLE
#define TYRA_VU1_EXP_K255_ADC_TABLE 0
#endif

/** (b) every static lit program (cull_d, cull_td, as_is_d, as_is_td, and the
 * D / TD paths of the clip C and TC images): the EE uploads the three light
 * directions ALREADY multiplied by the light (normal) matrix, so
 * CalculateTyraDirectionalLights loses its first three instructions per
 * vertex and the matrix is not uploaded at all. NOT bit-identical: the EE
 * rounds the product to nearest, VU1 truncates - --vu-check bounds the
 * colour difference (see "experiments" in its output). A bag whose lit
 * program is a project override keeps the unfolded upload. Images:
 * *_fold_vu1.vclpp. */
#ifndef TYRA_VU1_EXP_EE_LIGHT_FOLD
#define TYRA_VU1_EXP_EE_LIGHT_FOLD 0
#endif

/** (c) cull_tce: the env (matcap) normals are normalized ONCE on the EE (in
 * place, the first time StaPipCore::render sees an env bag's array - tracked
 * by pointer, count, bboxVersion and contentVersion) and the program drops
 * CalculateTyraEnvStq's per-vertex rsqrt normalize. as_is_tce keeps it (the
 * EE clipper lerps normals, which shortens them) and so does the TC clip
 * image's env path (its bag is the same, already-unit array; renormalizing a
 * unit vector costs the clip path nothing it did not pay before). NOT
 * bit-identical. Image: stapip_cull_tce_envn_vu1.vclpp. */
#ifndef TYRA_VU1_EXP_ENV_NORMALIZED
#define TYRA_VU1_EXP_ENV_NORMALIZED 0
#endif
