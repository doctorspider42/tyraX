/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: compile-time toggles for the VU1-audit experiments that
# only a physical-PS2 run can judge (docs/vu1-clipping.md, "Measured-only
# experiments"). The two measured winners default to 1. Their alternative
# microprograms remain separately assembled, so a toggle needs no Makefile
# change. The slower k255/ADC-table arm was removed.
#
# vclpp has no #if, so each toggle selects a whole alternative program IMAGE.
*/

#pragma once

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
#define TYRA_VU1_EXP_EE_LIGHT_FOLD 1
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
#define TYRA_VU1_EXP_ENV_NORMALIZED 1
#endif
