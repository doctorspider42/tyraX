/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "renderer/renderer_settings.hpp"

namespace Tyra {

RendererSettings::~RendererSettings() {}

void RendererSettings::copy(RendererSettings* out, const RendererSettings* in) {
  out->width = in->width;
  out->height = in->height;
  out->near = in->near;
  out->far = in->far;
  out->projectionScale = in->projectionScale;
  out->aspectRatio = in->aspectRatio;
  out->windowAspect = in->windowAspect;  // Modified by TyraX
  out->interlacedHeightF = in->interlacedHeightF;
  out->interlacedHeightUI = in->interlacedHeightUI;
  out->videoMode = in->videoMode;
  out->displayMode = in->displayMode;          // Modified by TyraX
  out->widescreen = in->widescreen;            // Modified by TyraX
  out->colorDepth = in->colorDepth;            // Modified by TyraX
  out->dither = in->dither;                    // Modified by TyraX
  out->rasterScaleX = in->rasterScaleX;        // Modified by TyraX (BLSS)
  out->rasterScaleY = in->rasterScaleY;        // Modified by TyraX (BLSS)
  out->tripleBuffering = in->tripleBuffering;  // Modified by TyraX
}

void RendererSettings::set(const RendererSettings& v) { copy(this, &v); }

void RendererSettings::print() const {
  auto text = getPrint();
  printf("%s\n", text.c_str());
}

std::string RendererSettings::getPrint() const {
  std::stringstream res;
  res << "RendererSettings(";
  res << "width: " << width << ", ";
  res << "height: " << height << ", ";
  res << "near: " << near << ", ";
  res << "far: " << far << ", ";
  res << "projectionScale: " << projectionScale << ", ";
  res << "aspectRatio: " << aspectRatio << ", ";
  res << "interlaced height: " << interlacedHeightF << ", ";
  // Modified by TyraX: the colour depth decides the framebuffer PSM, and
  // "why is my VRAM budget different" is the first question it raises.
  res << "color depth: " << (colorDepth == ColorDepth::Bits16 ? 16 : 32)
      << "bpp, ";
  res << "dither: " << (dither ? "on" : "off");
  res << ")";
  return res.str();
}

}  // namespace Tyra
