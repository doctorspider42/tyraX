// Swapping what runs on VU1, from the pad (../../../../docs/vu-authoring.md).
//
// This file is YOURS - the editor writes generated sources into src/gen/ and
// never touches src/scripts/.
//
// FOUR LOOKS, FOUR BUTTONS, one at a time - and one at a time is the thing
// being shown rather than a limitation worked around. A material class carries
// ONE program: installing a second over it replaces the first. So picking a
// look swaps what is resident on VU1 (one pipeline drain, one upload) while
// every look sits in the ELF the whole time. Fine on a button; NOT fine per
// frame. Pressing the active one turns it off.
//
//   TRIANGLE  cell shading + its ink outline
//   SQUARE    the PS1 vertex snap
//   CIRCLE    the travelling wave - and see enterClipMode()
//   CROSS     the two-colour palette (free: this scene has no jump)
#include "scripts/script.hpp"
#include "scripts/vu_programs.gen.hpp"
#include "scripts/vu_scripts.gen.hpp"

namespace Vu_lab {

class VuLookSwitch : public Script {
 public:
  void init(ScriptContext& ctx) override { (void)ctx; }

  void update(ScriptContext& ctx) override {
    const Tyra::PadButtons& hit = ctx.engine->pad.getClicked();
    int want = -1;
    if (hit.Triangle) want = vuscript::kCellShading;
    else if (hit.Square) want = vuscript::kVertexSnap;
    else if (hit.Circle) want = vuscript::kWobble;
    else if (hit.Cross) want = vuscript::kPalette;
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

    // SQUARE also cycles the stage-list looks, when the project has any
    // switched on. It does not in this scene - the three looks ship off, so
    // the C++ scripts above are unambiguous.
    if (hit.Square && vuprog::LOOK_COUNT > 1) {
      const int next = (vuprog::active() + 1) % vuprog::LOOK_COUNT;
      vuprog::activate(next);
      TYRA_LOG("VU look -> ", vuprog::lookName(next));
    }
  }

 private:
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
};

}  // namespace Vu_lab

TYRA_SCRIPT(Vu_lab::VuLookSwitch);
