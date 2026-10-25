// Weather and street-lamp core (docs/weather.md).
//
// ONE SOURCE, TWO HOMES - the roadstream_core.inl arrangement. The editor
// compiles this file inside namespace weather (src/roadlight.cpp: the
// --vehicle-check "wet roads and lamps" block and the viewport preview), and
// the codegen pastes it VERBATIM into namespace weather of a generated game's
// inc/daynight.gen.hpp (embedded at build by CMake). So: no #include, no C
// block comments (the paste can land inside one), plain floats, and nothing
// but asinf from math.h, which both homes already include.
//
// What lives here is the part of the weather that has to agree between the
// editor and the console: the state machine a scene's authored weather and the
// Set Weather flow node drive, and the street lamps' level from the sun.

// What a scene's weather or a Set Weather node asks for.
enum : int { kWeatherDry = 0, kWeatherRain = 1 };

// How long the roads take to get wet and to dry again, in seconds for the
// whole 0..1 range. Wetness FOLLOWS the rain: it never runs ahead of it and
// it lags behind it, so a shower that stops leaves the streets shining for
// a while - which is half of what makes a city look rained on.
constexpr float kWeatherSoakSeconds = 6.0F;
constexpr float kWeatherDrySeconds = 40.0F;

struct WeatherState {
  float rain = 0.0F;  // rain intensity now, 0..1: the drops and their density
  float wet = 0.0F;   // how wet the roads are now, 0..1: tint and streaks
  float from = 0.0F, to = 0.0F;   // the rain transition in flight
  float time = 0.0F, span = 0.0F; // its clock and length (0 = none)

  static float clamp01(float v) { return v < 0.0F ? 0.0F : (v > 1.0F ? 1.0F : v); }

  // A scene load: the scene's authored weather, already soaked - a scene
  // that starts in the rain starts with wet streets.
  void reset(float intensity) {
    rain = wet = from = to = clamp01(intensity);
    time = span = 0.0F;
  }

  // Set Weather. `seconds` <= 0 switches at once, wetness included (the
  // way to set a scene up from On Start); otherwise the rain ramps linearly
  // from where it is now and the roads follow at their own pace.
  void request(int kind, float intensity, float seconds) {
    const float target = kind == kWeatherRain ? clamp01(intensity) : 0.0F;
    if (!(seconds > 0.0F)) {
      reset(target);
      return;
    }
    from = rain;
    to = target;
    time = 0.0F;
    span = seconds;
  }

  // One frame of `dt` seconds (0 while the game is paused).
  void tick(float dt) {
    if (!(dt > 0.0F)) return;
    if (span > 0.0F) {
      time += dt;
      if (time >= span) {
        rain = to;
        span = 0.0F;
      } else {
        rain = from + (to - from) * (time / span);
      }
    }
    if (wet < rain) {
      wet += dt * (1.0F / kWeatherSoakSeconds);
      if (wet > rain) wet = rain;
    } else if (wet > rain) {
      wet -= dt * (1.0F / kWeatherDrySeconds);
      if (wet < rain) wet = rain;
    }
  }
};

// The street lamps' level, 0 (off, day) .. 1 (fully lit, night), from the
// sun's direction (its y is the sine of its elevation). Full at 3 degrees
// below the horizon, off at 6 above, smoothstepped between - real lamps come
// on in the dusk, before the sky is dark.
inline float weatherLampLevel(float sunY) {
  const float y = sunY < -1.0F ? -1.0F : (sunY > 1.0F ? 1.0F : sunY);
  const float elev = asinf(y) * 57.2957795F;
  float t = (elev + 3.0F) * (1.0F / 9.0F);
  t = t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
  return 1.0F - t * t * (3.0F - 2.0F * t);
}

// --- puddles -------------------------------------------------------------
//
// A puddle is a road-detail decal (src/roaddetail.cpp) whose chunks share ONE
// colour, set once a frame from the wetness: nothing per vertex, ever. It
// fills after the road is wet and empties before it is dry, so a shower that
// barely darkens the asphalt leaves no puddles and a soaked street keeps
// them for a while after the rain.
constexpr float kWeatherPuddleFrom = 0.35F;  // wetness where the first water shows
constexpr float kWeatherPuddleFull = 0.85F;  // ... and where the puddles are full

inline float weatherPuddleLevel(float wet) {
  float t = (wet - kWeatherPuddleFrom) * (1.0F / (kWeatherPuddleFull - kWeatherPuddleFrom));
  t = t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
  return t * t * (3.0F - 2.0F * t);
}

// The puddles' one colour (0..255, alpha 0..128 in the GS's 128 = 1 scale):
// still water mirrors what is above it, so a puddle is dark water plus a
// share of the sky (bluer and lighter than the wet asphalt by day) plus, at
// night, the warm glow of the lit street it mirrors (`lamps`, the street
// lamps' level 0..1). `sky` is the colour the sky is drawn with, 0..255.
// Alpha 0 = dry: the GS alpha test drops every texel.
inline void weatherPuddleColor(float wet, float skyR, float skyG, float skyB, float lamps,
                               float out[4]) {
  const float lv = weatherPuddleLevel(wet);
  const float l = lamps < 0.0F ? 0.0F : (lamps > 1.0F ? 1.0F : lamps);
  const float sky[3] = {skyR, skyG, skyB};
  const float water[3] = {12.0F, 14.0F, 19.0F};
  const float glow[3] = {72.0F, 62.0F, 46.0F};
  for (int a = 0; a < 3; ++a) {
    float c = water[a] + 0.5F * sky[a] + l * glow[a];
    out[a] = c < 0.0F ? 0.0F : (c > 255.0F ? 255.0F : c);
  }
  out[3] = 116.0F * lv;
}

// --- car lights on a wet road ----------------------------------------------
//
// Every car whose lamps are on mirrors them in a wet road: a streak lying on
// the road from under the lamp toward the viewer, the street lamps' wet
// streak at a car lamp's height. Two lamps a face, two faces a car, and only
// the faces turned to the camera draw - so a car costs two quads, four at
// most, in the street lamps' own sprite bag.
constexpr float kWeatherCarStreakFar = 45.0F;  // farther cars draw none
constexpr int kWeatherCarStreakMax = 24;       // quads a frame, every car together

// Which faces of a car mirror their lamps: the headlights while the lights
// are on (an unbroken lamp), the tail lamps while the lights are on or the
// brake is - and none of it on a dry road.
inline void weatherCarLamps(int lightsOn, int brakeOn, int broken, float wet, int* front,
                            int* rear) {
  const bool w = wet > 0.02F;
  *front = w && lightsOn > 0 && (broken & 1) == 0 ? 1 : 0;
  *rear = w && (lightsOn > 0 || brakeOn != 0) && (broken & 2) == 0 ? 1 : 0;
}

// One lamp's streak. The lamp at (lx, ly, lz) faces (nx, nz) (unit,
// horizontal); the road under the car is the plane through (lx, gy, lz) with
// slope (sx, sz) = dy/dx, dy/dz; the eye is (ex, ey, ez). Returns 0 when it
// draws nothing (dry, too far, the lamp turned away), else writes the quad's
// four corners - the lamp's end first, x y z each - and its brightness 0..1.
inline int weatherCarStreak(float lx, float ly, float lz, float gy, float sx, float sz,
                            float nx, float nz, float hw, float ex, float ey, float ez,
                            float wet, float quad[12], float* k) {
  if (!(wet > 0.02F)) return 0;
  float tx = ex - lx, tz = ez - lz;
  const float hd = sqrtf(tx * tx + tz * tz);
  if (hd < 0.6F || hd > kWeatherCarStreakFar) return 0;
  tx /= hd, tz /= hd;
  const float facing = tx * nx + tz * nz;
  if (facing < 0.15F) return 0;
  // The mirror point, from the foot of the lamp: a low lamp mirrors close to
  // the car, which is why a car's streak hugs its own bumper.
  const float h = ly - gy > 0.15F ? ly - gy : 0.15F;
  const float eyeH = ey - gy > 0.3F ? ey - gy : 0.3F;
  const float m = hd * h / (h + eyeH);
  float half = 0.6F * m + 0.5F;
  if (half > 4.0F) half = 4.0F;
  float a = m - half, b = m + half;
  if (a < 0.1F) a = 0.1F;
  if (b > hd - 0.5F) b = hd - 0.5F;
  if (b <= a + 0.2F) return 0;
  const float wd = hw + 0.004F * hd;
  const float px = -tz * wd, pz = tx * wd;
  const float x0 = lx + tx * a, z0 = lz + tz * a;
  const float x1 = lx + tx * b, z1 = lz + tz * b;
  const float cx[4] = {x0 - px, x0 + px, x1 + px, x1 - px};
  const float cz[4] = {z0 - pz, z0 + pz, z1 + pz, z1 - pz};
  for (int c = 0; c < 4; ++c) {
    quad[c * 3 + 0] = cx[c];
    quad[c * 3 + 1] = gy + sx * (cx[c] - lx) + sz * (cz[c] - lz) + 0.06F;
    quad[c * 3 + 2] = cz[c];
  }
  const float fade = hd > 0.7F * kWeatherCarStreakFar
                         ? (kWeatherCarStreakFar - hd) / (0.3F * kWeatherCarStreakFar)
                         : 1.0F;
  float w = wet > 1.0F ? 1.0F : wet;
  *k = w * sqrtf(facing) * fade;
  return 1;
}

// A whole car: up to four streaks (front pair, then rear pair) into quads
// (12 floats each), their brightness into ks and 1 for a tail lamp into
// rear. The lamps are the model's measured lamp boxes (lampFront/lampRear:
// half spacing x, height y, z along the body, size; size 0 = the model
// marked none, and the shape-blind guess the tail-lamp glow uses stands in).
// groundFront/groundRear are the road under each axle (the wheels' own
// contact heights), which also tilt the streaks with the car's pitch.
inline int weatherCarStreaks(const float pos[3], float yawDeg, float scale,
                             const float lampFront[4], const float lampRear[4], float track,
                             float wheelBase, float overhang, float groundFront,
                             float groundRear, int lightsOn, int brakeOn, int broken,
                             float ex, float ey, float ez, float wet, float quads[48],
                             float ks[4], int rear[4]) {
  int front = 0, back = 0;
  weatherCarLamps(lightsOn, brakeOn, broken, wet, &front, &back);
  if (!front && !back) return 0;
  const float yr = yawDeg * 0.017453293F;
  const float cy = cosf(yr), sy = sinf(yr);
  const float fx = sy, fz = cy;    // forward
  const float rx = cy, rz = -sy;   // right
  const float hz = 0.5F * wheelBase * scale;
  const float pitch = wheelBase > 0.01F ? (groundFront - groundRear) / (wheelBase * scale) : 0.0F;
  const float sx = pitch * fx, sz = pitch * fz;
  int n = 0;
  for (int face = 0; face < 2; ++face) {
    if (face == 0 ? !front : !back) continue;
    const float* L = face == 0 ? lampFront : lampRear;
    const bool measured = L[3] > 0.0F;
    const float side = measured ? L[0] * scale : 0.32F * track * scale;
    const float up = measured ? L[1] * scale : (face == 0 ? 0.55F : 0.62F) * scale;
    const float along = measured ? L[2] * scale
                                 : (face == 0 ? hz + overhang * scale
                                              : -(hz + overhang * scale));
    const float size = measured ? L[3] * scale : 0.16F * scale;
    const float gy = face == 0 ? groundFront : groundRear;
    const float nx = face == 0 ? fx : -fx, nz = face == 0 ? fz : -fz;
    for (int s = -1; s <= 1; s += 2) {
      const float lx = pos[0] + fx * along + rx * side * (float)s;
      const float lz = pos[2] + fz * along + rz * side * (float)s;
      if (weatherCarStreak(lx, gy + up, lz, gy, sx, sz, nx, nz, 0.6F * size + 0.08F, ex, ey, ez,
                           wet, &quads[n * 12], &ks[n])) {
        rear[n] = face;
        ++n;
      }
    }
  }
  return n;
}
