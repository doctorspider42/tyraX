#pragma once

#include <cmath>
#include <string>

// Color grading presets (Tools > Color Grading). DaVinci-style controls that
// compile down to what the PS2 GS blender can do with full-screen sprites
// (see RendererCorePostFx::setGrading): per-channel gain, per-channel lift
// and a mix toward a constant color. No per-pixel luma on the GS, so
// saturation is approximated by mixing toward mid-gray, and there is no
// gamma control at all.
struct ColorGradingPreset {
    std::string name;
    float brightness = 1.0f;   // 0..2, multiplies all channels
    float contrast = 1.0f;     // 0..2 around a 0.5 pivot
    float saturation = 1.0f;   // 0..1; below 1 mixes toward gray (approx.)
    float temperature = 0.0f;  // -1 cool (blue) .. +1 warm (orange)
    float tint[3] = {1.0f, 0.85f, 0.6f};  // mix target color
    float tintAmount = 0.0f;   // 0..1 mix toward tint
    float lift[3] = {0.0f, 0.0f, 0.0f};   // -0.5..0.5 per-channel offset
    float gain[3] = {1.0f, 1.0f, 1.0f};   // 0..2 per-channel multiplier
};

inline bool operator==(const ColorGradingPreset& a, const ColorGradingPreset& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.name == b.name && a.brightness == b.brightness &&
           a.contrast == b.contrast && a.saturation == b.saturation &&
           a.temperature == b.temperature && eq3(a.tint, b.tint) &&
           a.tintAmount == b.tintAmount && eq3(a.lift, b.lift) &&
           eq3(a.gain, b.gain);
}

// The integer GS-level parameters a preset compiles to - the exact numbers
// the PS2 blender runs with. The viewport preview replicates this quantized
// math (including the 0..255 clamp after every step) so both outputs match.
struct CompiledGrading {
    int gain[3] = {128, 128, 128};  // 0..255, 128 = 1x (Cd * FIX >> 7)
    int lift[3] = {0, 0, 0};        // -255..255 added after gain
    int mixColor[3] = {128, 128, 128};  // 0..255 mix target
    int mixAmt = 0;                     // 0..128, 128 = full replace

    bool neutral() const {
        return gain[0] == 128 && gain[1] == 128 && gain[2] == 128 &&
               lift[0] == 0 && lift[1] == 0 && lift[2] == 0 && mixAmt == 0;
    }
};

// brightness/contrast/temperature fold into the per-channel gain+lift;
// saturation (mix toward mid-gray) and tint fold into the single mix pass:
// lerp(lerp(x, gray, s), tint, t) == lerp(x, C, m) for constant targets.
inline CompiledGrading compileGrading(const ColorGradingPreset& p) {
    auto clampf = [](float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    };
    const float contrast = clampf(p.contrast, 0.0f, 2.0f);
    const float temp = clampf(p.temperature, -1.0f, 1.0f);
    // Mild warm/cool shift; +-1 moves R and B gains by 30%
    const float tempGain[3] = {1.0f + 0.3f * temp, 1.0f, 1.0f - 0.3f * temp};

    CompiledGrading out;
    for (int c = 0; c < 3; ++c) {
        const float g = clampf(p.brightness, 0.0f, 2.0f) * contrast *
                        clampf(p.gain[c], 0.0f, 2.0f) * tempGain[c];
        const float l = 0.5f * (1.0f - contrast) + clampf(p.lift[c], -0.5f, 0.5f);
        out.gain[c] = (int)clampf(std::round(g * 128.0f), 0.0f, 255.0f);
        out.lift[c] = (int)clampf(std::round(l * 255.0f), -255.0f, 255.0f);
    }

    const float s = clampf(1.0f - p.saturation, 0.0f, 1.0f);
    const float t = clampf(p.tintAmount, 0.0f, 1.0f);
    const float m = 1.0f - (1.0f - s) * (1.0f - t);
    if (m > 0.0001f) {
        for (int c = 0; c < 3; ++c) {
            const float target =
                (0.5f * s * (1.0f - t) + clampf(p.tint[c], 0.0f, 1.0f) * t) / m;
            out.mixColor[c] = (int)clampf(std::round(target * 255.0f), 0.0f, 255.0f);
        }
        out.mixAmt = (int)clampf(std::round(m * 128.0f), 0.0f, 128.0f);
    }
    return out;
}
