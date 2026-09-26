/*
# Modified by TyraX - new file: the PATH3 fence that keeps GIF-channel sends
# behind the 2D sprites still riding VIF1. Licensed under Apache License 2.0.
*/

#pragma once

namespace Tyra {

/**
 * True while 2D sprites have been handed to VIF1 (RendererCore2D, the
 * TYRA_2D_VIF1_DIRECT chain) and are not yet known to have reached the GIF.
 *
 * Sprites travel as DIRECT (PATH2) data inside a VIF1 chain that queues
 * behind the frame's 3D chains, so for a while after Renderer2D::render
 * returns they have not been drawn. Anything sent on the GIF DMA channel
 * (PATH3) in that window would overtake them: a texture upload could
 * overwrite a texture a queued sprite still samples, a CLAMP/ALPHA write
 * would change the state they draw with, a flip would show the frame
 * without them. Every PATH3 sender therefore calls path3Fence() first.
 */
extern bool path3FencePending;

/** Submits the open 2D chain and waits until VIF1 has passed it on. */
void path3FenceFlush();

inline void path3Fence() {
  if (path3FencePending) path3FenceFlush();
}

}  // namespace Tyra
