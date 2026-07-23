#pragma once

#include <string>

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
           a.fogEnd == b.fogEnd;
}
