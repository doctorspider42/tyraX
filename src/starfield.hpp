#pragma once

#include <vector>

// Procedural night sky (docs/day-night-cycle.md, "Stars"). Host-only: no GL, no
// Project, no templates.cpp - the treegen/dronegen/placement shape, so the whole
// generator is exercisable from a 40-line harness.
//
// The output is a plain list of stars. Both consumers - the editor viewport and
// the generated game's buildStarField - turn each one into a camera-facing quad
// themselves, so the console ships ~400 rows rather than ~2400 baked vertices,
// and the preview cannot drift from what the PS2 draws.
//
// Why this is affordable on a PS2 at all: the field is drawn through ADDITIVE
// bags (the GS computes Cs*FIX + Cd), so per-star brightness lives in the
// Gouraud vertex colours and genuinely adds light to the frame - a bright star
// is bright rather than a pale grey pixel, and the bloom pass flares it. One
// bag per magnitude tier is 3 submits for the whole sky, and fading the field in
// at dusk is one byte per frame (the bag's additive FIX), not a rebuild.

namespace starfield {

// Magnitude tiers. They exist for two reasons at once: a tier is drawn at its
// own quad size, and it is its own bag - which is what lets the tiers twinkle
// against each other for two extra submits instead of per-star work.
constexpr int kTiers = 3;

// Hard cap, so a slider cannot cost the EE its frame. 3 bags x 800 stars is
// 4800 vertices; the sky dome next to it is 504.
constexpr int kMaxStars = 800;

struct Params {
    int seed = 1;
    int count = 400;
    // How far apart the brightest and faintest stars are. 0 = every star the
    // same, 1 = a realistic long tail of faint ones under a few bright.
    float magnitudeSpread = 0.7f;
    // Density and glow along a great circle. Without it a random sky reads as
    // uniform noise rather than as a sky.
    float milkyWay = 0.6f;
    float milkyWayTilt = 30.0f;  // degrees the band leans off the horizon
    float sizeScale = 1.0f;      // multiplies every quad
};

bool operator==(const Params& a, const Params& b);
inline bool operator!=(const Params& a, const Params& b) { return !(a == b); }

struct Star {
    float dir[3];            // unit vector, where the star sits on the sky
    float size;              // apparent radius as a fraction of the dome radius
    unsigned char r, g, b;   // colour AND brightness - the additive bag adds it
    int tier;                // 0 = brightest .. kTiers-1 = faintest
};

// Deterministic in `p`: the same seed is the same sky, always. Re-rolling the
// seed is the only thing that reshuffles it, so nudging a slider adjusts the
// sky rather than replacing it.
std::vector<Star> generate(const Params& p);

}  // namespace starfield
