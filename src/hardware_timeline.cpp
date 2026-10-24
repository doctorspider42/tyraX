#include "hardware_timeline.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <cctype>
#include <ctime>
#include "theme.hpp"
#include "toolkit.hpp"
#include "widgets.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace hardware_timeline {
namespace {
struct Event { std::string label; uint32_t start, duration, frame, value; };
struct Capture { std::vector<Event> events, frames; };
uint32_t number(const std::string& s) {
    uint32_t n = 0;
    auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (error != std::errc() || end != s.data() + s.size())
        throw std::runtime_error("Invalid unsigned field");
    return n;
}
Capture read(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("No capture found. Arm, boot the rebuilt game, then load the capture.");
    std::string line;
    auto next = [&]() { if (!std::getline(f, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back(); return true; };
    if (!next() || line != "label,start_ticks,duration_ticks,frame,value")
        throw std::runtime_error("Invalid capture header");
    Capture result;
    bool complete = false;
    Event footer{};
    while (next()) {
        if (complete || result.events.size() > 32768 || line.empty() || line.back() == ',' || line.size() > 256)
            throw std::runtime_error("Invalid capture size or trailing data");
        std::array<std::string, 5> fields;
        std::istringstream row(line);
        for (auto& field : fields)
            if (!std::getline(row, field, ',')) throw std::runtime_error("Incomplete capture row");
        std::string extra;
        if (std::getline(row, extra, ',')) throw std::runtime_error("Extra capture field");
        Event e{fields[0], number(fields[1]), number(fields[2]), number(fields[3]), number(fields[4])};
        if (e.label == "END") { footer = e; complete = true; }
        else { result.events.push_back(e); if (e.label == "Frame") result.frames.push_back(e); }
    }
    if (!complete) throw std::runtime_error("Incomplete capture: missing END footer");
    if (footer.duration) throw std::runtime_error("Capture overflow. Arm fewer frames.");
    if (footer.start != result.events.size() || footer.frame < 1 || footer.frame > 32 ||
        result.frames.size() != footer.frame) throw std::runtime_error("Invalid event/frame count");
    std::sort(result.frames.begin(), result.frames.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
    for (size_t i = 0; i < result.frames.size(); ++i)
        if (uint64_t(result.frames[i].frame) != uint64_t(footer.value) + i)
            throw std::runtime_error("Missing or duplicate frame");
    for (auto& e : result.events) {
        auto it = std::find_if(result.frames.begin(), result.frames.end(), [&](auto& frame) { return frame.frame == e.frame; });
        if (it == result.frames.end() || e.start < it->start ||
            uint64_t(e.start) + e.duration > uint64_t(it->start) + it->duration)
            throw std::runtime_error("Event outside frame bounds");
    }
    return result;
}
}


namespace {
ImU32 col(const ImVec4& c, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a)); }

// The Debugger's in-card small-caps caption (devkit_ui.cpp dbgCaps), an
// ordinary item a SameLine can follow.
void caps(const char* text) {
    char up[64];
    std::snprintf(up, sizeof(up), "%s", text);
    for (char* c = up; *c; ++c) *c = (char)std::toupper((unsigned char)*c);
    ImGui::PushFont(ui::boldFont(), ImGui::GetFontSize() * 0.8f);
    ImGui::TextColored(theme::semantics().textDim, "%s", up);
    ImGui::PopFont();
}

// Lane colour by what the row measures: waits are where a frame stalls
// (danger), register snapshots are instants (warn), the frame itself is the
// backdrop, the debug runtime's own host I/O is "special", the rest are EE
// scopes (accent).
ImU32 laneCol(const std::string& label, bool instant) {
    const theme::Semantics& sem = theme::semantics();
    if (instant) return col(sem.warn);
    if (label == "Frame") return col(sem.accent, 0.3f);
    if (label.find("wait") != std::string::npos) return col(sem.danger, 0.9f);
    if (label.rfind("Live_debug", 0) == 0) return col(sem.special, 0.9f);
    return col(sem.accent, 0.9f);
}
}  // namespace

void draw(const std::string& projectDir, float scale) {
    // One editor window owns the view. Clear all capture state on project switch.
    static std::string project, message, loadedAt;
    static bool messageBad = false;
    static Capture capture;
    static int selected = 0, start = 120, count = 4;
    static bool states = true;
    static float zoom = 1;
    if (project != projectDir) {
        project = projectDir; capture = {}; selected = 0; zoom = 1; message.clear(); loadedAt.clear();
        start = 120; count = 4; states = true;
        int stateValue;
        std::ifstream cfg(std::filesystem::path(projectDir) / "bin/hardware-trace.cfg");
        int a, b;
        if (cfg >> a >> b >> stateValue && a >= 0 && a <= 1000000 && b >= 1 && b <= 32 && (stateValue == 0 || stateValue == 1))
            { start = a; count = b; states = stateValue != 0; }
    }
    const theme::Semantics& sem = theme::semantics();
    const auto S = [scale](float px) { return px * scale; };
    const auto bin = std::filesystem::path(projectDir) / "bin";
    const char* kWhat =
        "EE scopes and the existing DMA/GS waits on one shared clock. Rows overlap - this is not "
        "VU1/GS utilisation. A capture adds overhead: compare performance on unarmed runs.";
    auto load = [&]() {
        try {
            capture = read(bin / "hardware-trace.csv");
            selected = 0;
            const std::time_t now = std::time(nullptr);
            char hm[16] = "";
            if (const std::tm* lt = std::localtime(&now)) std::strftime(hm, sizeof(hm), "%H:%M", lt);
            loadedAt = hm;
            message.clear();
            messageBad = false;
        } catch (const std::exception& e) {
            capture = {};
            message = e.what();
            messageBad = true;
        }
    };

    ui::BeginCard("##hwtimeline", ImVec2(0, 0));
    // --- header: caption + what is loaded, the frame picker, the two actions
    caps("Hardware timeline");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kWhat);
    ImGui::SameLine();
    if (!capture.frames.empty()) {
        ImGui::TextDisabled("capture of %d frame%s from loop %u \xc2\xb7 loaded %s", (int)capture.frames.size(),
                            capture.frames.size() == 1 ? "" : "s", capture.frames.front().frame, loadedAt.c_str());
    } else {
        ImGui::TextDisabled("one boot, %d frame%s from loop %d", count, count == 1 ? "" : "s", start);
    }
    {
        const char* arm = "Arm next boot...";
        const char* reload = "Load hardware capture";
        const float gap = S(6);
        float w = ui::ChipButtonWidth(arm) + gap + ui::ChipButtonWidth(reload);
        const bool chips = !capture.frames.empty() && capture.frames.size() <= 8;
        if (chips) {
            w += S(14);
            for (const Event& f : capture.frames) w += ui::ChipButtonWidth(std::to_string(f.frame).c_str()) + S(2);
        } else if (!capture.frames.empty()) {
            w += S(14) + S(110);
        }
        // Right-aligned beside the caption when they fit, else on their own
        // line (the Debugger docks narrow); chips and buttons wrap too.
        ImGui::SameLine();
        const float rightX = ImGui::GetContentRegionMax().x - w;
        if (rightX >= ImGui::GetCursorPosX()) ImGui::SetCursorPosX(rightX);
        else ImGui::NewLine();
        const auto flow = [&](float next, float spacing) {
            ImGui::SameLine(0.0f, spacing);
            if (ImGui::GetContentRegionAvail().x < next) ImGui::NewLine();
        };
        bool firstItem = true;
        const auto place = [&](float next, float spacing) {
            if (!firstItem) flow(next, spacing);
            firstItem = false;
        };
        if (chips) {
            for (int i = 0; i < (int)capture.frames.size(); ++i) {
                place(ui::ChipButtonWidth(std::to_string(capture.frames[i].frame).c_str()), S(2));
                if (ui::ChipButton(std::to_string(capture.frames[i].frame).c_str(), selected == i)) selected = i;
            }
        } else if (!capture.frames.empty()) {
            place(S(110), 0.0f);
            ImGui::SetNextItemWidth(S(110));
            if (ImGui::BeginCombo("##Hardware frame", std::to_string(capture.frames[selected].frame).c_str())) {
                for (int i = 0; i < int(capture.frames.size()); ++i)
                    if (ImGui::Selectable(std::to_string(capture.frames[i].frame).c_str(), selected == i)) selected = i;
                ImGui::EndCombo();
            }
        }
        place(ui::ChipButtonWidth(reload), S(14));
        if (ui::ChipButton(reload)) load();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Reads bin/hardware-trace.csv now - the debugger never polls it");
        place(ui::ChipButtonWidth(arm), gap);
        if (ui::ChipButton(arm)) ImGui::OpenPopup("##hwarm");
    }
    if (ImGui::BeginPopup("##hwarm")) {
        const float labelW = S(130);
        ui::RowLabel("Start frame", "Zero-based engine loop index, loading and splash loops included", labelW);
        ImGui::SetNextItemWidth(S(110));
        ImGui::InputInt("##hw_start", &start, 0, 0);
        ui::RowLabel("Capture frames", "1 to 32", labelW);
        ImGui::SetNextItemWidth(S(110));
        ImGui::InputInt("##hw_count", &count, 0, 0);
        ImGui::SetCursorPosX(labelW + ImGui::GetStyle().WindowPadding.x);
        ImGui::Checkbox("Register snapshots", &states);
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(300));
        ImGui::TextUnformatted("Needs a rebuilt game. The console is not reset: the capture starts on its next boot.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if (ui::PrimaryButton("Arm next boot")) {
            start = std::clamp(start, 0, 1000000); count = std::clamp(count, 1, 32);
            std::ofstream cfg(bin / "hardware-trace.cfg", std::ios::trunc);
            cfg << start << ' ' << count << ' ' << int(states) << '\n'; cfg.close();
            messageBad = !cfg;
            message = cfg ? "Armed for the next boot. The loaded capture stays until the game writes a new one."
                          : "Cannot write bin/hardware-trace.cfg - build the project first.";
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::SecondaryButton("Disarm next boot")) {
            std::error_code ec; std::filesystem::remove(bin / "hardware-trace.cfg", ec);
            messageBad = (bool)ec;
            message = ec ? ec.message() : "Disarmed for the next boot.";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!message.empty()) ui::StatusLine(messageBad ? 2 : 3, message.c_str());

    if (capture.frames.empty()) {
        ImGui::Dummy(ImVec2(0, S(150)));
        const ImVec2 r0 = ImGui::GetItemRectMin();
        // EmptyState centres in the remaining space of the window; the card
        // auto-fits, so give it a fixed stage to centre in.
        ImGui::SetCursorScreenPos(r0);
        ImGui::BeginChild("##hwempty", ImVec2(0, S(150)));
        ui::EmptyState(ui::Glyph::Sliders, "No capture loaded",
                       "Arm the next boot, run the rebuilt game on the PS2, then load bin/hardware-trace.csv.");
        ImGui::EndChild();
        ui::EndCard();
        return;
    }

    // --- lanes: the frame first, then every scope that ran in it
    const Event& frame = capture.frames[selected];
    std::map<std::string, std::pair<uint64_t, unsigned>> totals;
    std::vector<std::string> lanes{"Frame"};
    for (auto& e : capture.events) if (e.frame == frame.frame) {
        auto& sum = totals[e.label]; sum.first += e.duration; ++sum.second;
        if (std::find(lanes.begin(), lanes.end(), e.label) == lanes.end()) lanes.push_back(e.label);
    }
    float labelW = 0;
    {
        ui::FontScope mono(ui::monoFont());
        for (auto& l : lanes) labelW = ImMax(labelW, ImGui::CalcTextSize(l.c_str()).x);
    }
    labelW += S(24);
    const float laneH = S(26), barH = S(12), axisH = S(26);
    const float height = lanes.size() * laneH + axisH;
    if (ImGui::BeginChild("Hardware timeline chart", ImVec2(0, height + ImGui::GetStyle().ScrollbarSize), 0,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = ImMax(S(300), ImGui::GetContentRegionAvail().x - labelW - S(8)) * zoom;
        const double perTick = width / (double)std::max(1u, frame.duration);
        ImGui::InvisibleButton("Timeline canvas", ImVec2(labelW + width + S(8), height));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float x0 = origin.x + labelW;
        const Event* hovered = nullptr;
        for (size_t i = 0; i < lanes.size(); ++i) {
            const float y = origin.y + i * laneH;
            ui::FontScope mono(ui::monoFont());
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.9f);
            dl->AddText(ImVec2(origin.x, y + (laneH - ImGui::GetFontSize()) * 0.5f), col(sem.text, 0.85f),
                        lanes[i].c_str());
            ImGui::PopFont();
            dl->AddRectFilled(ImVec2(x0, y + (laneH - barH) * 0.5f), ImVec2(x0 + width, y + (laneH + barH) * 0.5f),
                              col(sem.field), S(3));
        }
        for (auto& e : capture.events) if (e.frame == frame.frame) {
            const auto lane = std::find(lanes.begin(), lanes.end(), e.label) - lanes.begin();
            const float y = origin.y + lane * laneH + (laneH - barH) * 0.5f;
            const bool instant = e.duration == 0;
            ImVec2 a(x0 + float((e.start - frame.start) * perTick), y);
            ImVec2 b(a.x + ImMax(instant ? S(4) : 1.0f, float(e.duration * perTick)), y + barH);
            if (instant) { a.x -= S(2); b.x -= S(2); }
            dl->AddRectFilled(a, b, laneCol(e.label, instant), S(3));
            if (ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(a, b)) hovered = &e;
        }
        // Axis: 0 ms, every 5 ms, and the frame's own length at the end.
        const double frameMs = frame.duration / 294912.0;
        const float ay = origin.y + lanes.size() * laneH + S(6);
        ui::FontScope mono(ui::monoFont());
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.85f);
        const double step = frameMs > 40 / zoom ? 10.0 : frameMs > 8 / zoom ? 5.0 : 1.0;
        for (double ms = 0; ms < frameMs - step * 0.4; ms += step) {
            char t[16];
            std::snprintf(t, sizeof(t), ms == 0 ? "0 ms" : "%.0f", ms);
            dl->AddText(ImVec2(x0 + float(ms / frameMs) * width, ay), col(sem.textDim), t);
        }
        char end[24];
        std::snprintf(end, sizeof(end), "%.1f ms", frameMs);
        dl->AddText(ImVec2(x0 + width - ImGui::CalcTextSize(end).x, ay), col(sem.textDim), end);
        ImGui::PopFont();
        if (hovered) ImGui::SetTooltip("%s\nStart: %.4f ms\nDuration: %.4f ms\nValue: %u (0x%08X)", hovered->label.c_str(),
            (hovered->start - frame.start) / 294912.0, hovered->duration / 294912.0, hovered->value, hovered->value);
    }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(S(160));
    ImGui::SliderFloat("Timeline zoom", &zoom, 1, 16, "%.1fx");
    {
        const char* note = "Rows overlap - inclusive times, not utilisation.";
        ImGui::SameLine(0.0f, S(16));
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(note).x) ImGui::NewLine();
        ImGui::TextDisabled("%s", note);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kWhat);
    ui::EndCard();

    ImGui::Spacing();
    if (ImGui::BeginTable("Hardware totals", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Scope"); ImGui::TableSetupColumn("Events"); ImGui::TableSetupColumn("Inclusive ms"); ImGui::TableHeadersRow();
        std::vector<std::pair<std::string, std::pair<uint64_t, unsigned>>> sorted(totals.begin(), totals.end());
        std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second.first > b.second.first; });
        ui::FontScope mono(ui::monoFont());
        for (auto& [label, sum] : sorted) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(label.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%u", sum.second);
            ImGui::TableNextColumn(); ImGui::Text("%.4f", sum.first / 294912.0);
        }
        ImGui::EndTable();
    }
}
}
