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
