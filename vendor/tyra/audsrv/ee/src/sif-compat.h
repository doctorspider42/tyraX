/* Modified by TyraX - not upstream PS2SDK.
 *
 * One source tree, two PS2SDKs. Upstream renamed the EE-side SIF RPC entry
 * points to sce-prefixed names, and the two SDKs this fork has to build against
 * export DISJOINT sets - measured, not assumed:
 *
 *   h4570/tyra's ps2sdk (2022)   libkernel.a has SifCallRpc,    not sceSifCallRpc
 *   a current ps2dev ps2sdk      libkernel.a has sceSifCallRpc, not SifCallRpc
 *
 * So the sources compile either way and then fail to LINK on the other one,
 * with an "undefined reference to `SifCallRpc'" that says nothing about SDK
 * versions. Ten symbols, a pure rename, no behaviour change.
 *
 * The switch has to come from OUTSIDE, and that is not laziness - it cannot be
 * detected here. This module is always compiled against the pinned ps2sdk source
 * tree (the -I paths point at it), so every header the preprocessor can see is
 * the OLD one whichever image is building; what differs is the ps2sdk the result
 * gets LINKED against. __has_include(<sifrpc-common.h>) was tried and is exactly
 * that trap: the file is present in a current image and still invisible to this
 * compile, so it silently chose the old names and the game failed to link.
 *
 * ee/Makefile turns TYRAX_PS2SDK_SCE_SIF in the environment into the define, and
 * the image that knows which ps2sdk it ships is the one that sets it.
 *
 * Why this direction (write the OLD names, alias them to the new): the fork's
 * sources are upstream's at commit e78a9cb2, and keeping their diff against
 * upstream as small as possible is the whole point of a fork. Renaming ten call
 * sites would make every future rebase noisier than this header does.
 */

#ifndef TYRAX_AUDSRV_SIF_COMPAT_H
#define TYRAX_AUDSRV_SIF_COMPAT_H

#ifdef TYRAX_PS2SDK_SCE_SIF

#define SifBindRpc sceSifBindRpc
#define SifCallRpc sceSifCallRpc
#define SifDmaStat sceSifDmaStat
#define SifRegisterRpc sceSifRegisterRpc
#define SifRemoveRpc sceSifRemoveRpc
#define SifRemoveRpcQueue sceSifRemoveRpcQueue
#define SifRpcLoop sceSifRpcLoop
#define SifSetDma sceSifSetDma
#define SifSetRpcQueue sceSifSetRpcQueue
#define SifWriteBackDCache sceSifWriteBackDCache

#endif /* TYRAX_PS2SDK_SCE_SIF */

#endif /* TYRAX_AUDSRV_SIF_COMPAT_H */
