#pragma once

#include <string>

// Breakable street furniture (docs/roads.md "Breakable furniture"): a car
// that hits a lamp post, a sign, a bollard or a traffic light (a tree, if its
// road says so) at its kind's Break speed or faster knocks it over, Need for
// Speed style. The settings are roadfurn::Settings::breakable / brk (format
// v109); the tables are roadfurn::Tables::pieces (ROAD_FURN_PIECES). This
// module is the generated RUNTIME (a text patch applied to the filled
// template, the roadlight shape), the host twin of its two rules and the
// --vehicle-check block "breakable furniture".
//
// Host-only, no GL, no ImGui.
namespace roadfurnbreak {

// Which other features' hooks the break runtime reaches into.
struct Gates {
    bool streamed = false;  // road streaming (a rebuilt chunk re-collapses its broken pieces)
    bool lamps = false;     // lit street lamps (a broken lamp's pool and halo go out)
    bool traffic = false;   // road traffic (a broken signal's live lens goes out)
};
// Applies every hook to one filled template (templates.cpp fillTemplate,
// after roadlight::patchTemplate - it patches the lamp runtime too). Only
// called for a project with breakable furniture. fillTemplate runs per
// generated file, so an anchor that is not in this file is not an error:
// --vehicle-check counts kHookMarks over the whole generated project.
std::string patchTemplate(std::string s, const Gates& g);
// One line of every hook that must land in a breakable project (the lamp,
// traffic and streaming hooks are checked on their own).
inline constexpr int kHookCount = 11;
extern const char* const kHookMarks[kHookCount];

// --- the two rules, the runtime's twins (change one, change both) ----------

// A piece breaks instead of being a wall when the car's speed (units/s,
// either sign) is at or above its threshold; 0 = never.
bool breaks(float speed, float threshold);
// The car body rectangle (half track `hx`, half length to the bumper `hz`,
// heading `yawDeg` - forward is (sin yaw, cos yaw)) at (px, pz) touches the
// box whose XZ centre is (bx, bz) and whose larger half extent is `r`.
bool touches(float px, float pz, float yawDeg, float hx, float hz, float bx, float bz, float r);

// --vehicle-check "breakable furniture".
void check(void (*verdict)(bool, const char*));

}  // namespace roadfurnbreak
