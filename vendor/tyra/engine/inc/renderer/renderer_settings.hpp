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
 * regardless of region (tyra-editor fork). */
enum class VideoMode { Auto, NTSC, PAL };

/** Output scan mode (tyra-editor fork). Interlaced is the stock 480i/576i
 * FIELD mode (512x448 framebuffer). Progressive480p outputs a flicker-free
 * 448x448 frame over DTV 480p; HiDef1080i outputs a 448x540 frame as
 * 1080i (each field scans the same buffer - the gsKit/OPL approach). Both
 * DTV modes need component (YPbPr) cables on real hardware and ignore the
 * VideoMode region - they always run at 60 Hz. */
enum class DisplayMode { Interlaced, Progressive480p, HiDef1080i };

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
  /** Selects the scan mode and its framebuffer size (tyra-editor fork).
   * When (re)selected before RendererCoreGS allocates buffers, sizes them;
   * at runtime RendererCore::setDisplayOutput drives the re-allocation. */
  void setDisplayMode(const DisplayMode& mode) {
    displayMode = mode;
    updateGeometry();
  }
  /** 16:9 anamorphic output (tyra-editor fork): widens the projection so the
   * picture has correct proportions on a widescreen display (the framebuffer
   * stays the same - the TV does the horizontal stretch; in 1080i the GS
   * display window widens instead). */
  void setWidescreen(const bool& on) {
    widescreen = on;
    updateGeometry();
  }
  /** Vertical refresh in Hz. The DTV modes are 60 Hz regardless of region;
   * interlaced follows the video mode (PAL 50 / NTSC 60). Valid after
   * renderer init - GS init resolves an Auto mode to the console's actual
   * region first. */
  float getRefreshRate() const {
    if (displayMode != DisplayMode::Interlaced) return 60.0F;
    return videoMode == VideoMode::PAL ? 50.0F : 60.0F;
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

  /** Framebuffer size per scan mode + projection aspect (tyra-editor fork).
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
      default:
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
