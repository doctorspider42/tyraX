/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: destination-alpha shadow mask (flashlight shadow volumes).
*/

#pragma once

#include <packet2.h>
#include "renderer/core/gs/renderer_core_gs.hpp"
#include "renderer/core/renderer_core_sync.hpp"
#include "renderer/core/paths/path1/path1.hpp"

namespace Tyra {

/**
 * A 1-bit per-pixel shadow mask living in the FRAMEBUFFER'S OWN ALPHA - the
 * PS2's stand-in for a stencil buffer, and per the surviving accounts the
 * arrangement the survival-horror era actually shipped. There is no extra
 * VRAM: the mask is the destination alpha's MSB, which the GS's
 * destination-alpha test (TEST.DATE) can gate any later draw on.
 *
 * Usage per frame, once the scene's z-buffer is complete:
 *
 *   core.alphaMask.begin();       // FBMSK -> alpha-only + clear alpha to 0
 *   ... render shadow-volume bags through the static pipeline:
 *       camera-FRONT faces with vertex alpha 0x80 (set the bit where the
 *       face is closer than the scene), camera-BACK faces with alpha 0x00
 *       (clear it where the volume's far side is closer - the pixel is
 *       outside the volume). Plain TestOnly z against the real scene depth
 *       is the whole test - that is what makes the result exact per pixel.
 *   core.alphaMask.end();         // restore the raster environment
 *   ... render light bags with PipelineInfoBag::dateLit = true: the GS draws
 *       them only where the mask bit is 0, i.e. light lands everywhere the
 *       shadow does not - the mask gates the LIGHT, never paints darkness.
 *
 * The color channels are untouched throughout (FBMSK exposes only alpha),
 * and z is neither written by the clear nor by TestOnly volume bags, so the
 * bracket is invisible to everything that does not opt into DATE.
 */
class RendererCoreAlphaMask {
 public:
  RendererCoreAlphaMask();
  ~RendererCoreAlphaMask();

  /** Dependency wiring only (RendererCore). */
  void init(RendererSettings* settings, RendererCoreGS* gs,
            RendererCoreSync* sync, Path1* path1);

  /**
   * Drain in-flight PATH1 work, mask the frame's color channels (FBMSK) and
   * clear the whole raster's alpha to 0 ("everything lit"). The z buffer is
   * not touched: the clear sprite runs with z writes masked, and the ZBUF
   * register is put back inside the same packet.
   */
  void begin();

  /**
   * Like begin() but WITHOUT the alpha clear: re-enter the color mask while
   * KEEPING the mask accumulated so far. The interleaved walk (templates:
   * light passes and volumes alternating by distance, so a caster's own
   * light draws before its own volume enters the mask) opens one bracket
   * per caster, and only the first may reset the channel.
   */
  void beginKeep();

  /** Drain the volume draws and restore the raster environment (FBMSK 0). */
  void end();

  /**
   * THE COUNTING ARRANGEMENT (mesh-shaped shadow volumes). The 1-bit
   * set/clear above is only sound for CONVEX volumes - the GS cannot count
   * in destination alpha (blending never writes A). But it CAN add and
   * subtract in COLOR channels, so a silhouette-extruded volume from a
   * caster's real triangles counts in a dedicated PSMCT32 target instead:
   *
   *   core.alphaMask.allocateCount();      // once, in init(), before textures
   *   ...once the scene z is complete:
   *   core.alphaMask.maskClear();          // FB alpha -> 0 ("everything lit")
   *   ...for each BAND covering the volumes' screen rect:
   *   core.alphaMask.countBegin(x0, y0, x1, y1, bandY0);
   *   ... render the volume through the static pipeline: camera-front faces
   *       with additiveBlendFix (each +N per channel), camera-back faces
   *       with subtractiveBlendFix (-N), both TestOnly vs the scene depth.
   *       Pixels inside the volume end up net-positive; everything else
   *       returns to exact zero, whatever the overlap count.
   *   core.alphaMask.countResolve(x0, y0, x1, y1, bandY0);   // count>0 -> mask
   *
   * countResolve() samples the count target as a texture with TEXA.AEM = 1
   * (an all-zero texel expands to alpha 0, anything else to TA0 = 0x80) and
   * draws a sprite into the real framebuffer with FBMSK exposing only alpha
   * and ATEST != 0 - so it ORs 0x80 into the mask and never clears what an
   * earlier band or caster set. The DATE-gated light passes then run
   * unchanged. The count target is never displayed, so no color repaint is
   * owed anywhere; repaintAlpha() stays mandatory exactly as before.
   *
   * WHY THE TARGET IS 32-BIT AND WHY IT IS A BAND - the most expensive thing
   * this class knows, found on a real console after every emulator test
   * passed. The count target is DEPTH-TESTED against the SCENE's z buffer,
   * and on real GS hardware a colour buffer and the z buffer it shares must
   * have the same PAGE GEOMETRY: 32/24-bit pages are 64x32 pixels, 16-bit
   * pages are 64x64. A PSMCT16 count target over the scene's PSMZ32 makes
   * the depth comparison read shifted words for half of every page, which
   * lands as a 32-pixel CHECKERBOARD of wrong shadow wherever a volume is
   * counted. PCSX2 addresses each buffer from its own PSM and shows nothing.
   *
   * A full-raster 32-bit target is 1 MB at 512x512 - more VRAM than a project
   * has - so it is allocated as a BAND (kCountBandRows) and FRAME.FBP is slid
   * by whole page ROWS so the band covers the requested rect. ZBP never
   * moves, so the 1:1 x/y correspondence with the scene z holds exactly; the
   * band's y origin must be a multiple of 32 (a page row) for the slide to be
   * expressible. A rect taller than the band is counted band by band: the
   * mask is an OR, so the bands compose and nothing is lost but the fill.
   *
   * The counting N is 32, which at 8 bits per channel needs no headroom
   * argument at all beyond the 7 overlapping front faces before saturation.
   */
  void allocateCount();

  /** True when the count target exists - callers fall back to the convex
   * 1-bit brackets when the VRAM allocation was refused. */
  bool countReady() const { return countAllocated; }

  /** How many raster rows one count band covers. 256 rows x the raster
   * width at 32 bits is 512 KB - the same VRAM the (broken) 16-bit
   * full-raster target used to take. Must be a multiple of 32, the page
   * row height of a 32-bit buffer. */
  static constexpr int kCountBandRows = 256;

  /** Rows per band, clamped to the raster - what the caller steps by. */
  int countBandRows() const { return countH; }

  /** Clear the framebuffer's ALPHA to 0 ("everything lit") over the whole
   * raster. Call ONCE per frame, before the first band: a second call would
   * wipe the mask the earlier bands already resolved. */
  void maskClear();

  /** Redirect FRAME to the count band whose first row is bandY0 (a multiple
   * of 32; the scene z stays bound for the TestOnly volume draws), scissor
   * to the given raster rect intersected with the band (x1/y1 exclusive) and
   * clear that region to zero. */
  void countBegin(int x0, int y0, int x1, int y1, int bandY0);

  /** Drain the volume draws, convert count>0 into the framebuffer alpha's
   * MSB (an OR - never clears what an earlier band or caster set) over the
   * SAME rect and band countBegin was given, and restore the whole raster
   * environment (emitRasterRestore puts the scissor back). */
  void countResolve(int x0, int y0, int x1, int y1, int bandY0);

  /**
   * Repaint the raster's ALPHA byte to the scene's neutral 0x80, colors and
   * z untouched. MUST be called once the frame's last DATE-gated pass has
   * drawn: on the SDTV interlaced modes the flicker filter's PMODE blends
   * the two read circuits by PER-PIXEL framebuffer alpha, so a mask left in
   * the channel is shown by the CRTC - the volume shapes appear as soft
   * translucent wedges over the finished picture.
   */
  void repaintAlpha();

 private:
  RendererSettings* settings = nullptr;
  RendererCoreGS* gs = nullptr;
  RendererCoreSync* sync = nullptr;
  Path1* path1 = nullptr;
  packet2_t* beginPacket = nullptr;
  packet2_t* endPacket = nullptr;
  // repaintAlpha's own packet: it runs in the same frame as begin(), and a
  // FINISH-parity slip would let a shared packet be rebuilt while the GIF is
  // still fetching it.
  packet2_t* repaintPacket = nullptr;
  // beginKeep's own packet, same single-frame-reuse rule as repaintPacket.
  packet2_t* keepPacket = nullptr;
  // The counting pair: each is sent several times per frame (one bracket per
  // caster), rebuilt only after its own draw_wait_finish - the repaintPacket
  // rule again.
  packet2_t* countBeginPacket = nullptr;
  packet2_t* countResolvePacket = nullptr;
  packet2_t* maskClearPacket = nullptr;
  bool countAllocated = false;
  int countAddress = 0;
  int countW = 0;
  int countH = 0;

  /** The FBP/TBP a band starting at bandY0 must be addressed through: the
   * target's base slid DOWN by the band's page rows, so pixel (x, bandY0)
   * lands on the target's own row 0. Negative means the slide would leave
   * VRAM, which allocateCount() refuses up front. */
  int slidBase(int bandY0, int frameWidth) const;
  static int slidBaseFor(int base, int bandY0, int frameWidth);
};

}  // namespace Tyra
