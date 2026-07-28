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

/** Called right AFTER a chain was sent and VU1 went idle, with a pointer to
 * VU1 data memory (1024 quadwords) - i.e. what the microprogram left behind:
 * the matrices it was given, the vertex arrays it read, and the GIF packet it
 * staged for XGKICK. Installing this costs a pipeline STALL (the engine has to
 * wait for VU1), so the devkit installs it only for the frame it captures. */
typedef void (*VuMemHook)(const void* vu1DataMem, u32 bytes);

/** Null unless a devkit build installs one. */
extern VuPacketHook g_vuPacketHook;
extern VuMemHook g_vuMemHook;

}  // namespace Tyra
