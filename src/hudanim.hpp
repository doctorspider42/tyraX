// HUD motion - the host twin of the generated game's hudAnimEval
// (templates.cpp, TPL_GAME_CPP_SCENE). Header-only, no GL, no ImGui, no
// project.hpp: the editor's viewport overlay and the generated runtime read
// the SAME formula for every looped animation a HUD element can carry
// (docs/hud-animation.md), so what bobs in the editor bobs on the console.
//
// Change the math here AND in the generated twin, or the preview lies.
#pragma once

#include <cmath>

namespace hudanim {

// The animation kinds, in the order the UI Editor's combo lists them and the
// `.tyra` stores them (HudAnim::kind). Append only - the number is file format.
enum Kind {
    None = 0,
    Pulse,    // alpha dips by `amount` (0..1) once per period
    Bob,      // y moves +-amount px
    Sway,     // x moves +-amount px
    Breathe,  // scale grows by up to `amount` (fraction) once per period
    Blink,    // hard on/off: shown for `amount` (0..1) of every period
    Wobble,   // a circle of radius `amount` px
    Shake,    // jitter of +-amount px, re-rolled every `period` seconds
    KindCount
};

inline const char* kindName(int k) {
    static const char* names[KindCount] = {"None",  "Pulse",  "Bob",  "Sway",
                                           "Breathe", "Blink", "Wobble", "Shake"};
    return (k >= 0 && k < KindCount) ? names[k] : names[0];
}

// The transition an element plays when it is shown or hidden
// (HudTransition::kind). Runtime-only - the editor overlay does not preview
// it. Append only.
enum Transition {
    Cut = 0,
    Fade,
    SlideLeft,   // arrives from the left, leaves to the left
    SlideRight,
    SlideUp,     // arrives from above
    SlideDown,
    Pop,         // scales up from small while fading in
    TransitionCount
};

inline const char* transitionName(int k) {
    static const char* names[TransitionCount] = {
        "Cut", "Fade", "Slide from left", "Slide from right", "Slide from top",
        "Slide from bottom", "Pop"};
    return (k >= 0 && k < TransitionCount) ? names[k] : names[0];
}

// One-shot effects the Play HUD Effect flow node fires. Append only.
enum Effect { FxNone = 0, FxFlash, FxBounce, FxShake, EffectCount };

inline const char* effectName(int k) {
    static const char* names[EffectCount] = {"None", "Flash", "Bounce", "Shake"};
    return (k >= 0 && k < EffectCount) ? names[k] : names[0];
}

// What a looped animation does to a sprite at time t (seconds since the
// scene started): a pixel offset, a scale factor, an alpha factor and, for
// Blink, whether it is drawn at all.
struct Motion {
    float dx = 0.0f, dy = 0.0f;
    float scale = 1.0f;
    float alpha = 1.0f;
    bool visible = true;
};

// A deterministic -1..1 noise for the Shake kind. Both twins hash the same
// integer step, so the jitter is a function of time and not of a running
// RNG - a paused game and a resumed one shake the same way.
inline float noise(int step, int channel) {
    const float v = std::sin((float)step * 12.9898f + (float)channel * 78.233f) *
                    43758.5453f;
    return (v - std::floor(v)) * 2.0f - 1.0f;
}

// KEEP IN SYNC with hudAnimEval in the generated game (templates.cpp).
inline Motion evaluate(int kind, float period, float amount, float t) {
    Motion m;
    if (kind <= None || kind >= KindCount) return m;
    if (period < 0.01f) period = 0.01f;
    const float ph = t / period * 6.2831853f;
    switch (kind) {
        case Pulse:
            m.alpha = 1.0f - amount * 0.5f * (1.0f - std::cos(ph));
            break;
        case Bob: m.dy = -std::sin(ph) * amount; break;
        case Sway: m.dx = std::sin(ph) * amount; break;
        case Breathe:
            m.scale = 1.0f + amount * 0.5f * (1.0f - std::cos(ph));
            break;
        case Blink: {
            const float cyc = t / period;
            m.visible = (cyc - std::floor(cyc)) < amount;
            break;
        }
        case Wobble:
            m.dx = std::cos(ph) * amount;
            m.dy = std::sin(ph) * amount;
            break;
        case Shake: {
            const int step = (int)(t / period);
            m.dx = noise(step, 0) * amount;
            m.dy = noise(step, 1) * amount;
            break;
        }
        default: break;
    }
    if (m.alpha < 0.0f) m.alpha = 0.0f;
    if (m.alpha > 1.0f) m.alpha = 1.0f;
    if (m.scale < 0.05f) m.scale = 0.05f;
    return m;
}

// Where a bar's fill stands right now given what it is asked to show. The
// runtime integrates this per frame; the editor only draws the resting
// state, so it has no twin - but the shape of the numbers (a fraction 0..1
// after min/max) is decided here for both.
inline float fraction(float value, float minV, float maxV) {
    const float span = maxV - minV;
    if (span <= 0.0f) return 0.0f;
    float f = (value - minV) / span;
    return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

}  // namespace hudanim
