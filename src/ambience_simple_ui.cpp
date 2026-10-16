// Ambience Editor > Sky & time, the SIMPLE view (the default; docs/day-night-cycle.md,
// "Simple and Detailed"). The Detailed view (app.cpp) exposes every field of a
// preset - keyframes, arcs, the moon, the painted sky. This one asks the
// questions a level designer actually has, in their words: what time is it,
// what mood, where does the sun come from, is it foggy, does time pass.
//
// Nothing here is stored separately. Every control READS its value back from
// the preset (an average over the keys, the nearest compass point, the nearest
// fog level) and WRITES by rewriting the same fields the Detailed view edits,
// so the two views can never disagree and the saved format does not change.
#include "app.hpp"
#include "app_internal.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979f;

std::string hourText(float hour) {
    const float h = ambience::wrap24(hour);
    int hh = (int)h, mm = (int)((h - (float)hh) * 60.0f + 0.5f);
    if (mm >= 60) mm -= 60, hh = (hh + 1) % 24;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
    return buf;
}

const char* partOfDay(float hour) {
    const float h = ambience::wrap24(hour);
    if (h < 4.5f) return "Night";
    if (h < 7.5f) return "Dawn";
    if (h < 11.0f) return "Morning";
    if (h < 14.0f) return "Noon";
    if (h < 17.0f) return "Afternoon";
    if (h < 19.5f) return "Sunset";
    if (h < 21.5f) return "Dusk";
    return "Night";
}

ImU32 rgb(const float* c, float a = 1.0f) {
    auto ch = [](float v) { return (int)(ImClamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return IM_COL32(ch(c[0]), ch(c[1]), ch(c[2]), (int)(ImClamp(a, 0.0f, 1.0f) * 255.0f));
}

float luma(const float* c) { return c[0] * 0.299f + c[1] * 0.587f + c[2] * 0.114f; }

void desaturate(float* c, float amount) {
    const float l = luma(c);
    for (int i = 0; i < 3; ++i) c[i] += (l - c[i]) * amount;
}

void mixInto(float* c, const float* to, float t) {
    for (int i = 0; i < 3; ++i) c[i] += (to[i] - c[i]) * t;
}

// --- looks: a whole day in one click -----------------------------------------
// Each look is the default five-stop day reshaped, plus the hour it opens at,
// a fog level and whether the stars come out. Applying one rewrites the keys -
// Undo brings the old ones back.

enum Fog { FogOff = 0, FogLight, FogMedium, FogThick, FogCount };

void shapeGolden(std::vector<DayKey>& keys) {
    const float amber[3] = {1.0f, 0.62f, 0.36f};
    for (DayKey& k : keys) {
        if (k.hour < 5.0f || k.hour > 20.0f) continue;
        k.lightColor[0] = ImMin(1.0f, k.lightColor[0] + 0.05f);
        k.lightColor[2] = ImMax(0.0f, k.lightColor[2] - 0.18f);
        if (k.hour >= 14.0f) mixInto(k.skyColor, amber, 0.3f);
    }
}

void shapeOvercast(std::vector<DayKey>& keys) {
    for (DayKey& k : keys) {
        desaturate(k.skyColor, 0.7f);
        desaturate(k.skyTopColor, 0.7f);
        mixInto(k.skyTopColor, k.skyColor, 0.55f);  // a flat, low sky
        desaturate(k.lightColor, 0.8f);
        desaturate(k.fogColor, 0.6f);
        mixInto(k.fogColor, k.skyColor, 0.5f);
        const float sum = k.ambient + k.diffuse;  // soft light: mostly sky, little sun
        k.diffuse = sum * 0.3f;
        k.ambient = sum * 0.7f;
        k.stars = 0.0f;
    }
}

void shapeFoggy(std::vector<DayKey>& keys) {
    for (DayKey& k : keys) {
        desaturate(k.skyColor, 0.4f);
        desaturate(k.skyTopColor, 0.4f);
        mixInto(k.skyTopColor, k.skyColor, 0.4f);
        mixInto(k.fogColor, k.skyColor, 0.6f);
        k.stars *= 0.4f;
    }
}

struct Look {
    const char* name;
    const char* tip;
    float hour;
    Fog fog;
    bool stars;
    void (*shape)(std::vector<DayKey>&);
};

const Look kLooks[] = {
    {"Clear day", "Blue sky, neutral sun at noon, stars at night.", 12.0f, FogOff, true, nullptr},
    {"Sunrise", "Low warm sun just after dawn, a light haze.", 6.5f, FogLight, true, nullptr},
    {"Golden hour", "Warm amber light late in the afternoon.", 17.3f, FogLight, true, shapeGolden},
    {"Overcast", "Grey sky, soft light, hardly any shadow.", 12.0f, FogMedium, false, shapeOvercast},
    {"Foggy", "Pale sky and thick fog that hides the distance.", 9.0f, FogThick, false, shapeFoggy},
    {"Night", "Moonlight and stars.", 23.0f, FogOff, true, nullptr},
};
constexpr int kLookCount = (int)(sizeof(kLooks) / sizeof(kLooks[0]));

std::vector<DayKey> lookKeys(const Look& l) {
    std::vector<DayKey> keys = ambience::defaultKeys();
    if (l.shape) l.shape(keys);
    return keys;
}

// Fog levels in METRES (start, end); converted through unitsPerMeter.
const float kFogMetres[FogCount][2] = {{0, 0}, {60, 320}, {25, 160}, {6, 60}};

void setFog(AmbiencePreset& a, int level, float upm) {
    a.fogEnabled = level != FogOff;
    if (level == FogOff) return;
    a.fogStart = kFogMetres[level][0] * upm;
    a.fogEnd = kFogMetres[level][1] * upm;
}

// The level the preset's distances are closest to, or -1 = hand-set distances.
int fogLevelOf(const AmbiencePreset& a, float upm) {
    if (!a.fogEnabled) return FogOff;
    for (int l = FogLight; l < FogCount; ++l) {
        const float s = kFogMetres[l][0] * upm, e = kFogMetres[l][1] * upm;
        if (std::fabs(a.fogEnd - e) <= e * 0.2f && std::fabs(a.fogStart - s) <= s * 0.35f + upm) return l;
    }
    return -1;
}

void applyLook(AmbiencePreset& a, const Look& l, float upm) {
    DayCycle& c = a.cycle;
    c.enabled = true;
    c.keys = lookKeys(l);
    c.time = l.hour;
    c.starsEnabled = l.stars;
    setFog(a, l.fog, upm);
}

int lookOf(const AmbiencePreset& a, float upm) {
    if (!a.cycle.enabled) return -1;
    for (int i = 0; i < kLookCount; ++i) {
        const Look& l = kLooks[i];
        if (a.cycle.time != l.hour || a.cycle.starsEnabled != l.stars) continue;
        if (fogLevelOf(a, upm) != (int)l.fog) continue;
        if (a.cycle.keys == lookKeys(l)) return i;
    }
    return -1;
}

// --- directions --------------------------------------------------------------
// Bearings as the Detailed view reads them: 0 = +Z = north, 90 = +X = east.

const char* const kCompass[8] = {"north", "north-east", "east", "south-east",
                                 "south", "south-west", "west", "north-west"};

int compassIndex(float deg) {
    const float d = std::fmod(std::fmod(deg, 360.0f) + 360.0f, 360.0f);
    return ((int)((d + 22.5f) / 45.0f)) & 7;
}

float bearingOf(const float* dir) {
    return std::atan2(dir[0], dir[2]) * 180.0f / kPi;
}

float elevationOf(const float* dir) {
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    return len > 1e-6f ? std::asin(ImClamp(dir[1] / len, -1.0f, 1.0f)) * 180.0f / kPi : 0.0f;
}

void dirFrom(float bearingDeg, float elevationDeg, float* out) {
    const float b = bearingDeg * kPi / 180.0f, e = elevationDeg * kPi / 180.0f;
    out[0] = std::sin(b) * std::cos(e);
    out[1] = std::sin(e);
    out[2] = std::cos(b) * std::cos(e);
}

// --- the sky picture ---------------------------------------------------------

struct SkyLook {
    float top[3], horizon[3], light[3], fog[3];
    float ambient, diffuse, brightness, stars;
    float sunDir[3], moonDir[3];
    bool moon;
    float centreBearing;  // the bearing drawn in the middle of the picture
};

SkyLook skyLookOf(const AmbiencePreset& a, float hour) {
    SkyLook s{};
    const DayCycle& c = a.cycle;
    if (c.enabled) {
        const ambience::Resolved r = ambience::evaluate(c, hour);
        for (int i = 0; i < 3; ++i) {
            s.top[i] = r.skyTopColor[i];
            s.horizon[i] = r.skyColor[i];
            s.light[i] = r.lightColor[i];
            s.fog[i] = r.fogColor[i];
            s.sunDir[i] = r.sunDir[i];
            s.moonDir[i] = r.moonDir[i];
        }
        s.ambient = r.ambient;
        s.diffuse = r.diffuse * ImMax(0.0f, r.lightDir[1]);
        s.brightness = r.brightness;
        s.stars = c.starsEnabled ? r.stars : 0.0f;
        s.moon = true;
        float noon[3];
        ambience::arcDirection(c.sunAzimuth, c.sunTilt, c.sunrise, c.sunset,
                               (c.sunrise + c.sunset) * 0.5f, noon);
        s.centreBearing = bearingOf(noon);
    } else {
        for (int i = 0; i < 3; ++i) {
            s.top[i] = a.skyTopColor[i];
            s.horizon[i] = a.skyColor[i];
            s.light[i] = a.lightColor[i];
            s.fog[i] = a.fogColor[i];
            s.sunDir[i] = a.lightDir[i];
        }
        s.ambient = a.ambient;
        s.diffuse = a.diffuse * ImMax(0.0f, elevationOf(a.lightDir) / 90.0f);
        s.brightness = a.brightness;
        s.centreBearing = bearingOf(a.lightDir);
    }
    return s;
}

// Sky gradient, a ground band lit by the resolved light, the sun and the moon
// where they stand, stars and fog. A picture of the mood, not a render.
void drawSkyPicture(ImDrawList* dl, ImVec2 p, ImVec2 q, const SkyLook& s, bool fog) {
    const float w = q.x - p.x, h = q.y - p.y;
    const float r = ImGui::GetStyle().FrameRounding * 1.5f;
    const float horizonY = p.y + h * 0.7f;
    // Sky: horizontal slices so the corners can be rounded.
    const int n = 24;
    for (int i = 0; i < n; ++i) {
        const float t0 = (float)i / n, t1 = (float)(i + 1) / n;
        float c[3];
        for (int k = 0; k < 3; ++k) c[k] = s.top[k] + (s.horizon[k] - s.top[k]) * (t0 + t1) * 0.5f;
        const float y0 = p.y + (horizonY - p.y) * t0, y1 = p.y + (horizonY - p.y) * t1 + 1.0f;
        dl->AddRectFilled(ImVec2(p.x, y0), ImVec2(q.x, ImMin(y1, horizonY)), rgb(c), i == 0 ? r : 0.0f,
                          i == 0 ? ImDrawFlags_RoundCornersTop : ImDrawFlags_None);
    }
    // Stars, deterministic.
    if (s.stars > 0.02f) {
        unsigned seed = 1234567u;
        auto rnd = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return (float)((seed >> 8) & 0xFFFF) / 65535.0f;
        };
        for (int i = 0; i < 70; ++i) {
            const float x = p.x + rnd() * w, y = p.y + rnd() * (horizonY - p.y) * 0.85f;
            const float b = rnd();
            dl->AddCircleFilled(ImVec2(x, y), (0.6f + b * 1.1f) * ImMin(1.0f, h / 150.0f), IM_COL32(235, 240, 255, (int)(s.stars * (90 + b * 150))), 6);
        }
    }
    // Bodies: bearing relative to the picture's centre across the width,
    // elevation up the sky.
    auto place = [&](const float* dir) {
        float d = bearingOf(dir) - s.centreBearing;
        while (d > 180.0f) d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        const float x = p.x + w * 0.5f + (d / 110.0f) * (w * 0.5f);
        const float y = horizonY - (elevationOf(dir) / 90.0f) * (horizonY - p.y) * 0.92f;
        return ImVec2(x, y);
    };
    dl->PushClipRect(p, ImVec2(q.x, horizonY), true);
    // Body size follows the picture, so a look tile is not all sun.
    const float body = ImMin(ImGui::GetFontSize() * 0.55f, h * 0.07f);
    if (s.moon && elevationOf(s.moonDir) > -4.0f) {
        const ImVec2 m = place(s.moonDir);
        dl->AddCircleFilled(m, body * 1.9f, IM_COL32(200, 210, 235, 30), 32);
        dl->AddCircleFilled(m, body * 0.85f, IM_COL32(222, 228, 242, 235), 32);
    }
    if (elevationOf(s.sunDir) > -4.0f) {
        const ImVec2 sp = place(s.sunDir);
        float warm[3] = {1.0f, 0.93f, 0.78f};
        mixInto(warm, s.light, 0.35f);
        for (int g = 4; g >= 1; --g)
            dl->AddCircleFilled(sp, body * (1.0f + g), rgb(warm, 0.07f), 40);
        dl->AddCircleFilled(sp, body, rgb(warm), 40);
    }
    dl->PopClipRect();
    // Ground: a neutral grey-green lit like the scene would be.
    const float lit = ImClamp((s.ambient + s.diffuse) * s.brightness, 0.0f, 1.6f);
    float g0[3] = {0.30f, 0.33f, 0.27f}, g1[3];
    for (int k = 0; k < 3; ++k) {
        g0[k] = ImClamp(g0[k] * s.light[k] * lit * 1.3f, 0.0f, 1.0f);
        g1[k] = g0[k] * 0.55f;
    }
    if (fog) {
        mixInto(g0, s.fog, 0.55f);
        mixInto(g1, s.fog, 0.3f);
    }
    dl->AddRectFilledMultiColor(ImVec2(p.x, horizonY), ImVec2(q.x, q.y - r), rgb(g0), rgb(g0), rgb(g1), rgb(g1));
    dl->AddRectFilled(ImVec2(p.x, q.y - r), q, rgb(g1), r, ImDrawFlags_RoundCornersBottom);
    if (fog) {
        // A haze band over the horizon.
        const float bh = h * 0.22f;
        dl->AddRectFilledMultiColor(ImVec2(p.x, horizonY - bh), ImVec2(q.x, horizonY), rgb(s.fog, 0.0f),
                                    rgb(s.fog, 0.0f), rgb(s.fog, 0.85f), rgb(s.fog, 0.85f));
    }
    dl->AddRect(p, q, ImGui::GetColorU32(theme::semantics().border), r);
}

// The time of day as a strip of the day's own sky, a playhead and the hours.
// Drag (or click) to set the hour; snaps to 5 minutes. Returns true on change.
bool timeStrip(const char* id, float* hour, const DayCycle& c, float height) {
    const theme::Semantics& sem = theme::semantics();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, height));
    bool changed = false;
    if (ImGui::IsItemActive()) {
        float h = (ImGui::GetIO().MousePos.x - p.x) / w * 24.0f;
        h = ImClamp(std::round(h * 12.0f) / 12.0f, 0.0f, 24.0f - 1.0f / 12.0f);
        if (h != *hour) {
            *hour = h;
            changed = true;
        }
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding;
    const int n = 96;
    for (int i = 0; i < n; ++i) {
        const float hh = (i + 0.5f) * 24.0f / n;
        const DayKey k = ambience::sampleKeys(c, hh);
        float col[3];
        for (int j = 0; j < 3; ++j) col[j] = k.skyColor[j] * 0.6f + k.skyTopColor[j] * 0.4f;
        const float x0 = p.x + w * i / n, x1 = p.x + w * (i + 1) / n + 0.5f;
        const ImDrawFlags f = i == 0 ? ImDrawFlags_RoundCornersLeft
                                     : (i == n - 1 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_None);
        dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(ImMin(x1, p.x + w), p.y + height), rgb(col),
                          (i == 0 || i == n - 1) ? r : 0.0f, f);
    }
    dl->AddRect(p, ImVec2(p.x + w, p.y + height), ImGui::GetColorU32(sem.border), r);
    // Sunrise / sunset ticks.
    for (float t : {c.sunrise, c.sunset}) {
        const float x = p.x + w * ambience::wrap24(t) / 24.0f;
        dl->AddLine(ImVec2(x, p.y + height * 0.62f), ImVec2(x, p.y + height), IM_COL32(255, 214, 140, 200), 2.0f);
    }
    // Playhead.
    const float x = p.x + w * ambience::wrap24(*hour) / 24.0f;
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    dl->AddLine(ImVec2(x, p.y - 3.0f), ImVec2(x, p.y + height + 3.0f), IM_COL32(255, 255, 255, 235), 2.0f);
    dl->AddCircleFilled(ImVec2(x, p.y + height * 0.5f), hot ? 7.0f : 6.0f, IM_COL32(255, 255, 255, 245), 20);
    dl->AddCircle(ImVec2(x, p.y + height * 0.5f), hot ? 7.0f : 6.0f, IM_COL32(0, 0, 0, 90), 20, 1.5f);
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    // Hour labels.
    // Hour labels: the day's ends and noon, sunrise and sunset where they are.
    {
        ui::FontScope mono(ui::monoFont());
        const float ly = ImGui::GetCursorScreenPos().y + 2.0f;
        const float lfs = ImGui::GetFontSize() * 0.85f;
        auto label = [&](float hourAt, const std::string& text) {
            const float tw = ImGui::GetFont()->CalcTextSizeA(lfs, FLT_MAX, 0.0f, text.c_str()).x;
            const float lx = ImClamp(p.x + w * hourAt / 24.0f - tw * 0.5f, p.x, p.x + w - tw);
            dl->AddText(ImGui::GetFont(), lfs, ImVec2(lx, ly), ImGui::GetColorU32(sem.textDim), text.c_str());
        };
        label(0.0f, "00:00");
        label(24.0f, "24:00");
        const float sr = ambience::wrap24(c.sunrise), ss = ambience::wrap24(c.sunset);
        label(sr, hourText(sr) + " sunrise");
        label(ss, "sunset " + hourText(ss));
        if (std::fabs(12.0f - sr) > 3.0f && std::fabs(12.0f - ss) > 3.0f) label(12.0f, "12:00");
    }
    ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight() + 2.0f));
    return changed;
}

// Sunrise and sunset on one track: two knobs, the daylight between them.
bool sunRange(const char* id, float* rise, float* set) {
    const theme::Semantics& sem = theme::semantics();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const float valueW = ImGui::CalcTextSize("00:00-00:00").x + 8.0f;
    const float x0 = p.x + 7.0f, x1 = p.x + w - valueW - 7.0f;
    auto toX = [&](float hr) { return x0 + (x1 - x0) * hr / 24.0f; };
    static int dragging = 0;  // 1 = sunrise, 2 = sunset (one widget at a time)
    bool changed = false;
    if (ImGui::IsItemActivated()) {
        const float mx = ImGui::GetIO().MousePos.x;
        dragging = std::fabs(mx - toX(*rise)) <= std::fabs(mx - toX(*set)) ? 1 : 2;
    }
    if (ImGui::IsItemActive() && dragging) {
        float hr = (ImGui::GetIO().MousePos.x - x0) / (x1 - x0) * 24.0f;
        hr = std::round(hr * 12.0f) / 12.0f;
        if (dragging == 1) hr = ImClamp(hr, 1.0f, *set - 2.0f);
        else hr = ImClamp(hr, *rise + 2.0f, 23.0f);
        float& v = dragging == 1 ? *rise : *set;
        if (v != hr) v = hr, changed = true;
    }
    if (!ImGui::IsItemActive()) dragging = 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f;
    dl->AddRectFilled(ImVec2(x0, cy - 2), ImVec2(x1, cy + 2), ImGui::GetColorU32(ImGuiCol_FrameBg), 2.0f);
    dl->AddRectFilled(ImVec2(toX(*rise), cy - 2), ImVec2(toX(*set), cy + 2), ImGui::GetColorU32(sem.accent), 2.0f);
    for (float hr : {*rise, *set}) dl->AddCircleFilled(ImVec2(toX(hr), cy), 7.0f, IM_COL32(255, 255, 255, 245), 20);
    const std::string v = hourText(*rise) + "-" + hourText(*set);
    {
        ui::FontScope mono(ui::monoFont());
        const ImVec2 ts = ImGui::CalcTextSize(v.c_str());
        dl->AddText(ImVec2(p.x + w - ts.x, cy - ts.y * 0.5f), ImGui::GetColorU32(sem.text), v.c_str());
    }
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    return changed;
}

// A "look" tile: the look's own sky at its hour, the name under it.
bool lookTile(const Look& l, bool selected, float tw, float th) {
    const theme::Semantics& sem = theme::semantics();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float labelH = ImGui::GetTextLineHeightWithSpacing();
    const bool clicked = ImGui::InvisibleButton(l.name, ImVec2(tw, th + labelH));
    const bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetTooltip("%s\nReplaces this preset's day - Undo brings it back.", l.tip);
    AmbiencePreset tmp;
    tmp.cycle.enabled = true;
    tmp.cycle.keys = lookKeys(l);
    tmp.cycle.starsEnabled = l.stars;
    tmp.fogEnabled = l.fog != FogOff;
    const SkyLook s = skyLookOf(tmp, l.hour);
    tmp.cycle.time = l.hour;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawSkyPicture(dl, p, ImVec2(p.x + tw, p.y + th), s, tmp.fogEnabled);
    const float r = ImGui::GetStyle().FrameRounding * 1.5f;
    if (selected)
        dl->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + tw + 2, p.y + th + 2), ImGui::GetColorU32(sem.accent), r + 2, 0, 2.0f);
    else if (hov)
        dl->AddRect(p, ImVec2(p.x + tw, p.y + th), ImGui::GetColorU32(sem.text), r, 0, 1.0f);
    const ImVec2 ts = ImGui::CalcTextSize(l.name);
    dl->AddText(ImVec2(p.x + (tw - ts.x) * 0.5f, p.y + th + 3.0f),
                ImGui::GetColorU32(selected ? sem.text : sem.textDim), l.name);
    return clicked;
}

// The Simple view's rows: a dim caption (dotted underline + tooltip when it
// has help), then the control from the label column on - the column matches
// InspectorRows' 34% clamp, so these line up with the stock rows around them.
void rowLabel(const char* text, const char* tip, float lw) {
    const theme::Semantics& sem = theme::semantics();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    if (tip) {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (float x = a.x; x < b.x; x += 3.0f)
            dl->AddRectFilled(ImVec2(x, b.y - 1.0f), ImVec2(x + 1.0f, b.y), ImGui::GetColorU32(sem.textDim));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    }
    ImGui::SameLine(lw);
}

// A thin track with one knob and the value as text at the right end (the
// design's slider). `fromCol`/`toCol` non-zero paint the track as a gradient
// instead of filling up to the knob. InvisibleButton named `id` (scripts drag
// it). Returns true while the value changes.
bool trackSlider(const char* id, float* v, float lo, float hi, const std::string& shown,
                 ImU32 fromCol = 0, ImU32 toCol = 0) {
    const theme::Semantics& sem = theme::semantics();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    float valueW;
    {
        ui::FontScope mono(ui::monoFont());
        valueW = ImMax(ImGui::CalcTextSize("00:00-00:00").x * 0.6f, ImGui::CalcTextSize(shown.c_str()).x) + 10.0f;
    }
    const float x0 = p.x + 7.0f, x1 = p.x + w - valueW - 7.0f;
    bool changed = false;
    if (ImGui::IsItemActive() && x1 > x0) {
        const float t = ImClamp((ImGui::GetIO().MousePos.x - x0) / (x1 - x0), 0.0f, 1.0f);
        const float nv = lo + (hi - lo) * t;
        if (nv != *v) *v = nv, changed = true;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f;
    const float t = ImClamp((*v - lo) / (hi - lo), 0.0f, 1.0f);
    const float kx = x0 + (x1 - x0) * t;
    if (fromCol || toCol) {
        dl->AddRectFilledMultiColor(ImVec2(x0, cy - 2), ImVec2(x1, cy + 2), fromCol, toCol, toCol, fromCol);
    } else {
        dl->AddRectFilled(ImVec2(x0, cy - 2), ImVec2(x1, cy + 2), ImGui::GetColorU32(ImGuiCol_FrameBg), 2.0f);
        dl->AddRectFilled(ImVec2(x0, cy - 2), ImVec2(kx, cy + 2), ImGui::GetColorU32(sem.accent), 2.0f);
    }
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    dl->AddCircleFilled(ImVec2(kx, cy), hot ? 8.0f : 7.0f, IM_COL32(255, 255, 255, 245), 20);
    {
        ui::FontScope mono(ui::monoFont());
        const ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
        dl->AddText(ImVec2(p.x + w - ts.x, cy - ts.y * 0.5f), ImGui::GetColorU32(sem.text), shown.c_str());
    }
    return changed;
}

}  // namespace

// Sky & time, the Simple view: preset bar, the sky, the time of day, looks,
// then plain-word settings. Wide windows put the settings in a second column.
void App::drawAmbienceSimple(bool& changed) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float upm = project_.settings.unitsPerMeter > 0.0f ? project_.settings.unitsPerMeter : 1.0f;
    auto LW = [this] {
        return IM_TRUNC(ImClamp(ImGui::GetContentRegionAvail().x * 0.34f, scaled(110), scaled(170)));
    };
    auto num = [](const char* fmt, float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, v);
        return std::string(buf);
    };

    if ((selectedAmbience_ < 0 || selectedAmbience_ >= (int)project_.ambiencePresets.size()) &&
        !project_.ambiencePresets.empty()) {
        const int idx = project::ambienceIndexFor(project_, project_.active());
        selectedAmbience_ = idx >= 0 && idx < (int)project_.ambiencePresets.size() ? idx : 0;
    }
    if (project_.ambiencePresets.empty()) {
        ImGui::BeginChild("##amb_simple_empty", ImVec2(0, 0));
        if (ui::EmptyState(ui::Glyph::Sparkle, "No sky yet",
                           "A preset is the sky, the sunlight and the fog of a scene - pick a look "
                           "and a time of day.",
                           "Create a sky")) {
            AmbiencePreset a;
            a.name = "sky";
            applyLook(a, kLooks[0], upm);
            project_.ambiencePresets.push_back(std::move(a));
            selectedAmbience_ = 0;
            changed = true;
        }
        ImGui::EndChild();
        return;
    }

    int deleteIdx = -1;
    bool wantNew = false;

    // --- preset bar -----------------------------------------------------------
    {
        AmbiencePreset& a = project_.ambiencePresets[selectedAmbience_];
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::TextUnformatted("Preset");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImClamp(ImGui::GetContentRegionAvail().x * 0.3f, scaled(120), scaled(220)));
        if (ImGui::BeginCombo("##amb_simple_pick", a.name.c_str())) {
            for (int i = 0; i < (int)project_.ambiencePresets.size(); ++i) {
                const AmbiencePreset& o = project_.ambiencePresets[i];
                const std::string row = o.name + (project_.defaultAmbience == i ? "  (default)" : "") + "##spick";
                if (ImGui::Selectable(row.c_str(), i == selectedAmbience_)) {
                    selectedAmbience_ = i;
                    ambiencePlayHour_ = -1.0f;
                }
            }
            ImGui::Separator();
            if (ImGui::Selectable("+ New preset##simple")) wantNew = true;
            ImGui::EndCombo();
        }
        const SceneData& scn = project_.active();
        const bool isScenes = project::ambienceIndexFor(project_, scn) == selectedAmbience_;
        const std::string useLabel = "Use for scene " + scn.name + "##usescene";
        const float moreS = ImGui::GetFrameHeight();
        const float swW = ImGui::CalcTextSize("Preview in viewport").x + fs * 2.6f;
        // Which scene this lights, or the one click that makes it so.
        ImGui::SameLine(0.0f, gap);
        if (isScenes) {
            const std::string used = "Used by scene " + scn.name;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f - 2.0f);
            ui::Badge(used.c_str(), 3);
        } else {
            if (ui::SecondaryButton(useLabel.c_str())) {
                project_.active().ambiencePreset = a.name;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Light scene \"%s\" with this preset.", scn.name.c_str());
        }
        const float right = ImGui::GetWindowContentRegionMax().x;
        const float need = moreS + swW + gap * 2.0f;
        ImGui::SameLine();
        if (ImGui::GetCursorPosX() + need > right) ImGui::NewLine();
        ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX(), right - need + gap));
        ui::Switch("Preview in viewport", &ambiencePreview_);
        ImGui::SameLine(0.0f, gap);
        if (ui::MoreButton("Preset actions", moreS)) ImGui::OpenPopup("##amb_more");
        if (ImGui::BeginPopup("##amb_more")) {
            drawAmbiencePresetMenu(a, deleteIdx, changed);
            ImGui::EndPopup();
        }
    }
    ImGui::Spacing();

    AmbiencePreset& a = project_.ambiencePresets[ImClamp(selectedAmbience_, 0, (int)project_.ambiencePresets.size() - 1)];
    DayCycle& c = a.cycle;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool wide = avail.x >= scaled(900);
    const float leftW = wide ? ImFloor(avail.x * 0.56f) : avail.x;

    // --- left: picture, time, looks --------------------------------------------
    // Narrow (a docked column): one scrolling column whose two halves size to
    // their content. Wide: two columns that scroll on their own.
    if (!wide) ImGui::BeginChild("##amb_simple_col", ImVec2(0, avail.y));
    const ImGuiChildFlags halfFlags = wide ? ImGuiChildFlags_None : ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild("##amb_simple_l", ImVec2(wide ? leftW : 0.0f, wide ? avail.y : 0.0f), halfFlags);
    {
        const float w = ImGui::GetContentRegionAvail().x;
        const float picH = ImClamp(w * 0.38f, scaled(130), scaled(260));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        // Play the day moves only the picture (24 s a day); the preset keeps
        // its hour and the viewport stays put.
        if (ambiencePlayHour_ >= 0.0f) {
            if (!c.enabled) ambiencePlayHour_ = -1.0f;
            else ambiencePlayHour_ = ambience::wrap24(ambiencePlayHour_ + ImGui::GetIO().DeltaTime);
        }
        const bool playing = ambiencePlayHour_ >= 0.0f;
        const float hour = playing ? ambiencePlayHour_ : c.time;
        const SkyLook s = skyLookOf(a, hour);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        drawSkyPicture(dl, p, ImVec2(p.x + w, p.y + picH), s, a.fogEnabled);
        // Readout in the corner: the hour and what lights the scene.
        {
            const ImVec2 o(p.x + fs * 0.8f, p.y + fs * 0.7f);
            std::string big = c.enabled ? hourText(hour) : std::string("Fixed look");
            std::string small;
            if (c.enabled) {
                const float sunEl = elevationOf(s.sunDir);
                char buf[96];
                if (sunEl > 0.5f)
                    std::snprintf(buf, sizeof(buf), "%s · sun %.0f° up in the %s", partOfDay(hour), sunEl,
                                  kCompass[compassIndex(bearingOf(s.sunDir))]);
                else if (elevationOf(s.moonDir) > 0.5f)
                    std::snprintf(buf, sizeof(buf), "%s · the moon lights the scene", partOfDay(hour));
                else
                    std::snprintf(buf, sizeof(buf), "%s · sun below the horizon", partOfDay(hour));
                small = buf;
            } else {
                small = "The light never changes";
            }
            ImVec2 bs;
            {
                ui::FontScope mono(ui::monoFont());
                bs = ImGui::CalcTextSize(big.c_str());
            }
            const ImVec2 ss = ImGui::CalcTextSize(small.c_str());
            const ImVec2 box(ImMax(bs.x, ss.x) + fs * 1.2f, bs.y + ss.y + fs * 0.9f);
            dl->AddRectFilled(o, ImVec2(o.x + box.x, o.y + box.y), IM_COL32(8, 10, 14, 150), fs * 0.5f);
            {
                ui::FontScope mono(ui::monoFont());
                dl->AddText(ImVec2(o.x + fs * 0.6f, o.y + fs * 0.4f), IM_COL32(255, 255, 255, 245), big.c_str());
            }
            dl->AddText(ImVec2(o.x + fs * 0.6f, o.y + fs * 0.45f + bs.y), IM_COL32(220, 226, 236, 210), small.c_str());
        }
        // Bottom line: fog, and when the stars come out.
        {
            std::string line;
            const int fl = fogLevelOf(a, upm);
            static const char* kFogWords[] = {"No fog", "Light fog", "Medium fog", "Thick fog"};
            line = fl < 0 ? "Fog" : kFogWords[fl];
            if (c.enabled && c.starsEnabled && !c.keys.empty()) {
                // First moment after sunset the night sky is mostly in.
                for (float t = c.sunset; t < c.sunset + 8.0f; t += 1.0f / 12.0f)
                    if (ambience::sampleKeys(c, ambience::wrap24(t)).stars >= 0.5f) {
                        line += " · stars come out at " + hourText(t);
                        break;
                    }
            }
            dl->AddText(ImVec2(p.x + fs * 0.8f, p.y + picH - fs * 1.7f), IM_COL32(232, 234, 240, 190), line.c_str());
        }
        ImGui::Dummy(ImVec2(w, picH));
        // Play the day: a round button over the picture's top-right corner.
        if (c.enabled) {
            const float bsz = fs * 2.4f;
            const ImVec2 bp(p.x + w - bsz - fs * 0.8f, p.y + fs * 0.7f);
            const ImVec2 back = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(bp);
            if (ImGui::InvisibleButton(playing ? "Stop the day" : "Play the day", ImVec2(bsz, bsz)))
                ambiencePlayHour_ = playing ? -1.0f : c.time;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(playing ? "Stop" : "Watch the whole day go by in the picture (24 s).");
            const bool hov = ImGui::IsItemHovered();
            dl->AddRectFilled(bp, ImVec2(bp.x + bsz, bp.y + bsz), IM_COL32(14, 16, 21, hov ? 235 : 200), fs * 0.5f);
            const ImVec2 cc(bp.x + bsz * 0.5f, bp.y + bsz * 0.5f);
            const float g = bsz * 0.2f;
            if (playing) {
                dl->AddRectFilled(ImVec2(cc.x - g, cc.y - g), ImVec2(cc.x - g * 0.3f, cc.y + g), IM_COL32(232, 234, 240, 255));
                dl->AddRectFilled(ImVec2(cc.x + g * 0.3f, cc.y - g), ImVec2(cc.x + g, cc.y + g), IM_COL32(232, 234, 240, 255));
            } else {
                dl->AddTriangleFilled(ImVec2(cc.x - g * 0.7f, cc.y - g), ImVec2(cc.x - g * 0.7f, cc.y + g),
                                      ImVec2(cc.x + g, cc.y), IM_COL32(232, 234, 240, 255));
            }
            ImGui::SetCursorScreenPos(back);
        }
        ImGui::Spacing();

        // Time of day, or the switch that turns a fixed look into a day.
        if (c.enabled) {
            ui::Caption("Time of day");
            if (c.keys.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
                ImGui::TextWrapped("This day has no colours yet. Pick a look below, or:");
                ImGui::PopStyleColor();
                if (ui::GhostButton("Use the default day")) {
                    c.keys = ambience::defaultKeys();
                    changed = true;
                }
            }
            if (timeStrip("Time of day", &c.time, c, ImGui::GetFrameHeight() * 1.15f)) changed = true;
        } else {
            if (ui::Note(3, "No time of day here. Add one and the sky, sun and fog follow the hour you pick.",
                         "Add a day")) {
                c.enabled = true;
                if (c.keys.empty()) c.keys = ambience::defaultKeys();
                changed = true;
            }
        }
        ImGui::Spacing();

        // Looks.
        ui::Caption("Start from a look");
        {
            const int cur = lookOf(a, upm);
            const float lw = ImGui::GetContentRegionAvail().x;
            const int perRow = lw > scaled(560) ? 6 : (lw > scaled(300) ? 3 : 2);
            const float tw = ImFloor((lw - gap * (perRow - 1)) / perRow);
            const float th = ImFloor(tw * 0.55f);
            for (int i = 0; i < kLookCount; ++i) {
                if (i % perRow != 0) ImGui::SameLine(0.0f, gap);
                if (lookTile(kLooks[i], cur == i, tw, th)) {
                    applyLook(a, kLooks[i], upm);
                    ambiencePlayHour_ = -1.0f;
                    changed = true;
                }
            }
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::TextWrapped("A look replaces this preset's whole day. Undo brings the old one back.");
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    if (wide) ImGui::SameLine(0.0f, gap * 2.0f);
    else ImGui::Spacing();

    // --- right (or below): settings in plain words -----------------------------
    ImGui::BeginChild("##amb_simple_r", ImVec2(0, wide ? avail.y : 0.0f), halfFlags);
    {
        ui::InspectorRows rows(scaled(110), scaled(170));
        std::vector<DayKey>& keys = c.keys;
        const bool byKeys = c.enabled && !keys.empty();

        // Sun.
        ui::Caption("Sun");
        if (c.enabled) {
            int dir = compassIndex(c.sunAzimuth);
            if (ui::Combo("Rises in the", &dir, kCompass, 8)) {
                c.sunAzimuth = dir * 45.0f;
                changed = true;
            }
            ui::RowHelp("Where the sun comes up. It sets on the opposite side.");
            float peak = 90.0f - c.sunTilt;
            rowLabel("Height at noon", "How high the sun climbs. Low = long shadows all day.", LW());
            if (trackSlider("Height at noon", &peak, 10.0f, 88.0f, num("%.0f°", peak))) {
                c.sunTilt = 90.0f - std::round(peak);
                changed = true;
            }
            {
                // One track for both ends of the day (a hand-made row: the
                // label column matches InspectorRows' 34% clamp).
                rowLabel("Sunrise · sunset", nullptr, LW());
                if (sunRange("Sunrise and sunset", &c.sunrise, &c.sunset)) changed = true;
            }
        } else {
            int dir = compassIndex(bearingOf(a.lightDir));
            float el = ImClamp(elevationOf(a.lightDir), 5.0f, 88.0f);
            if (ui::Combo("Light comes from the", &dir, kCompass, 8)) {
                dirFrom(dir * 45.0f, el, a.lightDir);
                changed = true;
            }
            rowLabel("Sun height", "Low = long shadows, high = short ones.", LW());
            if (trackSlider("Sun height", &el, 5.0f, 88.0f, num("%.0f°", el))) {
                dirFrom(bearingOf(a.lightDir), std::round(el), a.lightDir);
                changed = true;
            }
        }

        // Mood: three sliders that rescale every key at once.
        ImGui::Spacing();
        ui::Caption("Mood");
        {
            float bright = 0.0f, warm = 0.0f, contrast = 0.0f;
            if (byKeys) {
                for (const DayKey& k : keys) {
                    bright += k.brightness;
                    warm += k.lightColor[0] - k.lightColor[2];
                    contrast += k.diffuse / ImMax(0.001f, k.ambient + k.diffuse);
                }
                bright /= keys.size(), warm /= keys.size(), contrast /= keys.size();
            } else {
                bright = a.brightness;
                warm = a.lightColor[0] - a.lightColor[2];
                contrast = a.diffuse / ImMax(0.001f, a.ambient + a.diffuse);
            }
            float pct = bright * 100.0f;
            rowLabel("Brightness", nullptr, LW());
            if (trackSlider("Brightness", &pct, 30.0f, 150.0f, num("%.0f%%", pct))) {
                const float target = pct / 100.0f;
                if (byKeys) {
                    const float f = bright > 0.01f ? target / bright : 1.0f;
                    for (DayKey& k : keys) k.brightness = ImClamp(bright > 0.01f ? k.brightness * f : target, 0.05f, 2.0f);
                } else {
                    a.brightness = target;
                }
                changed = true;
            }
            float wv = warm;
            const char* wword = wv < -0.08f ? "cooler" : (wv > 0.08f ? "warmer" : "neutral");
            rowLabel("Warmth", "The colour of the sunlight: bluish to golden.", LW());
            if (trackSlider("Warmth", &wv, -0.6f, 0.6f, wword, IM_COL32(110, 155, 255, 255), IM_COL32(255, 177, 92, 255))) {
                const float d = (wv - warm) * 0.5f;
                auto shift = [d](float* lc) {
                    lc[0] = ImClamp(lc[0] + d, 0.0f, 1.0f);
                    lc[2] = ImClamp(lc[2] - d, 0.0f, 1.0f);
                };
                if (byKeys) for (DayKey& k : keys) shift(k.lightColor);
                else shift(a.lightColor);
                changed = true;
            }
            float cv = contrast;
            const char* cword = cv < 0.38f ? "soft" : (cv > 0.6f ? "hard" : "medium");
            rowLabel("Shadows", "How much light comes straight from the sun rather than from the whole\n"
                                "sky. Low = soft and flat, high = bright sides and dark shade.", LW());
            if (trackSlider("Shadows", &cv, 0.1f, 0.9f, cword)) {
                const float v = cv;
                auto apply = [v](float& amb, float& dif) {
                    const float sum = amb + dif;
                    dif = sum * v;
                    amb = sum * (1.0f - v);
                };
                if (byKeys) for (DayKey& k : keys) apply(k.ambient, k.diffuse);
                else apply(a.ambient, a.diffuse);
                changed = true;
            }
            if (!c.enabled) {
                if (ui::ColorHex("Sky colour", a.skyTopColor)) changed = true;
                if (ui::ColorHex("Horizon colour", a.skyColor)) changed = true;
                if (ui::ColorHex("Sunlight colour", a.lightColor)) changed = true;
            }
        }

        // Fog.
        ImGui::Spacing();
        ui::Caption("Fog");
        {
            int level = fogLevelOf(a, upm);
            static const char* kFog[] = {"Off##fog", "Light##fog", "Medium##fog", "Thick##fog"};
            int shown = level < 0 ? -1 : level;
            if (ui::Segmented("##amb_fog", kFog, 4, &shown, -1.0f)) {
                setFog(a, shown, upm);
                changed = true;
            }
            const char* follows = c.enabled ? " Its colour follows the sky." : "";
            if (level < 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
                char buf[96];
                std::snprintf(buf, sizeof(buf), "Custom: from %.0f m to %.0f m (Detailed view)", a.fogStart / upm,
                              a.fogEnd / upm);
                ImGui::TextUnformatted(buf);
                ImGui::PopStyleColor();
            } else if (level != FogOff) {
                ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
                char buf[96];
                std::snprintf(buf, sizeof(buf), "Things fade out between %.0f m and %.0f m.%s", kFogMetres[level][0],
                              kFogMetres[level][1], follows);
                ImGui::TextWrapped("%s", buf);
                ImGui::PopStyleColor();
            }
            if (!c.enabled && a.fogEnabled && ui::ColorHex("Fog colour", a.fogColor)) changed = true;
        }

        if (c.enabled) {
            ImGui::Spacing();
            ui::Caption("While the game runs");
            const std::string passes = "Time passes (the game starts at " + hourText(c.time) +
                                       " and the sky keeps moving)###Time passes";
            if (ui::Switch(passes.c_str(), &c.runtime)) changed = true;
            if (c.runtime) {
                float minutes = c.dayLength / 60.0f;
                rowLabel("A whole day lasts", nullptr, LW());
                if (trackSlider("A whole day lasts", &minutes, 1.0f, 60.0f, num("%.0f min", minutes))) {
                    c.dayLength = std::round(minutes) * 60.0f;
                    changed = true;
                }
            }
            if (ui::Switch("Stars at night", &c.starsEnabled)) changed = true;
            // The baked half (shadows, baked light) stays at one hour while the
            // clock runs; say so when it will not match the start, and offer the fix.
            if (c.runtime) {
                const float bh = ambience::bakedHour(c);
                const float d = std::fabs(ambience::wrap24(bh) - ambience::wrap24(c.time));
                if (ImMin(d, 24.0f - d) >= 1.0f) {
                    const std::string msg = "Shadows and baked light stay at " + hourText(bh) +
                                            ", so they will not match a " + hourText(c.time) + " start.";
                    const std::string act = "Bake at " + hourText(c.time);
                    ImGui::Spacing();
                    if (ui::Note(1, msg.c_str(), act.c_str())) {
                        c.bakeHour = c.time;
                        changed = true;
                    }
                }
            }
        }
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::TextUnformatted(c.enabled ? "Keyframes, the moon, a painted sky and exact numbers:"
                                         : "Exact numbers and the painted sky:");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ui::Link("Detailed view")) {
            ambienceDetailed_ = true;
            saveGlobalConfig();
        }
    }
    ImGui::EndChild();
    if (!wide) ImGui::EndChild();

    if (deleteIdx >= 0) {
        deleteAmbiencePreset(deleteIdx);
        changed = true;
    }
    if (wantNew) {
        int counter = 0;
        std::string name;
        for (;;) {
            name = "sky-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& o : project_.ambiencePresets) taken |= (o.name == name);
            if (!taken) break;
        }
        AmbiencePreset np;
        np.name = name;
        applyLook(np, kLooks[0], upm);
        project_.ambiencePresets.push_back(std::move(np));
        selectedAmbience_ = (int)project_.ambiencePresets.size() - 1;
        changed = true;
    }
}
