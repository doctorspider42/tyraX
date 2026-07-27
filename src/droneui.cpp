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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <imgui.h>

#include "app.hpp"
#include "platform.hpp"

namespace {

constexpr float kPi = 3.14159265358979f;

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

// A VST-style knob. Vertical drag turns it (Shift = fine, Ctrl = coarse),
// double-click returns it to `def`, and the value is drawn under the dial.
//
// `curve` > 1 spends more travel on the low end of the range, which is what a
// frequency or time knob needs: linear pixels-per-Hz makes everything under
// 1 kHz a single pixel of a 14 kHz sweep. A symmetric range (-x..+x) is drawn
// bipolar, filling from the centre - so pan and mod amounts read at a glance.
// `hook` is false only when knobInt calls this with a scratch float - it writes
// the keyframe itself, from the int it actually owns.
bool knob(const char* label, float* v, float lo, float hi, float def,
          const char* fmt, float scale, float curve = 1.0f,
          const char* tip = nullptr, bool hook = true, bool autoMark = false) {
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

    if (active) {
        const ImGuiIO& io = ImGui::GetIO();
        const float d = -io.MouseDelta.y - io.MouseDelta.x * 0.15f;
        if (d != 0.0f) {
            float sens = 1.0f / (200.0f * scale);
            if (io.KeyShift) sens *= 0.2f;
            if (io.KeyCtrl) sens *= 3.0f;
            *v = fromNorm(toNorm(*v) + d * sens);
            *v = std::max(std::min(lo, hi), std::min(std::max(lo, hi), *v));
            changed = true;
        }
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        *v = def;
        changed = true;
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
    knob(label, &f, (float)lo, (float)hi, (float)def, fmt, scale, 1.0f, tip, false,
         hookAutomated(v));
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
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(22, 24, 28, 255));
    const float t = std::max(0.0f, std::min(1.0f, peak));
    const float h = size.y * t;
    const ImU32 col = t > 0.99f ? IM_COL32(230, 70, 60, 255)
                     : t > 0.8f ? IM_COL32(235, 190, 70, 255)
                                : IM_COL32(120, 200, 130, 255);
    dl->AddRectFilled(ImVec2(p.x, p.y + size.y - h), ImVec2(p.x + size.x, p.y + size.y),
                      col);
    const float y = p.y + size.y * (1.0f - 0.25f);  // ~-12 dBFS
    dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + size.x, y), IM_COL32(90, 94, 104, 255),
                1.0f * scale);
    dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(70, 74, 82, 255));
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
    std::snprintf(droneTrackName_, sizeof(droneTrackName_), "ambient");
    droneStatus_ = "New patch.";
    dronePushParams();
    if (droneLive_) droneLive_->reset();
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

void App::droneSeek(double sec) {
    droneHeadSec_ = std::max(0.0, std::min((double)droneParams_.lengthSec, sec));
    if (droneLive_) droneLive_->seek(droneHeadSec_);
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

// The transport strip: a foobar-style position bar over the whole piece with
// ticks, the elapsed fill, a draggable playhead and the time readout. Clicking
// it seeks the live audition too, so the timeline drives playback rather than
// just reporting it.
void App::drawDroneTimelineBar() {
    const float s = uiScaleApplied_;
    const float len = std::max(1.0f, droneParams_.lengthSec);
    const double head = droneHeadTime();

    const float h = scaled(20.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(scaled(120.0f), ImGui::GetContentRegionAvail().x -
                                                 scaled(96.0f));
    ImGui::InvisibleButton("dronepos", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActive())
        droneSeek((double)((ImGui::GetIO().MousePos.x - p.x) / w) * (double)len);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 br(p.x + w, p.y + h);
    dl->AddRectFilled(p, br, IM_COL32(24, 26, 31, 255), 3.0f * s);
    const float frac = std::max(0.0f, std::min(1.0f, (float)head / len));
    dl->AddRectFilled(p, ImVec2(p.x + w * frac, br.y), IM_COL32(52, 74, 104, 255),
                      3.0f * s);

    // Ticks: bars while they are readable, otherwise a round number of seconds.
    const float barSec = dronegen::barSeconds(droneParams_);
    const bool tickBars = w / (len / barSec) > scaled(16.0f);
    const float step = tickBars ? barSec
                                : (len > 240.0f ? 60.0f : len > 90.0f ? 30.0f : 10.0f);
    for (float t = 0.0f; t <= len; t += step) {
        const float x = p.x + w * (t / len);
        const bool major = tickBars ? (std::fmod(t / barSec, 4.0f) < 0.01f)
                                    : (std::fmod(t, step * 3.0f) < 0.01f);
        dl->AddLine(ImVec2(x, br.y - (major ? h * 0.55f : h * 0.3f)), ImVec2(x, br.y),
                    IM_COL32(96, 102, 114, major ? 220 : 130));
    }
    // Keyframes of every lane, so the bar shows where the piece changes.
    const int lanes = std::min(droneParams_.autoLaneCount, dronegen::kMaxAutoLanes);
    for (int i = 0; i < lanes; ++i) {
        const dronegen::AutoLane& lane = droneParams_.autoLanes[i];
        for (int k = 0; k < lane.count; ++k) {
            const float x = p.x + w * std::min(1.0f, lane.pts[k].t / len);
            dl->AddTriangleFilled(ImVec2(x, p.y), ImVec2(x - 3.0f * s, p.y + 5.0f * s),
                                  ImVec2(x + 3.0f * s, p.y + 5.0f * s),
                                  IM_COL32(232, 168, 62, 200));
        }
    }
    const float px = p.x + w * frac;
    dl->AddLine(ImVec2(px, p.y), ImVec2(px, br.y), IM_COL32(245, 210, 100, 255),
                2.0f * s);
    dl->AddRect(p, br, IM_COL32(70, 74, 82, 255), 3.0f * s);
    if (hovered) ImGui::SetTooltip("Click or drag to move the playhead (seeks the audition)");

    ImGui::SameLine();
    ImGui::Text("%d:%04.1f / %d:%02d", (int)(head / 60.0), std::fmod(head, 60.0),
                (int)(len / 60.0f), (int)std::fmod(len, 60.0f));
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

    ImGui::SetNextWindowSize(ImVec2(scaled(940.0f), scaled(640.0f)),
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

    // ---- top row: preset, seed, transport --------------------------------
    const std::vector<dronegen::Preset>& presets = dronegen::presets();
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::BeginCombo("##preset", presets[dronePreset_].name)) {
        for (int i = 0; i < (int)presets.size(); ++i) {
            if (ImGui::Selectable(presets[i].name, dronePreset_ == i)) {
                dronePreset_ = i;
                const float keepLen = p.lengthSec;
                const int keepRate = p.sampleRate;
                p = presets[i].params;
                p.lengthSec = keepLen;  // a preset is a sound, not a duration
                p.sampleRate = keepRate;
                dronePatchTitle_ = presets[i].name;
                dirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", presets[i].blurb);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("preset");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(110.0f));
    int seed = (int)p.seed;
    if (ImGui::InputInt("##seed", &seed)) {
        p.seed = (uint32_t)std::max(0, seed);
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Roll")) {
        uint32_t x = p.seed ? p.seed : 0x1234567u;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        p.seed = x;
        dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("New random stream: bell placement, drift and noise\n"
                          "change, the patch does not.");
    ImGui::SameLine();
    ImGui::TextDisabled("seed");

    ImGui::SameLine(0.0f, scaled(24.0f));
    if (droneAuditioning_) {
        if (ImGui::Button("Stop", ImVec2(scaled(70.0f), 0))) droneAudition(false);
    } else {
        if (ImGui::Button("Audition", ImVec2(scaled(70.0f), 0))) droneAudition(true);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Plays the patch live through your sound card.\n"
                          "Every knob you turn is heard immediately - this is the\n"
                          "same synthesizer that renders the file.");
    ImGui::SameLine();
    if (ImGui::Button("Restart") && droneLive_) droneLive_->reset();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rewinds the audition to bar 1 (and the arc envelope).");
    if (droneAuditioning_ && droneLive_) {
        ImGui::SameLine();
        const double t = droneLive_->timeSec();
        const float barSec = dronegen::barSeconds(p);
        ImGui::TextDisabled("t %02d:%05.2f  bar %.1f", (int)(t / 60.0),
                            std::fmod(t, 60.0), t / barSec + 1.0);
    } else if (!droneAudioError_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "%s",
                           droneAudioError_.c_str());
    }

    // ---- position bar (the timeline's transport) --------------------------
    drawDroneTimelineBar();

    // ---- display strip: waveform / live scope + spectrum + meters ---------
    {
        const float h = scaled(72.0f);
        ImGui::BeginChild("dronescope", ImVec2(0, h), true,
                          ImGuiWindowFlags_NoScrollbar);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetCursorScreenPos();
        const float availW = ImGui::GetContentRegionAvail().x;
        const float availH = ImGui::GetContentRegionAvail().y;
        const float meterW = scaled(10.0f);
        const float specW = scaled(150.0f);
        const float waveW = std::max(scaled(80.0f), availW - specW - meterW * 2.0f -
                                                        scaled(14.0f));
        const ImVec2 wp = o;
        const ImVec2 wsz(waveW, availH);
        dl->AddRectFilled(wp, ImVec2(wp.x + wsz.x, wp.y + wsz.y),
                          IM_COL32(20, 22, 26, 255));
        const float mid = wp.y + wsz.y * 0.5f;
        dl->AddLine(ImVec2(wp.x, mid), ImVec2(wp.x + wsz.x, mid),
                    IM_COL32(52, 55, 62, 255));

        if (droneAuditioning_ && droneLive_) {
            // Live scope: the last ~90 ms of output, drawn as a line.
            static float buf[dronegen::LiveSynth::kScopeSize];
            const int n = droneLive_->scope(buf, dronegen::LiveSynth::kScopeSize);
            const int step = std::max(1, n / (int)std::max(1.0f, wsz.x));
            ImVec2 prev(wp.x, mid);
            for (int i = 0, col = 0; i < n; i += step, ++col) {
                const float x = wp.x + (float)col * (wsz.x / (float)(n / step + 1));
                const ImVec2 cur(x, mid - buf[i] * wsz.y * 0.48f);
                if (col) dl->AddLine(prev, cur, IM_COL32(120, 210, 150, 220), 1.0f);
                prev = cur;
            }
            // Analyzer: one-pole smoothed log bands (a visualizer, not a
            // measurement - it reads the same shared scope ring).
            for (int b = 0; b < 32; ++b) {
                // crude band energy: bin the scope by frequency via zero-crossing
                // free Goertzel at the band centre
                const float hz = 40.0f * std::pow(2.0f, (float)b * (8.0f / 32.0f));
                const float w = 2.0f * kPi * hz / (float)std::max(8000, p.sampleRate);
                const float cc = 2.0f * std::cos(w);
                float s1 = 0.0f, s2 = 0.0f;
                for (int i = 0; i < n; ++i) {
                    const float s0 = buf[i] + cc * s1 - s2;
                    s2 = s1;
                    s1 = s0;
                }
                float m = std::sqrt(std::fabs(s1 * s1 + s2 * s2 - cc * s1 * s2)) /
                          (float)std::max(1, n);
                m = std::max(0.0f, 1.0f + std::log10(m + 1e-6f) / 3.0f);  // ~-60 dB floor
                droneBands_[b] += (m - droneBands_[b]) * 0.35f;
            }
        } else if (!droneWaveMin_.empty()) {
            const int cols = (int)droneWaveMin_.size();
            for (int x = 0; x < (int)wsz.x; ++x) {
                const int c = std::min(cols - 1, (int)((float)x / wsz.x * (float)cols));
                const float top = mid - droneWaveMax_[c] * wsz.y * 0.48f;
                const float bot = mid - droneWaveMin_[c] * wsz.y * 0.48f;
                dl->AddLine(ImVec2(wp.x + (float)x, top), ImVec2(wp.x + (float)x, bot),
                            IM_COL32(120, 165, 220, 220));
            }
            const float px = wp.x + wsz.x * std::min(1.0f, (float)(droneHeadTime() /
                                    std::max(1.0f, p.lengthSec)));
            dl->AddLine(ImVec2(px, wp.y), ImVec2(px, wp.y + wsz.y),
                        IM_COL32(240, 200, 90, 255), 1.5f * s);
        } else {
            const char* msg = "Audition to see the live output, or render to see the "
                              "finished track";
            dl->AddText(ImVec2(wp.x + scaled(8.0f), mid - ImGui::GetTextLineHeight() * 0.5f),
                        IM_COL32(120, 124, 134, 255), msg);
        }
        dl->AddRect(wp, ImVec2(wp.x + wsz.x, wp.y + wsz.y), IM_COL32(70, 74, 82, 255));

        // spectrum
        const ImVec2 sp(wp.x + wsz.x + scaled(6.0f), wp.y);
        dl->AddRectFilled(sp, ImVec2(sp.x + specW, sp.y + availH), IM_COL32(20, 22, 26, 255));
        for (int b = 0; b < 32; ++b) {
            const float bw = specW / 32.0f;
            const float hh = availH * std::min(1.0f, droneBands_[b]);
            dl->AddRectFilled(ImVec2(sp.x + bw * (float)b, sp.y + availH - hh),
                              ImVec2(sp.x + bw * (float)(b + 1) - 1.0f, sp.y + availH),
                              IM_COL32(90, 150, 220, 220));
        }
        dl->AddRect(sp, ImVec2(sp.x + specW, sp.y + availH), IM_COL32(70, 74, 82, 255));

        // meters
        const float pl = droneLive_ && droneAuditioning_ ? droneLive_->peakL() : 0.0f;
        const float pr = droneLive_ && droneAuditioning_ ? droneLive_->peakR() : 0.0f;
        meterBar(dl, ImVec2(sp.x + specW + scaled(6.0f), wp.y), ImVec2(meterW, availH),
                 pl, s);
        meterBar(dl, ImVec2(sp.x + specW + scaled(8.0f) + meterW, wp.y),
                 ImVec2(meterW, availH), pr, s);

        // click the waveform to move the playhead (a marker for the eye - the
        // audition always plays from its own transport)
        ImGui::InvisibleButton("wavehit", ImVec2(waveW, availH));
        if (ImGui::IsItemActive())
            droneSeek((double)((ImGui::GetIO().MousePos.x - wp.x) / waveW) *
                      (double)p.lengthSec);
        ImGui::EndChild();
    }

    // ---- tabs -------------------------------------------------------------
    ImGui::BeginChild("dronetabs", ImVec2(0, -scaled(78.0f)), false);
    if (ImGui::BeginTabBar("dronetabbar")) {
        // ================= HARMONY ==========================
        if (ImGui::BeginTabItem("Harmony")) {
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

            ImGui::Spacing();
            ImGui::TextDisabled("Root note %s   bar = %.2f s   scale",
                                dronegen::noteName(p.rootNote),
                                (double)dronegen::barSeconds(p));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(130.0f));
            dirty |= ImGui::Combo("##scale", &p.scale, dronegen::scaleNames(),
                                  dronegen::ScaleCount);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Only the bells use the scale; chords are spelled "
                                  "note by note below.");

            groupTitle("Chord progression");
            ImGui::TextDisabled(
                "Semitones from the root, one row per chord. Blank cells (-99) are "
                "unused voices; a layer picks which of the six it plays.");
            int stepCount = p.stepCount;
            ImGui::SetNextItemWidth(scaled(160.0f));
            if (ImGui::SliderInt("Chords", &stepCount, 1, dronegen::kMaxSteps)) {
                p.stepCount = stepCount;
                dirty = true;
            }
            if (ImGui::BeginTable("chords", 3 + dronegen::kMaxChordNotes,
                                  ImGuiTableFlags_SizingFixedFit |
                                      ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("#");
                ImGui::TableSetupColumn("bars");
                for (int i = 0; i < dronegen::kMaxChordNotes; ++i) {
                    char h[8];
                    std::snprintf(h, sizeof(h), "v%d", i + 1);
                    ImGui::TableSetupColumn(h);
                }
                ImGui::TableSetupColumn("notes");
                ImGui::TableHeadersRow();
                for (int i = 0; i < p.stepCount; ++i) {
                    dronegen::Step& st = p.steps[i];
                    ImGui::TableNextRow();
                    ImGui::PushID(i);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", i + 1);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(scaled(58.0f));
                    dirty |= ImGui::DragFloat("##bars", &st.bars, 0.25f, 0.25f, 64.0f,
                                              "%.2g");
                    std::string names;
                    for (int n = 0; n < dronegen::kMaxChordNotes; ++n) {
                        ImGui::TableNextColumn();
                        int v = n < st.count ? st.notes[n] : -99;
                        ImGui::SetNextItemWidth(scaled(44.0f));
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
                        ImGui::PopID();
                        if (n < st.count) {
                            if (!names.empty()) names += " ";
                            names += dronegen::noteName(p.rootNote + st.notes[n]);
                        }
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", names.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            const float pass = dronegen::progressionBars(p) * dronegen::barSeconds(p);
            ImGui::TextDisabled("One pass = %.1f bars = %.1f s.",
                                (double)dronegen::progressionBars(p), (double)pass);
            ImGui::SameLine();
            if (ImGui::SmallButton("Fit length to whole passes")) {
                const int n = std::max(1, (int)std::lround(p.lengthSec / pass));
                p.lengthSec = pass * (float)n;
                dirty = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("A looping track whose length is a whole number of\n"
                                  "passes never lands mid-chord at the loop point.");
            ImGui::EndTabItem();
        }

        // ================= LAYERS ==========================
        if (ImGui::BeginTabItem("Layers")) {
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
                float lv = L.level;
                if (knob("level", &lv, 0.0f, 1.0f, 0.5f, "%.2f", s)) {
                    L.level = lv;
                    dirty = true;
                }
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
            ImGui::EndTabItem();
        }

        // ================= MOTION ==========================
        if (ImGui::BeginTabItem("Motion")) {
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
            ImGui::EndTabItem();
        }

        // ================= AIR & BELLS ==========================
        if (ImGui::BeginTabItem("Air & Bells")) {
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
            ImGui::EndTabItem();
        }

        // ================= SPACE (FX) ==========================
        if (ImGui::BeginTabItem("Space")) {
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
            ImGui::EndTabItem();
        }

        // ================= MASTER ==========================
        if (ImGui::BeginTabItem("Master")) {
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
            ImGui::EndTabItem();
        }

        // ================= TIMELINE ==========================
        if (ImGui::BeginTabItem("Timeline")) {
            drawDroneTimelineTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    // ---- bottom bar: the document, the render, the patch I/O --------------
    ImGui::Separator();
    ImGui::SetNextItemWidth(scaled(150.0f));
    if (ImGui::InputText("Name", droneTrackName_, sizeof(droneTrackName_)))
        droneDirty_ = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Names both files: res/audio/<name>.wav and the\n"
                          "<name>.drone patch beside it.");
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        if (droneDirty_)
            ImGui::OpenPopup("dronenewask");
        else
            droneNewPatch();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open...")) ImGui::OpenPopup("droneopen");
    ImGui::SameLine();
    const std::string saveTarget =
        dronePatchRel_.empty() ? std::string("res/audio/") + droneTrackName_ + ".drone"
                               : dronePatchRel_;
    if (ImGui::Button("Save")) droneSavePatch(saveTarget);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Writes %s - the patch is a project asset, so it shows up\n"
                          "in the Asset Browser and reopens from there.",
                          saveTarget.c_str());
    ImGui::SameLine();
    if (!dronePatchRel_.empty())
        ImGui::TextDisabled("%s%s", dronePatchRel_.c_str(), droneDirty_ ? " *" : "");
    else
        ImGui::TextDisabled("unsaved patch%s", droneDirty_ ? " *" : "");

    ImGui::SameLine(0.0f, scaled(16.0f));
    if (droneRendering_) {
        ImGui::ProgressBar(droneRenderProgress_.load(), ImVec2(scaled(150.0f), 0));
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) droneRenderCancel_.store(true);
    } else {
        if (ImGui::Button("Render WAV", ImVec2(scaled(150.0f), 0))) droneStartRender();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Renders res/audio/<name>.wav (and saves the patch\n"
                              "next to it), then adds the track to the project's\n"
                              "Music list. Asks before replacing an existing file.");
    }
    if (!droneStatus_.empty()) {
        ImGui::SameLine(0.0f, scaled(12.0f));
        ImGui::TextDisabled("%s", droneStatus_.c_str());
    }

    // ---- the popups the buttons above raise -------------------------------
    if (ImGui::BeginPopup("droneopen")) {
        const std::vector<std::string> patches = dronePatchList();
        if (patches.empty()) ImGui::TextDisabled("No .drone patches in this project yet.");
        for (const std::string& rel : patches) {
            const bool open = rel == dronePatchRel_;
            if (ImGui::Selectable(rel.c_str(), open)) {
                droneLoadPatch(rel);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Separator();
        if (ImGui::Selectable("Browse for a file...")) {
            const std::string path = platform::pickFile(
                "Open drone patch", {{"TyraX drone patch (*.drone)", {"*.drone"}}});
            if (!path.empty()) droneLoadPatch(path);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Unsaved patch##dronenewask", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This patch has unsaved changes.");
        ImGui::TextDisabled("%s", dronePatchRel_.empty() ? "It was never saved."
                                                         : dronePatchRel_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Discard and start new", ImVec2(scaled(190.0f), 0))) {
            droneNewPatch();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save first", ImVec2(scaled(110.0f), 0))) {
            droneSavePatch(saveTarget);
            droneNewPatch();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(90.0f), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Overwrite confirmations. Staged by droneStartRender / droneSavePatch, which
    // refuse to touch an existing file until the answer comes back here.
    if (!droneAskWav_.empty() && !ImGui::IsPopupOpen("droneoverwrite"))
        ImGui::OpenPopup("droneoverwrite");
    if (ImGui::BeginPopupModal("Replace this track?##droneoverwrite", nullptr,
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

    if (!droneAskPatch_.empty() && !ImGui::IsPopupOpen("dronepatchask"))
        ImGui::OpenPopup("dronepatchask");
    if (ImGui::BeginPopupModal("Replace this patch?##dronepatchask", nullptr,
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
