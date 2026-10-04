#include "starfield.hpp"

#include <cmath>

namespace starfield {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Every random stream derives from the seed through this, so a re-generate is
// bit-identical and no star's value depends on how many stars came before it
// (the procgen determinism rule - never a running counter).
unsigned int mix32(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float rand01(int seed, int index, int channel) {
    const unsigned int h =
        mix32((unsigned int)seed * 0x9e3779b9u + (unsigned int)index * 0x85ebca6bu +
              (unsigned int)channel * 0xc2b2ae35u);
    return (float)(h >> 8) * (1.0f / 16777216.0f);  // [0,1)
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Star colour from a black-body temperature, roughly 2600 K (deep orange) to
// 12000 K (blue-white). A uniform-white field is the thing that reads as
// "pixels" rather than as stars, and real spectral colour is nearly free here.
void blackBody(float kelvin, float& r, float& g, float& b) {
    const float t = kelvin / 100.0f;
    auto lg = [](float v) { return std::log(v < 1e-3f ? 1e-3f : v); };
    if (t <= 66.0f) {
        r = 1.0f;
        g = clampf((99.4708025861f * lg(t) - 161.1195681661f) / 255.0f, 0.0f, 1.0f);
        b = t <= 19.0f
                ? 0.0f
                : clampf((138.5177312231f * lg(t - 10.0f) - 305.0447927307f) / 255.0f,
                         0.0f, 1.0f);
    } else {
        r = clampf(329.698727446f * std::pow(t - 60.0f, -0.1332047592f) / 255.0f,
                   0.0f, 1.0f);
        g = clampf(288.1221695283f * std::pow(t - 60.0f, -0.0755148492f) / 255.0f,
                   0.0f, 1.0f);
        b = 1.0f;
    }
}

}  // namespace

bool operator==(const Params& a, const Params& b) {
    return a.seed == b.seed && a.count == b.count &&
           a.magnitudeSpread == b.magnitudeSpread && a.milkyWay == b.milkyWay &&
           a.milkyWayTilt == b.milkyWayTilt && a.sizeScale == b.sizeScale;
}

std::vector<Star> generate(const Params& p) {
    std::vector<Star> out;
    int n = p.count;
    if (n < 0) n = 0;
    if (n > kMaxStars) n = kMaxStars;
    out.reserve((size_t)n);

    const float spread = clampf(p.magnitudeSpread, 0.0f, 1.0f);
    const float band = clampf(p.milkyWay, 0.0f, 1.0f);
    const float tilt = p.milkyWayTilt * kPi / 180.0f;
    // The band's pole: tilting the pole is what tilts the band.
    const float pole[3] = {0.0f, std::cos(tilt), std::sin(tilt)};

    for (int i = 0; i < n; ++i) {
        Star s{};

        // Uniform on the sphere (z then azimuth - the cosine-weighted latitude
        // is what keeps the poles from clumping).
        float z = rand01(p.seed, i, 0) * 2.0f - 1.0f;
        const float az = rand01(p.seed, i, 1) * 2.0f * kPi;

        // Milky Way: pull a share of the stars TOWARD the band plane rather
        // than rejecting samples, so the count the user asked for is the count
        // they get - a reject loop would quietly thin the sky as the slider
        // rises. The pull is toward "perpendicular to the pole", i.e. the
        // great circle itself.
        if (band > 0.0f && rand01(p.seed, i, 2) < band) {
            // Concentrate the pole-relative latitude near 0 with a power
            // curve; the exponent is what makes it a band and not a smear.
            const float u = rand01(p.seed, i, 3) * 2.0f - 1.0f;
            const float lat = u * std::pow(std::fabs(u), 2.0f) * 0.45f;
            // Rebuild the direction in the band's own frame, then rotate it
            // out by the pole's tilt.
            const float rr = std::sqrt(clampf(1.0f - lat * lat, 0.0f, 1.0f));
            const float bx = rr * std::cos(az), by = lat, bz = rr * std::sin(az);
            // Frame: pole is "up"; x stays x, and y/z rotate with the tilt.
            s.dir[0] = bx;
            s.dir[1] = by * pole[1] - bz * pole[2];
            s.dir[2] = by * pole[2] + bz * pole[1];
        } else {
            const float rr = std::sqrt(clampf(1.0f - z * z, 0.0f, 1.0f));
            s.dir[0] = rr * std::cos(az);
            s.dir[1] = z;
            s.dir[2] = rr * std::sin(az);
        }
        // Normalize defensively - everything downstream scales by the dome
        // radius and a short vector would put a star inside the scene.
        {
            const float l = std::sqrt(s.dir[0] * s.dir[0] + s.dir[1] * s.dir[1] +
                                      s.dir[2] * s.dir[2]);
            if (l > 1e-5f)
                for (int k = 0; k < 3; ++k) s.dir[k] /= l;
            else
                s.dir[0] = 0.0f, s.dir[1] = 1.0f, s.dir[2] = 0.0f;
        }

        // Magnitude. Raising a uniform to a power > 1 is what gives the real
        // shape - many faint stars under a handful of bright ones. spread 0
        // flattens it to "every star equal".
        const float u = rand01(p.seed, i, 4);
        const float exponent = 1.0f + spread * 5.0f;
        float bright = std::pow(u, exponent);
        bright = clampf(0.18f + 0.82f * bright, 0.0f, 1.0f);

        // Tier by brightness: 0 is the handful of bright ones.
        s.tier = bright > 0.62f ? 0 : (bright > 0.34f ? 1 : 2);

        // Angular size follows brightness, not the other way round: a bright
        // star reads as bigger on a 512x448 frame because it saturates more
        // pixels, and one texel is all a faint one deserves.
        // Sized in DEGREES against a 512x448 frame, which is what makes these
        // numbers what they are: the game's horizontal FOV is ~90 deg, so one
        // degree is ~6 px, and the corona sprite's bright core is only about a
        // third of its quad. Measured in PCSX2, the first pass (0.22 + 1.05)
        // gave bright stars a 3-pixel core - present, but too subtle to read as
        // a star. These put a bright one at ~3 deg, i.e. a ~6 px core.
        const float sizeDeg = (0.40f + 2.10f * bright * bright) * p.sizeScale;
        s.size = std::tan(sizeDeg * 0.5f * kPi / 180.0f);

        float cr, cg, cb;
        blackBody(2600.0f + rand01(p.seed, i, 5) * 9400.0f, cr, cg, cb);
        s.r = (unsigned char)(clampf(cr * bright, 0.0f, 1.0f) * 255.0f + 0.5f);
        s.g = (unsigned char)(clampf(cg * bright, 0.0f, 1.0f) * 255.0f + 0.5f);
        s.b = (unsigned char)(clampf(cb * bright, 0.0f, 1.0f) * 255.0f + 0.5f);

        out.push_back(s);
    }
    return out;
}

}  // namespace starfield
