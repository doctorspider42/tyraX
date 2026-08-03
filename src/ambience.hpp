#pragma once

#include <string>
#include <vector>

#include "starfield.hpp"

// Ambience presets (Tools > Ambience Editor). A named bundle of the scene's
// "mood" settings - sky gradient, baked lighting and distance fog - authored
// once and reused across scenes. One preset can be the project default; a
// scene picks which preset it uses (empty = the default), and the Set Ambience
// flow node swaps the sky at runtime.
//
// These fields mirror the matching ProjectSettings members exactly; when a
// scene resolves to a preset, project::resolvedSettings overlays them on top of
// the scene's settings, so all downstream codegen/viewport keeps reading the
// same ProjectSettings fields. Keep the defaults in sync with ProjectSettings.

// --- Day / night cycle -------------------------------------------------------
// docs/day-night-cycle.md. A preset may carry a 24-hour cycle: the sun and the
// moon travel authored arcs and a list of DayKeys says what the light and the
// sky look like at each hour. It is STATIC in the sense that the authored
// `time` is baked at build - but it is not a screen tint: the resolved light
// direction replaces AmbiencePreset::lightDir inside project::resolvedSettings,
// so the vertex bake, the AO bake, the GI bake and its probe grid, the runtime
// projected shadows, the lens flare and the god rays all follow the slider.

// One authored moment in the cycle. The list is cyclic: the key before 00:00 is
// the last key of the previous day, so a colour set at 21:00 carries through
// midnight to the 06:00 key.
struct DayKey {
    float hour = 12.0f;  // 0..24

    float skyColor[3] = {0.25f, 0.55f, 0.78f};     // horizon
    float skyTopColor[3] = {0.08f, 0.3f, 0.65f};   // zenith
    float lightColor[3] = {1.0f, 1.0f, 1.0f};      // tints the diffuse term
    float ambient = 0.55f;
    float diffuse = 0.45f;
    float brightness = 1.0f;
    float fogColor[3] = {0.5f, 0.5f, 0.55f};
    // Night-sky brightness, 0..1 (docs/day-night-cycle.md "Stars"). Rides the
    // starfield bag's additive FIX, so the whole field fades in for one byte
    // per frame instead of a geometry rebuild.
    float stars = 0.0f;
};

bool operator==(const DayKey& a, const DayKey& b);
inline bool operator!=(const DayKey& a, const DayKey& b) { return !(a == b); }

struct DayCycle {
    bool enabled = false;
    float time = 12.0f;  // 0..24 - the Ambience Editor's midnight-to-midnight slider

    // The sun's arc is a great circle through the sky: it rises on the horizon
    // at compass bearing `sunAzimuth` (0 = +Z, 90 = +X) and the arc leans
    // `sunTilt` degrees away from the zenith, so the peak elevation is
    // 90 - sunTilt. Between `sunrise` and `sunset` the sun is above the
    // horizon; the rest of the day it continues the same circle underneath.
    float sunAzimuth = 90.0f;
    float sunTilt = 25.0f;
    float sunrise = 6.0f;
    float sunset = 18.0f;
    float sunSize = 3.0f;  // apparent diameter in degrees (the real sun is 0.5)

    // The moon rides its own arc, shifted `moonOffset` hours behind the sun -
    // 12 puts it up exactly while the sun is down, which is the useful default.
    float moonAzimuth = 90.0f;
    float moonTilt = 35.0f;
    float moonOffset = 12.0f;
    float moonSize = 4.0f;
    // 0 = new, 0.5 = full, 1 = new again. Baked into the moon disc as a
    // terminator, so it costs nothing at runtime.
    float moonPhase = 0.5f;
    // Texture for the moon disc. Empty = the built-in bake from NASA's LRO
    // colour map (see menubake::bakeMoonPNG). A project asset path otherwise -
    // equirectangular sources are projected, square ones used as the disc.
    std::string moonTexture;

    // Procedural night sky (starfield.hpp). Enabled here, shaped by `starField`,
    // and faded in by the per-key `stars` value - so the field appears at dusk
    // and washes out at dawn instead of being switched by a threshold.
    bool starsEnabled = false;
    // How much the magnitude tiers shimmer against each other, 0 = steady. It
    // costs nothing per star: each tier is one bag with its own additive FIX.
    float starTwinkle = 0.35f;
    starfield::Params starField;

    // --- the hybrid runtime half (docs/day-night-cycle.md) -----------------
    // With this on the GAME advances the clock, and the half of the cycle that
    // costs nothing per frame follows it live: the sun and moon move, the sky
    // dome and the fog retint, the projected shadows swing, the lens flare and
    // god rays track the sun, and the stars fade in.
    //
    // What does NOT follow is the baked half - the vertex shading, the AO
    // lightmap and any GI. Those stay at the hour `time` names, because
    // re-baking them is ~170 ms of EE work. `runtimeGrade` is the answer to
    // that gap: a per-frame colour grade carries the world's overall
    // brightness and warmth, so noon geometry does not sit brightly lit under
    // a midnight sky.
    bool runtime = false;
    // Real seconds for a whole 24 h. 240 = a four-minute day, which is about
    // the shortest that still reads as time passing rather than as a strobe.
    float dayLength = 240.0f;
    bool runtimeGrade = true;
    // With the clock running, `time` and "the hour the geometry was baked at"
    // stop being the same question: the clock STARTS at `time` and walks away
    // from it, while shadows, AO and GI are frozen wherever they were baked.
    // Keeping them apart is what lets you bake at noon (the most neutral light
    // for a lightmap) and still open the level at dusk - and it is what the
    // drift grade measures against. Ignored while `runtime` is off, where there
    // is only one hour; use ambience::bakedHour rather than reading it raw.
    float bakeHour = 12.0f;

    std::vector<DayKey> keys;  // sorted by hour; see ambience::sampleKeys
};

bool operator==(const DayCycle& a, const DayCycle& b);
inline bool operator!=(const DayCycle& a, const DayCycle& b) { return !(a == b); }

struct AmbiencePreset {
    std::string name;

    // Sky gradient dome (skyColor = horizon, skyTopColor = zenith).
    float skyColor[3] = {0.25f, 0.55f, 0.78f};
    float skyTopColor[3] = {0.08f, 0.3f, 0.65f};
    bool skyDome = true;
    float zenithSize = 0.5f;  // 0.05..0.95; how much of the dome is zenith color

    // Lighting (baked into vertex colors at build; a runtime preset switch
    // cannot re-bake it - only the sky repaints live).
    float lightDir[3] = {0.37f, 0.82f, 0.44f};
    float ambient = 0.55f;
    float diffuse = 0.45f;
    float lightColor[3] = {1.0f, 1.0f, 1.0f};
    float brightness = 1.0f;

    // Baked ambient occlusion (docs/ambient-occlusion.md): terrain
    // self-shadowing + contact darkening between static geometry, multiplied
    // into the same baked vertex colors as the light above. aoRadius = world
    // units the darkening reaches; aoStrength = how dark full occlusion gets.
    bool aoEnabled = false;
    float aoStrength = 0.55f;  // 0..1
    float aoRadius = 2.5f;     // world units

    // GS hardware distance fog.
    bool fogEnabled = false;
    float fogColor[3] = {0.5f, 0.5f, 0.55f};
    float fogStart = 15.0f;
    float fogEnd = 120.0f;

    // Time of day (docs/day-night-cycle.md). When enabled it OVERWRITES the
    // sky/light/fog colours and lightDir above at the authored hour - they stay
    // the values a cycle-less preset uses, and the fallback if it is turned off.
    DayCycle cycle;
};

inline bool operator==(const AmbiencePreset& a, const AmbiencePreset& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.name == b.name && eq3(a.skyColor, b.skyColor) &&
           eq3(a.skyTopColor, b.skyTopColor) && a.skyDome == b.skyDome &&
           a.zenithSize == b.zenithSize &&
           eq3(a.lightDir, b.lightDir) && a.ambient == b.ambient &&
           a.diffuse == b.diffuse && eq3(a.lightColor, b.lightColor) &&
           a.brightness == b.brightness && a.aoEnabled == b.aoEnabled &&
           a.aoStrength == b.aoStrength && a.aoRadius == b.aoRadius &&
           a.fogEnabled == b.fogEnabled &&
           eq3(a.fogColor, b.fogColor) && a.fogStart == b.fogStart &&
           a.fogEnd == b.fogEnd && a.cycle == b.cycle;
}

// The cycle's math. Host-only, no GL, no Project - so the whole thing is
// exercisable from a small harness (the treegen/placement shape), and the
// viewport, codegen and the editor UI all read ONE answer instead of three.
namespace ambience {

// Everything the cycle resolves to at one instant. Fed into ProjectSettings by
// project::resolvedSettings, into the sky discs by the viewport and codegen,
// and into the readout by the Ambience Editor - one evaluation, four consumers.
struct Resolved {
    float skyColor[3];
    float skyTopColor[3];
    float lightDir[3];  // direction TO the light, normalized
    float lightColor[3];
    float ambient;
    float diffuse;
    float brightness;
    float fogColor[3];
    float stars;

    float sunDir[3];   // direction to the sun, normalized (may be below y = 0)
    float moonDir[3];
    float sunElevation;   // degrees above the horizon, negative when down
    float moonElevation;
    // Rotation of the moon disc's "up" around the view axis, radians. Keeps the
    // lit limb pointing at the sun the way the real moon's does.
    float moonUpAngle;
};

// The minimum elevation the resolved lightDir is allowed to have, in degrees.
// A light AT the horizon gives zero diffuse on flat ground and one below it
// lights the world from underneath; night's darkness comes from the authored
// key colours, never from pointing the sun into the floor.
constexpr float kMinLightElevation = 5.0f;

// Half-width of the sun/moon handover, in degrees of sun elevation. The light
// direction sweeps between the two bodies across it instead of snapping.
constexpr float kTwilightBand = 6.0f;

// Direction to a body on its arc at `hour`. `rise`/`set` are the hours it
// crosses the horizon; the arc continues below the horizon in between.
void arcDirection(float azimuthDeg, float tiltDeg, float rise, float set,
                  float hour, float outDir[3]);

// The moon's rise/set, derived from the sun's by moonOffset.
void moonRiseSet(const DayCycle& c, float& rise, float& set);

// Cyclic interpolation of the key list at `hour`. An empty list yields a
// default-constructed DayKey; a single key yields that key at every hour.
DayKey sampleKeys(const DayCycle& c, float hour);

// The whole cycle at `hour`.
Resolved evaluate(const DayCycle& c, float hour);

// The colour grade that carries the world's drift while the clock runs
// (docs/day-night-cycle.md, "The hybrid"). `now` is the cycle resolved at the
// live hour, `baked` at the hour the geometry was baked at; the result is what
// turns noon vertex colours into a plausible midnight. Identity when the two
// hours agree, so a clock parked on its baked hour changes nothing.
//
// This is a TWIN: the generated game computes the same thing per frame and
// hands it to postFx.setGrading. Change one and change the other.
struct Grade {
    float gain[3];
    float lift[3];
    float mixColor[3];
    float mixAmount;
};
Grade driftGrade(const Resolved& now, const Resolved& baked);

// A sensible five-stop 24 hours (night, dawn, day, dusk, night) - what the
// Ambience Editor's "Seed a default day" button writes.
std::vector<DayKey> defaultKeys();

// The hour the scene's geometry bakes at: the cycle's own `time` normally, and
// the separate `bakeHour` once the clock runs. Every bake-side consumer asks
// this rather than reading a field, so the two can never disagree.
float bakedHour(const DayCycle& c);

// hour folded into [0, 24).
float wrap24(float hour);

}  // namespace ambience
