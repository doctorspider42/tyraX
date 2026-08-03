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
