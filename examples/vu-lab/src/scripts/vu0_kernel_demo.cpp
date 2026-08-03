// The VU0 half of the authoring feature, running for real
// (../../../../docs/vu-authoring.md).
//
// This file is YOURS - the editor writes generated sources into src/gen/ and
// never touches src/scripts/, so a script added here survives every rebuild.
// That is also the only place a VU0 kernel can be driven from: a kernel is a
// compute job with no place in the scene pipeline, so nothing calls it unless
// your game does.
//
// What it demonstrates, in one loop:
//
//   VU0 computes  ->  the EE reads the answer back  ->  VU1 renders with it
//
// The kernel (Tools > VU Programs > VU0 kernel) is the SAME stage library the
// microprograms are built from - here a Wobble and a Squash - applied to a
// batch of quadwords instead of to vertices. Each frame this feeds it 32
// points, takes the first one back, and writes it into the pillar's per-mesh VU
// parameters, so a number produced on VU0 ends up driving a VU1 effect.
//
// Two things worth knowing before copying this:
//
//   - run() BLOCKS the EE. VU0's register file is the same one COP2 macro mode
//     uses - the engine's own Vec4/M4x4 math - so nothing may be doing vector
//     arithmetic while the kernel runs. 32 elements is microseconds; a big
//     batch is not free.
//   - Writing data.vuParams needs no `dirty` flag. It is not vertex data, so
//     nothing has to be rebuilt: the value is uploaded with the next frame's
//     object-data packet.
#include "scripts/script.hpp"
#include "scripts/vu_programs.gen.hpp"
#include "scripts/vu_scripts.gen.hpp"
#include "vu0_points.gen.hpp"

namespace Vu_lab {

class Vu0KernelDemo : public Script {
 public:
  void init(ScriptContext& ctx) override {
    (void)ctx;
    // A ring of points around the origin. Their content does not matter - what
    // matters is that the kernel transforms them on VU0 and the EE gets the
    // result back.
    for (int i = 0; i < kCount; ++i) {
      const float t = (float)i * (6.2831853F / (float)kCount);
      in[i].set(cosf(t) * 4.0F, 0.0F, sinf(t) * 4.0F, 1.0F);
    }
    kernel.setParams(0.0F, 0.0F, 0.0F, 0.0F);
    kernel.setTime(0.0F);
    kernel.run(in, out, kCount);
    TYRA_LOG("VU0 kernel: point 0 in (", in[0].x, ", ", in[0].y, ") out (",
             out[0].x, ", ", out[0].y, ")");
  }

  void update(ScriptContext& ctx) override {
    // TRIANGLE swaps the whole look. Every generated look is already in the
    // ELF - microcode is a byte range in EE memory - so this is one pipeline
    // drain and one upload of the program cache, not a rebuild of anything.
    // Fine on a button; NOT fine per frame.
    // TRIANGLE takes the project's own VU program off VU1 and puts it back.
    // Only what is ACTIVE occupies micro memory, so this is how a game carries
    // more programs than fit at once - and it is one pipeline drain and one
    // upload, which belongs on a button and not in this update loop.
    if (ctx.engine->pad.getClicked().Triangle && vuscript::COUNT > 0) {
      const bool on = vuscript::active(vuscript::kCellShading);
      if (on)
        vuscript::deactivate(vuscript::kCellShading);
      else
        vuscript::activate(vuscript::kCellShading);
      TYRA_LOG("VU script cell shading -> ", on ? "off" : "on");
    }
    // NO clipping switch bound here on purpose. vuprog::setVU1Clipping()
    // exists and works, but THIS project does not fit in VU1 clipping: the
    // clip family is roughly twice the as_is family, and the cell-shading
    // script already replaces the cull half of four classes. Tools > VU
    // Programs prices both modes and says so - and a console run that ignored
    // it hit the engine's own "programs overflow into the draw-finish program"
    // assert, which is exactly what that estimate now predicts.

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

    // The kernel's Wobble displaced Y by up to its amplitude (1.5). Map that
    // onto 0..1 and hand it to the pillar's Desaturate slot - the pillar's
    // colour is now the output of a VU0 computation, rendered by VU1.
    float grey = (out[0].y + 1.5F) * (1.0F / 3.0F);
    if (grey < 0.0F) grey = 0.0F;
    if (grey > 1.0F) grey = 1.0F;
    for (int i = 0; i < ctx.objectCount; ++i) {
      // Type 0 is a box; the pillar is the tall one. Matching on shape rather
      // than on an index keeps this working if the scene is edited.
      if (ctx.objects[i].data.type != 0) continue;
      if (ctx.objects[i].data.scale[1] < 2.0F) continue;
      ctx.objects[i].data.vuParams[1] = grey;
    }
  }

 private:
  static constexpr int kCount = 32;
  Tyra::Vec4 in[kCount];
  Tyra::Vec4 out[kCount];
  Tyra::TyraXVu0Kernel kernel;
  float clock = 0.0F;
};

}  // namespace Vu_lab

TYRA_SCRIPT(Vu_lab::Vu0KernelDemo);
