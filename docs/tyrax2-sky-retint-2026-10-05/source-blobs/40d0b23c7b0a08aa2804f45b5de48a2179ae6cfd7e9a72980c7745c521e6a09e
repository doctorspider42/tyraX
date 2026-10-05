/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: the frame-timing rig (docs/profiling.md, "Timing a frame
# that BLSS is in").
*/

#pragma once

/**
 * Compile the frame-timing counters in. OFF by default, so a release
 * libtyra.a carries not one instruction of any of this - and neither does a
 * debug one. Turn it on by editing the 0 below to a 1 (the engine and the
 * generated game both read THIS header, so one edit moves both halves) or by
 * passing -DTYRA_FRAME_PROFILE=1 to both builds.
 *
 * The engine is compiled once per checkout into a shared Docker volume and
 * reused by every project, so this cannot be a per-project setting the way
 * DEBUG_SHOW_PROFILER is - it is a source-level switch on purpose.
 *
 * TWO LEVELS (2026-09-23). 1 is the whole rig: FRAMETIME, the static
 * pipeline's telemetry (FTCLIP), and the packet-structure walker (FTPKT,
 * TYRA_STAPIP_PACKET_PROFILE). 2 is FRAMETIME ALONE - tFrameWork and the
 * fairness drain, with the pipeline's telemetry left off and the walker
 * compiled out.
 *
 * Level 2 exists because level 1 is not transparent enough to time a shipped
 * frame. Measured on a physical PS2 at a parked Motor District vantage, the
 * HUD's SCENE row read 13.21 ms uninstrumented and 17.70 ms at level 1: the
 * walker parses every DMA tag and VIF code of every packet sent, every frame,
 * and that pushed a frame whose work straddled 20 ms clean over it. So level 1
 * answers "what is the frame MADE of" and level 2 answers "how long does the
 * frame the player gets take". Do not quote an absolute `work` off level 1.
 */
#ifndef TYRA_FRAME_PROFILE
#define TYRA_FRAME_PROFILE 0
#endif

/**
 * The GS fill-rate calibration sweep (gsFillProbe below), on top of the
 * counters. Separate because it is DESTRUCTIVE - it paints over the frame
 * being displayed - and because it answers a different question: not "how long
 * did this frame take" but "can this machine measure GS fill at all".
 */
#ifndef TYRA_FRAME_PROFILE_CALIB
#define TYRA_FRAME_PROFILE_CALIB 0
#endif

#if TYRA_FRAME_PROFILE

#include <tamtypes.h>

namespace Tyra {

class RendererCoreGS;
class RendererCoreSync;
class Path1;

/**
 * Per-frame EE/GS timings, in COP0 Count ticks (294.912 MHz - half the
 * 590 MHz EE clock, so ticks / 294912 = milliseconds).
 *
 * Why COP0 rather than the FPS counter: turning the frame limiter off to read
 * an uncapped rate CHANGES WHAT IS BEING MEASURED. BLSS' pass 3 reprojects the
 * previous frame and its `motion` feature is a per-frame camera delta
 * (RendererCoreBlss::buildReproj), so a different frame rate feeds the network
 * a different input, lights different cells and draws a different amount of
 * fill; the generated game's updateFrameClock() also clamps dt and diverges
 * above ~200 FPS. Reading the clock inside a vsync-locked frame instead gives
 * the SUB-FRAME work time, where a 22 ms -> 17 ms improvement is fully visible
 * and nothing about the frame has changed.
 *
 * Every counter is written once per frame by the site that owns it and read by
 * the generated game's drawDebugHud, which prints the FRAMETIME line.
 */
namespace FrameProfile {

/** COP0 Count. Wraps every ~14.6 s; u32 subtraction is wrap-safe. */
inline u32 ticks() {
  u32 v;
  asm volatile("mfc0 %0, $9" : "=r"(v));
  return v;
}

/**
 * THE PRIMARY METRIC: RendererCore::beginFrame() to the read immediately
 * before `if (isFrameLimitOn) graph_wait_vsync()`. Everything after that read
 * is idle, so this is the real per-frame work at a locked 50/60 Hz.
 * `tExcluded` is already subtracted.
 */
extern u32 tFrameWork;

/**
 * The fairness fence, and the reason it exists. BLSS' three brackets each end
 * in dma_channel_wait(GIF) + draw_wait_finish(), so a BLSS frame is
 * SERIALISED: its GS work is inside tFrameWork. With BLSS off nothing drains
 * until flipBuffers, i.e. AFTER the vsync wait, so its GS work would fall
 * outside tFrameWork and the two arms would not be comparable at all.
 * endFrame() therefore runs one guarded sync.align3D() at the same point in
 * both arms and charges it here. Large `drain` = the frame is GS-bound;
 * ~0 = EE-bound, and an upscaler cannot help an EE-bound frame.
 */
extern u32 tDrain;

/** RendererCoreBlss::beginScene - redirect + clear + drain. 0 when off. */
extern u32 tBlssBegin;

/**
 * RendererCoreBlss::endScene - the GS OVERHANG of the half-res scene, because
 * the bracket's align3D() waits for whatever the GS is still rasterising.
 * This is the discriminator the whole feature stands on.
 */
extern u32 tBlssEnd;

/** RendererCoreBlss::composite - features + MLP + packet build + GS raster. */
extern u32 tBlssComposite;

/**
 * The EE half of the above: up to the DMA kick (inference + packet build).
 * tBlssComposite - tBlssCompositeEe is the GS raster of the 1..5 passes.
 */
extern u32 tBlssCompositeEe;

/**
 * THE SPLIT COUNTERS. The first hardware A/B attributed 5.10 ms to "the
 * composite's EE half" off ONE counter, and the remaining ~3.9 ms to "extra
 * scene submission" BY SUBTRACTING everything else from the A/B difference -
 * which is an inference, not a measurement, and it is the term nobody had
 * looked inside. These five make both attributable. They cost one mfc0 pair
 * each per frame (tBlssProxy: one pair per submitted bag).
 *
 * Cleared by whoever owns the frame's phase - beginScene for the proxy term
 * (StaPipCore only ever adds), composite for the other four.
 */

/**
 * The BLSS bag-proxy feed inside StaPipCore::render: the package loop, the
 * consecutive-part merge, and addBagBox/addBagSphere.
 */
extern u32 tBlssProxy;

/**
 * The GRID-ACCUMULATION half of the above - RendererCoreBlss::addBag alone,
 * i.e. everything downstream of a proxy's screen bbox. It is a SUBSET of
 * tBlssProxy, so `proxy - accum` is the projection half (eight corners through
 * the MVP, the near clip, the bbox reduce).
 *
 * It exists because `proxy` became the largest EE term in the feature and the
 * two halves want opposite fixes: the projection half is arithmetic per PROXY
 * and would move to VU0, the accumulation half is a read-modify-write per
 * (proxy, TILE) and moves by changing the memory layout or by describing the
 * frame more coarsely - which is a twin-contract change. Splitting them by
 * subtraction is what this page already refused to do once.
 */
extern u32 tBlssAccum;

/** finishTileStats + buildReproj (255 corners, 2 divides each). */
extern u32 tBlssReproj;

/** buildFeatures (224 tiles, a sqrtf each). */
extern u32 tBlssFeat;

/** runNet - the MLP over the covered tiles, plus the corner averaging. */
extern u32 tBlssNet;

/** emitPassState + emitGrid + the restore block: the packet build itself. */
extern u32 tBlssPacket;

/**
 * Instrumentation the GAME wants kept OUT of tFrameWork - its own once-a-
 * second sort + snprintf + TYRA_LOG, which is host: file I/O and would
 * otherwise show up as a 20 ms spike in one frame of every fifty. The game
 * adds to it, endFrame() subtracts it from tFrameWork and clears it.
 */
extern u32 tExcluded;

/**
 * THE TWO TERMS tFrameWork CANNOT SEE (added 2026-09-23), so that a FRAMETIME
 * line accounts for the whole frame the player gets instead of the render
 * block alone. tFrameWork starts at beginFrame(), and a game's loop() does a
 * great deal before it calls that - on the Motor District ~430 lines of
 * scripts, physics, vehicles and cameras - so on a physical PS2 a day frame
 * measured work=17.4 ms with 0 of 50 frames over budget while the HUD read
 * 38.5 FPS, i.e. ~30% of frames still missing the 20 ms field. The miss was in
 * the term nobody was timing.
 *
 * tPre    - the previous present's end to this beginFrame(): the engine's
 *           info/pad update and every line of the game's loop() that runs
 *           before it calls beginFrame.
 * tStall  - the present itself: the vsync wait plus the flip.
 * tPeriod - present-end to present-end, the true frame period.
 *
 * pre + work + stall ~= period is the rig checking itself (as window means:
 * the game samples these from inside the next frame, so a single sample mixes
 * two frames). Not meaningful with frame extrapolation on - a synthetic present
 * sits between two real ones and is not stamped here.
 */
extern u32 tPre;
extern u32 tStall;
extern u32 tPeriod;

/**
 * THE CALIBRATION GATE. Draws `k` full-screen textured, alpha-blended sprites
 * (source: the previously presented frame buffer; destination: the current
 * one), each followed by its own draw_finish + wait, and returns the elapsed
 * COP0 ticks. Sweeping k and fitting the slope measures what one full-screen
 * GS pass costs on THIS machine.
 *
 * It exists because PCSX2 runs a software rasteriser and is not fill-rate
 * accurate, while BLSS trades GS fill for GS fill: if the slope measured in
 * the emulator is near zero, no PCSX2 number about this feature's GS cost is
 * admissible and the measurement has to move to hardware. Real hardware should
 * read roughly 0.2-0.4 ms per pass at 512x448 (229 376 px, ~8 px/clk textured
 * at 147 MHz).
 *
 * Destructive: it paints over the frame being displayed. Only call it from a
 * fixture that is measuring, never from a game that is being looked at.
 *
 * `outW`/`outH` REPORT THE RASTER THE SWEEP RAN AT, and they are not a
 * convenience. The sprite is sized from the CURRENT framebuffer, so the slope
 * is milliseconds per pass *at whatever mode the fixture happened to boot in* -
 * and the figure this rig published (0.5872 ms) was taken on a PAL 576i fixture
 * at 512x512, then read for a year against 512x448 coverages, which is 14.3 %
 * more pixels than the number describes. A per-pass constant is meaningless
 * without its resolution; hand it back with the measurement so the two cannot
 * be separated again.
 */
u32 gsFillProbe(RendererCoreGS* gs, RendererCoreSync* sync, Path1* path1,
                int k, int* outW = nullptr, int* outH = nullptr);

}  // namespace FrameProfile
}  // namespace Tyra

#endif  // TYRA_FRAME_PROFILE
