// routecam.cpp - the motion gate's fixed route.
//
// COPY THIS INTO A FIXTURE PROJECT as src/scripts/routecam.cpp and change the
// namespace to the project's own (the generated inc/scripts/script.hpp names
// it).  It is a global script - TYRA_SCRIPT, no attachment - so it runs the
// moment the project is rebuilt.  See the tyra-testing skill, "The motion
// gate", for the rest of the fixture's settings.
//
// The camera is driven from a FRAME INDEX, never from a clock and never from
// the pad: `--pad`'s driver refreshes at 25 Hz off the HOST wall clock, so a
// stick lands at a different frame offset in every run and two runs would not
// traverse the same content.  Frame k of run A therefore shows exactly what
// frame k of run B shows, which is what makes two runs comparable at all.
//
// The pad IS used, for one thing only: a Cross press resets the route index to
// 0.  That is a single one-shot event, so its ~5-frame arrival jitter shifts
// the WHOLE route by <= 0.1 s equally for every capture in the run, instead of
// perturbing each frame independently.  If the press never arrives the route
// still runs - only its phase is unknown.
//
// Four legs, 200 frames (3.3 s at 60 Hz) each, closed so the loop has no
// position jump.  A leg is longer than one capture burst on purpose: the burst
// has to sit INSIDE its leg with room to spare, because the pad press that
// starts the route arrives a few frames late and the emulator does not
// necessarily run at 100 %.
//
//   0 HOLD    parked at A.  The parked stability gate, as a leg: the picture is
//             still, the emitters are not.  This is the noise floor.
//   1 PAN     yaw sweeps 50 deg at A - rotation only.
//   2 DOLLY   forward 6 units along the swept heading, camera dropping
//             1.6 -> 0.9, so the textured ground is seen at a grazing angle.
//   3 RETURN  the way back: reverse dolly + reverse pan, ending exactly at A.

#include <math.h>

#include "scripts/script.hpp"

namespace Upscaler_lab {   // <-- the fixture project's namespace

class RouteCam : public Script {
 public:
  void update(ScriptContext& ctx) override {
    // getPRESSED, with the edge found here.  getClicked() is a single-frame
    // edge that livepad::tick raises inside the frame it polls, and a global
    // script that runs after it either sees that one frame or never sees the
    // press at all - measured: a Cross press from --pad reset nothing.
    const bool down = ctx.engine->pad.getPressed().Cross != 0;
    if (down && !wasDown) f = 0;
    wasDown = down;

    const int kLeg = 200;
    const int i = f % (kLeg * 4);
    const int leg = i / kLeg;
    const float u = (float)(i % kLeg) / (float)kLeg;

    const float kDeg = 3.14159265F / 180.0F;
    const float ax = 2.0F, ay = 1.6F, az = 14.0F;  // pose A
    const float yawA = 188.0F, sweep = 50.0F;      // degrees
    const float reach = 6.0F, drop = 0.7F;         // dolly length / camera dip
    const float pitch = -6.0F * kDeg;

    float yaw = yawA, ex = ax, ey = ay, ez = az;
    if (leg == 1) {
      yaw = yawA + sweep * u;
    } else if (leg == 2) {
      yaw = yawA + sweep;
      const float d = reach * u;
      ex = ax + sinf(yaw * kDeg) * d;
      ez = az + cosf(yaw * kDeg) * d;
      ey = ay - drop * u;
    } else if (leg == 3) {
      yaw = yawA + sweep * (1.0F - u);
      const float d = reach * (1.0F - u);
      const float held = (yawA + sweep) * kDeg;
      ex = ax + sinf(held) * d;
      ez = az + cosf(held) * d;
      ey = ay - drop * (1.0F - u);
    }

    const float r = yaw * kDeg;
    const float cp = cosf(pitch);
    ctx.cameraEye = Tyra::Vec4(ex, ey, ez);
    ctx.cameraAt = Tyra::Vec4(ex + sinf(r) * cp * 10.0F, ey + sinf(pitch) * 10.0F,
                              ez + cosf(r) * cp * 10.0F);
    ctx.cameraOverride = true;
    ++f;
  }

 private:
  int f = 0;
  bool wasDown = false;
};

TYRA_SCRIPT(RouteCam);

}  // namespace Upscaler_lab
