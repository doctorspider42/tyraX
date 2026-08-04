// The VU0 half of the authoring feature, running for real
// (../../../../docs/vu-authoring.md).
//
// This file is YOURS - the editor writes generated sources into src/gen/ and
// never touches src/scripts/. That is also the only place a kernel can be
// driven from: a kernel is a compute job with no place in the scene pipeline,
// so nothing calls it unless your game does.
//
// BOTH WAYS OF WRITING ONE are here:
//
//   "points"  - Tools > VU Programs > VU0 kernel, composed from the same stage
//               library the microprograms are built from (Wobble + Squash),
//               applied to quadwords instead of vertices. No C++ at all.
//               Each frame it feeds 32 points and the first result becomes the
//               pillar's Desaturate parameter, so the pillar's colour is a VU0
//               computation rendered by VU1.
//   "Ranges"  - src/vu0/ranges.cpp, a `vu::Kernel` in ordinary C++, for the
//               arithmetic the catalogue does not describe. See runRanges().
//
// Two things worth knowing before copying this:
//
//   - run() BLOCKS the EE. VU0's register file is the one COP2 macro mode uses
//     - the engine's own Vec4/M4x4 math - so nothing may have vector arithmetic
//     in flight. 32 elements is microseconds; a big batch is not free.
//   - Writing data.vuParams needs no `dirty` flag: it is not vertex data, so it
//     rides along with the next frame's object-data packet.
#include "scripts/script.hpp"
#include "scripts/vu_programs.gen.hpp"
#include "scripts/vu_scripts.gen.hpp"
#include "scripts/vu0_kernels.gen.hpp"
#include "vu0_points.gen.hpp"

namespace Vu_lab {

class Vu0KernelDemo : public Script {
 public:
  void init(ScriptContext& ctx) override {
    // A ring of points around the origin. The content does not matter - what
    // matters is that VU0 transforms them and the EE gets the result back.
    for (int i = 0; i < kCount; ++i) {
      const float t = (float)i * (6.2831853F / (float)kCount);
      in[i].set(cosf(t) * 4.0F, 0.0F, sinf(t) * 4.0F, 1.0F);
    }
    kernel.setParams(0.0F, 0.0F, 0.0F, 0.0F);
    kernel.setTime(0.0F);
    kernel.run(in, out, kCount);
    TYRA_LOG("VU0 kernel: point 0 in (", in[0].x, ", ", in[0].y, ") out (",
             out[0].x, ", ", out[0].y, ")");
    checkRanges(ctx);
  }

  void update(ScriptContext& ctx) override {
    // FOUR LOOKS, FOUR BUTTONS, one at a time - and one at a time is the thing
    // being shown, not a limitation worked around. A material class carries ONE
    // program: installing a second over it replaces the first. So picking a
    // look swaps what is resident on VU1 (one pipeline drain, one upload) while
    // every look sits in the ELF the whole time. Fine on a button; NOT fine per
    // frame. Pressing the active one turns it off.
    {
      const Tyra::PadButtons& hit = ctx.engine->pad.getClicked();
      int want = -1;
      if (hit.Triangle) want = vuscript::kCellShading;
      else if (hit.Square) want = vuscript::kVertexSnap;
      else if (hit.Circle) want = vuscript::kWobble;
      else if (hit.Cross) want = vuscript::kPalette;   // free: the scene has
                                                       // the player's jump off
      if (want >= 0 && want < vuscript::COUNT) {
        const bool already = vuscript::active(want);
        // Whatever happens next, the wobble's VU1 arrangement ends here -
        // including when CIRCLE turns the wobble off.
        leaveClipMode(ctx);
        vuscript::deactivateAll();
        if (!already) {
          if (want == vuscript::kWobble) enterClipMode(ctx);
          vuscript::activate(want);
        }
        TYRA_LOG("VU look -> ", already ? "none" : vuscript::name(want));
      }
    }

    // SQUARE cycles the stage-list looks, when the project has any switched on.
    if (ctx.engine->pad.getClicked().Square && vuprog::LOOK_COUNT > 1) {
      const int next = (vuprog::active() + 1) % vuprog::LOOK_COUNT;
      vuprog::activate(next);
      TYRA_LOG("VU look -> ", vuprog::lookName(next));
    }

    clock += 1.0F / 50.0F;
    if (clock > 6433.98F) clock -= 6433.98F;  // 2*pi*1024, see setTime

    kernel.setTime(clock);
    kernel.run(in, out, kCount);
    runRanges(ctx);

    // Wobble displaced Y by up to its amplitude (1.5); map that onto 0..1 and
    // hand it to the pillar's Desaturate slot.
    float grey = (out[0].y + 1.5F) * (1.0F / 3.0F);
    if (grey < 0.0F) grey = 0.0F;
    if (grey > 1.0F) grey = 1.0F;
    for (int i = 0; i < ctx.objectCount; ++i) {
      // Type 0 is a box and the pillar is the tall one. Matching on shape
      // rather than on an index survives an edit to the scene.
      if (ctx.objects[i].data.type != 0) continue;
      if (ctx.objects[i].data.scale[1] < 2.0F) continue;
      ctx.objects[i].data.vuParams[1] = grey;
    }
  }

 private:
  // "How far away is everything?", for the whole scene in one call - the job a
  // game actually keeps a vector unit for. Subtract, dot, root, over an array
  // the game already has; the answers pick levels of detail, decide what is
  // worth drawing, and name what the player is standing next to.
  //
  // Note what is NOT in the kernel: any adding-up. VU0 has no cross-element
  // reduction, so the nearest object is found by the loop below, on the EE.
  // VU0 computes the terms, the CPU folds them.
  int runRanges(ScriptContext& ctx) {
    int n = ctx.objectCount;
    if (n > kRangeMax) n = kRangeMax;   // run() takes its own count
    if (n <= 0) return -1;
    for (int i = 0; i < n; ++i)
      // W rides through untouched, so the index comes back with the distance
      // and there is no second array to keep in step.
      rangeIn[i].set(ctx.objects[i].data.position[0],
                     ctx.objects[i].data.position[1],
                     ctx.objects[i].data.position[2], (float)i);
    // xyz = where to measure from, w = 1 / the width of an LOD band: 1/12 puts
    // band 0 out to twelve units, then 24, then 36 and beyond.
    ranges.setParams(ctx.playerPosition.x, ctx.playerPosition.y,
                     ctx.playerPosition.z, 1.0F / 12.0F);
    ranges.run(rangeIn, rangeOut, n);

    int nearest = -1;
    float best = 1e9F;
    for (int i = 0; i < n; ++i)
      if (rangeOut[i].x < best) {
        best = rangeOut[i].x;
        nearest = i;
      }
    // Logged only when it CHANGES - this runs every frame. That is also how a
    // game uses it: the "press USE" prompt appears when the nearest thing
    // becomes a different thing.
    if (nearest != lastNearest) {
      lastNearest = nearest;
      TYRA_LOG("VU0 Ranges: nearest object is #", (int)rangeOut[nearest].w,
               " at ", best, " units, LOD band ", (int)rangeOut[nearest].y);
    }
    return n;
  }

  // The same numbers on the EE, ONCE. "The kernel ran" and "the kernel is
  // right" are different claims and only the second is worth anything; sqrtf
  // against VU0's rsqrt is a fair comparison, both being single-precision, so a
  // disagreement here is an arithmetic bug and not a tolerance argument.
  void checkRanges(ScriptContext& ctx) {
    const int n = runRanges(ctx);
    if (n <= 0) return;
    float worst = 0.0F;
    for (int i = 0; i < n; ++i) {
      const float dx = ctx.objects[i].data.position[0] - ctx.playerPosition.x;
      const float dy = ctx.objects[i].data.position[1] - ctx.playerPosition.y;
      const float dz = ctx.objects[i].data.position[2] - ctx.playerPosition.z;
      float d = sqrtf(dx * dx + dy * dy + dz * dz) - rangeOut[i].x;
      if (d < 0.0F) d = -d;
      if (d > worst) worst = d;
    }
    TYRA_LOG("VU0 Ranges: ", n, " objects, worst disagreement with the EE ",
             worst, " units");
  }

  // CIRCLE does not only pick the Wobble - it rearranges VU1 around it.
  //
  // The Wobble displaces in OBJECT space, so under the EE clipper the effect
  // stops wherever the mesh was already cut: props are submitted whole to
  // compensate and the terrain cannot be (a chunk straddling the near plane
  // wraps the GS raster window unclipped), so the wave broke along the chunks
  // at the frame edge. Under VU1 clipping the clipper is a VU program that
  // still has the object-space position, the script runs inside it, and the
  // chunk is cut AFTER the displacement.
  //
  // Not free: the clip family is roughly twice the as_is family it replaces,
  // and this project's four classes do not all fit under the 2042-slot ceiling.
  // So the demo hands Directional lights back and HIDES the one dyn-lit ball -
  // a dropped class does not crash (the engine walks down to a resident
  // relative) but it draws in the wrong style, which reads as a material bug.
  static constexpr unsigned kWobbleClasses = (1u << 0) | (1u << 3) | (1u << 4);

  void enterClipMode(ScriptContext& ctx) {
    if (clipMode) return;
    savedClasses = vuprog::residentClasses();
    savedClipping = vuprog::vu1Clipping();
    // Narrow FIRST: each call is a drain and a cache upload, and this order
    // means the wider set is never uploaded in the more expensive mode.
    vuprog::setResidentClasses(kWobbleClasses);
    vuprog::setVU1Clipping(true);
    hiddenCount = 0;
    for (int i = 0; i < ctx.objectCount && hiddenCount < kMaxHidden; ++i) {
      if (ctx.objects[i].data.dynLit == 0 || !ctx.objects[i].visible) continue;
      ctx.objects[i].visible = false;
      hidden[hiddenCount++] = i;
    }
    clipMode = true;
    TYRA_LOG("VU1 clipping ON, classes -> ", (int)kWobbleClasses, ", hidden ",
             hiddenCount);
  }

  void leaveClipMode(ScriptContext& ctx) {
    if (!clipMode) return;
    clipMode = false;
    for (int i = 0; i < hiddenCount; ++i)
      if (hidden[i] < ctx.objectCount) ctx.objects[hidden[i]].visible = true;
    hiddenCount = 0;
    vuprog::setVU1Clipping(savedClipping);
    vuprog::setResidentClasses(savedClasses);
    TYRA_LOG("VU1 clipping restored");
  }

  bool clipMode = false;
  bool savedClipping = false;
  unsigned savedClasses = 0;
  static constexpr int kMaxHidden = 16;
  int hidden[kMaxHidden] = {};
  int hiddenCount = 0;

  static constexpr int kCount = 32;
  Tyra::Vec4 in[kCount];
  Tyra::Vec4 out[kCount];
  Tyra::TyraXVu0Kernel kernel;  // the stage-composed one, from the panel

  // The C++ one, from src/vu0/ranges.cpp. The driver class is named after the
  // kernel; scripts/vu0_kernels.gen.hpp is written by the build container,
  // which is the only place src/vu0/*.cpp can be compiled and run.
  static constexpr int kRangeMax = 64;  // its maxElements()
  Tyra::Vec4 rangeIn[kRangeMax];
  Tyra::Vec4 rangeOut[kRangeMax];
  Tyra::RangesKernel ranges;
  int lastNearest = -2;
  float clock = 0.0F;
};

}  // namespace Vu_lab

TYRA_SCRIPT(Vu_lab::Vu0KernelDemo);
