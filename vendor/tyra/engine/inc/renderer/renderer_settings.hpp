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
  /** Selects the scan mode and its framebuffer size (tyra-editor fork).
   * Must run before RendererCoreGS::init allocates the buffers. Deliberately
   * leaves aspectRatio at the constructor's 512/448: every mode's display
   * window is tuned to present the same 4:3 picture, so keeping the
   * projection aspect fixed keeps world proportions identical across modes
   * (the framebuffers just have different pixel densities). */
  void setDisplayMode(const DisplayMode& mode) {
    displayMode = mode;
    switch (mode) {
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
};

}  // namespace Tyra
