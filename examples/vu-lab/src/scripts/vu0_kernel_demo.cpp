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
    // FOUR LOOKS, FOUR BUTTONS, one at a time.
    //
    // One at a time is not a limitation the demo works around, it is the thing
    // the demo is showing. A material class carries ONE program: install a
    // second over the same class and it simply replaces the first. So picking
    // a look means swapping what is resident on VU1 - one pipeline drain and
    // one upload - and Tools > VU Programs shows micro memory change as you
    // press. Every one of these is in the ELF the whole time; only the chosen
    // one occupies the chip.
    //
    // Pressing the button of the look that is already on turns it off and
    // gives the classes back to the engine's own programs.
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
        // Whatever happens next, the wobble's special VU1 arrangement ends
        // here - including when CIRCLE is pressed to turn the wobble off.
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
  // CIRCLE does not only pick the Wobble - it rearranges VU1 around it.
  //
  // The Wobble displaces vertices in OBJECT space, and until the clip family
  // was describable that meant the effect stopped wherever the EE clipper had
  // already cut the mesh: props were submitted whole to compensate
  // (`fullClipChecks`), and the terrain could not be, because a chunk
  // straddling the near plane wraps the GS raster window if it is drawn
  // unclipped. The wave visibly broke along the chunks at the edge of the
  // frame. Under VU1 clipping it does not: the clipper is a VU program with
  // the object-space position still in hand, the script runs inside it, and
  // the chunk is cut AFTER the displacement.
  //
  // It is not free. The clip family is roughly twice the as_is family it
  // stands in for, and this project draws four material classes - all four in
  // VU1 clipping do not fit under the 2042-slot ceiling (the engine's own
  // "programs overflow into the draw-finish program" assert is what catches
  // it, and Tools > VU Programs prices both modes before you get there). So
  // the demo pays for the clipper by handing a class back.
  //
  // Colour + Textured + Reflective. Directional lights is the one dropped:
  // the sky, the terrain, the boxes and the chrome ball all keep their own
  // program, and the single dyn-lit ball is HIDDEN rather than left to draw -
  // a dropped class does not crash (the engine walks the mesh down to a
  // resident relative) but it draws in the wrong style, which reads as a
  // material bug rather than as a budget the demo chose.
  static constexpr unsigned kWobbleClasses = (1u << 0) | (1u << 3) | (1u << 4);

  void enterClipMode(ScriptContext& ctx) {
    if (clipMode) return;
    savedClasses = vuprog::residentClasses();
    savedClipping = vuprog::vu1Clipping();
    // Narrow FIRST: each of these is a pipeline drain and a program-cache
    // upload, and doing it in this order means the wider set is never
    // uploaded in the more expensive mode even for one call.
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
  Tyra::TyraXVu0Kernel kernel;
  float clock = 0.0F;
};

}  // namespace Vu_lab

TYRA_SCRIPT(Vu_lab::Vu0KernelDemo);
