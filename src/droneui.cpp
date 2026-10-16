// Tools > Drone Generator - the UI half of the ambient/drone music generator
// (docs/drone-generator.md). App:: methods declared in app.hpp but kept in
// their own TU, the assetbrowser.cpp precedent: a self-contained subsystem with
// its own widget vocabulary has no business growing app.cpp further.
//
// The DSP lives in dronegen.cpp and the sound card in audiopreview.cpp; this
// file only turns knobs and draws meters. The one rule it must respect: every
// parameter edit calls dronePushParams(), because the whole point of the tool is
// that you hear the knob you are turning.

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <imgui.h>
#include <imgui_internal.h>

#include "app.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "uiscript.hpp"

namespace {

constexpr float kPi = 3.14159265358979f;

// Popup names. ONE spelling each, used by OpenPopup, IsPopupOpen and
// BeginPopupModal alike, because those three must agree on the ID. Note the
// THREE hashes: ImHashStr only resets the hash on "###", so a "Title##id" name
// hashes as the whole string and does NOT match an OpenPopup("id") - which
// leaves a modal that is open but never drawn, and an open modal swallows every
// click in the window. That is exactly how a cosmetic title change broke Save
// and Render (PROGRESS 215).
constexpr const char* kPopupOverwrite = "Replace this track?###droneoverwrite";
constexpr const char* kPopupPatchAsk = "Replace this patch?###dronepatchask";
constexpr const char* kPopupNewAsk = "Unsaved patch###dronenewask";

// --- automation write hook -------------------------------------------------

// Every knob binds straight to a field of the window's `droneParams_`, so the
// pointer a knob was handed IS that parameter's address - which is all the
// timeline needs to know which lane to write. That is why arming Write
// automates all ~137 parameters without a single knob call site mentioning
// automation. Set once per frame by drawDroneGeneratorWindow.
struct AutoWriteHook {
    bool armed = false;
    const void* base = nullptr;  // &droneParams_
    size_t size = 0;             // sizeof(dronegen::Params)
    void* user = nullptr;        // the App
    void (*write)(void* user, size_t offset, float value) = nullptr;
    bool (*automated)(void* user, size_t offset) = nullptr;
};
AutoWriteHook g_autoHook;

ptrdiff_t hookOffset(const void* field) {
    if (!g_autoHook.base) return -1;
    const ptrdiff_t off = (const char*)field - (const char*)g_autoHook.base;
    return (off < 0 || (size_t)off >= g_autoHook.size) ? -1 : off;
}

void hookEdit(const void* field, float value) {
    if (!g_autoHook.armed || !g_autoHook.write) return;
    const ptrdiff_t off = hookOffset(field);
    if (off < 0) return;  // not a patch field
    g_autoHook.write(g_autoHook.user, (size_t)off, value);
}

// Does this field have a timeline lane? Knobs mark themselves so it is obvious
// why one springs back when Write is off.
bool hookAutomated(const void* field) {
    if (!g_autoHook.automated) return false;
    const ptrdiff_t off = hookOffset(field);
    return off >= 0 && g_autoHook.automated(g_autoHook.user, (size_t)off);
}

// --- the rotary ------------------------------------------------------------

// A drag has to remember where it is BETWEEN frames, not just where the value
// landed. A stepped dial rounds its value to a whole step every frame, so a
// drag that only accumulates within one frame has to clear half a step in a
// single mouse move or it rounds straight back to where it started - which is
// why Octave (7 steps) needed a 17-pixel jerk to move at all and felt dead,
// while Root (61 steps) always worked. One entry is enough: ImGui only ever
// has one active item.
struct KnobDrag {
    ImGuiID id = 0;
    float raw = 0.0f;  // un-stepped, un-rounded position of the drag
};
KnobDrag g_knobDrag;

// A VST-style knob. Vertical drag turns it (Shift = fine, Ctrl = coarse),
// double-click returns it to `def`, and the value is drawn under the dial.
//
// `curve` > 1 spends more travel on the low end of the range, which is what a
// frequency or time knob needs: linear pixels-per-Hz makes everything under
// 1 kHz a single pixel of a 14 kHz sweep. A symmetric range (-x..+x) is drawn
// bipolar, filling from the centre - so pan and mod amounts read at a glance.
// `step` > 0 quantizes the dial (knobInt passes 1), which is what makes the
// drawn pointer and the readout agree with the whole number the caller stores.
// `hook` is false only when knobInt calls this with a scratch float - it writes
// the keyframe itself, from the int it actually owns.
bool knob(const char* label, float* v, float lo, float hi, float def,
          const char* fmt, float scale, float curve = 1.0f,
          const char* tip = nullptr, bool hook = true, bool autoMark = false,
          float step = 0.0f) {
    const float cell = 62.0f * scale;
    const float dia = 42.0f * scale;
    const bool bipolar = lo < 0.0f && hi > 0.0f && std::fabs(lo + hi) < 1e-4f;

    ImGui::PushID(label);
    ImGui::BeginGroup();

    // Display name = the label up to "##", the ImGui convention: the suffix
    // disambiguates the ID and is not drawn. That is what lets a panel hold a
    // delay "Mix" and a reverb "Mix" - two visible items sharing an ID is an
    // ImGui error, not a cosmetic problem (it breaks hover and drag state).
    char cap[40];
    {
        const char* end = std::strstr(label, "##");
        size_t n = end ? (size_t)(end - label) : std::strlen(label);
        if (n > sizeof(cap) - 1) n = sizeof(cap) - 1;
        std::memcpy(cap, label, n);
        cap[n] = '\0';
    }
    // Centred caption (labels are short by design; a long one just left-aligns)
    {
        const float tw = ImGui::CalcTextSize(cap).x;
        if (tw < cell) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cell - tw) * 0.5f);
        ImGui::TextUnformatted(cap);
    }

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImGuiID id = ImGui::GetID("dial");
    ImGui::InvisibleButton("dial", ImVec2(cell, dia));
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    const bool automated = hook ? hookAutomated(v) : autoMark;
    bool changed = false;

    const float span = hi - lo;
    auto toNorm = [&](float val) {
        const float t = span != 0.0f ? (val - lo) / span : 0.0f;
        return std::pow(std::max(0.0f, std::min(1.0f, t)), 1.0f / curve);
    };
    auto fromNorm = [&](float t) {
        t = std::max(0.0f, std::min(1.0f, t));
        return lo + span * std::pow(t, curve);
    };
    auto settle = [&](float val) {
        if (step > 0.0f) val = lo + step * std::round((val - lo) / step);
        return std::max(std::min(lo, hi), std::min(std::max(lo, hi), val));
    };

    if (ImGui::IsItemActivated()) {
        g_knobDrag.id = id;
        g_knobDrag.raw = *v;
    }
    if (active && g_knobDrag.id == id) {
        const ImGuiIO& io = ImGui::GetIO();
        const float d = -io.MouseDelta.y - io.MouseDelta.x * 0.15f;
        if (d != 0.0f) {
            float sens = 1.0f / (200.0f * scale);
            if (io.KeyShift) sens *= 0.2f;
            if (io.KeyCtrl) sens *= 3.0f;
            g_knobDrag.raw = fromNorm(toNorm(g_knobDrag.raw) + d * sens);
            // Only report an edit when the stored value really moved: a stepped
            // dial spends most of its travel inside one step, and a knob that
            // claimed a change every frame would re-generate the piece for
            // nothing.
            const float nv = settle(g_knobDrag.raw);
            if (nv != *v) {
                *v = nv;
                changed = true;
            }
        }
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        g_knobDrag.raw = def;
        if (*v != def) {
            *v = def;
            changed = true;
        }
    }

    const float t = toNorm(*v);
    const ImVec2 c(p.x + cell * 0.5f, p.y + dia * 0.5f);
    const float r = dia * 0.5f - 2.0f * scale;
    const float a0 = kPi * 0.75f, sweep = kPi * 1.5f;
    const float ang = a0 + t * sweep;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 body = IM_COL32(38, 40, 46, 255);
    const ImU32 rim = hovered || active ? IM_COL32(150, 155, 165, 255)
                                        : IM_COL32(85, 88, 96, 255);
    const ImU32 track = IM_COL32(58, 60, 68, 255);
    const ImU32 fill = active ? IM_COL32(255, 205, 100, 255)
                              : IM_COL32(232, 168, 62, 255);

    dl->AddCircleFilled(c, r, body, 32);
    dl->PathArcTo(c, r - 1.0f * scale, a0, a0 + sweep, 32);
    dl->PathStroke(track, 0, 3.0f * scale);
    const float from = bipolar ? a0 + 0.5f * sweep : a0;
    if (std::fabs(ang - from) > 0.01f) {
        dl->PathArcTo(c, r - 1.0f * scale, std::min(from, ang), std::max(from, ang), 32);
        dl->PathStroke(fill, 0, 3.0f * scale);
    }
    const ImVec2 tip0(c.x + std::cos(ang) * r * 0.28f, c.y + std::sin(ang) * r * 0.28f);
    const ImVec2 tip1(c.x + std::cos(ang) * r * 0.78f, c.y + std::sin(ang) * r * 0.78f);
    dl->AddLine(tip0, tip1, IM_COL32(235, 238, 245, 255), 2.0f * scale);
    dl->AddCircle(c, r, rim, 32, 1.5f * scale);
    if (automated) {
        // Same amber as the lanes and the keyframe markers: this dial is driven
        // by the timeline, and turning it only sticks while Write is armed.
        dl->AddCircleFilled(ImVec2(c.x + r * 0.78f, c.y - r * 0.78f), 3.0f * scale,
                            IM_COL32(245, 190, 70, 255));
    }

    // Value readout
    {
        char buf[40];
        std::snprintf(buf, sizeof(buf), fmt, *v);
        const float tw = ImGui::CalcTextSize(buf).x;
        if (tw < cell) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cell - tw) * 0.5f);
        ImGui::TextDisabled("%s", buf);
    }
    ImGui::EndGroup();
    if (hovered && !active) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), fmt, *v);
        ImGui::SetTooltip("%s: %s\n%s\ndrag to turn, Shift = fine, double-click resets%s",
                          cap, buf, tip ? tip : "",
                          automated ? "\nAUTOMATED - the timeline drives it; arm Write "
                                      "to record a keyframe here"
                                    : "");
    }
    ImGui::PopID();
    if (changed && hook) hookEdit(v, *v);
    return changed;
}

// Integer knob: the same dial, rounded to whole steps. The range stays exactly
// lo..hi (no fudge for the top value) so a symmetric one - Octave, Semi, the
// shimmer interval - still reads as bipolar. Each end value covers half a step
// of travel, which is how every hardware stepped encoder behaves anyway.
bool knobInt(const char* label, int* v, int lo, int hi, int def, const char* fmt,
             float scale, const char* tip = nullptr) {
    float f = (float)*v;
    // The dial is told "no hook" (its float is a scratch copy) but the marker
    // still has to appear, so the automation flag is queried on the int itself.
    // The step of 1 is what makes the drag survive between frames - see
    // KnobDrag; without it a short-range dial (Octave, Semi, Unison) is stuck.
    knob(label, &f, (float)lo, (float)hi, (float)def, fmt, scale, 1.0f, tip, false,
         hookAutomated(v), 1.0f);
    const int nv = std::max(lo, std::min(hi, (int)std::lround(f)));
    if (nv == *v) return false;
    *v = nv;
    hookEdit(v, (float)nv);
    return true;
}

// Section caption inside a tab.
void groupTitle(const char* text) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.62f, 0.72f, 0.9f, 1.0f), "%s", text);
    ImGui::Separator();
}

// --- the arc envelope editor ----------------------------------------------

// Five draggable breakpoints defining the piece-long intensity curve. Dragging
// grabs the nearest point in x and sets its value from y - the whole widget is
// 40 lines because it has exactly one job.
bool arcEditor(const char* id, float* pts, int n, ImVec2 size, float scale) {
    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("arc", size);
    bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(26, 28, 33, 255),
                      3.0f * scale);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(70, 74, 82, 255),
                3.0f * scale);
    for (int g = 1; g < 4; ++g) {
        const float y = p.y + size.y * (float)g / 4.0f;
        dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + size.x, y), IM_COL32(48, 50, 58, 255));
    }
    if (ImGui::IsItemActive()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float tx = std::max(0.0f, std::min(1.0f, (m.x - p.x) / size.x));
        const int idx = std::max(0, std::min(n - 1, (int)std::lround(tx * (float)(n - 1))));
        pts[idx] = std::max(0.0f, std::min(1.0f, 1.0f - (m.y - p.y) / size.y));
        changed = true;
    }
    auto at = [&](int i) {
        return ImVec2(p.x + size.x * (float)i / (float)(n - 1),
                      p.y + size.y * (1.0f - pts[i]));
    };
    for (int i = 0; i + 1 < n; ++i) {
        const ImVec2 a = at(i), b = at(i + 1);
        dl->AddLine(a, b, IM_COL32(232, 168, 62, 255), 2.0f * scale);
        dl->AddQuadFilled(a, b, ImVec2(b.x, p.y + size.y), ImVec2(a.x, p.y + size.y),
                          IM_COL32(232, 168, 62, 34));
    }
    for (int i = 0; i < n; ++i)
        dl->AddCircleFilled(at(i), 4.0f * scale, IM_COL32(255, 225, 160, 255));
    ImGui::PopID();
    return changed;
}

// --- displays -------------------------------------------------------------

// Peak meter with a -12 dB tick, drawn vertically.
void meterBar(ImDrawList* dl, ImVec2 p, ImVec2 size, float peak, float scale) {
    const theme::Semantics& sem = theme::semantics();
    const float r = size.x * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(sem.field), r);
    const float t = std::max(0.0f, std::min(1.0f, peak));
    const float h = size.y * t;
    const ImU32 col = ImGui::GetColorU32(t > 0.99f ? sem.danger : t > 0.8f ? sem.warn : sem.ok);
    if (h > 0.5f)
        dl->AddRectFilled(ImVec2(p.x, p.y + size.y - h), ImVec2(p.x + size.x, p.y + size.y), col, r);
    const float y = p.y + size.y * (1.0f - 0.25f);  // ~-12 dBFS
    dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + size.x, y), ImGui::GetColorU32(sem.border), 1.0f * scale);
}

// The overview of a WAV already on disk (16-bit PCM, what Render writes), so
// a track opened from the rail shows its waveform without re-rendering.
bool waveOverviewFromWav(const std::filesystem::path& abs, std::vector<float>& mn, std::vector<float>& mx) {
    std::ifstream f(abs, std::ios::binary);
    if (!f) return false;
    std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u32 = [&](size_t o) {
        return (uint32_t)(uint8_t)d[o] | (uint32_t)(uint8_t)d[o + 1] << 8 | (uint32_t)(uint8_t)d[o + 2] << 16 |
               (uint32_t)(uint8_t)d[o + 3] << 24;
    };
    auto u16 = [&](size_t o) { return (uint16_t)((uint8_t)d[o] | (uint8_t)d[o + 1] << 8); };
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
        return false;
    int channels = 0, bits = 0, format = 0;
    size_t dataOff = 0, dataLen = 0;
    for (size_t o = 12; o + 8 <= d.size();) {
        const uint32_t len = u32(o + 4);
        if (std::memcmp(d.data() + o, "fmt ", 4) == 0 && o + 24 <= d.size()) {
            format = u16(o + 8);
            channels = u16(o + 10);
            bits = u16(o + 22);
        } else if (std::memcmp(d.data() + o, "data", 4) == 0) {
            dataOff = o + 8;
            dataLen = std::min<size_t>(len, d.size() - dataOff);
            break;
        }
        o += 8 + len + (len & 1);
    }
    if (format != 1 || bits != 16 || channels < 1 || !dataOff) return false;
    const size_t frames = dataLen / (2 * (size_t)channels);
    if (frames == 0) return false;
    const int cols = 900;
    mn.assign(cols, 0.0f);
    mx.assign(cols, 0.0f);
    for (int c = 0; c < cols; ++c) {
        const size_t a = frames * (size_t)c / cols, b = std::max(a + 1, frames * (size_t)(c + 1) / cols);
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = a; i < b && i < frames; ++i)
            for (int ch = 0; ch < channels; ++ch) {
                const float v = (int16_t)u16(dataOff + (i * channels + ch) * 2) / 32768.0f;
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        mn[c] = lo;
        mx[c] = hi;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Audition / live synth
// ---------------------------------------------------------------------------

void App::droneAudition(bool on) {
    if (!on) {
        if (droneDevice_) droneDevice_->stop();
        droneAuditioning_ = false;
        return;
    }
    if (!droneLive_ || droneLiveRate_ != droneParams_.sampleRate) {
        // The sample rate is structural in the synth, so switching it rebuilds
        // both sides (tracked here rather than read back off the device, which
        // reports 0 once stopped). Stop the device FIRST: its callback holds the
        // old LiveSynth, and ma_device_uninit joins the audio thread.
        if (droneDevice_) droneDevice_->stop();
        droneLive_.reset(new dronegen::LiveSynth(droneParams_));
        droneLiveRate_ = droneParams_.sampleRate;
    }
    if (!droneDevice_) droneDevice_.reset(new audiopreview::Device());
    droneLive_->push(droneParams_);
    dronegen::LiveSynth* live = droneLive_.get();
    droneAuditioning_ =
        droneDevice_->start(droneParams_.sampleRate,
                            [live](float* out, int frames) { live->render(out, frames); });
    droneAudioError_ = droneAuditioning_ ? std::string() : droneDevice_->error();
}

void App::dronePlay(bool record) {
    // Play starts AT THE PLAYHEAD in both modes - "let me hear how it ends" is a
    // transport need, not a mode. What the mode decides is what happens at the
    // end (Record stops, Generate plays on) and whether knobs are recorded.
    if (droneHeadSec_ >= (double)droneParams_.lengthSec - 0.05) droneHeadSec_ = 0.0;
    droneAudition(true);
    if (!droneAuditioning_) return;  // no sound card - nothing to drive
    if (droneLive_) droneLive_->seek(droneHeadSec_);  // settles: see setTime
    droneRecording_ = record && droneMode_ == 1;
    if (droneRecording_) droneWriteArmed_ = true;
}

void App::droneStop() {
    // Park the playhead where playback got to, so Play resumes instead of
    // restarting (and so the lane editors keep showing that moment).
    if (droneAuditioning_ && droneLive_)
        droneHeadSec_ = std::min((double)droneParams_.lengthSec, droneLive_->timeSec());
    droneAudition(false);
    droneRecording_ = false;
}

void App::dronePushParams() {
    if (droneLive_) droneLive_->push(droneParams_);
}

// ---------------------------------------------------------------------------
// Offline render (worker thread) -> res/audio/<name>.wav + <name>.drone
// ---------------------------------------------------------------------------

void App::droneStartRender(bool confirmedOverwrite) {
    if (droneRendering_ || !hasProject_) return;
    if (droneRenderThread_.joinable()) droneRenderThread_.join();

    std::string name = droneTrackName_;
    // Keep it a safe, PS2-friendly file name: the ISO9660 writer and the
    // generated code both refer to this path as a literal.
    std::string clean;
    for (char c : name) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_')
            clean += (char)tolower((unsigned char)c);
        else if (c == ' ')
            clean += '-';
    }
    if (clean.empty()) clean = "drone";
    droneRenderTarget_ = "res/audio/" + clean + ".wav";

    // Never clobber a track silently: a render writes the WAV *and* the .drone
    // next to it, so an accidental name collision would take someone's patch
    // with it. Ask first, unless this call is the answer to that question.
    if (!confirmedOverwrite) {
        std::error_code eec;
        if (std::filesystem::exists(project_.filePath(droneRenderTarget_), eec)) {
            droneAskWav_ = droneRenderTarget_;
            return;
        }
    }
    // Resolved now, not when the render finishes: opening another project
    // mid-render must not redirect the file into it.
    droneRenderAbs_ = project_.filePath(droneRenderTarget_);

    droneRenderProgress_.store(0.0f);
    droneRenderCancel_.store(false);
    droneRenderDone_.store(false);
    droneRendering_ = true;
    droneRenderedWith_ = droneParams_;
    droneStatus_ = "Rendering " + clean + ".wav...";

    const dronegen::Params p = droneParams_;
    droneRenderThread_ = std::thread([this, p]() {
        dronegen::RenderResult r = dronegen::render(p, [this](float frac) {
            droneRenderProgress_.store(frac);
            return !droneRenderCancel_.load();
        });
        droneRenderResult_ = std::move(r);
        droneRenderDone_.store(true);
    });
}

void App::droneTickRender() {
    if (!droneRendering_ || !droneRenderDone_.load()) return;
    if (droneRenderThread_.joinable()) droneRenderThread_.join();
    droneRendering_ = false;

    if (droneRenderResult_.cancelled) {
        droneStatus_ = "Render cancelled.";
        droneRenderResult_ = dronegen::RenderResult();
        return;
    }

    const std::filesystem::path abs(droneRenderAbs_);
    std::error_code ec;
    std::filesystem::create_directories(abs.parent_path(), ec);
    std::string err;
    if (!dronegen::writeWav(abs.string(), droneRenderResult_, droneRenderedWith_.seed,
                            droneRenderedWith_.master.dither, err)) {
        droneStatus_ = "Could not write the WAV: " + err;
        return;
    }
    // The patch travels with the track: a .drone sidecar next to the WAV is what
    // makes a shipped piece re-editable months later (and the Asset Browser
    // carries it along on rename/move/delete - see App::assetSidecars).
    {
        std::filesystem::path patch = abs;
        patch.replace_extension(".drone");
        std::ofstream f(patch, std::ios::binary);
        if (f) f << dronegen::toText(droneRenderedWith_, dronePatchTitle_);
    }

    // The rendered pair IS the saved document from here on.
    {
        std::filesystem::path rel(droneRenderTarget_);
        rel.replace_extension(".drone");
        dronePatchRel_ = rel.generic_string();
        droneDirty_ = false;
    }

    bool known = false;
    for (const std::string& m : project_.music) known |= (m == droneRenderTarget_);
    if (!known) project_.music.push_back(droneRenderTarget_);
    wavIssueCache_.clear();
    droneTrackScanAt_ = -1.0;
    droneBuildWaveOverview();
    droneHeadSec_ = 0.0;

    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "%s: %.0f s, %s, peak %.2f%s - play it with a Play Music node",
                  droneRenderTarget_.c_str(),
                  (double)droneRenderResult_.frames / droneRenderResult_.sampleRate,
                  droneRenderResult_.channels == 2 ? "stereo" : "mono",
                  (double)droneRenderResult_.peak, known ? " (replaced)" : "");
    droneStatus_ = msg;
    saveAll(msg);
}

bool App::droneSavePatch(const std::string& rel, bool confirmed) {
    if (!hasProject_) return false;
    // Saving over the file you are editing is what "save" means; saving over
    // SOMEONE ELSE'S patch is a question.
    if (!confirmed && rel != dronePatchRel_) {
        std::error_code ec;
        if (std::filesystem::exists(project_.filePath(rel), ec)) {
            droneAskPatch_ = rel;
            return false;
        }
    }
    const std::filesystem::path abs = project_.filePath(rel);
    std::error_code ec;
    std::filesystem::create_directories(abs.parent_path(), ec);
    std::ofstream f(abs, std::ios::binary);
    if (!f) {
        droneStatus_ = "Could not write " + rel;
        return false;
    }
    f << dronegen::toText(droneParams_, dronePatchTitle_);
    dronePatchRel_ = rel;
    droneDirty_ = false;
    droneStatus_ = "Saved " + rel;
    droneTrackScanAt_ = -1.0;  // the rail and the render state re-read the disk
    assetsChanged();  // it is a project asset now, so the browser must see it
    return true;
}

void App::droneNewPatch() {
    droneParams_ = dronegen::Params();
    dronePreset_ = 0;
    dronePatchTitle_.clear();
    dronePatchRel_.clear();
    droneDirty_ = false;
    droneHeadSec_ = 0.0;
    droneWaveMin_.clear();
    droneWaveMax_.clear();
    droneRenderResult_ = dronegen::RenderResult();
    // Never default to the name of a track the project already has: a Render
    // would then aim at someone's WAV and .drone.
    const std::string name = droneUniqueTrackName("ambient");
    std::snprintf(droneTrackName_, sizeof(droneTrackName_), "%s", name.c_str());
    droneStatus_ = "New patch.";
    dronePushParams();
    if (droneLive_) droneLive_->reset();
}

std::string App::droneUniqueTrackName(const std::string& base) const {
    if (!hasProject_) return base;
    for (int k = 1;; ++k) {
        const std::string n = k == 1 ? base : base + "-" + std::to_string(k);
        std::error_code ec;
        if (!std::filesystem::exists(project_.filePath("res/audio/" + n + ".wav"), ec) &&
            !std::filesystem::exists(project_.filePath("res/audio/" + n + ".drone"), ec))
            return n;
    }
}

void App::droneScanTracks() {
    droneTrackScanAt_ = ImGui::GetTime();
    droneTracks_.clear();
    for (const std::string& rel : dronePatchList()) {
        DroneTrackRow row;
        row.rel = rel;
        const std::filesystem::path abs = project_.filePath(rel);
        {
            std::ifstream f(abs, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            dronegen::Params tmp;
            std::string err;
            if (!dronegen::fromText(ss.str(), tmp, row.title, err)) row.title.clear();
        }
        std::filesystem::path wav = abs;
        wav.replace_extension(".wav");
        std::error_code ec;
        row.hasWav = std::filesystem::exists(wav, ec);
        if (row.hasWav) {
            // A render writes both files together; a Save afterwards leaves
            // the patch newer than its track (2 s of slack for the pair).
            const auto tp = std::filesystem::last_write_time(abs, ec);
            const auto tw = std::filesystem::last_write_time(wav, ec);
            if (!ec) row.newerThanWav = tp - tw > std::chrono::seconds(2);
        }
        droneTracks_.push_back(std::move(row));
    }
}

std::vector<std::string> App::dronePatchList() const {
    std::vector<std::string> out;
    if (!hasProject_) return out;
    std::error_code ec;
    const std::filesystem::path root = project_.filePath("res");
    if (!std::filesystem::exists(root, ec)) return out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = (char)tolower((unsigned char)c);
        if (ext != ".drone") continue;
        out.push_back("res/" + std::filesystem::relative(e.path(), root, ec)
                                   .generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool App::droneLoadPatch(const std::string& relOrAbs) {
    std::filesystem::path abs(relOrAbs);
    if (abs.is_relative() && hasProject_) abs = project_.filePath(relOrAbs);
    std::ifstream f(abs, std::ios::binary);
    if (!f) {
        droneStatus_ = "Cannot read " + abs.string();
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    dronegen::Params p;
    std::string title, err;
    if (!dronegen::fromText(ss.str(), p, title, err)) {
        droneStatus_ = "Not a usable patch: " + err;
        return false;
    }
    droneParams_ = p;
    dronePatchTitle_ = title;
    const std::string stem = abs.stem().string();
    std::snprintf(droneTrackName_, sizeof(droneTrackName_), "%s", stem.c_str());
    droneStatus_ = "Loaded patch " + abs.filename().string();
    // A patch opened from inside the project becomes the open document; one
    // browsed from elsewhere is treated as an import (Save gives it a home).
    {
        std::error_code rec;
        const std::filesystem::path root(project_.dir);
        const std::filesystem::path rel = std::filesystem::relative(abs, root, rec);
        const std::string relStr = rel.generic_string();
        dronePatchRel_ = (!rec && !relStr.empty() && relStr.rfind("..", 0) != 0)
                             ? relStr
                             : std::string();
    }
    droneDirty_ = false;
    droneHeadSec_ = 0.0;
    droneWaveMin_.clear();
    droneWaveMax_.clear();
    {
        std::filesystem::path wav = abs;
        wav.replace_extension(".wav");
        std::error_code wec;
        if (std::filesystem::exists(wav, wec)) waveOverviewFromWav(wav, droneWaveMin_, droneWaveMax_);
    }
    if (droneLive_) {
        droneLive_->push(droneParams_);
        droneLive_->reset();
    }
    showDroneGenerator_ = true;
    return true;
}

void App::droneBuildWaveOverview() {
    droneWaveMin_.clear();
    droneWaveMax_.clear();
    const dronegen::RenderResult& r = droneRenderResult_;
    if (r.frames <= 0 || r.samples.empty()) return;
    const int cols = 900;  // more than any sane window width; the strip samples it
    droneWaveMin_.assign(cols, 0.0f);
    droneWaveMax_.assign(cols, 0.0f);
    for (int c = 0; c < cols; ++c) {
        const int a = (int)((int64_t)r.frames * c / cols);
        const int b = std::max(a + 1, (int)((int64_t)r.frames * (c + 1) / cols));
        float lo = 0.0f, hi = 0.0f;
        for (int i = a; i < b && i < r.frames; ++i) {
            for (int ch = 0; ch < r.channels; ++ch) {
                const float v = r.samples[(size_t)i * r.channels + ch];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        }
        droneWaveMin_[c] = lo;
        droneWaveMax_[c] = hi;
    }
}

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

double App::droneHeadTime() const {
    // While auditioning the transport owns the playhead; stopped, the user does.
    if (droneAuditioning_ && droneLive_)
        return std::min((double)droneParams_.lengthSec, droneLive_->timeSec());
    return droneHeadSec_;
}

void App::droneSeek(double sec, bool settle) {
    droneHeadSec_ = std::max(0.0, std::min((double)droneParams_.lengthSec, sec));
    if (droneLive_) droneLive_->seek(droneHeadSec_, settle);
}

bool App::droneIsAutomated(size_t offset) const {
    const int idx = dronegen::paramIndexForOffset(offset);
    return idx >= 0 && dronegen::autoLaneFind(droneParams_, idx) != nullptr;
}

void App::droneWriteAuto(size_t offset, float value) {
    const int idx = dronegen::paramIndexForOffset(offset);
    if (idx < 0) return;  // not an automatable field (format/mastering/enum)
    const float t = (float)droneHeadTime();
    if (!dronegen::autoWrite(droneParams_, idx, value, t)) {
        droneStatus_ = "All " + std::to_string(dronegen::kMaxAutoLanes) +
                       " automation lanes are in use - delete one in Timeline.";
        return;
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s @ %d:%04.1f",
                  dronegen::paramTable()[(size_t)idx].label, (int)(t / 60.0f),
                  std::fmod(t, 60.0f));
    droneWriteMsg_ = buf;
    dronePushParams();
}

namespace {
ImU32 colA(ImVec4 c, float a) {
    c.w *= a;
    return ImGui::GetColorU32(c);
}

// One colour per chord of the progression, the same on the timeline strip and
// the Harmony cards. Meanings from the theme, cycled.
ImU32 chordColour(int i, float alpha = 1.0f) {
    const theme::Semantics& s = theme::semantics();
    const ImVec4 pal[] = {s.accent, s.ok, s.special, s.warn, s.axisZ, s.danger, s.axisY, s.textDim};
    ImVec4 c = pal[((i % 8) + 8) % 8];
    c.w *= alpha;
    return ImGui::GetColorU32(c);
}
}  // namespace

// The ONE timeline: the position bar and the rendered waveform in the same
// strip - bar numbers, the playhead, a triangle per keyframe - with the chord
// sequence as coloured segments under it and the output meters at its end.
// Clicking or dragging it seeks the live audition too, so the timeline drives
// playback rather than just reporting it.
void App::drawDroneTimelineBar() {
    const theme::Semantics& sem = theme::semantics();
    const float s = uiScaleApplied_;
    const float len = std::max(1.0f, droneParams_.lengthSec);
    const double head = droneHeadTime();
    const float fs = ImGui::GetFontSize();

    const float meterW = scaled(6.0f);
    const float numH = fs * 0.85f, waveH = scaled(54.0f), stripH = scaled(5.0f);
    const float h = numH + waveH + scaled(6.0f) + stripH;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(scaled(120.0f), ImGui::GetContentRegionAvail().x - meterW * 2.0f - scaled(12.0f));
    ImGui::InvisibleButton("dronepos", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActive())
        droneSeek((double)((ImGui::GetIO().MousePos.x - p.x) / w) * (double)len, false);
    if (ImGui::IsItemDeactivated()) droneSeek(droneHeadSec_, true);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float wy0 = p.y + numH, wy1 = wy0 + waveH;
    const float mid = (wy0 + wy1) * 0.5f;
    const float frac = std::max(0.0f, std::min(1.0f, (float)head / len));
    const float px = p.x + w * frac;

    // Bars: a hairline per bar while readable, a number every fourth.
    const float barSec = dronegen::barSeconds(droneParams_);
    const bool tickBars = w / (len / barSec) > scaled(6.0f);
    const float step = tickBars ? barSec : (len > 240.0f ? 60.0f : len > 90.0f ? 30.0f : 10.0f);
    int idx = 0;
    for (float t = 0.0f; t < len - 1e-3f; t += step, ++idx) {
        const float x = p.x + w * (t / len);
        const bool major = tickBars ? (idx % 4 == 0) : (idx % 3 == 0);
        if (major) {
            dl->AddLine(ImVec2(x, p.y), ImVec2(x, wy1), ImGui::GetColorU32(sem.border), 1.0f);
            char lab[16];
            if (tickBars) std::snprintf(lab, sizeof lab, "%d", idx + 1);
            else std::snprintf(lab, sizeof lab, "%d:%02d", (int)(t / 60.0f), (int)std::fmod(t, 60.0f));
            dl->AddText(ImGui::GetFont(), fs * 0.75f, ImVec2(x + 3.0f, p.y), ImGui::GetColorU32(sem.textDim), lab);
        }
    }

    // The finished track's waveform, played part in the accent.
    if (!droneWaveMin_.empty()) {
        const int cols = (int)droneWaveMin_.size();
        const float colW = std::max(2.0f, 3.0f * s);
        for (float x = 0.0f; x < w; x += colW) {
            const int c = std::min(cols - 1, (int)(x / w * (float)cols));
            const float top = mid - droneWaveMax_[c] * waveH * 0.48f;
            const float bot = mid - droneWaveMin_[c] * waveH * 0.48f;
            const bool played = p.x + x < px;
            dl->AddRectFilled(ImVec2(p.x + x, top), ImVec2(p.x + x + colW * 0.6f, std::max(bot, top + 1.0f)),
                              colA(played ? sem.accent : sem.textDim, played ? 1.0f : 0.55f));
        }
    } else {
        dl->AddLine(ImVec2(p.x, mid), ImVec2(p.x + w, mid), ImGui::GetColorU32(sem.border));
        const char* msg = "Render to see the finished track here";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, mid - ts.y - 3.0f), colA(sem.textDim, 0.8f), msg);
    }

    // Keyframes of every lane, so the bar shows where the piece changes.
    const int lanes = std::min(droneParams_.autoLaneCount, dronegen::kMaxAutoLanes);
    for (int i = 0; i < lanes; ++i) {
        const dronegen::AutoLane& lane = droneParams_.autoLanes[i];
        for (int k = 0; k < lane.count; ++k) {
            const float x = p.x + w * std::min(1.0f, lane.pts[k].t / len);
            dl->AddTriangleFilled(ImVec2(x, wy0), ImVec2(x - 3.0f * s, wy0 + 5.0f * s),
                                  ImVec2(x + 3.0f * s, wy0 + 5.0f * s), colA(sem.warn, 0.85f));
        }
    }

    // The chord sequence over the piece, pass after pass.
    {
        const float sy = wy1 + scaled(6.0f);
        float t = 0.0f;
        int guard = 0;
        const int n = std::max(1, droneParams_.stepCount);
        while (t < len && guard++ < 4096) {
            for (int i = 0; i < n && t < len; ++i) {
                const float d = std::max(0.05f, droneParams_.steps[i].bars) * barSec;
                const float x0 = p.x + w * (t / len), x1 = p.x + w * std::min(1.0f, (t + d) / len);
                dl->AddRectFilled(ImVec2(x0 + 1.0f, sy), ImVec2(std::max(x0 + 2.0f, x1 - 1.0f), sy + stripH),
                                  chordColour(i, 0.85f), stripH * 0.5f);
                t += d;
            }
        }
    }

    dl->AddLine(ImVec2(px, p.y), ImVec2(px, wy1), ImGui::GetColorU32(sem.ok), 2.0f * s);
    if (hovered) ImGui::SetTooltip("Click or drag to move the playhead (seeks the audition)");

    // meters
    const float pl = droneLive_ && droneAuditioning_ ? droneLive_->peakL() : 0.0f;
    const float pr = droneLive_ && droneAuditioning_ ? droneLive_->peakR() : 0.0f;
    meterBar(dl, ImVec2(p.x + w + scaled(8.0f), wy0), ImVec2(meterW, waveH), pl, s);
    meterBar(dl, ImVec2(p.x + w + scaled(10.0f) + meterW, wy0), ImVec2(meterW, waveH), pr, s);
}

// The Timeline tab: the lane list. A lane is created by arming Write and turning
// a knob (the fast path the tool is built around) or explicitly from the picker.
void App::drawDroneTimelineTab() {
    const float s = uiScaleApplied_;
    dronegen::Params& p = droneParams_;
    const std::vector<dronegen::ParamDesc>& table = dronegen::paramTable();
    const float len = std::max(1.0f, p.lengthSec);
    const double head = droneHeadTime();

    if (ImGui::Checkbox("Write keyframes", &droneWriteArmed_)) droneWriteMsg_.clear();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Armed, turning ANY knob drops a keyframe at the playhead - which is\n"
            "how a lane is normally born. Audition while you write and the piece\n"
            "records itself; a knob dragged for a few seconds merges into a couple\n"
            "of keyframes rather than a smear.");
    ImGui::SameLine();
    if (droneWriteArmed_)
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.3f, 1.0f), "WRITE");
    else
        ImGui::TextDisabled("read only");
    if (!droneWriteMsg_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("last: %s", droneWriteMsg_.c_str());
    }

    ImGui::SameLine(0.0f, scaled(20.0f));
    if (ImGui::Button("Add lane...")) ImGui::OpenPopup("addlane");
    if (ImGui::BeginPopup("addlane")) {
        ImGui::SetNextItemWidth(scaled(220.0f));
        ImGui::InputTextWithHint("##lanefilter", "filter...", droneLaneFilter_,
                                 sizeof(droneLaneFilter_));
        std::string needle = droneLaneFilter_;
        for (char& c : needle) c = (char)tolower((unsigned char)c);
        ImGui::BeginChild("lanelist", ImVec2(scaled(260.0f), scaled(260.0f)), true);
        for (size_t i = 0; i < table.size(); ++i) {
            std::string hay = table[i].label;
            for (char& c : hay) c = (char)tolower((unsigned char)c);
            if (!needle.empty() && hay.find(needle) == std::string::npos) continue;
            if (dronegen::autoLaneFind(p, (int)i)) continue;  // already has a lane
            if (ImGui::Selectable(table[i].label)) {
                // Seed the lane with the parameter's current value, so adding it
                // never changes the sound until a point is moved.
                dronegen::autoWrite(p, (int)i, dronegen::paramGet(p, (int)i),
                                    (float)head);
                dronePushParams();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d / %d lanes", std::min(p.autoLaneCount, dronegen::kMaxAutoLanes),
                        dronegen::kMaxAutoLanes);

    ImGui::Separator();
    if (p.autoLaneCount <= 0) {
        ImGui::TextDisabled(
            "No automation yet.\n\n"
            "Arm Write, hit Audition, and turn a knob: a lane appears with a\n"
            "keyframe at the playhead. Drag points to reshape them, double-click\n"
            "an empty spot to add one, right-click a point to remove it.\n"
            "Automation sets the parameter's value over time; the LFOs and the arc\n"
            "still modulate on top of it.");
        return;
    }

    int removeLane = -1;
    const int lanes = std::min(p.autoLaneCount, dronegen::kMaxAutoLanes);
    for (int i = 0; i < lanes; ++i) {
        dronegen::AutoLane& lane = p.autoLanes[i];
        if (lane.param < 0 || lane.param >= (int)table.size()) continue;
        ImGui::PushID(i);

        // Value range of the lane, padded so the extremes are not on the border.
        float lo = lane.pts[0].value, hi = lane.pts[0].value;
        for (int k = 1; k < lane.count; ++k) {
            lo = std::min(lo, lane.pts[k].value);
            hi = std::max(hi, lane.pts[k].value);
        }
        const float pad = std::max(1e-4f, (hi - lo) * 0.15f);
        lo -= pad;
        hi += pad;

        ImGui::TextUnformatted(table[(size_t)lane.param].label);
        ImGui::SameLine();
        ImGui::TextDisabled("= %.4g  (%d keys)", (double)dronegen::autoValueAt(lane, (float)head),
                            lane.count);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - scaled(46.0f));
        if (ImGui::SmallButton("x")) removeLane = lane.param;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this lane");

        const float h = scaled(52.0f);
        const ImVec2 o = ImGui::GetCursorScreenPos();
        const float w = std::max(scaled(120.0f), ImGui::GetContentRegionAvail().x -
                                                     scaled(8.0f));
        ImGui::InvisibleButton("lane", ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 br(o.x + w, o.y + h);
        dl->AddRectFilled(o, br, IM_COL32(22, 24, 29, 255), 3.0f * s);

        auto toScreen = [&](const dronegen::AutoPoint& pt) {
            return ImVec2(o.x + w * std::min(1.0f, std::max(0.0f, pt.t / len)),
                          br.y - h * (pt.value - lo) / std::max(1e-6f, hi - lo));
        };
        auto fromScreen = [&](ImVec2 m) {
            dronegen::AutoPoint pt;
            pt.t = std::max(0.0f, std::min(len, (m.x - o.x) / w * len));
            pt.value = lo + (hi - lo) * std::max(0.0f, std::min(1.0f, (br.y - m.y) / h));
            return pt;
        };

        // Grab the nearest point on press; drag it; right-click deletes it.
        static int dragLane = -1, dragPoint = -1;
        if (ImGui::IsItemActivated()) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            int best = -1;
            float bestD = 0.0f;
            for (int k = 0; k < lane.count; ++k) {
                const ImVec2 sp = toScreen(lane.pts[k]);
                const float d = std::fabs(sp.x - m.x) + std::fabs(sp.y - m.y) * 0.25f;
                if (best < 0 || d < bestD) { best = k; bestD = d; }
            }
            if (best >= 0 && bestD < scaled(14.0f)) {
                dragLane = i;
                dragPoint = best;
            }
        }
        if (ImGui::IsItemActive() && dragLane == i && dragPoint >= 0 &&
            dragPoint < lane.count) {
            const dronegen::AutoPoint np = fromScreen(ImGui::GetIO().MousePos);
            lane.pts[dragPoint] = np;
            for (int k = 1; k < lane.count; ++k)
                for (int j = k; j > 0 && lane.pts[j].t < lane.pts[j - 1].t; --j) {
                    std::swap(lane.pts[j], lane.pts[j - 1]);
                    if (dragPoint == j) dragPoint = j - 1;
                    else if (dragPoint == j - 1) dragPoint = j;
                }
            dronePushParams();
        }
        if (ImGui::IsItemDeactivated() && dragLane == i) { dragLane = -1; dragPoint = -1; }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            dragPoint < 0) {
            const dronegen::AutoPoint np = fromScreen(ImGui::GetIO().MousePos);
            dronegen::autoWrite(p, lane.param, np.value, np.t, 0.0f);
            dronePushParams();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
            lane.count > 1) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            int best = -1;
            float bestD = 0.0f;
            for (int k = 0; k < lane.count; ++k) {
                const ImVec2 sp = toScreen(lane.pts[k]);
                const float d = std::fabs(sp.x - m.x) + std::fabs(sp.y - m.y) * 0.25f;
                if (best < 0 || d < bestD) { best = k; bestD = d; }
            }
            if (best >= 0 && bestD < scaled(14.0f)) {
                for (int k = best; k + 1 < lane.count; ++k) lane.pts[k] = lane.pts[k + 1];
                --lane.count;
                dronePushParams();
            }
        }

        // The curve, its points, and the playhead.
        for (int k = 0; k + 1 < lane.count; ++k)
            dl->AddLine(toScreen(lane.pts[k]), toScreen(lane.pts[k + 1]),
                        IM_COL32(232, 168, 62, 255), 2.0f * s);
        if (lane.count == 1) {
            const ImVec2 sp = toScreen(lane.pts[0]);
            dl->AddLine(ImVec2(o.x, sp.y), ImVec2(br.x, sp.y),
                        IM_COL32(232, 168, 62, 160), 1.5f * s);
        }
        for (int k = 0; k < lane.count; ++k)
            dl->AddCircleFilled(toScreen(lane.pts[k]), 4.0f * s,
                                IM_COL32(255, 225, 160, 255));
        const float px = o.x + w * std::min(1.0f, (float)head / len);
        dl->AddLine(ImVec2(px, o.y), ImVec2(px, br.y), IM_COL32(245, 210, 100, 200),
                    1.5f * s);
        dl->AddRect(o, br, IM_COL32(70, 74, 82, 255), 3.0f * s);
        ImGui::Spacing();
        ImGui::PopID();
    }
    if (removeLane >= 0) {
        dronegen::autoRemoveLane(p, removeLane);
        dronePushParams();
    }
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

void App::drawDroneGeneratorWindow() {
    droneTickRender();
    if (!showDroneGenerator_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(1180.0f), scaled(760.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Drone Generator", &showDroneGenerator_)) {
        ImGui::End();
        return;
    }
    const float s = uiScaleApplied_;
    dronegen::Params& p = droneParams_;
    bool dirty = false;

    // Arm the knob hook for this frame: with Write on, any knob edit below lands
    // as a keyframe (see AutoWriteHook - the knobs themselves know nothing).
    g_autoHook.armed = droneWriteArmed_;
    g_autoHook.base = &droneParams_;
    g_autoHook.size = sizeof(dronegen::Params);
    g_autoHook.user = this;
    g_autoHook.write = [](void* user, size_t off, float v) {
        ((App*)user)->droneWriteAuto(off, v);
    };
    g_autoHook.automated = [](void* user, size_t off) {
        return ((App*)user)->droneIsAutomated(off);
    };

    // Automated parameters are shown at the playhead, so a knob under a lane
    // reads what is actually playing rather than the value it had before the
    // lane existed. Read mode therefore looks like a DAW: the knob follows the
    // timeline, and moving it only sticks while Write is armed.
    if (p.autoLaneCount > 0) dronegen::applyAutomation(p, droneHeadTime());

    // ---- open the project's own track --------------------------------------
    // Once per project: a project that already has a patch opens on it instead
    // of on "Init Drone" named after a file a Render would replace. Only over
    // the untouched default - never over something being worked on.
    if (droneAutoOpenFor_ != project_.dir) {
        droneAutoOpenFor_ = project_.dir;
        droneTrackScanAt_ = -1.0;
        droneScanTracks();
        if (dronePatchRel_.empty() && !droneDirty_ && !droneRendering_ && !droneAuditioning_) {
            const std::string want = std::string("res/audio/") + droneTrackName_ + ".drone";
            const DroneTrackRow* pick = nullptr;
            for (const DroneTrackRow& t : droneTracks_)
                if (t.rel == want) pick = &t;
            if (!pick && !droneTracks_.empty()) pick = &droneTracks_.front();
            if (pick) {
                droneLoadPatch(pick->rel);
            } else {
                const std::string n = droneUniqueTrackName(droneTrackName_);
                std::snprintf(droneTrackName_, sizeof(droneTrackName_), "%s", n.c_str());
            }
        }
    }
    if (droneTrackScanAt_ < 0.0 || ImGui::GetTime() - droneTrackScanAt_ > 2.0) droneScanTracks();

    const theme::Semantics& sem = theme::semantics();
    bool openNewAsk = false;
    const float bodyH = ImGui::GetContentRegionAvail().y;

    // ---- left rail: tracks in this project, then presets -------------------
    ImGui::BeginChild("##dronerail", ImVec2(scaled(210.0f), bodyH), ImGuiChildFlags_None);
    {
        char cnt[16];
        std::snprintf(cnt, sizeof cnt, "%d", (int)droneTracks_.size());
        ui::Caption("In this project", cnt);
        const float rowH = ImGui::GetFontSize() * 2.6f;
        for (const DroneTrackRow& t : droneTracks_) {
            const bool sel = t.rel == dronePatchRel_;
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const std::string label = (t.title.empty() ? std::filesystem::path(t.rel).stem().string() : t.title) +
                                      "##" + t.rel;
            if (ImGui::InvisibleButton(label.c_str(), ImVec2(w, rowH)) && !sel) {
                if (droneDirty_) {
                    dronePendingLoad_ = t.rel;
                    openNewAsk = true;
                } else {
                    droneStop();
                    droneLoadPatch(t.rel);
                }
            }
            uiscript::markLastItemChecked(sel);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s%s", t.rel.c_str(), t.hasWav ? "" : "\nnot rendered yet");
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float r = ImGui::GetStyle().FrameRounding + 1.0f;
            if (sel) dl->AddRectFilled(rp, ImVec2(rp.x + w, rp.y + rowH), ImGui::GetColorU32(sem.surface), r);
            const float fs = ImGui::GetFontSize();
            // a small waveform mark
            for (int i = 0; i < 5; ++i) {
                const float hh = fs * (0.15f + 0.35f * std::fabs(std::sin((float)i * 1.7f + 0.6f)));
                const float x = rp.x + scaled(8.0f) + (float)i * scaled(3.0f);
                dl->AddLine(ImVec2(x, rp.y + rowH * 0.5f - hh), ImVec2(x, rp.y + rowH * 0.5f + hh),
                            ImGui::GetColorU32(sel ? sem.accent : sem.textDim), 1.5f);
            }
            const float tx = rp.x + scaled(30.0f);
            float badgeW = 0.0f;
            if (t.hasWav) {
                const float bfs = fs * 0.8f;
                badgeW = ImGui::GetFont()->CalcTextSizeA(bfs, FLT_MAX, 0.0f, "wav").x + bfs;
                const ImVec2 b0(rp.x + w - badgeW - scaled(6.0f), rp.y + rowH * 0.5f - fs + 2.0f);
                const ImVec4 bc = t.newerThanWav ? sem.warn : sem.ok;
                dl->AddRectFilled(b0, ImVec2(b0.x + badgeW, b0.y + bfs + 4.0f),
                                  ImGui::GetColorU32(ImVec4(bc.x, bc.y, bc.z, 0.16f)), bfs);
                dl->AddText(ImGui::GetFont(), bfs, ImVec2(b0.x + bfs * 0.5f, b0.y + 2.0f), ImGui::GetColorU32(bc),
                            "wav");
            }
            dl->PushClipRect(rp, ImVec2(rp.x + w - badgeW - scaled(10.0f), rp.y + rowH), true);
            {
                ui::FontScope bold(sel ? ui::boldFont() : nullptr);
                dl->AddText(ImVec2(tx, rp.y + rowH * 0.5f - fs), ImGui::GetColorU32(sem.text),
                            label.c_str(), ImGui::FindRenderedTextEnd(label.c_str()));
            }
            {
                ui::FontScope mono(ui::monoFont());
                dl->AddText(ImGui::GetFont(), fs * 0.84f, ImVec2(tx, rp.y + rowH * 0.5f + 1.0f),
                            ImGui::GetColorU32(sem.textDim), t.rel.c_str());
            }
            dl->PopClipRect();
        }
        if (ui::GhostButton("+ New track", -1.0f)) {
            dronePendingLoad_.clear();
            if (droneDirty_)
                openNewAsk = true;
            else {
                droneStop();
                droneNewPatch();
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("A fresh patch with a file name no track in this project uses.");
        ImGui::Dummy(ImVec2(0.0f, scaled(6.0f)));
        ui::Caption("Start from a preset");
        const std::vector<dronegen::Preset>& presets = dronegen::presets();
        ImGui::BeginChild("##dronepresets", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
        for (int i = 0; i < (int)presets.size(); ++i) {
            if (ui::ListRow(presets[i].name, nullptr, false, ui::Glyph::Sparkle)) {
                // A preset is a SOUND: length, rate and the file name stay.
                dronePreset_ = i;
                const float keepLen = p.lengthSec;
                const int keepRate = p.sampleRate;
                p = presets[i].params;
                p.lengthSec = keepLen;
                p.sampleRate = keepRate;
                dronePatchTitle_ = presets[i].name;
                dirty = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s\n(replaces the sound; keeps the length and file name)", presets[i].blurb);
        }
        ImGui::EndChild();
        if (ui::GhostButton("Open file...", -1.0f)) {
            const std::string path = platform::pickFile(
                "Open drone patch", {{"TyraX drone patch (*.drone)", {"*.drone"}}});
            if (!path.empty()) droneLoadPatch(path);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("A .drone patch from outside this project (Save patch gives it a home).");
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);

    ImGui::BeginChild("##dronemain", ImVec2(0, bodyH), ImGuiChildFlags_None);

    // ---- identity: title, chips, render state ---------------------------------
    const std::string fileWav = std::string("res/audio/") + droneTrackName_ + ".wav";
    const DroneTrackRow* fileRow = nullptr;
    for (const DroneTrackRow& t : droneTracks_)
        if (t.rel == std::string("res/audio/") + droneTrackName_ + ".drone") fileRow = &t;
    bool wavExists = false;
    {
        std::error_code wec;
        wavExists = std::filesystem::exists(project_.filePath(fileWav), wec);
    }
    {
        const float y0 = ImGui::GetCursorPosY();
        // The buffer follows the model unless the field is being typed in.
        if (ImGui::GetActiveID() != ImGui::GetID("##dronetitle"))
            std::snprintf(droneTitleBuf_, sizeof droneTitleBuf_, "%s", dronePatchTitle_.c_str());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
        ImGui::PushFont(ui::boldFont(), ImGui::GetFontSize() * 1.3f);
        const float titleW = ImMin(scaled(220.0f), ImGui::CalcTextSize(droneTitleBuf_[0] ? droneTitleBuf_ : "Untitled").x +
                                                       scaled(24.0f));
        ImGui::SetNextItemWidth(ImMax(scaled(90.0f), titleW));
        if (ImGui::InputTextWithHint("##dronetitle", "Untitled", droneTitleBuf_, sizeof droneTitleBuf_)) {
            dronePatchTitle_ = droneTitleBuf_;
            droneDirty_ = true;
        }
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("The patch's title (saved in the .drone). The file name is in the footer.");
        const float rowMid = y0 + (ImGui::GetItemRectSize().y - ImGui::GetTextLineHeight()) * 0.5f;
        auto chip = [&](const char* text, const char* tip) {
            ImGui::SameLine(0.0f, scaled(8.0f));
            ImGui::SetCursorPosY(rowMid);
            ui::FontScope mono(ui::monoFont());
            ui::Badge(text, 4);
            if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tip);
        };
        char b[64];
        {
            std::string root = dronegen::noteName(p.rootNote);
            while (!root.empty() && (std::isdigit((unsigned char)root.back()) || root.back() == '-')) root.pop_back();
            std::snprintf(b, sizeof b, "%s %s", root.c_str(), dronegen::scaleNames()[std::clamp(p.scale, 0, (int)dronegen::ScaleCount - 1)]);
            chip(b, "Root and scale (Harmony).");
        }
        std::snprintf(b, sizeof b, "%.0f BPM", (double)p.bpm);
        chip(b, "Tempo (Harmony).");
        std::snprintf(b, sizeof b, "%d:%02d", (int)(p.lengthSec / 60.0f), (int)std::fmod(p.lengthSec, 60.0f));
        chip(b, "Length of the piece (Master).");
        std::snprintf(b, sizeof b, "%d kHz %s", (int)std::lround(p.sampleRate / 1000.0), p.stereo ? "stereo" : "mono");
        chip(b, "Render format (Master).");
        std::snprintf(b, sizeof b, "seed %u", (unsigned)p.seed);
        chip(b, "The random stream: bell placement, drift and noise.");
        ImGui::SameLine(0.0f, scaled(4.0f));
        ImGui::SetCursorPosY(rowMid - (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f);
        if (ui::GhostButton("Roll")) {
            uint32_t x = p.seed ? p.seed : 0x1234567u;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            p.seed = x;
            dirty = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("New random stream: bell placement, drift and noise\n"
                              "change, the patch does not.");
        // Render state, right-aligned: is the WAV on disk what this patch makes?
        int kind = 4;
        const char* state = "not rendered yet";
        if (droneRendering_) {
            kind = 3;
            state = "rendering...";
        } else if (wavExists) {
            const bool changed = droneDirty_ || (fileRow && fileRow->newerThanWav);
            kind = changed ? 1 : 0;
            state = changed ? "changed since last render" : "rendered";
        }
        const float sw = ImGui::CalcTextSize(state).x + ImGui::GetFontSize();
        const float right = ImGui::GetContentRegionMax().x;
        ImGui::SameLine();
        if (ImGui::GetCursorPosX() < right - sw) {
            ImGui::SetCursorPos(ImVec2(right - sw, rowMid));
            ui::StatusLine(kind, state);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Compares the patch with %s: unsaved edits, or a patch\n"
                                  "saved after the last render, mean the WAV is behind.",
                                  fileWav.c_str());
        }
        ImGui::SetCursorPosY(y0 + ImGui::GetItemRectSize().y + scaled(14.0f));
    }

    // Record mode stops itself at the end of the piece instead of playing on
    // forever. Polled here rather than in the audio callback, which must not
    // touch the device it is being called from.
    if (droneAuditioning_ && droneMode_ == 1 && droneLive_ &&
        droneLive_->timeSec() >= (double)p.lengthSec) {
        droneStop();
        droneHeadSec_ = (double)p.lengthSec;
        droneStatus_ = "Reached the end of the piece.";
    }

    // ---- transport card: to-start, Play, Rec, time, mode; the timeline -------
    ui::BeginCard("##dronetransport");
    {
        const float h = ImGui::GetFrameHeight() * 1.35f;
        const float y0 = ImGui::GetCursorPosY();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (ui::GhostButton("|<", h)) droneSeek(0.0);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Rewind to the start (works while playing too)");
        ImGui::SameLine();
        ImGui::SetCursorPosY(y0 - (h - ImGui::GetFrameHeight()) * 0.5f);
        // Play / Stop: a round green button (labels kept: "Play" / "Stop").
        {
            const ImVec2 c0 = ImGui::GetCursorScreenPos();
            const char* lbl = droneAuditioning_ && !droneRecording_ ? "Stop" : "Play";
            if (ImGui::InvisibleButton(lbl, ImVec2(h, h))) {
                if (droneAuditioning_) droneStop();
                else dronePlay(false);
            }
            const bool hov = ImGui::IsItemHovered();
            const ImVec2 c(c0.x + h * 0.5f, c0.y + h * 0.5f);
            dl->AddCircleFilled(c, h * 0.5f, ImGui::GetColorU32(ImVec4(sem.ok.x, sem.ok.y, sem.ok.z, hov ? 1.0f : 0.85f)), 32);
            const ImU32 ink = ImGui::GetColorU32(sem.ground);
            const float k = h * 0.2f;
            if (droneAuditioning_ && !droneRecording_)
                dl->AddRectFilled(ImVec2(c.x - k * 0.8f, c.y - k * 0.8f), ImVec2(c.x + k * 0.8f, c.y + k * 0.8f), ink, 1.5f);
            else
                dl->AddTriangleFilled(ImVec2(c.x - k * 0.7f, c.y - k), ImVec2(c.x - k * 0.7f, c.y + k),
                                      ImVec2(c.x + k, c.y), ink);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(droneMode_ == 1 ? "Plays from the playhead and stops at the end\nof the piece."
                                                  : "Plays from the playhead and keeps going past\nthe end until you stop it.");
        }
        ImGui::SameLine();
        // Rec: a red ring; filled while recording. In Sound design it switches
        // to Record first (recording only exists there).
        {
            const ImVec2 c0 = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("Rec", ImVec2(h, h))) {
                if (droneRecording_) {
                    droneStop();
                } else {
                    if (droneMode_ != 1) {
                        droneStop();
                        droneMode_ = 1;
                    }
                    dronePlay(true);
                }
            }
            uiscript::markLastItemChecked(droneRecording_);
            const bool hov = ImGui::IsItemHovered();
            const ImVec2 c(c0.x + h * 0.5f, c0.y + h * 0.5f);
            if (droneRecording_)
                dl->AddCircleFilled(c, h * 0.5f, ImGui::GetColorU32(ImVec4(sem.danger.x, sem.danger.y, sem.danger.z, 0.35f)), 32);
            else if (hov)
                dl->AddCircleFilled(c, h * 0.5f, ImGui::GetColorU32(ImVec4(sem.danger.x, sem.danger.y, sem.danger.z, 0.12f)), 32);
            dl->AddCircle(c, h * 0.5f - 1.0f, ImGui::GetColorU32(sem.danger), 32, 1.5f);
            dl->AddCircleFilled(c, h * 0.16f, ImGui::GetColorU32(sem.danger), 20);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Record: plays from the playhead with keyframe writing armed -\n"
                                  "turn a knob and the move is recorded onto its lane.");
        }
        ImGui::SameLine(0.0f, scaled(14.0f));
        {
            const double head = droneHeadTime();
            const float len = std::max(1.0f, p.lengthSec);
            const float ty = y0 + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f;
            ImGui::SetCursorPosY(ty);
            {
                ui::FontScope bold(ui::monoFont());
                ImGui::Text("%d:%04.1f", (int)(head / 60.0), std::fmod(head, 60.0));
            }
            ImGui::SameLine(0.0f, scaled(4.0f));
            ImGui::SetCursorPosY(ty);
            {
                ui::FontScope mono(ui::monoFont());
                ImGui::TextDisabled("/ %d:%02d", (int)(len / 60.0f), (int)std::fmod(len, 60.0f));
            }
            if (droneAuditioning_) {
                ImGui::SameLine(0.0f, scaled(10.0f));
                ImGui::TextDisabled("%s - bar %.1f", droneRecording_ ? "recording" : "playing",
                                    head / dronegen::barSeconds(p) + 1.0);
            } else if (!droneAudioError_.empty()) {
                ImGui::SameLine(0.0f, scaled(10.0f));
                ImGui::TextColored(sem.warn, "%s", droneAudioError_.c_str());
            }
            if (droneMode_ == 0) {
                ImGui::SameLine(0.0f, scaled(10.0f));
                ImGui::SetCursorPosY(y0);
                if (ui::GhostButton("Restart") && droneLive_) {
                    droneLive_->reset();
                    droneHeadSec_ = 0.0;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Rewinds to bar 1 and re-seeds the random streams.");
            }
        }
        // Mode, right-aligned: a segmented control showing the CURRENT mode.
        {
            static const char* const kModes[] = {"Sound design", "Record"};
            float segW = scaled(6.0f);
            for (const char* m : kModes) segW += ImGui::CalcTextSize(m).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - segW);
            ImGui::SetCursorPosY(y0);
            int mode = droneMode_;
            if (ui::Segmented("##dronemode", kModes, 2, &mode)) {
                droneStop();
                droneMode_ = mode;
                if (mode == 0) droneWriteArmed_ = false;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Sound design: free-running audition that loops past the end -\n"
                                  "for dialling a sound in; nothing is written.\n"
                                  "Record: Play stops at the end of the piece; Rec writes\n"
                                  "a keyframe for every knob you turn.");
        }
        ImGui::SetCursorPosY(y0 + h + scaled(6.0f));
        drawDroneTimelineBar();
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0.0f, scaled(2.0f)));

    // ---- tabs -------------------------------------------------------------
    if (ui::BeginQuietTabBar("dronetabbar")) {
        // Selector only: the content is drawn below, gated by droneTab_.
        static int lastFrame = -10;
        const bool force = lastFrame != ImGui::GetFrameCount() - 1;
        static const char* const kTabs[] = {"Harmony", "Layers", "Motion", "Air & Bells",
                                            "Space", "Master", "Timeline"};
        for (int i = 0; i < 7; ++i) {
            const ImGuiTabItemFlags f = force && i == droneTab_ ? ImGuiTabItemFlags_SetSelected : 0;
            if (ui::QuietTab(kTabs[i], i == droneTab_, f) && !force) droneTab_ = i;
        }
        ui::EndQuietTabBar();
        lastFrame = ImGui::GetFrameCount();
    }
    const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing() +
                          ImGui::GetStyle().ItemSpacing.y * 2.0f;
    ImGui::BeginChild("dronetabs", ImVec2(0, -footerH), false);
    {
        // ================= HARMONY ==========================
        if (droneTab_ == 0) {
            groupTitle("Tonality");
            int rootIdx = p.rootNote;
            if (knobInt("Root", &rootIdx, 12, 72, 33, "%.0f", s,
                        "The drone's root note. Everything else is an offset "
                        "from it.")) {
                p.rootNote = rootIdx;
                dirty = true;
            }
            ImGui::SameLine();
            dirty |= knob("Tuning", &p.tuning, 415.0f, 466.0f, 440.0f, "%.1f Hz", s);
            ImGui::SameLine();
            dirty |= knob("Glide", &p.glide, 0.0f, 20.0f, 2.5f, "%.1f s", s, 1.6f,
                          "How long a chord change takes to slide.");
            ImGui::SameLine();
            dirty |= knob("Tempo", &p.bpm, 20.0f, 140.0f, 60.0f, "%.0f BPM", s);
            ImGui::SameLine();
            int bpb = p.beatsPerBar;
            if (knobInt("Beats", &bpb, 2, 12, 4, "%.0f", s, "Beats per bar.")) {
                p.beatsPerBar = bpb;
                dirty = true;
            }
            ImGui::SameLine(0.0f, scaled(24.0f));
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(0.0f, scaled(10.0f)));
            ImGui::TextDisabled("Scale");
            ImGui::SetNextItemWidth(scaled(150.0f));
            dirty |= ImGui::Combo("##scale", &p.scale, dronegen::scaleNames(),
                                  dronegen::ScaleCount);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Only the bells use the scale; chords are spelled "
                                  "note by note below.");
            {
                ui::FontScope mono(ui::monoFont());
                ImGui::TextDisabled("bar = %.2f s", (double)dronegen::barSeconds(p));
            }
            ImGui::EndGroup();

            // ---- chord progression as cards ---------------------------------
            ImGui::Dummy(ImVec2(0.0f, scaled(6.0f)));
            const float barSec = dronegen::barSeconds(p);
            const float pass = dronegen::progressionBars(p) * barSec;
            {
                char cnt[24];
                std::snprintf(cnt, sizeof cnt, "%d of %d", p.stepCount, dronegen::kMaxSteps);
                const float y = ImGui::GetCursorPosY();
                ui::Caption("Chord progression", cnt);
                const float aw = ImGui::CalcTextSize("+ Chord").x + ImGui::GetStyle().FramePadding.x * 2.6f;
                ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - aw, y - scaled(4.0f)));
                if (ui::GhostButton("+ Chord", aw, p.stepCount < dronegen::kMaxSteps)) {
                    // A new chord copies the last one: the next change usually
                    // moves one voice, not all of them.
                    p.steps[p.stepCount] = p.steps[std::max(0, p.stepCount - 1)];
                    droneChordSel_ = p.stepCount;
                    ++p.stepCount;
                    dirty = true;
                }
                ImGui::SetCursorPosY(y + ImGui::GetFontSize() + scaled(10.0f));
            }
            if (droneChordSel_ >= p.stepCount) droneChordSel_ = p.stepCount - 1;
            if (droneChordSel_ < 0) droneChordSel_ = 0;
            // Which chords the piece never reaches: their first start is at or
            // past the end (the progression is longer than the track).
            std::vector<int> unheard;
            {
                float t = 0.0f;
                for (int i = 0; i < p.stepCount; ++i) {
                    if (t >= p.lengthSec - 1e-3f) unheard.push_back(i);
                    t += std::max(0.05f, p.steps[i].bars) * barSec;
                }
            }
            {
                const int perRow = 4;
                const float gap = ImGui::GetStyle().ItemSpacing.x;
                const float cw = (ImGui::GetContentRegionAvail().x - gap * (perRow - 1)) / perRow;
                const float ch = ImGui::GetTextLineHeight() * 3.6f + scaled(14.0f);
                for (int i = 0; i < p.stepCount; ++i) {
                    const dronegen::Step& st = p.steps[i];
                    const bool sel = droneChordSel_ == i;
                    const bool off = std::find(unheard.begin(), unheard.end(), i) != unheard.end();
                    if (i % perRow) ImGui::SameLine(0.0f, gap);
                    const ImVec2 c0 = ImGui::GetCursorScreenPos();
                    char lab[24];
                    std::snprintf(lab, sizeof lab, "Chord %d", i + 1);
                    if (ImGui::InvisibleButton(lab, ImVec2(cw, ch))) droneChordSel_ = i;
                    uiscript::markLastItemChecked(sel);
                    const bool hov = ImGui::IsItemHovered();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const float r = ImGui::GetStyle().FrameRounding + 3.0f;
                    const ImVec2 c1(c0.x + cw, c0.y + ch);
                    const float a = off ? 0.45f : 1.0f;
                    dl->AddRectFilled(c0, c1, colA(sel ? sem.surface : sem.field, hov && !sel ? 0.8f : 1.0f), r);
                    dl->AddRect(c0, c1, sel ? colA(sem.accent, 0.6f) : colA(sem.border, 1.0f), r, 0, sel ? 1.5f : 1.0f);
                    const float fs = ImGui::GetFontSize();
                    const float x = c0.x + scaled(10.0f);
                    float y = c0.y + scaled(8.0f);
                    dl->AddRectFilled(ImVec2(x, y + fs * 0.25f), ImVec2(x + fs * 0.55f, y + fs * 0.8f),
                                      chordColour(i, a), 2.0f);
                    char num[8];
                    std::snprintf(num, sizeof num, "%d", i + 1);
                    {
                        ui::FontScope bold(ui::boldFont());
                        dl->AddText(ImVec2(x + fs * 0.9f, y), colA(sem.text, a), num);
                    }
                    char bars[24];
                    std::snprintf(bars, sizeof bars, "%g bar%s", (double)st.bars, st.bars == 1.0f ? "" : "s");
                    dl->AddText(ImVec2(x + fs * 1.8f, y), colA(sem.textDim, a), bars);
                    y += fs + scaled(8.0f);
                    std::string names, semis;
                    for (int n = 0; n < st.count; ++n) {
                        if (n) names += " ", semis += " ";
                        names += dronegen::noteName(p.rootNote + st.notes[n]);
                        semis += std::to_string(st.notes[n]);
                    }
                    dl->PushClipRect(c0, ImVec2(c1.x - 4.0f, c1.y), true);
                    {
                        ui::FontScope bold(ui::boldFont());
                        dl->AddText(ImVec2(x, y), colA(sem.text, a), names.c_str());
                    }
                    y += fs + scaled(4.0f);
                    {
                        ui::FontScope mono(ui::monoFont());
                        dl->AddText(ImGui::GetFont(), fs * 0.85f, ImVec2(x, y), colA(sem.textDim, a), semis.c_str());
                    }
                    dl->PopClipRect();
                    if (off && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("Never heard: the track ends before this chord starts.");
                }
            }
            // Always offered: a looping track that is not a whole number of
            // passes lands mid-chord at the loop point.
            const int fitN = std::max(1, (int)std::lround(p.lengthSec / pass));
            const float fitted = pass * (float)fitN;
            const bool fits = std::fabs(fitted - p.lengthSec) < 0.05f;
            const char* fitLabel = "Fit length to whole passes";
            const float fitW = ImGui::CalcTextSize(fitLabel).x + ImGui::GetStyle().FramePadding.x * 2.6f;
            auto fitButton = [&] {
                if (ui::SecondaryButton(fitLabel, fitW, !fits)) {
                    p.lengthSec = fitted;
                    dirty = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip(fits ? "The length is already a whole number of passes."
                                           : "Sets the length to %d pass%s = %d:%02d. A looping track whose\n"
                                             "length is a whole number of passes never lands mid-chord\n"
                                             "at the loop point.",
                                      fitN, fitN == 1 ? "" : "es", (int)(fitted / 60.0f),
                                      (int)std::fmod(fitted, 60.0f));
            };
            ImGui::Dummy(ImVec2(0.0f, scaled(4.0f)));
            if (!unheard.empty()) {
                // The silent half of a progression is the consequence nobody
                // hears; say it next to the cause, with the fix beside it.
                std::string which;
                for (size_t k = 0; k < unheard.size(); ++k) {
                    if (k) which += k + 1 == unheard.size() ? " and " : ", ";
                    which += std::to_string(unheard[k] + 1);
                }
                char msg[320];
                std::snprintf(msg, sizeof msg,
                              "%s %s never play%s: one pass is %.4g bars = %d:%02d at %.0f BPM, the "
                              "track is %d:%02d. Fit the length to whole passes, or shorten the chords.",
                              unheard.size() == 1 ? "Chord" : "Chords", which.c_str(),
                              unheard.size() == 1 ? "s" : "", (double)dronegen::progressionBars(p),
                              (int)(pass / 60.0f), (int)std::fmod(pass, 60.0f), (double)p.bpm,
                              (int)(p.lengthSec / 60.0f), (int)std::fmod(p.lengthSec, 60.0f));
                const float gapX = ImGui::GetStyle().ItemSpacing.x;
                const float y0 = ImGui::GetCursorPosY();
                ui::Callout(1, msg, ImGui::GetContentRegionAvail().x - fitW - gapX);
                const float ch = ImGui::GetItemRectSize().y;
                ImGui::SameLine(0.0f, gapX);
                ImGui::SetCursorPosY(y0 + (ch - ImGui::GetFrameHeight()) * 0.5f);
                fitButton();
                ImGui::SetCursorPosY(y0 + ch + ImGui::GetStyle().ItemSpacing.y);
            } else {
                fitButton();
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("One pass = %.4g bars = %.1f s.", (double)dronegen::progressionBars(p),
                                    (double)pass);
            }

            // ---- the selected chord's notes ---------------------------------
            ImGui::Dummy(ImVec2(0.0f, scaled(6.0f)));
            {
                dronegen::Step& st = p.steps[droneChordSel_];
                ImGui::PushID(droneChordSel_);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Chord %d", droneChordSel_ + 1);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(scaled(70.0f));
                dirty |= ImGui::DragFloat("##bars", &st.bars, 0.25f, 0.25f, 64.0f, "%.2g bars");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("How long the chord is held.");
                for (int n = 0; n < dronegen::kMaxChordNotes; ++n) {
                    ImGui::SameLine();
                    int v = n < st.count ? st.notes[n] : -99;
                    ImGui::SetNextItemWidth(scaled(46.0f));
                    ImGui::PushID(n);
                    if (ImGui::DragInt("##n", &v, 0.25f, -99, 36, v <= -99 ? "-" : "%d")) {
                        if (v <= -90) {
                            // clearing a voice shortens the chord from here
                            st.count = std::min(st.count, n);
                        } else {
                            st.notes[n] = std::max(-36, std::min(36, v));
                            st.count = std::max(st.count, n + 1);
                        }
                        dirty = true;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("Voice %d: semitones from the root (drag left to the end\n"
                                          "for \"-\", unused - which shortens the chord from here).",
                                          n + 1);
                    ImGui::PopID();
                }
                ImGui::SameLine();
                if (ui::GhostButton("Remove chord", 0.0f, p.stepCount > 1)) {
                    for (int i = droneChordSel_; i + 1 < p.stepCount; ++i) p.steps[i] = p.steps[i + 1];
                    --p.stepCount;
                    droneChordSel_ = std::min(droneChordSel_, p.stepCount - 1);
                    dirty = true;
                }
                ImGui::PopID();
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Semitones from the root; each layer picks which voices it plays. "
                                "Click a chord to edit its notes.");
            ImGui::PopTextWrapPos();
        }

        // ================= LAYERS ==========================
        if (droneTab_ == 1) {
            // Mixer strip first: balancing four stacks should not need four
            // tab clicks.
            groupTitle("Mix");
            for (int i = 0; i < dronegen::kMaxLayers; ++i) {
                dronegen::Layer& L = p.layers[i];
                ImGui::PushID(i);
                if (i) ImGui::SameLine(0.0f, scaled(18.0f));
                ImGui::BeginGroup();
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "L%d", i + 1);
                dirty |= ImGui::Checkbox(lbl, &L.on);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", dronegen::waveNames()[L.wave]);
                // The very same parameter as the layer's own Level knob below,
                // deliberately: this strip exists so four stacks can be
                // balanced without four tab clicks, not to add a second gain.
                // It binds straight to the field, so it automates like any
                // other knob (a scratch copy would have no address the write
                // hook recognises, and would silently record nothing).
                dirty |= knob("level", &L.level, 0.0f, 1.0f, 0.5f, "%.2f", s, 1.0f,
                              "The same control as this layer's Level knob -\n"
                              "the two always read alike.");
                ImGui::EndGroup();
                ImGui::PopID();
            }

            if (ImGui::BeginTabBar("layertabs")) {
                for (int i = 0; i < dronegen::kMaxLayers; ++i) {
                    char name[16];
                    std::snprintf(name, sizeof(name), "Layer %d", i + 1);
                    if (!ImGui::BeginTabItem(name)) continue;
                    dronegen::Layer& L = p.layers[i];
                    ImGui::PushID(i);
                    dirty |= ImGui::Checkbox("Enabled", &L.on);
                    ImGui::SameLine(0.0f, scaled(20.0f));
                    ImGui::SetNextItemWidth(scaled(120.0f));
                    dirty |= ImGui::Combo("Wave", &L.wave, dronegen::waveNames(),
                                          dronegen::WaveCount);
                    ImGui::SameLine(0.0f, scaled(20.0f));
                    // Which chord degrees this stack plays: the difference
                    // between a sub, a pad and a top voice.
                    ImGui::TextDisabled("Plays:");
                    for (int n = 0; n < dronegen::kMaxChordNotes; ++n) {
                        ImGui::SameLine();
                        char t[8];
                        std::snprintf(t, sizeof(t), "%d##nb%d", n + 1, n);
                        bool on = (L.notes & (1 << n)) != 0;
                        if (ImGui::Checkbox(t, &on)) {
                            L.notes = on ? (L.notes | (1 << n)) : (L.notes & ~(1 << n));
                            dirty = true;
                        }
                    }

                    groupTitle("Pitch");
                    int oct = L.octave, semi = L.semi;
                    if (knobInt("Octave", &oct, -3, 3, 0, "%.0f", s)) {
                        L.octave = oct;
                        dirty = true;
                    }
                    ImGui::SameLine();
                    if (knobInt("Semi", &semi, -12, 12, 0, "%.0f", s)) {
                        L.semi = semi;
                        dirty = true;
                    }
                    ImGui::SameLine();
                    dirty |= knob("Fine", &L.fine, -50.0f, 50.0f, 0.0f, "%.0f ct", s);
                    ImGui::SameLine();
                    int uni = L.unison;
                    if (knobInt("Unison", &uni, 1, dronegen::kMaxUnison, 1, "%.0f", s,
                                "Detuned copies of every voice."))
                        { L.unison = uni; dirty = true; }
                    ImGui::SameLine();
                    dirty |= knob("Detune", &L.detune, 0.0f, 60.0f, 12.0f, "%.0f ct", s);
                    ImGui::SameLine();
                    dirty |= knob("Spread", &L.spread, 0.0f, 1.0f, 0.6f, "%.2f", s, 1.0f,
                                  "Stereo fan of the unison stack.");
                    ImGui::SameLine();
                    dirty |= knob("Drift", &L.drift, 0.0f, 40.0f, 6.0f, "%.0f ct", s,
                                  1.0f, "Slow random detuning - what keeps a held\n"
                                        "chord from sounding frozen.");

                    groupTitle("Shape");
                    dirty |= knob("Level", &L.level, 0.0f, 1.0f, 0.5f, "%.2f", s);
                    ImGui::SameLine();
                    dirty |= knob("Pan", &L.pan, -1.0f, 1.0f, 0.0f, "%+.2f", s);
                    ImGui::SameLine();
                    dirty |= knob("Tone", &L.tone, 0.0f, 1.0f, 0.6f, "%.2f", s, 1.0f,
                                  "Per-voice low pass, 80 Hz to 12 kHz.");
                    ImGui::SameLine();
                    dirty |= knob("Attack", &L.attack, 0.05f, 30.0f, 4.0f, "%.1f s", s,
                                  1.7f);
                    ImGui::SameLine();
                    dirty |= knob("Release", &L.release, 0.05f, 30.0f, 5.0f, "%.1f s", s,
                                  1.7f);
                    if (L.wave == dronegen::WaveFm) {
                        ImGui::SameLine();
                        dirty |= knob("Ratio", &L.fmRatio, 0.5f, 8.0f, 2.0f, "%.2f", s);
                        ImGui::SameLine();
                        dirty |= knob("Index", &L.fmIndex, 0.0f, 8.0f, 1.5f, "%.2f", s);
                    }
                    if (L.wave == dronegen::WavePulse) {
                        ImGui::SameLine();
                        dirty |= knob("Width", &L.pw, 0.05f, 0.95f, 0.35f, "%.2f", s);
                    }
                    ImGui::PopID();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }

        // ================= MOTION ==========================
        if (droneTab_ == 2) {
            groupTitle("LFOs");
            for (int i = 0; i < dronegen::kNumLfos; ++i) {
                dronegen::Lfo& L = p.lfos[i];
                ImGui::PushID(i);
                if (i) ImGui::Spacing();
                ImGui::BeginGroup();
                ImGui::Text("LFO %d", i + 1);
                ImGui::SetNextItemWidth(scaled(130.0f));
                dirty |= ImGui::Combo("##shape", &L.shape, dronegen::lfoShapeNames(),
                                      dronegen::LfoShapeCount);
                if (ImGui::Checkbox("per bar", &L.sync)) dirty = true;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Rate in cycles per bar instead of Hz, so the\n"
                                      "motion lines up with the progression (and with\n"
                                      "the loop point).");
                ImGui::EndGroup();
                ImGui::SameLine();
                dirty |= knob(L.sync ? "Cycles/bar" : "Rate", &L.rate, 0.005f,
                              L.sync ? 4.0f : 8.0f, 0.07f,
                              L.sync ? "%.3f" : "%.3f Hz", s, 2.0f);
                ImGui::SameLine();
                dirty |= knob("Depth", &L.depth, 0.0f, 1.0f, 1.0f, "%.2f", s);
                ImGui::PopID();
            }

            groupTitle("Modulation matrix");
            ImGui::TextDisabled("Route a source at a destination. This is where a "
                                "static patch becomes a piece.");
            if (ImGui::BeginTable("mods", 3,
                                  ImGuiTableFlags_SizingFixedFit |
                                      ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("source");
                ImGui::TableSetupColumn("destination");
                ImGui::TableSetupColumn("amount");
                ImGui::TableHeadersRow();
                for (int i = 0; i < dronegen::kNumMods; ++i) {
                    dronegen::ModRow& m = p.mods[i];
                    ImGui::TableNextRow();
                    ImGui::PushID(i);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(scaled(110.0f));
                    dirty |= ImGui::Combo("##src", &m.src, dronegen::modSourceNames(),
                                          dronegen::ModSrcCount);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(scaled(160.0f));
                    dirty |= ImGui::Combo("##dst", &m.dst, dronegen::modTargetNames(),
                                          dronegen::ModDstTargetCount);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(scaled(150.0f));
                    dirty |= ImGui::SliderFloat("##amt", &m.amount, -1.0f, 1.0f, "%+.2f");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            groupTitle("Arc");
            ImGui::TextDisabled("The shape of the whole piece: five points from start "
                                "to end.\nUse it as the \"Arc\" modulation source "
                                "above.");
            if (arcEditor("arc", p.arc, dronegen::kArcPoints,
                          ImVec2(scaled(420.0f), scaled(90.0f)), s))
                dirty = true;
        }

        // ================= AIR & BELLS ==========================
        if (droneTab_ == 3) {
            groupTitle("Air (filtered noise)");
            dirty |= ImGui::Checkbox("Air on", &p.noise.on);
            dirty |= knob("Level##air", &p.noise.level, 0.0f, 0.6f, 0.12f, "%.3f", s);
            ImGui::SameLine();
            dirty |= knob("Colour", &p.noise.cutoff, 60.0f, 8000.0f, 900.0f, "%.0f Hz", s,
                          2.2f, "Centre of the noise band.");
            ImGui::SameLine();
            dirty |= knob("Res", &p.noise.res, 0.0f, 0.95f, 0.3f, "%.2f", s, 1.0f,
                          "Narrows the band - past 0.7 it whistles.");
            ImGui::SameLine();
            dirty |= knob("Motion", &p.noise.motionRate, 0.005f, 1.0f, 0.05f, "%.3f Hz",
                          s, 2.0f, "How fast the band wanders.");
            ImGui::SameLine();
            dirty |= knob("Sweep", &p.noise.motionDepth, 0.0f, 1.0f, 0.5f, "%.2f", s,
                          1.0f, "How far it wanders (octaves).");
            ImGui::SameLine();
            dirty |= knob("Width", &p.noise.stereo, 0.0f, 1.0f, 0.7f, "%.2f", s, 1.0f,
                          "Channel decorrelation.");

            groupTitle("Bells (sparse notes)");
            dirty |= ImGui::Checkbox("Bells on", &p.bells.on);
            ImGui::SameLine(0.0f, scaled(20.0f));
            ImGui::SetNextItemWidth(scaled(140.0f));
            dirty |= ImGui::Combo("Timbre", &p.bells.timbre, dronegen::bellNames(),
                                  dronegen::BellTimbreCount);
            ImGui::SameLine(0.0f, scaled(20.0f));
            dirty |= ImGui::Checkbox("Chord notes only", &p.bells.chordLock);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("On: bells pick a note from the chord that is "
                                  "playing.\nOff: they roam the scale.");
            ImGui::SameLine(0.0f, scaled(20.0f));
            ImGui::SetNextItemWidth(scaled(120.0f));
            const char* kQuant[] = {"free", "on the beat", "on the bar"};
            dirty |= ImGui::Combo("Timing", &p.bells.quantize, kQuant, 3);

            dirty |= knob("Density", &p.bells.density, 0.0f, 90.0f, 8.0f, "%.0f /min", s,
                          1.5f);
            ImGui::SameLine();
            dirty |= knob("Level##bells", &p.bells.level, 0.0f, 1.0f, 0.35f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Decay", &p.bells.decay, 0.1f, 15.0f, 3.0f, "%.1f s", s, 1.6f);
            ImGui::SameLine();
            int bo = p.bells.octave;
            if (knobInt("Octave", &bo, -1, 4, 2, "%.0f", s, "Octaves above the root."))
                { p.bells.octave = bo; dirty = true; }
            ImGui::SameLine();
            int br = p.bells.range;
            if (knobInt("Range", &br, 1, 36, 12, "%.0f", s, "Semitones of reach."))
                { p.bells.range = br; dirty = true; }
            ImGui::SameLine();
            dirty |= knob("Bright", &p.bells.bright, 0.0f, 1.0f, 0.5f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Spread", &p.bells.spread, 0.0f, 1.0f, 0.8f, "%.2f", s, 1.0f,
                          "Random stereo placement per note.");
            ImGui::SameLine();
            dirty |= knob("To delay", &p.bells.delaySend, 0.0f, 1.5f, 0.5f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("To reverb", &p.bells.revSend, 0.0f, 1.5f, 0.8f, "%.2f", s);
        }

        // ================= SPACE (FX) ==========================
        if (droneTab_ == 4) {
            groupTitle("Filter & drive");
            ImGui::SetNextItemWidth(scaled(120.0f));
            dirty |= ImGui::Combo("##ftype", &p.filter.type, dronegen::filterNames(),
                                  dronegen::FilterTypeCount);
            ImGui::SameLine();
            dirty |= knob("Cutoff", &p.filter.cutoff, 40.0f, 12000.0f, 2200.0f, "%.0f Hz",
                          s, 2.4f);
            ImGui::SameLine();
            dirty |= knob("Res", &p.filter.res, 0.0f, 0.95f, 0.25f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Drive", &p.fx.drive, 0.0f, 1.0f, 0.15f, "%.2f", s, 1.0f,
                          "Soft saturation before the filter.");
            ImGui::SameLine();
            dirty |= knob("Dry/wet", &p.fx.driveMix, 0.0f, 1.0f, 1.0f, "%.2f", s);

            groupTitle("Chorus & tape");
            dirty |= knob("Ch rate", &p.fx.chorusRate, 0.01f, 3.0f, 0.25f, "%.2f Hz", s,
                          1.8f);
            ImGui::SameLine();
            dirty |= knob("Ch depth", &p.fx.chorusDepth, 0.0f, 1.0f, 0.4f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Ch mix", &p.fx.chorusMix, 0.0f, 1.0f, 0.35f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Ch width", &p.fx.chorusSpread, 0.0f, 1.0f, 1.0f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Wow", &p.fx.wow, 0.0f, 1.0f, 0.15f, "%.2f", s, 1.0f,
                          "Slow tape pitch drift.");
            ImGui::SameLine();
            dirty |= knob("Flutter", &p.fx.flutter, 0.0f, 1.0f, 0.05f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Hiss", &p.fx.hiss, 0.0f, 1.0f, 0.0f, "%.2f", s);

            groupTitle("Delay");
            ImGui::SetNextItemWidth(scaled(110.0f));
            if (ImGui::BeginCombo("##ddiv", dronegen::delayDivName(p.fx.delayDiv))) {
                for (int i = 0; i < 6; ++i)
                    if (ImGui::Selectable(dronegen::delayDivName(i), p.fx.delayDiv == i)) {
                        p.fx.delayDiv = i;
                        dirty = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (p.fx.delayDiv == 0) {
                dirty |= knob("Time", &p.fx.delayTime, 0.02f, 4.0f, 1.5f, "%.2f s", s,
                              1.8f);
                ImGui::SameLine();
            }
            dirty |= knob("Feedback", &p.fx.delayFeedback, 0.0f, 0.95f, 0.55f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Mix##delay", &p.fx.delayMix, 0.0f, 1.0f, 0.25f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Damp##delay", &p.fx.delayDamp, 0.0f, 0.95f, 0.5f, "%.2f", s, 1.0f,
                          "Each repeat loses its top end.");
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(0, scaled(14.0f)));
            dirty |= ImGui::Checkbox("Ping-pong", &p.fx.pingpong);
            ImGui::TextDisabled("%.2f s", (double)dronegen::delaySeconds(p));
            ImGui::EndGroup();

            groupTitle("Reverb");
            dirty |= knob("Size", &p.fx.revSize, 0.0f, 1.0f, 0.7f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Decay", &p.fx.revDecay, 0.2f, 40.0f, 9.0f, "%.1f s", s, 1.8f,
                          "RT60. A 30-second tail is a legitimate choice here.");
            ImGui::SameLine();
            dirty |= knob("Damp##reverb", &p.fx.revDamp, 0.0f, 1.0f, 0.45f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Low cut##reverb", &p.fx.revLowCut, 20.0f, 600.0f, 90.0f, "%.0f Hz", s,
                          1.8f, "Keeps the tail out of the sub range.");
            ImGui::SameLine();
            dirty |= knob("Predelay", &p.fx.revPredelay, 0.0f, 0.25f, 0.03f, "%.3f s", s);
            ImGui::SameLine();
            dirty |= knob("Diffusion", &p.fx.revDiffusion, 0.0f, 1.0f, 0.7f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Width", &p.fx.revWidth, 0.0f, 1.0f, 1.0f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Mix##reverb", &p.fx.revMix, 0.0f, 1.0f, 0.45f, "%.2f", s);

            dirty |= knob("Shimmer", &p.fx.shimmer, 0.0f, 1.0f, 0.0f, "%.2f", s, 1.0f,
                          "Pitch-shifted feedback inside the tail - the sound\n"
                          "everyone means by \"ambient reverb\".");
            ImGui::SameLine();
            int ss = p.fx.shimmerSemi;
            if (knobInt("Shift", &ss, -12, 19, 12, "%.0f", s, "Semitones of the shimmer."))
                { p.fx.shimmerSemi = ss; dirty = true; }

            groupTitle("Output tone");
            dirty |= knob("Low cut##out", &p.fx.lowCut, 10.0f, 300.0f, 40.0f, "%.0f Hz", s,
                          1.6f);
            ImGui::SameLine();
            dirty |= knob("High cut", &p.fx.highCut, 800.0f, 11000.0f, 9000.0f, "%.0f Hz",
                          s, 2.0f);
            ImGui::SameLine();
            dirty |= knob("Tilt", &p.fx.tilt, -9.0f, 9.0f, 0.0f, "%+.1f dB", s, 1.0f,
                          "Darker to brighter, pivoting at 700 Hz.");
            ImGui::SameLine();
            dirty |= knob("Stereo", &p.fx.width, 0.0f, 2.0f, 1.0f, "%.2f", s, 1.0f,
                          "Mid/side width. 0 = mono, 1 = as recorded.");
            ImGui::SameLine();
            dirty |= knob("Mono under", &p.fx.monoBelow, 20.0f, 500.0f, 140.0f, "%.0f Hz",
                          s, 1.6f, "Bass summed to the centre - kinder to a TV.");
        }

        // ================= MASTER ==========================
        if (droneTab_ == 5) {
            groupTitle("Piece");
            dirty |= knob("Length", &p.lengthSec, 5.0f, 600.0f, 60.0f, "%.0f s", s, 1.8f);
            ImGui::SameLine();
            dirty |= knob("Level", &p.master.level, 0.0f, 1.0f, 0.8f, "%.2f", s);
            ImGui::SameLine();
            dirty |= knob("Normalize", &p.master.normalize, 0.0f, 1.0f, 0.89f, "%.2f", s,
                          1.0f, "Target peak of the rendered file. 0 = leave it alone.");
            if (!p.master.loopSeamless) {
                ImGui::SameLine();
                dirty |= knob("Fade in", &p.master.fadeIn, 0.0f, 20.0f, 2.0f, "%.1f s", s);
                ImGui::SameLine();
                dirty |= knob("Fade out", &p.master.fadeOut, 0.0f, 20.0f, 4.0f, "%.1f s",
                              s);
            } else {
                ImGui::SameLine();
                dirty |= knob("Loop tail", &p.master.loopTail, 0.5f, 30.0f, 6.0f, "%.1f s",
                              s, 1.4f,
                              "How much of the reverb tail is folded back over\n"
                              "the start of the file. The fold is windowed to zero\n"
                              "at its end, so the join never clicks - but a tail\n"
                              "much shorter than the reverb decay fades out while\n"
                              "the room is still ringing, which reads as the wrap\n"
                              "losing its space.");
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Dummy(ImVec2(0, scaled(14.0f)));
                const float wantTail =
                    std::min(30.0f, std::min(p.fx.revDecay, p.lengthSec * 0.9f));
                if (p.master.loopTail < p.fx.revDecay * 0.6f) {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                                       "tail < reverb decay (%.0f s)", (double)p.fx.revDecay);
                    if (ImGui::SmallButton("Match decay")) {
                        p.master.loopTail = wantTail;
                        dirty = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Folds back as much tail as the reverb\n"
                                          "actually produces (%.1f s), so the wrap keeps\n"
                                          "the room instead of fading out early.",
                                          (double)wantTail);
                } else {
                    ImGui::TextDisabled("covers the reverb tail");
                }
                ImGui::EndGroup();
            }

            ImGui::Spacing();
            dirty |= ImGui::Checkbox("Seamless loop", &p.master.loopSeamless);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Renders past the end and adds the tail over the beginning -\n"
                    "which is exactly what a looping player hears. Leave it on for\n"
                    "background music (Play Music with Loop checked); turn it off\n"
                    "for a one-shot cue and use the fades instead.");
            ImGui::SameLine(0.0f, scaled(20.0f));
            dirty |= ImGui::Checkbox("Limiter", &p.master.limiter);
            ImGui::SameLine(0.0f, scaled(20.0f));
            dirty |= ImGui::Checkbox("Dither", &p.master.dither);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("TPDF noise at 16-bit. Seeded from the patch, so a\n"
                                  "re-render is still byte-identical.");

            groupTitle("File");
            const char* kRates[] = {"11025 Hz", "22050 Hz (PS2 song player)",
                                    "44100 Hz"};
            int rateIdx = p.sampleRate == 11025 ? 0 : p.sampleRate == 44100 ? 2 : 1;
            ImGui::SetNextItemWidth(scaled(240.0f));
            if (ImGui::Combo("Sample rate", &rateIdx, kRates, 3)) {
                p.sampleRate = rateIdx == 0 ? 11025 : rateIdx == 2 ? 44100 : 22050;
                dirty = true;
                // Structural change: the audition device and synth are rebuilt.
                if (droneAuditioning_) {
                    droneAudition(false);
                    droneLive_.reset();
                    droneAudition(true);
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Tyra's AudioSong streams 16-bit 22050 Hz stereo -\n"
                                  "anything else is for auditioning or for exporting\n"
                                  "to another tool. The Music list can also downsample\n"
                                  "per build (Project > Music > PS2 build).");
            ImGui::SameLine();
            if (ImGui::Checkbox("Stereo", &p.stereo)) dirty = true;

            const unsigned long long bytes = dronegen::wavBytes(p);
            ImGui::TextDisabled("Renders to %.1f MB of WAV (%.0f s at %d Hz %s).",
                                (double)bytes / (1024.0 * 1024.0), (double)p.lengthSec,
                                p.sampleRate, p.stereo ? "stereo" : "mono");
            if (bytes > 24ull * 1024 * 1024)
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                                   "That is a lot of disc for one track - a shorter "
                                   "seamless loop usually reads the same.");

            if (droneRenderResult_.frames > 0 && !droneRendering_) {
                ImGui::Spacing();
                ImGui::TextDisabled("Last render: peak %.2f, RMS %.3f, normalize gain "
                                    "x%.2f.",
                                    (double)droneRenderResult_.peak,
                                    (double)droneRenderResult_.rms,
                                    (double)droneRenderResult_.gainApplied);
            }
        }

        // ================= TIMELINE ==========================
        if (droneTab_ == 6) {
            drawDroneTimelineTab();
        }
    }
    ImGui::EndChild();

    // ---- footer: what Render writes, Save patch, Render WAV -------------------
    ImGui::Separator();
    const std::string saveTarget =
        dronePatchRel_.empty() ? std::string("res/audio/") + droneTrackName_ + ".drone"
                               : dronePatchRel_;
    {
        const float y0 = ImGui::GetCursorPosY();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("File");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(120.0f));
        if (ImGui::InputText("##Name", droneTrackName_, sizeof(droneTrackName_))) droneDirty_ = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Names both files: res/audio/<name>.wav and the\n"
                              "<name>.drone patch beside it.");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("writes");
        ImGui::SameLine(0.0f, scaled(4.0f));
        {
            ui::FontScope mono(ui::monoFont());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(fileWav.c_str());
        }
        bool inMusic = false;
        for (const std::string& m : project_.music) inMusic |= (m == fileWav);
        ImGui::SameLine(0.0f, scaled(4.0f));
        ImGui::AlignTextToFramePadding();
        // The consequence in words, as much of it as fits before the buttons
        // (the whole sentence is the tooltip either way).
        const std::string tail = std::string("+ ") + droneTrackName_ + ".drone - " +
                                 (wavExists ? "replaces the current file" : "a new file") + " - " +
                                 (inMusic ? "in Music list" : "joins the Music list");
        const std::string shortTail = wavExists ? "- replaces it" : "- new file";
        const float room = ImGui::GetContentRegionMax().x - ImGui::GetCursorPosX() - scaled(150.0f) -
                           ImGui::CalcTextSize("Save patch").x - ImGui::GetStyle().FramePadding.x * 2.6f -
                           ImGui::GetStyle().ItemSpacing.x * 3.0f;
        ImGui::TextDisabled("%s", ImGui::CalcTextSize(tail.c_str()).x <= room ? tail.c_str() : shortTail.c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s\nRender WAV writes the track and saves the patch beside it;\n"
                              "replacing an existing file asks first.\n\nOpen patch: %s%s", tail.c_str(),
                              dronePatchRel_.empty() ? "(never saved)" : dronePatchRel_.c_str(),
                              droneDirty_ ? " - unsaved edits" : "");

        // Right: Save patch, Render WAV (progress inside the button).
        const float rw = scaled(150.0f);
        const float sw = ImGui::CalcTextSize("Save patch").x + ImGui::GetStyle().FramePadding.x * 2.6f;
        const float right = ImGui::GetContentRegionMax().x;
        ImGui::SameLine();
        if (ImGui::GetCursorPosX() > right - rw - sw - ImGui::GetStyle().ItemSpacing.x) ImGui::NewLine();
        ImGui::SetCursorPos(ImVec2(right - rw - sw - ImGui::GetStyle().ItemSpacing.x,
                                   ImGui::GetCursorPosY() > y0 + 1.0f ? ImGui::GetCursorPosY() : y0));
        if (ui::SecondaryButton(droneDirty_ ? "Save patch *###Save patch" : "Save patch###Save patch", sw))
            droneSavePatch(saveTarget);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Writes %s - the patch is a project asset, so it shows up\n"
                              "in the Asset Browser and reopens from there.",
                              saveTarget.c_str());
        ImGui::SameLine();
        if (droneRendering_) {
            const ImVec2 b0 = ImGui::GetCursorScreenPos();
            const float bh = ImGui::GetFrameHeight();
            if (ImGui::InvisibleButton("Cancel##dronerender", ImVec2(rw, bh))) droneRenderCancel_.store(true);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float r = ImGui::GetStyle().FrameRounding;
            const float f = std::clamp(droneRenderProgress_.load(), 0.0f, 1.0f);
            dl->AddRectFilled(b0, ImVec2(b0.x + rw, b0.y + bh), colA(sem.accent, 0.25f), r);
            dl->AddRectFilled(b0, ImVec2(b0.x + rw * f, b0.y + bh), colA(sem.accent, 1.0f), r);
            char t[48];
            std::snprintf(t, sizeof t, "Rendering %d%%", (int)(f * 100.0f));
            const ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(b0.x + (rw - ts.x) * 0.5f, b0.y + (bh - ts.y) * 0.5f), ImGui::GetColorU32(sem.text), t);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Click to cancel the render.");
        } else {
            if (ui::PrimaryButton("Render WAV", rw)) droneStartRender();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Renders %s (and saves the patch next to it),\n"
                                  "then adds the track to the project's Music list.\n"
                                  "Asks before replacing an existing file.",
                                  fileWav.c_str());
        }
    }
    if (!droneStatus_.empty() || !droneWriteMsg_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", droneStatus_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();  // ##dronemain

    if (openNewAsk) ImGui::OpenPopup(kPopupNewAsk);

    // ---- the popups the buttons above raise -------------------------------
    if (ImGui::BeginPopupModal(kPopupNewAsk, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This patch has unsaved changes.");
        ImGui::TextDisabled("%s", dronePatchRel_.empty() ? "It was never saved."
                                                         : dronePatchRel_.c_str());
        ImGui::Spacing();
        // What waits on the answer: a rail click on another track, or New.
        auto next = [&] {
            droneStop();
            if (!dronePendingLoad_.empty()) droneLoadPatch(dronePendingLoad_);
            else droneNewPatch();
            dronePendingLoad_.clear();
        };
        const char* discard = dronePendingLoad_.empty() ? "Discard and start new" : "Discard and open";
        if (ui::DangerButton(discard, scaled(190.0f))) {
            next();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::PrimaryButton("Save first", scaled(110.0f))) {
            if (droneSavePatch(saveTarget)) next();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton("Cancel", scaled(90.0f))) {
            dronePendingLoad_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Overwrite confirmations. Staged by droneStartRender / droneSavePatch, which
    // refuse to touch an existing file until the answer comes back here.
    if (!droneAskWav_.empty() && !ImGui::IsPopupOpen(kPopupOverwrite))
        ImGui::OpenPopup(kPopupOverwrite);
    if (ImGui::BeginPopupModal(kPopupOverwrite, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s already exists.", droneAskWav_.c_str());
        std::filesystem::path sidecar(droneAskWav_);
        sidecar.replace_extension(".drone");
        std::error_code sec;
        const bool hasPatch =
            std::filesystem::exists(project_.filePath(sidecar.generic_string()), sec);
        ImGui::TextDisabled("Rendering replaces the track%s.",
                            hasPatch ? " and the patch saved beside it" : "");
        ImGui::Spacing();
        if (ImGui::Button("Replace", ImVec2(scaled(120.0f), 0))) {
            const std::string keep = droneAskWav_;
            droneAskWav_.clear();
            (void)keep;
            droneStartRender(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120.0f), 0))) {
            droneAskWav_.clear();
            droneStatus_ = "Render cancelled - the existing track was kept.";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!droneAskPatch_.empty() && !ImGui::IsPopupOpen(kPopupPatchAsk))
        ImGui::OpenPopup(kPopupPatchAsk);
    if (ImGui::BeginPopupModal(kPopupPatchAsk, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s already exists.", droneAskPatch_.c_str());
        ImGui::TextDisabled("It is a different patch than the one you have open.");
        ImGui::Spacing();
        if (ImGui::Button("Replace it", ImVec2(scaled(120.0f), 0))) {
            const std::string target = droneAskPatch_;
            droneAskPatch_.clear();
            droneSavePatch(target, true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120.0f), 0))) {
            droneAskPatch_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (dirty) {
        dronePushParams();
        droneDirty_ = true;  // the document differs from its file
    }
    // Leave nothing armed for whatever draws next: the hook is a per-frame
    // arrangement between this window and its own knobs.
    g_autoHook = AutoWriteHook();
    ImGui::End();
}
