/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#pragma once

#include <sstream>
#include <string>

namespace Tyra {

/** Output video signal. Auto follows the console region (ps2sdk
 * graph_get_region), the other two force a 60 Hz NTSC / 50 Hz PAL mode
 * regardless of region (TyraX fork). */
enum class VideoMode { Auto, NTSC, PAL };

/** Output scan mode (TyraX fork). Interlaced is the stock 480i/576i
 * FIELD mode (512x448 framebuffer). Progressive480p outputs a flicker-free
 * 448x448 frame over DTV 480p; HiDef1080i outputs a 448x540 frame as
 * 1080i (each field scans the same buffer - the gsKit/OPL approach). Both
 * DTV modes need component (YPbPr) cables on real hardware and ignore the
 * VideoMode region - they always run at 60 Hz. InterlacedField is the
 * 480i/576i signal with true field rendering: half-height (512x224)
 * framebuffers scanned in FRAME mode, a fresh image rendered for every
 * field - at full speed the TV gets 50/60 distinct pictures per second
 * for roughly half the fill/VRAM cost of Interlaced. Pal576i is the
 * full-height PAL frame (the "true PAL" of European retail releases): a
 * 512x512 framebuffer scanned as the classic 576i FIELD signal, 512 of
 * the raster's ~576 visible lines - it ignores the VideoMode region the
 * other way around, always outputting 50 Hz PAL. Costs ~380 KB more GS
 * VRAM than Interlaced (three 512-line buffers), shrinking the texture
 * budget to roughly 1 MB.
 * Values are serialized in projects and flow graphs - append only. */
enum class DisplayMode { Interlaced, Progressive480p, HiDef1080i, InterlacedField, Pal576i };

class RendererSettings {
 public:
  RendererSettings()
      : width(512.0F),
        height(448.0F),
        interlacedHeightF(height / 2),
        near(0.1F),
        far(51200.0F),
        projectionScale(4096.0F),
        aspectRatio(width / height),
        interlacedHeightUI(static_cast<unsigned int>(interlacedHeightF)),
        videoMode(VideoMode::Auto),
        displayMode(DisplayMode::Interlaced) {}
  ~RendererSettings();

  const float& getWidth() const { return width; }
  const float& getHeight() const { return height; }
  const VideoMode& getVideoMode() const { return videoMode; }
  void setVideoMode(const VideoMode& mode) { videoMode = mode; }
  const DisplayMode& getDisplayMode() const { return displayMode; }
  const bool& getWidescreen() const { return widescreen; }
  /** Selects the scan mode and its framebuffer size (TyraX fork).
   * When (re)selected before RendererCoreGS allocates buffers, sizes them;
   * at runtime RendererCore::setDisplayOutput drives the re-allocation. */
  void setDisplayMode(const DisplayMode& mode) {
    displayMode = mode;
    updateGeometry();
  }
  /** 16:9 anamorphic output (TyraX fork): widens the projection so the
   * picture has correct proportions on a widescreen display (the framebuffer
   * stays the same - the TV does the horizontal stretch; in 1080i the GS
   * display window widens instead). */
  void setWidescreen(const bool& on) {
    widescreen = on;
    updateGeometry();
  }
  /** Vertical refresh in Hz. The DTV modes are 60 Hz regardless of region,
   * Pal576i is 50 Hz regardless of region; the other interlaced modes
   * follow the video mode (PAL 50 / NTSC 60). Valid after renderer init -
   * GS init resolves an Auto mode to the console's actual region first. */
  float getRefreshRate() const {
    if (displayMode == DisplayMode::Progressive480p ||
        displayMode == DisplayMode::HiDef1080i)
      return 60.0F;
    if (displayMode == DisplayMode::Pal576i) return 50.0F;
    return videoMode == VideoMode::PAL ? 50.0F : 60.0F;
  }
  /** True field rendering: the frame/z buffers are half the logical height
   * (TyraX fork). */
  bool isFieldRendering() const {
    return displayMode == DisplayMode::InterlacedField;
  }

  /**
   * Triple buffering (TyraX fork, docs/frame-pacing.md). Must be set before
   * RendererCoreGS allocates buffers - it decides how many frame buffers the
   * permanent VRAM region holds, and the third one is not cheap (a full
   * display buffer: 229 376 words at 512x448x32, half that in
   * InterlacedField). Off by default, and the engine falls back to two
   * buffers when the third does not fit.
   */
  const bool& getTripleBuffering() const { return tripleBuffering; }
  void setTripleBuffering(const bool& on) { tripleBuffering = on; }

  /** Frame buffers the renderer wants: 3 with triple buffering on, else 2.
   * What it actually GOT is RendererCoreGS::getFrameBufferCount(). */
  unsigned int getFrameBufferCount() const { return tripleBuffering ? 3u : 2u; }
  /** Height of the physical frame/z buffers - half the logical height when
   * field rendering, the logical height otherwise (TyraX fork). Everything
   * that sizes or addresses the framebuffer (allocation, XYOFFSET/SCISSOR,
   * clears, post fx, the projection's raster scale) uses this; game-facing
   * layout keeps getHeight(). */
  const float& getRenderHeightF() const {
    return isFieldRendering() ? interlacedHeightF : height;
  }
  unsigned int getRenderHeightUI() const {
    return static_cast<unsigned int>(getRenderHeightF());
  }

  /**
   * Modified by TyraX (BLSS neural upscaler, docs/neural-upscaler.md):
   * the 3D pass' RASTER scale divisor. 1,1 (the default) means the 3D scene
   * rasterises straight into the display buffer; 2,2 or 1,2 means it
   * rasterises into RendererCoreBlss' low-res target and the reconstruction
   * passes blow it back up.
   *
   * It composes with the field-rendering split above: the raster height is
   * getRenderHeightF() / sy, so InterlacedField's already-halved buffer is
   * halved again rather than fought with.
   *
   * ONLY the projection's raster scale reads these (see
   * RendererCore3D::setProjection). The world-space frustum planes come from
   * fov + aspectRatio and are deliberately untouched - exactly the invariant
   * InterlacedField already relies on. Everything that sizes or addresses the
   * DISPLAY buffer (clears, 2D/HUD, post fx, env-map/shadow-map restores)
   * keeps getWidth()/getRenderHeightF(); getting that split wrong draws half
   * the frame off-screen.
   */
  void setRasterScale(const int& sx, const int& sy) {
    rasterScaleX = sx < 1 ? 1 : sx;
    rasterScaleY = sy < 1 ? 1 : sy;
  }
  const int& getRasterScaleX() const { return rasterScaleX; }
  const int& getRasterScaleY() const { return rasterScaleY; }
  bool isRasterScaled() const {
    return rasterScaleX != 1 || rasterScaleY != 1;
  }
  /** Width of the raster the 3D projection is built for (TyraX fork). */
  float getRasterWidthF() const {
    return width / static_cast<float>(rasterScaleX);
  }
  /** Height of the raster the 3D projection is built for (TyraX fork) -
   * the physical render height divided by the raster scale. */
  float getRasterHeightF() const {
    return getRenderHeightF() / static_cast<float>(rasterScaleY);
  }
  unsigned int getRasterWidthUI() const {
    return static_cast<unsigned int>(getRasterWidthF());
  }
  unsigned int getRasterHeightUI() const {
    return static_cast<unsigned int>(getRasterHeightF());
  }
  const float& getNear() const { return near; }
  const float& getFar() const { return far; }
  const float& getProjectionScale() const { return projectionScale; }
  const float& getAspectRatio() const { return aspectRatio; }
  const float& getInterlacedHeightF() const { return interlacedHeightF; }
  const unsigned int& getInterlacedHeightUI() const {
    return interlacedHeightUI;
  }

  static void copy(RendererSettings* out, const RendererSettings* in);
  void set(const RendererSettings& v);

  void print() const;
  std::string getPrint() const;

 private:
  float width, height, interlacedHeightF, near, far, projectionScale,
      aspectRatio;
  unsigned int interlacedHeightUI;
  VideoMode videoMode;
  DisplayMode displayMode;
  bool widescreen = false;
  // Modified by TyraX: BLSS raster scale (1,1 = off - no project pays for it).
  int rasterScaleX = 1;
  int rasterScaleY = 1;
  // Modified by TyraX: triple buffering (docs/frame-pacing.md). Off by
  // default - the third buffer is a full display buffer of GS VRAM.
  bool tripleBuffering = false;

  /** Framebuffer size per scan mode + projection aspect (TyraX fork).
   * The projection aspect keeps the stock 512/448 value as the 4:3 baseline
   * and scales with the physical shape of the mode's display window, so
   * world proportions look the same in every mode on the same TV. */
  void updateGeometry() {
    switch (displayMode) {
      case DisplayMode::Progressive480p:
        width = 448.0F;
        height = 448.0F;
        break;
      case DisplayMode::HiDef1080i:
        width = 448.0F;
        height = 540.0F;
        break;
      case DisplayMode::Pal576i:  // full-height PAL frame
        width = 512.0F;
        height = 512.0F;
        break;
      default:  // Interlaced and InterlacedField share the logical 512x448.
        width = 512.0F;
        height = 448.0F;
        break;
    }
    interlacedHeightF = height / 2;
    interlacedHeightUI = static_cast<unsigned int>(interlacedHeightF);
    // Physical aspect of the display window on the TV. The SDTV modes fill
    // (a 4:3-shaped part of) the raster, so widescreen means the TV
    // stretches the same signal to 16:9. 1080i's raster is natively 16:9:
    // 4:3 games get a pillarboxed 1344-VCK window, widescreen ones a
    // 1792/1920 window (the widest 448 * integer-magh fit).
    float windowAspect = widescreen ? (16.0F / 9.0F) : (4.0F / 3.0F);
    if (displayMode == DisplayMode::HiDef1080i && widescreen)
      windowAspect = (1792.0F / 1920.0F) * (16.0F / 9.0F);
    aspectRatio = (512.0F / 448.0F) * windowAspect / (4.0F / 3.0F);
  }
};

}  // namespace Tyra
