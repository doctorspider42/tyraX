#include "ambience.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg = kPi / 180.0f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void normalize3(float v[3]) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < 1e-6f) {
        v[0] = 0.0f;
        v[1] = 1.0f;
        v[2] = 0.0f;
        return;
    }
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
}

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

void lerp3(const float a[3], const float b[3], float t, float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = lerpf(a[i], b[i], t);
}

// Smooth 0..1 ramp between edges - used for the sun/moon handover so the light
// sweeps across twilight instead of switching bodies on one frame.
float smoothstepf(float e0, float e1, float x) {
    if (e1 <= e0) return x < e0 ? 0.0f : 1.0f;
    const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool eq3(const float* a, const float* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

}  // namespace

bool operator==(const DayKey& a, const DayKey& b) {
    return a.hour == b.hour && eq3(a.skyColor, b.skyColor) &&
           eq3(a.skyTopColor, b.skyTopColor) && eq3(a.lightColor, b.lightColor) &&
           a.ambient == b.ambient && a.diffuse == b.diffuse &&
           a.brightness == b.brightness && eq3(a.fogColor, b.fogColor) &&
           a.stars == b.stars;
}

bool operator==(const DayCycle& a, const DayCycle& b) {
    return a.enabled == b.enabled && a.time == b.time &&
           a.sunAzimuth == b.sunAzimuth && a.sunTilt == b.sunTilt &&
           a.sunrise == b.sunrise && a.sunset == b.sunset &&
           a.sunSize == b.sunSize && a.moonAzimuth == b.moonAzimuth &&
           a.moonTilt == b.moonTilt && a.moonOffset == b.moonOffset &&
           a.moonSize == b.moonSize && a.moonPhase == b.moonPhase &&
           a.moonOpacity == b.moonOpacity &&
           a.moonTexture == b.moonTexture && a.runtime == b.runtime &&
           a.dayLength == b.dayLength && a.runtimeGrade == b.runtimeGrade &&
           a.bakeHour == b.bakeHour &&
           a.starsEnabled == b.starsEnabled &&
           a.starTwinkle == b.starTwinkle && a.starField == b.starField &&
           a.keys == b.keys;
}

namespace ambience {

float bakedHour(const DayCycle& c) {
    return wrap24(c.runtime ? c.bakeHour : c.time);
}

float wrap24(float hour) {
    float h = std::fmod(hour, 24.0f);
    if (h < 0.0f) h += 24.0f;
    return h;
}

void arcDirection(float azimuthDeg, float tiltDeg, float rise, float set,
                  float hour, float outDir[3]) {
    // The arc is a great circle. R is where it crosses the horizon at sunrise
    // (bearing `azimuth`, 0 = +Z, 90 = +X); P is its peak, a quarter turn
    // later, leaning `tilt` degrees off the zenith. R and P are perpendicular,
    // so cos(t)*R + sin(t)*P walks the circle: t = 0 rise, pi/2 peak, pi set,
    // and on past the horizon for the night half.
    const float az = azimuthDeg * kDeg;
    const float tilt = clampf(tiltDeg, -89.0f, 89.0f) * kDeg;
    const float R[3] = {std::sin(az), 0.0f, std::cos(az)};
    // Horizontal axis perpendicular to R - the direction the arc leans toward.
    const float H[3] = {std::cos(az), 0.0f, -std::sin(az)};
    const float P[3] = {std::sin(tilt) * H[0], std::cos(tilt),
                        std::sin(tilt) * H[2]};

    // Hours the body spends above the horizon, and how far into its cycle we
    // are. Both clamped away from the degenerate all-day / all-night ends.
    float up = wrap24(set - rise);
    up = clampf(up, 0.25f, 23.75f);
    const float since = wrap24(hour - rise);
    const float t = since <= up
                        ? kPi * (since / up)
                        : kPi + kPi * ((since - up) / (24.0f - up));

    const float c = std::cos(t), s = std::sin(t);
    outDir[0] = c * R[0] + s * P[0];
    outDir[1] = c * R[1] + s * P[1];
    outDir[2] = c * R[2] + s * P[2];
    normalize3(outDir);
}

void moonRiseSet(const DayCycle& c, float& rise, float& set) {
    rise = wrap24(c.sunrise + c.moonOffset);
    set = wrap24(c.sunset + c.moonOffset);
}

DayKey sampleKeys(const DayCycle& c, float hour) {
    if (c.keys.empty()) return DayKey{};
    if (c.keys.size() == 1) return c.keys[0];

    // The list is kept sorted by the UI, but a hand-edited .tyra need not be -
    // sort a local index list rather than trusting the file.
    std::vector<int> order(c.keys.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return wrap24(c.keys[a].hour) < wrap24(c.keys[b].hour);
    });

    const float h = wrap24(hour);
    const int n = (int)order.size();
    // First key at or after h; wrapping past the end lands on the first key of
    // the next day, which is what makes the list cyclic across midnight.
    int hi = 0;
    while (hi < n && wrap24(c.keys[order[hi]].hour) <= h) ++hi;
    const int lo = (hi - 1 + n) % n;
    hi %= n;

    const DayKey& a = c.keys[order[lo]];
    const DayKey& b = c.keys[order[hi]];
    const float ha = wrap24(a.hour);
    float span = wrap24(wrap24(b.hour) - ha);
    if (span < 1e-4f) return a;  // two keys at the same hour
    const float t = clampf(wrap24(h - ha) / span, 0.0f, 1.0f);

    DayKey out;
    out.hour = h;
    lerp3(a.skyColor, b.skyColor, t, out.skyColor);
    lerp3(a.skyTopColor, b.skyTopColor, t, out.skyTopColor);
    lerp3(a.lightColor, b.lightColor, t, out.lightColor);
    lerp3(a.fogColor, b.fogColor, t, out.fogColor);
    out.ambient = lerpf(a.ambient, b.ambient, t);
    out.diffuse = lerpf(a.diffuse, b.diffuse, t);
    out.brightness = lerpf(a.brightness, b.brightness, t);
    out.stars = lerpf(a.stars, b.stars, t);
    return out;
}

Resolved evaluate(const DayCycle& c, float hour) {
    Resolved r{};
    const float h = wrap24(hour);

    const DayKey k = sampleKeys(c, h);
    for (int i = 0; i < 3; ++i) {
        r.skyColor[i] = k.skyColor[i];
        r.skyTopColor[i] = k.skyTopColor[i];
        r.lightColor[i] = k.lightColor[i];
        r.fogColor[i] = k.fogColor[i];
    }
    r.ambient = k.ambient;
    r.diffuse = k.diffuse;
    r.brightness = k.brightness;
    r.stars = clampf(k.stars, 0.0f, 1.0f);

    arcDirection(c.sunAzimuth, c.sunTilt, c.sunrise, c.sunset, h, r.sunDir);
    float mRise = 0.0f, mSet = 0.0f;
    moonRiseSet(c, mRise, mSet);
    arcDirection(c.moonAzimuth, c.moonTilt, mRise, mSet, h, r.moonDir);
    r.sunElevation = std::asin(clampf(r.sunDir[1], -1.0f, 1.0f)) / kDeg;
    r.moonElevation = std::asin(clampf(r.moonDir[1], -1.0f, 1.0f)) / kDeg;

    // Which body lights the scene. Weighted rather than switched, so the light
    // sweeps across the twilight window instead of snapping - which matters
    // because the direction is BAKED into shadows and AO, and a hard flip
    // between two authored times reads as a bug.
    //
    // The two bodies are near-OPPOSITE at the crossover (a full moon rises as
    // the sun sets), so a plain weighted sum cancels to zero there and the
    // normalize picks up noise: measured as a 180-degree flip of the shadow
    // between 17:59 and 18:01. The fix is a zenith term that peaks exactly
    // where the cancellation would - 4*w*(1-w) is 1 at w = 0.5 and 0 at both
    // ends - so the light walks from one horizon UP OVER THE TOP and down to
    // the other. That is also the honest answer physically: at twilight the
    // dominant light really is the sky overhead, not a body at the horizon.
    const float wSun = smoothstepf(-kTwilightBand, kTwilightBand, r.sunElevation);
    const float zenithPull = 4.0f * wSun * (1.0f - wSun);
    float L[3];
    for (int i = 0; i < 3; ++i)
        L[i] = wSun * r.sunDir[i] + (1.0f - wSun) * r.moonDir[i];
    L[1] += zenithPull;
    normalize3(L);

    // Never light the world from below the floor - see kMinLightElevation.
    const float minY = std::sin(kMinLightElevation * kDeg);
    if (L[1] < minY) {
        const float hx = L[0], hz = L[2];
        const float hl = std::sqrt(hx * hx + hz * hz);
        const float want = std::sqrt(std::max(0.0f, 1.0f - minY * minY));
        if (hl > 1e-5f) {
            L[0] = hx / hl * want;
            L[2] = hz / hl * want;
        }
        L[1] = minY;
        normalize3(L);
    }
    for (int i = 0; i < 3; ++i) r.lightDir[i] = L[i];

    // Orientation of the moon's lit limb: the sun's direction projected into
    // the moon disc's plane. The disc is baked with its lit side toward +X, so
    // the renderer rotates the quad by this angle.
    {
        const float* f = r.moonDir;
        float right[3] = {f[2], 0.0f, -f[0]};  // cross(up, f), up = (0,1,0)
        if (std::sqrt(right[0] * right[0] + right[2] * right[2]) < 1e-5f) {
            right[0] = 1.0f;
            right[2] = 0.0f;
        }
        normalize3(right);
        const float u[3] = {f[1] * right[2] - f[2] * right[1],
                            f[2] * right[0] - f[0] * right[2],
                            f[0] * right[1] - f[1] * right[0]};
        const float sx = r.sunDir[0] * right[0] + r.sunDir[1] * right[1] +
                         r.sunDir[2] * right[2];
        const float sy = r.sunDir[0] * u[0] + r.sunDir[1] * u[1] +
                         r.sunDir[2] * u[2];
        r.moonUpAngle = std::atan2(sy, sx);
    }
    return r;
}

Grade driftGrade(const Resolved& now, const Resolved& baked) {
    Grade g{};
    // How much dimmer/brighter the world is than the hour it was baked at. The
    // ambient term is what a baked vertex colour is mostly made of, so it is
    // the honest ratio to use; brightness rides on top of it.
    const float lit = (baked.ambient + 0.5f * baked.diffuse) * baked.brightness;
    const float want = (now.ambient + 0.5f * now.diffuse) * now.brightness;
    float k = lit > 1e-4f ? want / lit : 1.0f;
    k = clampf(k, 0.15f, 2.0f);
    // Per channel, so a night lit by a blue moon actually goes blue rather than
    // just going grey: the light colour's ratio rides on top of the level.
    for (int i = 0; i < 3; ++i) {
        const float cb = baked.lightColor[i] < 0.02f ? 0.02f : baked.lightColor[i];
        const float cn = now.lightColor[i];
        g.gain[i] = clampf(k * (0.45f + 0.55f * (cn / cb)), kMinDriftGain, 2.0f);
        // A tiny lift toward the sky at night: real darkness is not black, it is
        // the sky reflected off everything, and pure gain alone crushes to mud.
        g.lift[i] = clampf(now.skyColor[i] * 0.06f * (1.0f - k), 0.0f, 0.12f);
        g.mixColor[i] = now.skyColor[i];
    }
    // Mix toward the sky colour with distance from the baked hour - which is
    // what aerial perspective does, and what stops a warm-baked prop staying
    // warm under a cold sky.
    g.mixAmount = clampf(0.30f * (1.0f - k) + (k > 1.0f ? 0.10f * (k - 1.0f) : 0.0f),
                         0.0f, 0.35f);
    return g;
}

void driftCompensation(const Grade& g, float out[3]) {
    for (int i = 0; i < 3; ++i)
        out[i] = clampf(g.gain[i] > 1e-4f ? 1.0f / g.gain[i] : 1.0f, 1.0f, 2.0f);
}

std::vector<DayKey> defaultKeys() {
    auto key = [](float hour, float skyR, float skyG, float skyB, float topR,
                  float topG, float topB, float ltR, float ltG, float ltB,
                  float amb, float dif, float bright, float fogR, float fogG,
                  float fogB, float stars) {
        DayKey k;
        k.hour = hour;
        k.skyColor[0] = skyR, k.skyColor[1] = skyG, k.skyColor[2] = skyB;
        k.skyTopColor[0] = topR, k.skyTopColor[1] = topG, k.skyTopColor[2] = topB;
        k.lightColor[0] = ltR, k.lightColor[1] = ltG, k.lightColor[2] = ltB;
        k.ambient = amb;
        k.diffuse = dif;
        k.brightness = bright;
        k.fogColor[0] = fogR, k.fogColor[1] = fogG, k.fogColor[2] = fogB;
        k.stars = stars;
        return k;
    };
    // Night is lit by a dim blue moon, dawn/dusk warm and low-contrast, noon the
    // bright neutral the no-cycle defaults already use.
    //
    // The night stops are DOUBLED (00:00 and 04:30, 21:30 and 23:59) on purpose.
    // Keys interpolate linearly, so a lone midnight key ramping to a 06:00 dawn
    // makes 03:00 read as half-sunrise - a pink sky in the dead of night. A
    // second key holding the same colour is what keeps night looking like night
    // and confines the warm ramp to the hour either side of the horizon.
    return {
        key(0.0f, 0.05f, 0.07f, 0.15f, 0.01f, 0.02f, 0.07f, 0.45f, 0.55f, 0.85f,
            0.18f, 0.22f, 0.85f, 0.05f, 0.07f, 0.13f, 1.0f),
        key(4.5f, 0.06f, 0.08f, 0.17f, 0.01f, 0.03f, 0.08f, 0.45f, 0.55f, 0.85f,
            0.18f, 0.23f, 0.85f, 0.06f, 0.08f, 0.14f, 1.0f),
        key(6.0f, 0.85f, 0.52f, 0.35f, 0.25f, 0.32f, 0.60f, 1.00f, 0.72f, 0.48f,
            0.35f, 0.45f, 0.95f, 0.70f, 0.52f, 0.45f, 0.15f),
        key(8.0f, 0.45f, 0.62f, 0.80f, 0.12f, 0.34f, 0.68f, 1.00f, 0.90f, 0.76f,
            0.48f, 0.46f, 1.00f, 0.58f, 0.55f, 0.56f, 0.0f),
        key(12.0f, 0.25f, 0.55f, 0.78f, 0.08f, 0.30f, 0.65f, 1.00f, 0.98f, 0.92f,
            0.55f, 0.45f, 1.00f, 0.50f, 0.50f, 0.55f, 0.0f),
        key(16.0f, 0.42f, 0.58f, 0.76f, 0.11f, 0.30f, 0.63f, 1.00f, 0.88f, 0.70f,
            0.48f, 0.47f, 1.00f, 0.60f, 0.54f, 0.52f, 0.0f),
        key(18.0f, 0.92f, 0.45f, 0.24f, 0.22f, 0.24f, 0.50f, 1.00f, 0.60f, 0.34f,
            0.32f, 0.48f, 0.95f, 0.75f, 0.45f, 0.35f, 0.15f),
        key(19.5f, 0.22f, 0.18f, 0.28f, 0.04f, 0.06f, 0.16f, 0.65f, 0.60f, 0.80f,
            0.24f, 0.30f, 0.90f, 0.20f, 0.17f, 0.24f, 0.75f),
        key(21.5f, 0.06f, 0.08f, 0.16f, 0.01f, 0.02f, 0.08f, 0.45f, 0.55f, 0.85f,
            0.18f, 0.23f, 0.85f, 0.06f, 0.08f, 0.14f, 1.0f),
    };
}

}  // namespace ambience
