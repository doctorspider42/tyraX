/*
# TyraX addition: the VU1 packet tap.
#
# Debugging a VU1 microprogram is famously blind: you cannot print from VU1 and
# its output goes straight to the GS. But the INPUT is ours - every vertex the
# static pipeline draws leaves the EE as one DMA chain built by
# StaPipQBufferRenderer::sendPacket(). This is a seam there: a hook the editor's
# devkit layer installs to copy one packet out for inspection (docs/devkit.md).
#
# Zero-cost by construction: the engine only carries a null function pointer and
# the branch that tests it (once per bag flush, not per vertex). The capture code
# itself lives in the generated game's devkit translation unit, which does not
# exist in a release build - so a shipped game has nothing to link.
*/
#pragma once

#include <tamtypes.h>

namespace Tyra {

/** Installed by the devkit; called with the bytes of a finished VU1 DMA chain
 * just before it is sent. `qwc` is the chain length in quadwords (16 bytes),
 * `programName` the VU1 program the last buffer selected ("" if unknown). The
 * hook must copy what it needs and return promptly - it runs inside the frame. */
typedef void (*VuPacketHook)(const void* data, u32 qwc, const char* programName);

/** Null unless a devkit build installs one. */
extern VuPacketHook g_vuPacketHook;

}  // namespace Tyra
