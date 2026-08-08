/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include <math.h>
#include "renderer/core/renderer_core.hpp"
#include "thread/threading.hpp"
#include "debug/debug.hpp"
#include "debug/frame_profile.hpp"

namespace Tyra {

#if TYRA_FRAME_PROFILE
namespace FrameProfile {
// COP0 Count at the top of this frame's beginFrame(). Not published in the
// header: nothing outside endFrame() has any business reading a half-frame.
static u32 frameStart = 0;
}  // namespace FrameProfile
#endif

RendererCore::RendererCore() { isFrameLimitOn = true; }
RendererCore::~RendererCore() {}

void RendererCore::init(VideoMode videoMode, DisplayMode displayMode,
                        bool widescreen) {
  settings.setVideoMode(videoMode);
  // Must precede gs.init - it sizes the frame/z buffers (TyraX fork).
  settings.setDisplayMode(displayMode);
  settings.setWidescreen(widescreen);
  path3.init(&settings);
  sync.init(&path3, &path1);
  gs.init(&settings);
  // Post fx VRAM sits right above the frame/z buffers; allocate it before
  // any texture buffer so texture free (FIFO) never reclaims it.
  postFx.init(&settings, &gs);
  // Same rule for the dynamic env map's render target (TyraX fork).
  envMap.init(&settings, &gs, &sync, &path1);
  // Projected shadows: wiring only - VRAM is allocated lazily when the game
  // calls shadowMap.allocate() (init() also re-places the buffers after a
  // display-mode VRAM reset if they were on).
  shadowMap.init(&settings, &gs, &sync, &path1);
  // Camera-feed render target (TyraX fork, "texture feeds"): a second
  // instance of the same redirect bracket, permanently allocated below
  // every texture for the same FIFO-free reason. Costs 128 KB of VRAM
  // whether the game uses feeds or not. Clamp: feeds sample through plain
  // surface UVs and the default Repeat bleeds the opposite edge rows into
  // the screen border.
  camFeed.init(&settings, &gs, &sync, &path1);
  camFeed.getTexture()->setWrapSettings(TextureWrap::Clamp,
                                        TextureWrap::Clamp);
  // BLSS, the neural upscaler (TyraX fork): its low-res render target belongs
  // in the same permanent region, in the same relative order - but it is only
  // taken when the generated game's init() calls blss.configure(), so a
  // project with BLSS off costs zero VRAM words. This call is dependency
  // wiring, and the re-place after a display-mode VRAM reset (see
  // setDisplayOutput below).
  blss.init(&settings, &gs, &sync, &path1, &renderer3D);
  // ... and how configure() asks for the permanent region to be laid out
  // again, because turning BLSS on shrinks the z buffer to the raster size
  // and the z buffer was allocated three lines into gs.init().
  blss.setVramRebuild(&RendererCore::rebuildPermanentBuffersThunk, this);
  // Split-screen viewports (TyraX fork) - no VRAM, just raster brackets.
  splitView.init(&settings, &gs, &sync, &path1);
  texture.init(&gs, &path3);
  renderer3D.init(&settings, &path1);
  renderer2D.init(&settings, &texture.clut);
}

void RendererCore::setClearScreenColor(const Color& color) { bgColor = color; }

// Modified by TyraX (BLSS): see the header. Deliberately NOT gs.reinit() -
// the display geometry has not changed, and reinit()'s programDisplay() would
// reset the GS and blank the output in the middle of a game's init().
void RendererCore::rebuildPermanentBuffers() {
  texture.evictAll();
  gs.reallocateBuffers();
  postFx.init(&settings, &gs);
  envMap.init(&settings, &gs, &sync, &path1);
  shadowMap.init(&settings, &gs, &sync, &path1);  // re-places if allocated
  camFeed.init(&settings, &gs, &sync, &path1);
  camFeed.getTexture()->setWrapSettings(TextureWrap::Clamp, TextureWrap::Clamp);
  TYRA_LOG("Permanent GS buffers re-placed (raster scale ",
           settings.getRasterScaleX(), "x", settings.getRasterScaleY(),
           "), texture heap free MB: ", gs.vram.getFreeSpaceInMB());
}

void RendererCore::rebuildPermanentBuffersThunk(void* user) {
  static_cast<RendererCore*>(user)->rebuildPermanentBuffers();
}

// Modified by TyraX: runtime video output switch - see the header.
void RendererCore::setDisplayOutput(const DisplayMode& mode,
                                    const bool& widescreen) {
  const bool modeChanged = settings.getDisplayMode() != mode;
  const bool wsChanged = settings.getWidescreen() != widescreen;
  if (!modeChanged && !wsChanged) return;

  settings.setDisplayMode(mode);
  settings.setWidescreen(widescreen);

  if (modeChanged) {
    // The framebuffer size changes, so the whole VRAM layout does: drop
    // every texture allocation (they lazily re-upload), rebuild the
    // frame/z buffers + display, and re-place the post-fx scratch buffers
    // right above them (same order as init, so texture eviction can never
    // reclaim them).
    texture.evictAll();
    gs.reinit();
    postFx.init(&settings, &gs);
    envMap.init(&settings, &gs, &sync, &path1);
    shadowMap.init(&settings, &gs, &sync, &path1);  // re-places if allocated
    camFeed.init(&settings, &gs, &sync, &path1);
    // Same for the BLSS low-res target: vram.reset() forgot it, and its size
    // follows the new framebuffer geometry (re-places only if configured).
    blss.init(&settings, &gs, &sync, &path1, &renderer3D);
  } else {
    // Same buffers - only the display window shape changes (1080i widens;
    // the SDTV modes are stretched by the TV, their window stays as-is).
    gs.reprogramDisplay();
  }

  // Re-derive the projection (and thus next frame's frustum planes) from
  // the new framebuffer size / aspect.
  renderer3D.setFov(renderer3D.getFov());
}

// Modified by TyraX: GS hardware distance fog.
void RendererCore::setFog(const Color& color, const float& start,
                          const float& end) {
  TYRA_ASSERT(end > start, "Fog end distance must be greater than start!");
  fog.enabled = true;
  fog.color = color;
  fog.start = start;
  fog.end = end;
  const float range = end - start;
  fog.scale = -255.0F / range;
  fog.offset = 255.0F * end / range;
  gs.setFogColor(static_cast<u8>(color.r), static_cast<u8>(color.g),
                 static_cast<u8>(color.b));
}

void RendererCore::disableFog() {
  fog.enabled = false;
  fog.scale = 0.0F;
  fog.offset = 255.0F;
}

// Modified by TyraX: dynamic spot light (flashlight).
void RendererCore::setSpotLight(const Color& color, const Vec4& position,
                                const Vec4& direction, const float& range,
                                const float& cutoffDegrees,
                                const float& softness) {
  TYRA_ASSERT(range > 0.0F, "Spot light range must be positive!");
  spot.enabled = true;
  spot.color = color;
  spot.position = position;
  spot.direction = direction;
  const float len = sqrtf(direction.x * direction.x +
                          direction.y * direction.y +
                          direction.z * direction.z);
  if (len > 1e-5F) {
    spot.direction.x /= len;
    spot.direction.y /= len;
    spot.direction.z /= len;
  }
  spot.direction.w = 0.0F;
  spot.range = range;
  const float halfAngle = cutoffDegrees * 3.14159265F / 180.0F;
  spot.cosCutoff = cosf(halfAngle);
  spot.softness = softness < 1.0F ? 1.0F : softness;
  spot.point = false;
}

// Modified by TyraX: scene dynamic lights - a per-frame registry the StaPip
// picks ONE light per mesh from (the color programs have a single light slot).
int RendererCore::addDynPointLight(const Color& color, const Vec4& position,
                                   const float& range) {
  TYRA_ASSERT(range > 0.0F, "Dynamic point light range must be positive!");
  if (dynLightCount >= DYN_LIGHTS_MAX) return -1;
  auto& l = dynLights[dynLightCount];
  l.enabled = true;
  l.point = true;
  l.position = position;
  l.position.w = 1.0F;
  l.color = color;
  l.range = range;
  return static_cast<int>(dynLightCount++);
}

const RendererCoreSpotLight* RendererCore::pickDynLight(
    const Vec4& worldCenter, const float& worldRadius) const {
  // Score = luminance * quadratic falloff at the sphere's NEAREST point, so
  // a big mesh near a torch competes fairly with the camera flashlight.
  const RendererCoreSpotLight* best = &spot;
  float bestScore = -1.0F;

  const RendererCoreSpotLight* candidates[DYN_LIGHTS_MAX + 1];
  u32 count = 0;
  if (spot.enabled) candidates[count++] = &spot;
  for (u32 i = 0; i < dynLightCount; i++) candidates[count++] = &dynLights[i];

  for (u32 i = 0; i < count; i++) {
    const auto* l = candidates[i];
    const float dx = l->position.x - worldCenter.x;
    const float dy = l->position.y - worldCenter.y;
    const float dz = l->position.z - worldCenter.z;
    float d = sqrtf(dx * dx + dy * dy + dz * dz) - worldRadius;
    if (d < 0.0F) d = 0.0F;
    if (d >= l->range) continue;
    const float att = 1.0F - d / l->range;
    float score =
        (l->color.r + l->color.g + l->color.b) * (1.0F / 3.0F) * att * att;
    if (!l->point) {
      // Spot cone: down-rank when the whole sphere sits outside the cone
      // (approximate - sin(angle) ~ radius/distance). Never zero: an aimed
      // flashlight sweeping onto a mesh must not pop a torch off mid-swing
      // when both scores are close.
      const float dist = d + worldRadius;
      if (dist > 1e-4F) {
        const float cosAng = -(dx * l->direction.x + dy * l->direction.y +
                               dz * l->direction.z) /
                             dist;
        if (cosAng + worldRadius / dist < l->cosCutoff) score *= 0.05F;
      }
    }
    if (score > bestScore) {
      bestScore = score;
      best = l;
    }
  }
  return best;
}

void RendererCore::beginFrame() {
#if TYRA_FRAME_PROFILE
  FrameProfile::frameStart = FrameProfile::ticks();
#endif
  renderer3D.update();
  drained3DFor2D = false;
  postFxAppliedMask = 0;
  postFxDrained = false;
  Threading::switchThread();
  path3.clearScreen(&gs.zBuffer, bgColor);
}

void RendererCore::beginFrame(const CameraInfo3D& cameraInfo) {
#if TYRA_FRAME_PROFILE
  FrameProfile::frameStart = FrameProfile::ticks();
#endif
  renderer3D.update(cameraInfo);
  drained3DFor2D = false;
  postFxAppliedMask = 0;
  postFxDrained = false;
  Threading::switchThread();
  path3.clearScreen(&gs.zBuffer, bgColor);
}

// Modified by TyraX: mid-frame post fx (Tools > UI Editor screen stack).
// Applies only the not-yet-applied selected passes; the PATH1 drain barrier
// (endFrame's) runs once, before the first pass that actually draws - after
// that no more 3D is submitted this frame, so later passes composite safely
// over a finished frame (and over any HUD sprites drawn in between).
void RendererCore::applyPostFx(int passes) {
  passes &= ~postFxAppliedMask;
  if (passes == 0) return;
  postFxAppliedMask |= passes;
  if (!postFx.isEnabled(passes)) return;
  if (!postFxDrained && path1.isVU1Configured()) {
    sync.align3D();
    postFxDrained = true;
  }
  postFx.apply(passes);
}

// Modified by TyraX: user-authored full-screen effect (custom screen
// effects). Same PATH1 drain barrier as applyPostFx() so the pass composites
// over finished 3D; no once-per-frame mask (a custom pass always draws).
void RendererCore::applyCustomPostFx(RendererCorePostFx::CustomFxBuild build,
                                     void* user) {
  if (!postFxDrained && path1.isVU1Configured()) {
    sync.align3D();
    postFxDrained = true;
  }
  postFx.applyCustom(build, user);
}

// Modified by TyraX: portal through-view bracket. Mid-frame: drain PATH1
// (scissor/z-mask are global GS state) but do NOT latch postFxDrained -
// the frame submits more 3D after this.
void RendererCore::portalViewBegin(int x0, int y0, int x1, int y1) {
  if (path1.isVU1Configured()) sync.align3D();
  postFx.portalMaskBegin(x0, y0, x1, y1);
}

void RendererCore::portalViewEnd(const float* xy, const u32* z, int count,
                                 u8 clearR, u8 clearG, u8 clearB) {
  if (path1.isVU1Configured()) sync.align3D();
  postFx.portalMaskEnd(xy, z, count, clearR, clearG, clearB);
}

void RendererCore::endFrame() {
  Threading::switchThread();
  // The dynamic pipeline kicks the scene on PATH1/VU1 asynchronously (double
  // buffered - sendPacket() returns while the DMA is still draining). PostFx
  // composites over the framebuffer via PATH3 and writes no z, so any scene
  // triangles the GS is still rasterizing would draw back over the grain/bloom
  // (they pass the GEQUAL z-test) and erase it - a few frames every so often,
  // exactly when VU1 lags. Drain PATH1 first so we composite over a finished
  // frame. Only pay the barrier when an effect is actually on, and only once
  // a 3D pipeline has brought VU1 up (VIF1 DMA init + double buffer): before
  // that - e.g. the pure-2D loading screen - there is nothing on PATH1 to
  // drain and the draw-finish handshake would spin forever waiting for a
  // FINISH that VU1 can't deliver yet.
  applyPostFx();
#if TYRA_FRAME_PROFILE
  // THE FAIRNESS FENCE (inc/debug/frame_profile.hpp, tDrain). One guarded
  // drain, at one point, in BOTH arms - a BLSS frame is already serialised by
  // its three brackets, a plain frame would otherwise defer its whole GS load
  // past the vsync wait into flipBuffers and read as free. Guarded on
  // isVU1Configured() because the handshake spins forever before VU1 is up
  // (the pure-2D loading screen).
  {
    const u32 d0 = FrameProfile::ticks();
    if (path1.isVU1Configured()) sync.align3D();
    const u32 d1 = FrameProfile::ticks();
    FrameProfile::tDrain = d1 - d0;
    // The game's own once-a-second sort + snprintf + TYRA_LOG is host: file
    // I/O; it is measurement apparatus, not frame work, so it comes back out.
    FrameProfile::tFrameWork =
        d1 - FrameProfile::frameStart - FrameProfile::tExcluded;
    FrameProfile::tExcluded = 0;
  }
#endif
  texture.traceFrame();  // Modified by TyraX: GS VRAM residency report
  if (isFrameLimitOn) graph_wait_vsync();
  gs.flipBuffers();
}

}  // namespace Tyra
