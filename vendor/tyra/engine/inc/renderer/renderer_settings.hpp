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
        videoMode(VideoMode::Auto) {}
  ~RendererSettings();

  const float& getWidth() const { return width; }
  const float& getHeight() const { return height; }
  const VideoMode& getVideoMode() const { return videoMode; }
  void setVideoMode(const VideoMode& mode) { videoMode = mode; }
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
};

}  // namespace Tyra
