// Tools > Drone Generator, the SIMPLE view (the default; docs/drone-generator.md,
// "Simple and Detailed"). The Detailed view (droneui.cpp) is a modular synth:
// four layers, three LFOs, a mod matrix, chords in semitones, thirty effect
// knobs and a timeline. This one asks what a level designer actually wants to
// say: start from a mood, how bright / deep / moving / spacious / windy / busy,
// how it feels, how it changes over the track, how long and how loud - hear it,
// save it, play it in a scene.
//
// Nothing here is stored separately. Every control READS its value back from
// dronegen::Params and WRITES by rescaling the fields the Detailed view edits
// (the math is dronegen::simple, host-only and harness-checked), so the two
// views can never disagree and a .drone file is unchanged.
#include "app.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "uiscript.hpp"
#include "widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace dronegen;

ImU32 hexCol(uint32_t rgb, float a = 1.0f) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (int)(ImClamp(a, 0.0f, 1.0f) * 255.0f));
}

uint32_t lerpHex(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int sh) {
        const float x = (float)((a >> sh) & 255), y = (float)((b >> sh) & 255);
        return (uint32_t)(x + (y - x) * ImClamp(t, 0.0f, 1.0f) + 0.5f) << sh;
    };
    return ch(16) | ch(8) | ch(0);
}

ImU32 semCol(const ImVec4& c, float a = 1.0f) {
    ImVec4 v = c;
    v.w *= a;
    return ImGui::GetColorU32(v);
}

ImVec4 mix4(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

std::string mmss(double sec) {
    if (sec < 0.0) sec = 0.0;
    char b[24];
    std::snprintf(b, sizeof b, "%d:%02d", (int)(sec / 60.0), (int)std::fmod(sec, 60.0));
    return b;
}

int moodByTitle(const std::string& t) {
    const std::vector<Mood>& ms = moods();
    for (int i = 0; i < (int)ms.size(); ++i)
        if (t == ms[(size_t)i].name) return i;
    return -1;
}

// The hero's second sentence, per arc shape.
const char* shapeSentence(int shape) {
    static const char* const s[simple::ShapeCount] = {"Stays even all the way.", "Slowly opens up in the middle.",
                                                       "Rises and falls twice.", "Keeps building to the end."};
    return s[ImClamp(shape, 0, simple::ShapeCount - 1)];
}

const char* shapeCaption(int shape) {
    static const char* const s[simple::ShapeCount] = {
        "Stays the same all the way.",
        "Grows brighter and fuller in the middle, then settles back. Loops cleanly.",
        "Rises and falls twice. Loops cleanly.",
        "Keeps building to the end, then jumps back when it loops - best for a scene that plays it once."};
    return s[ImClamp(shape, 0, simple::ShapeCount - 1)];
}

// A mood's little picture line: drips, wind, pulse, flat, sway, bells,
// sparkles, engine - a hint of the character, not a waveform.
void moodGlyph(ImDrawList* dl, int mood, ImVec2 a, ImVec2 b, ImU32 col) {
    const float w = b.x - a.x, h = b.y - a.y;
    const float mid = a.y + h * 0.58f;
    const int n = 48;
    ImVec2 pts[n + 1];
    auto line = [&](auto f) {
        for (int i = 0; i <= n; ++i) {
            const float t = (float)i / n;
            pts[i] = ImVec2(a.x + w * 0.04f + w * 0.92f * t, mid - f(t) * h);
        }
        dl->AddPolyline(pts, n + 1, col, 0, 1.5f);
    };
    switch (mood) {
        case 0:  // drips: a low hum and three drops falling
            line([](float t) { return 0.05f * std::sin(t * 9.0f); });
            for (int k = 0; k < 3; ++k) {
                const float x = a.x + w * (0.2f + 0.3f * k), top = a.y + h * (0.15f + 0.08f * (k % 2));
                dl->AddLine(ImVec2(x, top), ImVec2(x, top + h * 0.25f), col, 1.0f);
                dl->AddCircleFilled(ImVec2(x, top + h * 0.28f), h * 0.05f + 0.8f, col);
            }
            break;
        case 1:  // wind: a busy, airy line
            line([](float t) {
                return 0.16f * std::sin(t * 23.0f) * std::sin(t * 5.0f + 1.0f) + 0.07f * std::sin(t * 61.0f);
            });
            break;
        case 2:  // pulse: a square wave
            line([](float t) { return std::sin(t * 2.0f * 3.14159f * 11.0f) > 0.0f ? 0.12f : -0.12f; });
            break;
        case 3:  // flat: still
            line([](float t) { return 0.02f * std::sin(t * 6.0f); });
            break;
        case 4:  // sway
            line([](float t) { return 0.2f * std::sin(t * 2.0f * 3.14159f * 2.0f); });
            break;
        case 5:  // bells: a slow line and two rings
            line([](float t) { return 0.08f * std::sin(t * 7.0f); });
            for (int k = 0; k < 2; ++k) {
                const ImVec2 c(a.x + w * (0.25f + 0.4f * k), a.y + h * 0.28f);
                dl->AddCircle(c, h * 0.13f, col, 16, 1.0f);
                dl->AddCircleFilled(c, h * 0.05f, col);
            }
            break;
        case 6:  // sparkles
            line([](float t) { return 0.12f * std::sin(t * 2.0f * 3.14159f * 1.5f); });
            for (int k = 0; k < 4; ++k) {
                const ImVec2 c(a.x + w * (0.12f + 0.25f * k), a.y + h * (0.2f + 0.08f * (k % 2)));
                const float r = h * 0.1f;
                dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.3f, c.y), ImVec2(c.x - r * 0.3f, c.y), col);
                dl->AddTriangleFilled(ImVec2(c.x, c.y + r), ImVec2(c.x - r * 0.3f, c.y), ImVec2(c.x + r * 0.3f, c.y), col);
                dl->AddTriangleFilled(ImVec2(c.x - r, c.y), ImVec2(c.x, c.y - r * 0.3f), ImVec2(c.x, c.y + r * 0.3f), col);
                dl->AddTriangleFilled(ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r * 0.3f), ImVec2(c.x, c.y - r * 0.3f), col);
            }
            break;
        default:  // engine: short regular pulses on a baseline
            line([](float t) { return std::fmod(t * 9.0f, 1.0f) < 0.3f ? 0.1f : 0.0f; });
            break;
    }
}

// The mood picture: its gradient, a hairline frame, the glyph.
void moodPicture(ImDrawList* dl, const Mood& m, int index, ImVec2 a, ImVec2 b, float r) {
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32_WHITE, r);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, a, ImVec2(a.x, b.y), hexCol(m.top),
                                                  hexCol(m.bottom));
    dl->AddRect(a, b, hexCol(m.line, 0.25f), r);
    moodGlyph(dl, index, a, b, hexCol(m.line, 0.95f));
}

// A mood tile: picture, name, and (wide) the two-word caption. `id` is the
// InvisibleButton (##drn_mood_N); the name is drawn, the label is the ID.
bool moodTile(const char* id, int index, bool selected, float w, float picH, bool caption) {
    const theme::Semantics& sem = theme::semantics();
    const Mood& m = moods()[(size_t)index];
    const float fs = ImGui::GetFontSize();
    const float textH = fs * 1.25f + (caption ? fs * 1.05f : 0.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, picH + textH));
    uiscript::markLastItemChecked(selected);
    const bool hov = ImGui::IsItemHovered();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("%s\nReplaces the whole sound; the length and the file name stay.", m.caption);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding + 2.0f;
    moodPicture(dl, m, index, p, ImVec2(p.x + w, p.y + picH), r);
    if (selected)
        dl->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + w + 2, p.y + picH + 2), semCol(sem.accent), r + 2, 0, 2.0f);
    else if (hov)
        dl->AddRect(p, ImVec2(p.x + w, p.y + picH), semCol(sem.text, 0.6f), r, 0, 1.0f);
    dl->PushClipRect(p, ImVec2(p.x + w, p.y + picH + textH), true);
    {
        ui::FontScope bold(ui::boldFont());
        dl->AddText(ImVec2(p.x + 1.0f, p.y + picH + fs * 0.3f), semCol(sem.text), m.name);
    }
    if (caption)
        dl->AddText(ImGui::GetFont(), fs * 0.86f, ImVec2(p.x + 1.0f, p.y + picH + fs * 1.4f), semCol(sem.textDim),
                    m.tile);
    dl->PopClipRect();
    return clicked;
}

// A vertical level meter.
void meterV(ImDrawList* dl, ImVec2 a, ImVec2 b, float peak) {
    const theme::Semantics& sem = theme::semantics();
    dl->AddRectFilled(a, b, semCol(sem.field), 2.0f);
    // -48..0 dBFS on the bar - a quiet drone still moves it.
    const float db = 20.0f * std::log10(std::max(1e-5f, peak));
    const float t = ImClamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
    if (t <= 0.0f) return;
    const ImVec4 c = peak > 0.97f ? sem.danger : peak > 0.8f ? sem.warn : sem.ok;
    dl->AddRectFilled(ImVec2(a.x, b.y - (b.y - a.y) * t), b, semCol(c), 2.0f);
}

// One Play Music node that plays this track.
struct TrackUse {
    int scene = -1, object = -1, node = -1;
    std::string text;  // " · starts with the level · loops · volume 80"
    int volume = 80;
};

// Every Play Music node across the project whose track is `wav`, with how it
// is started (the trigger at the head of its exec chain).
std::vector<TrackUse> scanUses(const Project& pr, const std::string& wav) {
    std::vector<TrackUse> out;
    for (int si = 0; si < (int)pr.scenes.size(); ++si) {
        const SceneData& sc = pr.scenes[(size_t)si];
        for (int oi = 0; oi < (int)sc.objects.size(); ++oi) {
            const FlowGraph& g = sc.objects[(size_t)oi].flowGraph;
            for (const FlowNode& n : g.nodes) {
                if (n.type != "PlayMusic" || n.str != wav) continue;
                TrackUse u;
                u.scene = si;
                u.object = oi;
                u.node = n.id;
                u.volume = (int)std::lround(n.num[0]);
                // Walk the exec chain back to its trigger (a few hops at most).
                std::string how = " · not started by anything";
                int cur = n.id;
                for (int hop = 0; hop < 16; ++hop) {
                    const FlowLink* in = nullptr;
                    for (const FlowLink& l : g.links)
                        if (l.kind == FlowLinkExec && l.toNode == cur) {
                            in = &l;
                            break;
                        }
                    if (!in) break;
                    const FlowNode* from = nullptr;
                    for (const FlowNode& m : g.nodes)
                        if (m.id == in->fromNode) from = &m;
                    if (!from) break;
                    const FlowNodeType* t = flowNodeType(from->type);
                    if (from->type == "OnStart") {
                        how = " · starts with the level";
                        break;
                    }
                    if (t && t->trigger) {
                        how = std::string(" · on ") + t->title;
                        break;
                    }
                    cur = from->id;
                }
                u.text = how;
                if (n.num[1] != 0.0f) u.text += " · loops";
                u.text += " · volume " + std::to_string(u.volume);
                out.push_back(std::move(u));
            }
        }
    }
    return out;
}

const char* const kSliderLabel[simple::SlCount] = {"Brightness", "Pitch", "Movement", "Space", "Wind", "Chimes"};
const char* const kSliderId[simple::SlCount] = {"##drn_bright", "##drn_pitch", "##drn_move",
                                                 "##drn_space", "##drn_wind", "##drn_chimes"};
const char* const kSliderTip[simple::SlCount] = {
    "Dark = muffled, as if behind a wall. Bright = clear and airy.",
    "How low the hum sits. Deep is felt more than heard on a TV.",
    "How much the sound changes by itself while it plays.",
    "Small room to endless canyon: how long the echo hangs.",
    "A bed of air noise under the hum.",
    "Occasional notes on top: drips, bells, plucks. Zero turns them off."};

}  // namespace

float App::drawDroneViewSwitch(float right, float y, bool draw) {
    static const char* const kViews[] = {"Simple", "Detailed"};
    const float segW = ImGui::CalcTextSize("SimpleDetailed").x + ImGui::GetFontSize() * 3.2f;
    if (!draw) return segW;
    const ImVec2 back = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(right - segW, y));
    int view = droneDetailed_ ? 1 : 0;
    if (ui::Segmented("##drone_view", kViews, 2, &view, segW)) {
        droneDetailed_ = view == 1;
        droneDragSlider_ = -1;
        saveGlobalConfig();
    }
    ImGui::SetCursorPos(back);
    return segW;
}

// Adds On Start -> Play Music(this track) to `object` of `scene` (or a new
// "Level music" empty there), or retargets an existing Play Music. One
// commitChange for the whole edit.
void App::droneUseInScene(int scene, int object, int replaceNode, bool createObject) {
    if (scene < 0 || scene >= (int)project_.scenes.size()) return;
    const std::string wav = std::string("res/audio/") + droneTrackName_ + ".wav";
    SceneData& sc = project_.scenes[(size_t)scene];
    if (createObject) {
        std::string name = "Level music";
        for (int k = 2;; ++k) {
            bool taken = false;
            for (const SceneObject& o : sc.objects) taken |= o.name == name;
            if (!taken) break;
            name = "Level music " + std::to_string(k);
        }
        SceneObject o;  // the addEmpty() marker, in a scene that may not be active
        o.name = name;
        o.type = PrimitiveType::Empty;
        o.position[1] = 1.0f;
        o.scale[0] = o.scale[1] = o.scale[2] = 0.5f;
        o.color[0] = o.color[1] = o.color[2] = 0.75f;
        o.collisionMode = 2;
        sc.objects.push_back(o);
        object = (int)sc.objects.size() - 1;
    }
    if (object < 0 || object >= (int)sc.objects.size()) return;
    FlowGraph& g = sc.objects[(size_t)object].flowGraph;
    if (replaceNode >= 0) {
        for (FlowNode& n : g.nodes)
            if (n.id == replaceNode) n.str = wav;
    } else {
        float maxY = 0.0f;
        for (const FlowNode& n : g.nodes) maxY = std::max(maxY, n.pos[1]);
        const FlowNode* start = nullptr;
        for (const FlowNode& n : g.nodes)
            if (n.type == "OnStart") {
                start = &n;
                break;
            }
        int startId;
        float sx, sy;
        if (start) {
            startId = start->id;
            sx = start->pos[0];
            sy = start->pos[1];
        } else {
            FlowNode s;
            s.id = g.nextId++;
            s.type = "OnStart";
            s.pos[0] = 40.0f;
            s.pos[1] = g.nodes.empty() ? 40.0f : maxY + 160.0f;
            g.nodes.push_back(s);
            startId = s.id;
            sx = s.pos[0];
            sy = s.pos[1];
        }
        FlowNode m;
        m.id = g.nextId++;
        m.type = "PlayMusic";
        m.str = wav;
        m.num[0] = 80.0f;  // volume
        m.num[1] = 1.0f;   // loop
        m.pos[0] = sx + 260.0f;
        m.pos[1] = start ? std::max(sy, maxY + 160.0f) : sy;
        g.nodes.push_back(m);
        FlowLink l;
        l.id = g.nextId++;
        l.fromNode = startId;
        l.toNode = m.id;
        l.kind = FlowLinkExec;
        g.links.push_back(l);
    }
    bool known = false;
    for (const std::string& mu : project_.music) known |= mu == wav;
    if (!known) project_.music.push_back(wav);
    commitChange();
    droneStatus_ = "Scene " + sc.name + " plays " + wav + " when it starts (" + sc.objects[(size_t)object].name + ").";
}

void App::drawDroneSimple(bool& dirty, bool& openNewAsk) {
    const theme::Semantics& sem = theme::semantics();
    dronegen::Params& p = droneParams_;
    const float fs = ImGui::GetFontSize();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const ImGuiIO& io = ImGui::GetIO();
    const bool wide = ImGui::GetContentRegionAvail().x >= scaled(900.0f);
    const float len = std::max(1.0f, p.lengthSec);

    // ---- what is on disk, and where it plays ----------------------------------
    const std::string wavRel = std::string("res/audio/") + droneTrackName_ + ".wav";
    const std::string droneRel = std::string("res/audio/") + droneTrackName_ + ".drone";
    bool wavExists = false, droneExists = false;
    {
        std::error_code ec;
        wavExists = std::filesystem::exists(project_.filePath(wavRel), ec);
        droneExists = std::filesystem::exists(project_.filePath(droneRel), ec);
    }
    const DroneTrackRow* fileRow = nullptr;
    for (const DroneTrackRow& t : droneTracks_)
        if (t.rel == droneRel) fileRow = &t;
    bool inMusic = false;
    for (const std::string& m : project_.music) inMusic |= m == wavRel;
    const std::vector<TrackUse> uses = scanUses(project_, wavRel);
    std::vector<std::string> useScenes;
    for (const TrackUse& u : uses) {
        const std::string& n = project_.scenes[(size_t)u.scene].name;
        if (std::find(useScenes.begin(), useScenes.end(), n) == useScenes.end()) useScenes.push_back(n);
    }

    // ---- which mood this is ---------------------------------------------------
    int moodSel = moodByTitle(dronePatchTitle_);
    if (moodSel < 0) moodSel = droneMoodFrom_;
    const bool fromMood = moodSel >= 0 && moodSel < (int)moods().size();
    const bool edited = fromMood && !simple::sameAsMood(p, moodSel);
    if (droneLoadRFor_.empty()) {
        for (int s = 0; s < simple::SlCount; ++s) droneLoadR_[s] = simple::read(p, s);
        droneLoadRFor_ = "loaded";
    }

    // ---- transport -------------------------------------------------------------
    auto togglePlay = [&] {
        if (droneAuditioning_) {
            droneStop();
            return;
        }
        // The Simple view only has Sound design: loop past the end, write nothing.
        droneMode_ = 0;
        droneWriteArmed_ = false;
        dronePlay(false);
    };
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput &&
        !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Space, false))
        togglePlay();
    const double head = droneAuditioning_ && droneLive_ ? std::fmod(droneLive_->timeSec(), (double)len)
                                                        : std::min((double)len, droneHeadSec_);
    // Live waveform history: one peak per column as playback passes it.
    if (droneAuditioning_ && droneLive_) {
        constexpr int kCols = 600;
        if ((int)droneLiveHist_.size() != kCols) droneLiveHist_.assign(kCols, -1.0f);
        float buf[512];
        const int n = droneLive_->scope(buf, 512);
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = std::max(pk, std::fabs(buf[i]));
        const int col = ImClamp((int)(head / len * kCols), 0, kCols - 1);
        droneLiveHist_[(size_t)col] = pk;
    }

    // ---- title row: heading, Simple | Detailed ---------------------------------
    {
        const float y0 = ImGui::GetCursorPosY();
        ui::Heading(wide ? "Drone Generator" : "Drone", wide ? "background music for your levels" : nullptr);
        const float after = ImGui::GetCursorPosY();
        drawDroneViewSwitch(ImGui::GetContentRegionMax().x, y0 - scaled(2.0f));
        ImGui::SetCursorPosY(std::max(after, y0 + ImGui::GetFrameHeight() + scaled(4.0f)));
    }

    // ---- track bar ---------------------------------------------------------------
    {
        const std::string fileName = std::string(droneTrackName_) + ".drone";
        const std::string title = dronePatchTitle_.empty() ? std::string("Untitled") : dronePatchTitle_;
        ImGui::AlignTextToFramePadding();
        if (wide) {
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::TextUnformatted("Track");
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }
        // Swatch = the colour of the mood the title names.
        {
            const ImVec2 sp = ImGui::GetCursorScreenPos();
            const float d = ImGui::GetFrameHeight() * 0.55f;
            const float y = sp.y + (ImGui::GetFrameHeight() - d) * 0.5f;
            const int mi = moodByTitle(dronePatchTitle_);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (mi >= 0) moodPicture(dl, moods()[(size_t)mi], -1, ImVec2(sp.x, y), ImVec2(sp.x + d, y + d), 3.0f);
            else dl->AddRectFilled(ImVec2(sp.x, y), ImVec2(sp.x + d, y + d), semCol(sem.surface), 3.0f);
            ImGui::Dummy(ImVec2(d, ImGui::GetFrameHeight()));
            ImGui::SameLine(0.0f, scaled(6.0f));
        }
        const std::string preview = title + "   " + fileName;
        ImGui::SetNextItemWidth(wide ? scaled(250.0f) : ImMax(scaled(140.0f), ImGui::GetContentRegionAvail().x -
                                                                                scaled(170.0f)));
        if (ImGui::BeginCombo("##drn_track", preview.c_str())) {
            for (const DroneTrackRow& t : droneTracks_) {
                const bool sel = t.rel == dronePatchRel_;
                const std::string stem = std::filesystem::path(t.rel).filename().string();
                const std::string row = (t.title.empty() ? stem : t.title) + "   " + stem + "##" + t.rel;
                if (ImGui::Selectable(row.c_str(), sel) && !sel) {
                    if (droneDirty_) {
                        dronePendingLoad_ = t.rel;
                        openNewAsk = true;
                    } else {
                        droneStop();
                        droneLoadPatch(t.rel);
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::Selectable("New track##drn_tracknew")) {
                dronePendingLoad_.clear();
                if (droneDirty_) openNewAsk = true;
                else {
                    droneStop();
                    droneNewPatch();
                }
            }
            if (ImGui::Selectable("Open file...##drn_trackopen")) {
                const std::string path = platform::pickFile(
                    "Open drone patch", {{"TyraX drone patch (*.drone)", {"*.drone"}}});
                if (!path.empty()) droneLoadPatch(path);
            }
            ImGui::EndCombo();
        }
        // Where it plays: one scene, several, or nothing.
        if (!useScenes.empty() && wide) {
            ImGui::SameLine(0.0f, scaled(10.0f));
            const std::string chip = useScenes.size() == 1
                                         ? "Plays in scene " + useScenes[0] + "##drn_plays"
                                         : "Plays in " + std::to_string(useScenes.size()) + " scenes##drn_plays";
            if (ui::ChipButton(chip.c_str(), true)) droneScrollToGame_ = true;
            if (useScenes.size() > 1 && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                std::string all;
                for (const std::string& n : useScenes) all += (all.empty() ? "" : "\n") + n;
                ImGui::SetTooltip("%s", all.c_str());
            }
        }
        // Status and "..." at the right end.
        int kind = 4;
        const char* state = "Not saved";
        if (droneRendering_) {
            kind = 3;
            state = "Saving...";
        } else if (droneExists) {
            const bool changed = droneDirty_ || !wavExists || (fileRow && fileRow->newerThanWav);
            kind = changed ? 1 : 0;
            state = changed ? "Changed since last save" : "Saved";
        } else if (droneDirty_) {
            kind = 4;
            state = "Not saved";
        }
        const float moreS = ImGui::GetFrameHeight();
        const float sw = ImGui::CalcTextSize(state).x + fs * 1.4f;
        const float right = ImGui::GetContentRegionMax().x;
        ImGui::SameLine();
        if (ImGui::GetCursorPosX() < right - sw - moreS - gap) {
            const float y = ImGui::GetCursorPosY();
            ImGui::SetCursorPos(ImVec2(right - sw - moreS - gap, y + ImGui::GetStyle().FramePadding.y));
            ui::StatusLine(kind, state);
            ImGui::SameLine();
            ImGui::SetCursorPosY(y);
        }
        ImGui::SetCursorPosX(right - moreS);
        if (ui::MoreButton("Track actions##drn_more", moreS)) ImGui::OpenPopup("##drn_more");
        if (ImGui::BeginPopup("##drn_more")) {
            if (ImGui::MenuItem("New track")) {
                dronePendingLoad_.clear();
                if (droneDirty_) openNewAsk = true;
                else {
                    droneStop();
                    droneNewPatch();
                }
            }
            if (ImGui::MenuItem("Open file...")) {
                const std::string path = platform::pickFile(
                    "Open drone patch", {{"TyraX drone patch (*.drone)", {"*.drone"}}});
                if (!path.empty()) droneLoadPatch(path);
            }
            ImGui::Separator();
            ImGui::TextDisabled("File name (res/audio/<name>.wav)");
            ImGui::SetNextItemWidth(scaled(200.0f));
            if (ImGui::InputText("##drn_file", droneTrackName_, sizeof(droneTrackName_))) droneDirty_ = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Names both files Save to project writes; the old ones stay where they are.");
            if (ImGui::MenuItem("Duplicate")) {
                // The same patch under a fresh file name: Save writes a new pair.
                const std::string n = droneUniqueTrackName(std::string(droneTrackName_) + "-copy");
                std::snprintf(droneTrackName_, sizeof(droneTrackName_), "%s", n.c_str());
                dronePatchRel_.clear();
                droneDirty_ = true;
                droneStatus_ = "Duplicated as " + n + " - Save to project writes it.";
            }
            ImGui::EndPopup();
        }
    }
    ImGui::Dummy(ImVec2(0.0f, scaled(4.0f)));

    // ---- footer size (fixed at the bottom) --------------------------------------
    const float footerH = wide ? ImGui::GetFrameHeight() * 1.5f + scaled(10.0f)
                               : ImGui::GetTextLineHeightWithSpacing() * 3.0f + ImGui::GetFrameHeight() + scaled(12.0f);
    const float bodyH = std::max(scaled(120.0f), ImGui::GetContentRegionAvail().y - footerH);

    auto LW = [this] {
        return IM_TRUNC(ImClamp(ImGui::GetContentRegionAvail().x * 0.24f, scaled(84.0f), scaled(128.0f)));
    };
    auto dimWrapped = [&](const char* text, const ImVec4* col = nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, col ? *col : sem.textDim);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    };

    // ======== sections, as lambdas so both layouts order them freely ========

    // Hero: the picture of what you hear.
    auto hero = [&](float heroH) {
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const ImVec2 b(a.x + w, a.y + heroH);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = ImGui::GetStyle().FrameRounding + 4.0f;
        const float bright = simple::read(p, simple::SlBright);
        uint32_t top = 0x1E2A45, bottom = 0x06080E;
        if (fromMood) top = moods()[(size_t)moodSel].top, bottom = moods()[(size_t)moodSel].bottom;
        top = lerpHex(top, 0xFFE1A8, 0.25f * bright);
        {
            const int v0 = dl->VtxBuffer.Size;
            dl->AddRectFilled(a, b, IM_COL32_WHITE, r);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, a,
                                                          ImVec2(a.x + w * 0.35f, b.y), hexCol(top), hexCol(bottom));
            dl->AddRect(a, b, semCol(sem.border), r);
        }
        const ImU32 white = IM_COL32(240, 243, 250, 255), whiteDim = IM_COL32(220, 226, 236, 190);
        const float pad = wide ? fs * 1.3f : fs * 0.9f;

        // Title (inline rename) and the sentence under it.
        ImGui::SetCursorScreenPos(ImVec2(a.x + pad - 1.0f, a.y + pad * 0.7f));
        if (ImGui::GetActiveID() != ImGui::GetID("##drn_title"))
            std::snprintf(droneTitleBuf_, sizeof droneTitleBuf_, "%s", dronePatchTitle_.c_str());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(white));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.0f, 0.0f));
        ImGui::PushFont(ui::boldFont(), fs * (wide ? 1.55f : 1.25f));
        const float titleW = ImMin(w * 0.55f, ImGui::CalcTextSize(droneTitleBuf_[0] ? droneTitleBuf_ : "Untitled").x +
                                                  scaled(20.0f));
        ImGui::SetNextItemWidth(ImMax(scaled(90.0f), titleW));
        if (ImGui::InputTextWithHint("##drn_title", "Untitled", droneTitleBuf_, sizeof droneTitleBuf_)) {
            dronePatchTitle_ = droneTitleBuf_;
            droneDirty_ = true;
        }
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("The track's title - click to rename (saved in the .drone).");
        const float titleBottom = ImGui::GetItemRectMax().y;
        bool shapeExact = true;
        const int shape = simple::readShape(p, &shapeExact);
        std::string sentence = fromMood ? std::string(moods()[(size_t)moodSel].caption) + " " : std::string();
        sentence += shapeSentence(shape);
        dl->PushClipRect(a, b, true);
        dl->AddText(ImGui::GetFont(), fs * 0.95f, ImVec2(a.x + pad, titleBottom + fs * 0.2f), whiteDim,
                    sentence.c_str());
        // "from Dark cave · edited"
        if (fromMood) {
            const std::string chip = std::string("from ") + moods()[(size_t)moodSel].name + (edited ? " · edited" : "");
            const ImVec2 ts = ImGui::CalcTextSize(chip.c_str());
            const ImVec2 c0(b.x - pad - ts.x - fs, a.y + pad * 0.7f);
            if (c0.x > a.x + pad + titleW + fs) {
                dl->AddRectFilled(c0, ImVec2(b.x - pad, c0.y + ts.y + fs * 0.6f), IM_COL32(8, 10, 16, 140), fs);
                dl->AddText(ImVec2(c0.x + fs * 0.5f, c0.y + fs * 0.3f), whiteDim, chip.c_str());
            }
        }
        dl->PopClipRect();

        // Waveform band.
        const float ctrlH = wide ? scaled(56.0f) : scaled(44.0f);
        const float bandTop = titleBottom + fs * (wide ? 2.6f : 1.6f);
        const float bandBot = b.y - pad * 0.6f - ctrlH - fs * (wide ? 1.0f : 0.5f);
        if (bandBot - bandTop > fs) {
            const ImVec2 b0(a.x, bandTop), b1(b.x, bandBot);
            ImGui::SetCursorScreenPos(b0);
            ImGui::InvisibleButton("##drn_wave", ImVec2(w, bandBot - bandTop));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Click or drag to move the playhead.");
            if (ImGui::IsItemActive()) {
                const float t = ImClamp((io.MousePos.x - b0.x) / w, 0.0f, 1.0f);
                droneSeek((double)t * len, false);
            }
            if (ImGui::IsItemDeactivated()) droneSeek(droneHeadSec_, true);
            const bool useWav = wavExists && !droneDirty_ && !(fileRow && fileRow->newerThanWav) &&
                                !droneWaveMin_.empty();
            const float pitch = scaled(3.0f);
            const int bars = std::max(8, (int)(w / pitch));
            const float midY = (bandTop + bandBot) * 0.5f, half = (bandBot - bandTop) * 0.5f;
            float norm = 0.05f;
            if (useWav)
                for (size_t i = 0; i < droneWaveMin_.size(); ++i)
                    norm = std::max(norm, std::max(-droneWaveMin_[i], droneWaveMax_[i]));
            else
                for (float v : droneLiveHist_) norm = std::max(norm, v);
            const float headX = b0.x + (float)(head / len) * w;
            for (int i = 0; i < bars; ++i) {
                const float t = (i + 0.5f) / bars;
                float amp = -1.0f;
                if (useWav) {
                    const size_t k = std::min(droneWaveMin_.size() - 1, (size_t)(t * droneWaveMin_.size()));
                    amp = (droneWaveMax_[k] - droneWaveMin_[k]) * 0.5f;
                } else if (!droneLiveHist_.empty()) {
                    const size_t k = std::min(droneLiveHist_.size() - 1, (size_t)(t * droneLiveHist_.size()));
                    amp = droneLiveHist_[k];
                }
                const float x = b0.x + t * w;
                const float hh = amp < 0.0f ? 1.0f : std::max(1.0f, half * 0.92f * std::min(1.0f, amp / norm));
                const bool played = x < headX;
                const ImU32 c = amp < 0.0f ? IM_COL32(200, 210, 230, 40)
                                           : played ? semCol(sem.accent, 0.95f) : IM_COL32(200, 210, 230, 95);
                dl->AddRectFilled(ImVec2(x - pitch * 0.33f, midY - hh), ImVec2(x + pitch * 0.33f, midY + hh), c);
            }
            // The arc: how full it gets over the track (only when it drives something).
            if (simple::arcActive(p)) {
                const ImU32 ac = semCol(sem.warn, 0.85f);
                ImVec2 prev;
                const int segs = 64;
                for (int i = 0; i <= segs; ++i) {
                    const float t = (float)i / segs;
                    const float f = t * (kArcPoints - 1);
                    const int k = std::min((int)f, kArcPoints - 2);
                    const float v = ImClamp(p.arc[k] + (p.arc[k + 1] - p.arc[k]) * (f - k), 0.0f, 1.0f);
                    const ImVec2 q(b0.x + t * w, bandBot - v * (bandBot - bandTop));
                    if (i > 0 && (i % 2) == 1) dl->AddLine(prev, q, ac, 1.2f);
                    prev = q;
                }
                if (wide) {
                    const char* leg = "how full it gets over the track";
                    const ImVec2 ts = ImGui::CalcTextSize(leg);
                    const float lx = b.x - pad - ts.x, ly = bandTop - ts.y - fs * 0.2f;
                    dl->AddLine(ImVec2(lx - fs * 1.6f, ly + ts.y * 0.5f), ImVec2(lx - fs * 0.4f, ly + ts.y * 0.5f),
                                ac, 1.2f);
                    dl->AddText(ImGui::GetFont(), fs * 0.86f, ImVec2(lx, ly), ac, leg);
                }
            }
            dl->AddLine(ImVec2(headX, bandTop - fs * 0.3f), ImVec2(headX, bandBot + fs * 0.3f), IM_COL32_WHITE, 2.0f);
        }

        // Bottom row: Play / Stop, time, loop note, meters.
        const float rowY = b.y - pad * 0.6f - ctrlH;
        {
            const ImVec2 c0(a.x + pad, rowY);
            ImGui::SetCursorScreenPos(c0);
            const char* lbl = droneAuditioning_ ? "Stop##drn_play" : "Play##drn_play";
            if (ImGui::InvisibleButton(lbl, ImVec2(ctrlH, ctrlH))) togglePlay();
            uiscript::markLastItemChecked(droneAuditioning_);
            const bool hov = ImGui::IsItemHovered();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(droneAuditioning_ ? "Stop (Space)" : "Play from the playhead; it loops (Space)");
            const ImVec2 c(c0.x + ctrlH * 0.5f, c0.y + ctrlH * 0.5f);
            const float k = ctrlH * 0.18f;
            if (droneAuditioning_) {
                dl->AddCircleFilled(c, ctrlH * 0.5f, semCol(sem.text, hov ? 1.0f : 0.9f), 40);
                dl->AddRectFilled(ImVec2(c.x - k * 0.8f, c.y - k * 0.8f), ImVec2(c.x + k * 0.8f, c.y + k * 0.8f),
                                  semCol(sem.ground), 2.0f);
            } else {
                dl->AddCircleFilled(c, ctrlH * 0.5f, semCol(sem.ok, hov ? 1.0f : 0.88f), 40);
                dl->AddTriangleFilled(ImVec2(c.x - k * 0.6f, c.y - k), ImVec2(c.x - k * 0.6f, c.y + k),
                                      ImVec2(c.x + k, c.y), semCol(sem.ground));
            }
            // Time.
            const float tx = c0.x + ctrlH + fs * 0.9f;
            const std::string now = mmss(head), total = " / " + mmss(len);
            {
                ui::FontScope mono(ui::monoFont());
                const float big = fs * (wide ? 1.4f : 1.2f);
                const float ty = wide ? rowY + ctrlH * 0.12f : rowY + (ctrlH - big) * 0.5f;
                dl->AddText(ImGui::GetFont(), big, ImVec2(tx, ty), white, now.c_str());
                const float nw = ImGui::GetFont()->CalcTextSizeA(big, FLT_MAX, 0.0f, now.c_str()).x;
                dl->AddText(ImGui::GetFont(), fs * 0.95f, ImVec2(tx + nw, ty + big - fs * 1.0f), whiteDim, total.c_str());
            }
            const char* loop = p.master.loopSeamless ? (wide ? "Loops forever in the game, no gap" : "loops")
                                                     : (wide ? "Plays once, then stops" : "once");
            const float meterW = scaled(5.0f), meterGap = scaled(3.0f);
            const float mx = b.x - pad - meterW * 2.0f - meterGap;
            if (wide) {
                dl->AddText(ImGui::GetFont(), fs * 0.9f, ImVec2(tx, rowY + ctrlH * 0.62f), whiteDim, loop);
            } else {
                const ImVec2 ts = ImGui::CalcTextSize(loop);
                dl->AddText(ImVec2(mx - fs * 0.8f - ts.x, rowY + (ctrlH - ts.y) * 0.5f), whiteDim, loop);
            }
            if (!droneAudioError_.empty() && !droneAuditioning_)
                dl->AddText(ImGui::GetFont(), fs * 0.85f, ImVec2(tx, rowY - fs * 1.1f), semCol(sem.warn),
                            droneAudioError_.c_str());
            const float pl = droneAuditioning_ && droneLive_ ? droneLive_->peakL() : 0.0f;
            const float pr = droneAuditioning_ && droneLive_ ? droneLive_->peakR() : 0.0f;
            meterV(dl, ImVec2(mx, rowY + ctrlH * 0.15f), ImVec2(mx + meterW, rowY + ctrlH * 0.85f), pl);
            meterV(dl, ImVec2(mx + meterW + meterGap, rowY + ctrlH * 0.15f),
                   ImVec2(mx + meterW * 2.0f + meterGap, rowY + ctrlH * 0.85f), pr);
        }
        ImGui::SetCursorScreenPos(a);
        ImGui::Dummy(ImVec2(w, heroH));
    };

    // Over the track: the arc as four shapes.
    auto overTrack = [&] {
        ui::Caption("Over the track");
        bool exact = true;
        const int shape = simple::readShape(p, &exact);
        const bool canShape = simple::arcActive(p) || simple::freeModRows(p) > 0;
        int sel = exact ? shape : -1;
        bool hov = false;
        const float segW = wide ? ImMin(ImGui::GetContentRegionAvail().x * 0.48f, scaled(310.0f)) : -1.0f;
        if (ui::SegmentedApprox("##drn_shape", simple::shapeNames(), simple::ShapeCount, &sel, exact ? -1 : shape,
                                segW, canShape, &hov)) {
            if (simple::applyShape(p, sel)) dirty = true;
        }
        if (hov)
            ImGui::SetTooltip(canShape ? "How full the track gets from start to end (the arc)."
                                       : "All 6 motion slots are in use - Detailed view > Motion");
        std::string cap = exact ? std::string() : std::string("Close to: ");
        cap += shapeCaption(shape);
        if (wide) {
            ImGui::SameLine(0.0f, fs);
            ImGui::BeginGroup();
        }
        const ImVec4 amber = sem.warn;
        dimWrapped(cap.c_str(), shape == simple::ShapeBuildUp ? &amber : nullptr);
        if (wide) ImGui::EndGroup();
    };

    // Start from a mood: 4 x 2 tiles, the undo toast, Another take.
    auto moodGrid = [&](bool narrow) {
        ui::Caption("Start from a mood");
        const float aw = ImGui::GetContentRegionAvail().x;
        const float tw = ImFloor((aw - gap * 3.0f) / 4.0f);
        const float picH = narrow ? scaled(34.0f) : scaled(50.0f);
        for (int i = 0; i < (int)moods().size(); ++i) {
            if (i % 4) ImGui::SameLine(0.0f, gap);
            char id[24];
            std::snprintf(id, sizeof id, "##drn_mood_%d", i);
            if (moodTile(id, i, moodSel == i, tw, picH, !narrow)) {
                DroneMoodUndo u;
                u.valid = true;
                u.params = p;
                u.title = dronePatchTitle_;
                u.what = std::string("Replaced with ") + moods()[(size_t)i].name + ".";
                u.moodFrom = droneMoodFrom_;
                u.wasDirty = droneDirty_;
                u.until = ImGui::GetTime() + 10.0;
                u.serial = droneEditSerial_ + 1;  // this pick's own edit
                droneMoodUndo_ = u;
                simple::applyMood(p, i);
                // A typed title stays; an empty one or another mood's name follows.
                if (dronePatchTitle_.empty() || dronePatchTitle_ == "Untitled" || moodByTitle(dronePatchTitle_) >= 0)
                    dronePatchTitle_ = moods()[(size_t)i].name;
                droneMoodFrom_ = i;
                dirty = true;
                if (!droneAuditioning_) {
                    droneMode_ = 0;
                    droneWriteArmed_ = false;
                    dronePushParams();
                    dronePlay(false);  // picking a mood is a request to hear it
                }
            }
        }
        // One-step undo: shown until the next edit or for ten seconds.
        const bool undoLive = droneMoodUndo_.valid && droneMoodUndo_.serial == droneEditSerial_ &&
                              ImGui::GetTime() < droneMoodUndo_.until;
        if (undoLive) {
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::TextUnformatted(droneMoodUndo_.what.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ui::Link("Undo##drn_undo")) {
                p = droneMoodUndo_.params;
                dronePatchTitle_ = droneMoodUndo_.title;
                droneMoodFrom_ = droneMoodUndo_.moodFrom;
                droneDirty_ = droneMoodUndo_.wasDirty;
                droneMoodUndo_.valid = false;
                dronePushParams();
            }
        }
        const char* takeLbl = "Another take##drn_take";
        const float takeW = ImGui::CalcTextSize("Another take").x + ImGui::GetStyle().FramePadding.x * 3.0f;
        if (!narrow) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImMax(fs * 8.0f, aw - takeW - gap * 2.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::TextWrapped("A mood replaces the whole sound; length and file name stay. Undo appears right "
                               "after you pick.");
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::SameLine(ImGui::GetContentRegionMax().x - takeW);
        }
        if (ui::SecondaryButton(takeLbl, takeW)) {
            DroneMoodUndo u;
            u.valid = true;
            u.params = p;
            u.title = dronePatchTitle_;
            u.what = "New take.";
            u.moodFrom = droneMoodFrom_;
            u.wasDirty = droneDirty_;
            u.until = ImGui::GetTime() + 10.0;
            u.serial = droneEditSerial_ + 1;
            droneMoodUndo_ = u;
            simple::anotherTake(p);
            dirty = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Same mood and settings, new details: where the drips fall, how the notes drift");
    };

    // Sound: the six sliders.
    auto sound = [&] {
        ui::Caption("Sound");
        const float valueW = ImGui::CalcTextSize("big cavern 11 s").x;
        const ImU32 fromB = semCol(mix4(sem.field, sem.accent, 0.35f));
        const ImU32 toB = semCol(mix4(sem.warn, sem.text, 0.65f));
        for (int s = 0; s < simple::SlCount; ++s) {
            const float r = simple::read(p, s);
            const bool autom = simple::automated(p, s);
            std::string tip = kSliderTip[s];
            if (s == simple::SlBright && p.filter.type != FilterLowPass)
                tip += "\n(the filter is a band/high pass - Detailed view)";
            if (autom) tip += "\n\nChanges over time (timeline) - moving this shifts the whole curve.";
            ImGui::PushID(s);
            ui::RowLabel(kSliderLabel[s], tip.c_str(), LW());
            if (autom) {
                const ImVec2 lmax = ImGui::GetItemRectMax(), lmin = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(lmax.x + fs * 0.45f, (lmin.y + lmax.y) * 0.5f), fs * 0.18f, semCol(sem.warn));
            }
            ImGui::PopID();
            const bool en = simple::writable(p, s);
            char detail[24] = "";
            if (s == simple::SlPitch) std::snprintf(detail, sizeof detail, "%s", noteName(p.rootNote));
            if (s == simple::SlSpace) std::snprintf(detail, sizeof detail, "%.0f s", (double)p.fx.revDecay);
            const std::string w = simple::word(p, s, r);
            float v = r;
            const bool changed = ui::TrackSlider(kSliderId[s], &v, 0.0f, 1.0f, w, s == simple::SlBright ? fromB : 0,
                                                 s == simple::SlBright ? toB : 0, detail, valueW, en);
            if (!en && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("All 6 motion slots are in use - Detailed view > Motion");
            if (ImGui::IsItemActivated()) {
                droneDragSlider_ = s;
                droneDragP0_ = p;
                droneDragR0_ = r;
            }
            if (changed && droneDragSlider_ == s) {
                simple::write(p, droneDragP0_, s, droneDragR0_, v);
                dirty = true;
            }
            if (ImGui::IsItemDeactivated() && droneDragSlider_ == s) droneDragSlider_ = -1;
            if (en && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                // Back to the mood's value (or the value at the last load).
                const float target = fromMood ? simple::read(moods()[(size_t)moodSel].params, s) : droneLoadR_[s];
                const dronegen::Params p0 = p;
                simple::write(p, p0, s, simple::read(p0, s), target);
                droneDragSlider_ = -1;
                dirty = true;
            }
        }
    };

    // Feeling: the chords as five words.
    auto feeling = [&] {
        ui::Caption("Feeling");
        bool exact = true;
        const int f = simple::readFeeling(p, &exact);
        int sel = exact ? f : -1;
        if (ui::SegmentedApprox("##drn_feeling", simple::feelingNames(), simple::FeelCount, &sel, exact ? -1 : f,
                                -1.0f)) {
            simple::applyFeeling(p, sel);
            dirty = true;
        }
        char cap[160];
        if (exact)
            std::snprintf(cap, sizeof cap, "Picks the notes of the %d chord%s. How long each one lasts stays.",
                          p.stepCount, p.stepCount == 1 ? "" : "s");
        else
            std::snprintf(cap, sizeof cap, "Close to %s - your own chords. Picking a feeling replaces them.",
                          simple::feelingNames()[f]);
        dimWrapped(cap);
    };

    // Track: length (whole passes) and loudness.
    auto track = [&] {
        ui::Caption("Track");
        ui::RowLabel("Length", "Ambient music loops without a seam, so a short track sounds like a long one - "
                               "and saves disc space.", LW());
        static const float kLens[4] = {30.0f, 60.0f, 120.0f, 180.0f};
        static const char* const kLenLabels[4] = {"30 s##drn_len", "1 min##drn_len", "2 min##drn_len",
                                                  "3 min##drn_len"};
        bool onChip = false;
        const float chipsX = ImGui::GetCursorPosX();
        for (int i = 0; i < 4; ++i) {
            const bool on = std::fabs(p.lengthSec - kLens[i]) < 0.5f;
            onChip |= on;
            if (i) ImGui::SameLine(0.0f, scaled(6.0f));
            if (ui::ChipButton(kLenLabels[i], on)) {
                simple::applyLength(p, kLens[i]);
                droneHeadSec_ = std::min(droneHeadSec_, (double)p.lengthSec);
                dirty = true;
            }
            uiscript::markLastItemChecked(on);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Ambient music loops without a seam, so a short track sounds like a long one -\n"
                                  "and saves disc space. The chords are stretched to whole passes.");
        }
        {
            const unsigned long long bytes = wavBytes(p);
            char sz[64];
            if (onChip)
                std::snprintf(sz, sizeof sz, "%.1f MB on disc", (double)bytes / (1024.0 * 1024.0));
            else
                std::snprintf(sz, sizeof sz, "%s · %.1f MB on disc", mmss(p.lengthSec).c_str(),
                              (double)bytes / (1024.0 * 1024.0));
            ui::FontScope mono(ui::monoFont());
            // Beside the chips when it fits, else under them (in the control column).
            ImGui::SameLine(0.0f, scaled(10.0f));
            if (ImGui::CalcTextSize(sz).x > ImGui::GetContentRegionAvail().x) {
                ImGui::NewLine();
                ImGui::SetCursorPosX(chipsX);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(bytes > 24ull * 1024 * 1024 ? sem.warn : sem.textDim, "%s", sz);
        }
        std::string loudTip = "How loud the file is mastered. The level in a scene is set on its Play Music node";
        if (!uses.empty())
            loudTip += " (volume " + std::to_string(uses[0].volume) + " in " +
                       project_.scenes[(size_t)uses[0].scene].name + ")";
        loudTip += ".";
        ui::RowLabel("Loudness", loudTip.c_str(), LW());
        static const char* const kLoud[3] = {"Soft", "Normal", "Full"};
        int lsel = simple::readLoudness(p);
        const int cur = lsel;
        if (ui::SegmentedApprox("##drn_loud", kLoud, 3, &lsel, -1, ImMin(ImGui::GetContentRegionAvail().x,
                                                                          scaled(240.0f))) &&
            lsel != cur) {
            simple::applyLoudness(p, lsel);
            dirty = true;
        }
        if (cur < 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("as mixed");
        }
    };

    // In the game: who plays it, and the one click that makes a scene play it.
    auto inGame = [&] {
        if (droneScrollToGame_) {
            ImGui::SetScrollHereY(0.2f);
            droneScrollToGame_ = false;
        }
        ui::Caption("In the game");
        for (size_t i = 0; i < uses.size(); ++i) {
            const TrackUse& u = uses[i];
            const ImVec2 r0 = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const float h = ImGui::GetFrameHeight() + scaled(10.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(r0, ImVec2(r0.x + w, r0.y + h), semCol(sem.field), ImGui::GetStyle().FrameRounding + 2.0f);
            dl->AddRect(r0, ImVec2(r0.x + w, r0.y + h), semCol(sem.border), ImGui::GetStyle().FrameRounding + 2.0f);
            dl->AddCircleFilled(ImVec2(r0.x + fs * 0.8f, r0.y + h * 0.5f), fs * 0.22f, semCol(sem.ok));
            const std::string& sn = project_.scenes[(size_t)u.scene].name;
            const float ty = r0.y + (h - fs) * 0.5f;
            float x = r0.x + fs * 1.4f;
            {
                ui::FontScope bold(ui::boldFont());
                dl->AddText(ImVec2(x, ty), semCol(sem.text), sn.c_str());
                x += ImGui::CalcTextSize(sn.c_str()).x;
            }
            const char* btn = "Open in Flow";
            const float bw = ImGui::CalcTextSize(btn).x + fs;
            dl->PushClipRect(r0, ImVec2(r0.x + w - bw - fs * 0.5f, r0.y + h), true);
            dl->AddText(ImVec2(x, ty), semCol(sem.textDim), u.text.c_str());
            dl->PopClipRect();
            ImGui::SetCursorScreenPos(ImVec2(r0.x + w - bw - fs * 0.4f, r0.y + (h - ImGui::GetFrameHeight()) * 0.5f));
            ImGui::PushID((int)i);
            if (ui::GhostButton("Open in Flow", bw)) {
                selectScene(u.scene);
                selectOnly(u.object);
                openGraphOf(u.scene, u.object);
                flowSelectReq_ = u.node;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Selects that object and shows its flow graph with the Play Music node.");
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(r0.x, r0.y + h + scaled(6.0f)));
            ImGui::Dummy(ImVec2(0, 0));
        }
        bool openPick = false;
        if (uses.empty()) {
            if (ui::Note(1, "No scene plays this yet. Save it, then choose where it plays.", "Play in scene..."))
                openPick = true;
        } else {
            if (ui::GhostButton("+ Play in another scene...##drn_useinscene")) openPick = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(wavExists ? "Adds On Start -> Play Music to a scene's player (or another object)."
                                        : "Save the track first - a scene plays the saved WAV.");
        if (openPick) ImGui::OpenPopup("##drn_scenes");
        if (ImGui::BeginPopup("##drn_scenes")) {
            if (!wavExists) {
                ImGui::TextUnformatted("Save the track first - a scene plays the saved WAV.");
                if (ui::PrimaryButton("Save to project##drn_savepop")) {
                    droneStartRender();
                    ImGui::CloseCurrentPopup();
                }
            } else {
                ImGui::TextDisabled("Play %s when this scene starts:", wavRel.c_str());
                std::vector<int> order;
                order.push_back(project_.activeScene);
                for (int si = 0; si < (int)project_.scenes.size(); ++si)
                    if (si != project_.activeScene) order.push_back(si);
                for (int si : order) {
                    if (si < 0 || si >= (int)project_.scenes.size()) continue;
                    const SceneData& sc = project_.scenes[(size_t)si];
                    const std::string row = sc.name + (si == project_.activeScene ? "  (open)" : "") + "##drn_sc" +
                                            std::to_string(si);
                    if (!ImGui::Selectable(row.c_str())) continue;
                    // The player object, else the first object already running
                    // something On Start, else ask for a "Level music" empty.
                    int obj = -1;
                    for (int oi = 0; oi < (int)sc.objects.size() && obj < 0; ++oi)
                        if (sc.objects[(size_t)oi].type == PrimitiveType::Player) obj = oi;
                    for (int oi = 0; oi < (int)sc.objects.size() && obj < 0; ++oi)
                        for (const FlowNode& n : sc.objects[(size_t)oi].flowGraph.nodes)
                            if (n.type == "OnStart") {
                                obj = oi;
                                break;
                            }
                    droneUseAsk_ = DroneUseAsk();
                    droneUseAsk_.scene = si;
                    if (obj < 0) {
                        droneUseAsk_.create = true;
                        droneUseAsk_.text = "Nothing in " + sc.name +
                                            " runs logic when the scene starts. Add a \"Level music\" object to it?";
                        continue;
                    }
                    // Already playing music on start?
                    const FlowGraph& g = sc.objects[(size_t)obj].flowGraph;
                    int existing = -1;
                    std::string existingTrack;
                    for (const FlowLink& l : g.links) {
                        if (l.kind != FlowLinkExec) continue;
                        const FlowNode *from = nullptr, *to = nullptr;
                        for (const FlowNode& n : g.nodes) {
                            if (n.id == l.fromNode) from = &n;
                            if (n.id == l.toNode) to = &n;
                        }
                        if (from && to && from->type == "OnStart" && to->type == "PlayMusic") {
                            existing = to->id;
                            existingTrack = to->str;
                        }
                    }
                    if (existing >= 0 && existingTrack == wavRel) {
                        droneUseAsk_ = DroneUseAsk();
                        droneStatus_ = sc.name + " already plays this track when it starts.";
                    } else if (existing >= 0) {
                        droneUseAsk_.object = obj;
                        droneUseAsk_.replaceNode = existing;
                        droneUseAsk_.text = sc.name + " already plays " +
                                            std::filesystem::path(existingTrack).filename().string() +
                                            " when it starts. Replace it?";
                    } else {
                        droneUseAsk_ = DroneUseAsk();
                        droneUseInScene(si, obj, -1, false);
                    }
                }
            }
            ImGui::EndPopup();
        }
    };

    auto detailedLink = [&](const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, scaled(4.0f));
        if (ui::Link("Detailed view##drn_detailed")) {
            droneDetailed_ = true;
            droneDragSlider_ = -1;
            saveGlobalConfig();
        }
    };

    // ======== body ========
    const float vgap = scaled(14.0f);
    if (wide) {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float colGap = scaled(28.0f);
        const float leftW = ImFloor((avail - colGap) * 1.28f / 2.28f);
        ImGui::BeginChild("##drn_left", ImVec2(leftW, bodyH));
        hero(scaled(282.0f));
        ImGui::Dummy(ImVec2(0, vgap));
        overTrack();
        ImGui::Dummy(ImVec2(0, vgap));
        moodGrid(false);
        ImGui::EndChild();
        ImGui::SameLine(0.0f, colGap);
        ImGui::BeginChild("##drn_right", ImVec2(0, bodyH));
        sound();
        ImGui::Dummy(ImVec2(0, vgap));
        feeling();
        ImGui::Dummy(ImVec2(0, vgap));
        track();
        ImGui::Dummy(ImVec2(0, vgap));
        inGame();
        // The footer link sits at the column's foot when there is room.
        const float linkY = ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - scaled(4.0f);
        if (ImGui::GetCursorPosY() + vgap < linkY) ImGui::SetCursorPosY(linkY);
        else ImGui::Dummy(ImVec2(0, vgap));
        detailedLink("Chords, the four sound layers, effects, the timeline and exact numbers:");
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("##drn_col", ImVec2(0, bodyH));
        hero(scaled(162.0f));
        ImGui::Dummy(ImVec2(0, vgap * 0.6f));
        moodGrid(true);
        ImGui::Dummy(ImVec2(0, vgap * 0.6f));
        sound();
        ImGui::Dummy(ImVec2(0, vgap * 0.6f));
        feeling();
        ImGui::Dummy(ImVec2(0, vgap * 0.6f));
        overTrack();
        ImGui::Dummy(ImVec2(0, vgap * 0.6f));
        track();
        ImGui::Dummy(ImVec2(0, vgap * 0.6f));
        inGame();
        ImGui::EndChild();
    }

    // ======== footer: what Save writes, Save to project ========
    ImGui::Separator();
    {
        std::string line = std::string("Saves ") + wavRel + " and its patch · " +
                           (inMusic ? "already in the Music list" : "adds it to the Music list");
        if (useScenes.size() == 1) line += " · " + useScenes[0] + " picks it up on the next build";
        else if (useScenes.size() > 1) line += " · " + std::to_string(useScenes.size()) + " scenes pick it up on the next build";
        if (!wide) line += " · " + mmss(p.lengthSec) + " · " +
                           std::to_string((int)std::lround(wavBytes(p) / (1024.0 * 1024.0) * 10.0) / 10) + "." +
                           std::to_string((int)std::lround(wavBytes(p) / (1024.0 * 1024.0) * 10.0) % 10) + " MB";
        const float bw = scaled(wide ? 150.0f : 140.0f);
        const float bh = ImGui::GetFrameHeight() * (wide ? 1.3f : 1.25f);
        const float y0 = ImGui::GetCursorPosY() + scaled(4.0f);
        const float textW = ImGui::GetContentRegionAvail().x - bw - gap * 2.0f;
        ImGui::SetCursorPosY(y0 + (wide ? (bh - ImGui::GetTextLineHeight()) * 0.5f : 0.0f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::TextWrapped("%s", line.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !droneStatus_.empty())
            ImGui::SetTooltip("%s", droneStatus_.c_str());
        const float afterText = ImGui::GetCursorPosY();
        ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - bw, y0));
        if (droneRendering_) {
            const ImVec2 b0 = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("Cancel##drn_save", ImVec2(bw, bh))) droneRenderCancel_.store(true);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float r = ImGui::GetStyle().FrameRounding;
            const float f = std::clamp(droneRenderProgress_.load(), 0.0f, 1.0f);
            dl->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + bh), semCol(sem.accent, 0.25f), r);
            dl->AddRectFilled(b0, ImVec2(b0.x + bw * f, b0.y + bh), semCol(sem.accent), r);
            char t[48];
            std::snprintf(t, sizeof t, "Saving %d%%", (int)(f * 100.0f));
            const ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(b0.x + (bw - ts.x) * 0.5f, b0.y + (bh - ts.y) * 0.5f), semCol(sem.text), t);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Click to cancel.");
        } else {
            if (ui::PrimaryButtonText("Save to project##drn_save", "Save to project", bw)) droneStartRender();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Renders %s, saves the patch next to it and adds the track to the\n"
                                  "Music list. Asks before replacing a file.",
                                  wavRel.c_str());
        }
        ImGui::SetCursorPosY(std::max(afterText, y0 + bh + scaled(4.0f)));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));  // an item, so the cursor move is layout
        if (!wide) detailedLink("Chords, layers, effects, timeline:");
    }

    // ---- In the game's questions --------------------------------------------------
    constexpr const char* kUseAsk = "Play in scene?###drn_useask";
    if (droneUseAsk_.scene >= 0 && !droneUseAsk_.text.empty() && !ImGui::IsPopupOpen(kUseAsk))
        ImGui::OpenPopup(kUseAsk);
    if (ImGui::BeginPopupModal(kUseAsk, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(droneUseAsk_.text.c_str());
        ImGui::Spacing();
        const char* yes = droneUseAsk_.create ? "Add it" : "Replace";
        if (ui::PrimaryButton(yes, scaled(120.0f))) {
            const DroneUseAsk a = droneUseAsk_;
            droneUseAsk_ = DroneUseAsk();
            droneUseInScene(a.scene, a.object, a.replaceNode, a.create);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton("Cancel##drn_useask", scaled(100.0f))) {
            droneUseAsk_ = DroneUseAsk();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
