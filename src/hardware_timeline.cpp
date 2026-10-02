#include "hardware_timeline.hpp"
#include <imgui.h>
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

void draw(const std::string& projectDir) {
    // One editor window owns the view. Clear all capture state on project switch.
    static std::string project, message;
    static Capture capture;
    static int selected = 0, start = 120, count = 4;
    static bool states = true;
    static float zoom = 1;
    if (project != projectDir) {
        project = projectDir; capture = {}; selected = 0; zoom = 1; message.clear();
        start = 120; count = 4; states = true;
        int stateValue;
        std::ifstream cfg(std::filesystem::path(projectDir) / "bin/hardware-trace.cfg");
        int a, b;
        if (cfg >> a >> b >> stateValue && a >= 0 && a <= 1000000 && b >= 1 && b <= 32 && (stateValue == 0 || stateValue == 1))
            { start = a; count = b; states = stateValue != 0; }
    }
    const auto bin = std::filesystem::path(projectDir) / "bin";
    ImGui::TextWrapped("EE scopes and existing DMA/GS waits on a shared clock. Rows overlap; this is not VU1/GS utilization. Capture adds overhead; use unarmed runs for performance comparisons.");
    ImGui::SetNextItemWidth(130); ImGui::InputInt("Start frame", &start);
    ImGui::SetNextItemWidth(130); ImGui::InputInt("Capture frames", &count);
    ImGui::Checkbox("Register snapshots", &states);
    if (ImGui::Button("Arm next boot")) {
        start = std::clamp(start, 0, 1000000); count = std::clamp(count, 1, 32);
        std::ofstream cfg(bin / "hardware-trace.cfg", std::ios::trunc);
        cfg << start << ' ' << count << ' ' << int(states) << '\n'; cfg.close();
        message = cfg ? "Armed for next boot. Existing capture remains until the game writes a new one." : "Cannot write configuration: build the project first.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Disarm next boot")) {
        std::error_code ec; std::filesystem::remove(bin / "hardware-trace.cfg", ec);
        message = ec ? ec.message() : "Disarmed for next boot.";
    }
    if (ImGui::Button("Load hardware capture")) {
        try { capture = read(bin / "hardware-trace.csv"); selected = 0;
            message = "Loaded " + std::to_string(capture.events.size()) + " events from " + (bin / "hardware-trace.csv").string(); }
        catch (const std::exception& e) { capture = {}; message = e.what(); }
    }
    ImGui::TextWrapped("Requires a rebuilt game. Arm does not reset the console. Load reads the completed CSV on demand; no debugger polling is added.");
    if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
    if (capture.frames.empty()) return;
    ImGui::BeginDisabled(selected == 0);
    if (ImGui::Button("Previous hardware frame")) --selected;
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(selected + 1 == int(capture.frames.size()));
    if (ImGui::Button("Next hardware frame")) ++selected;
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("Hardware frame", std::to_string(capture.frames[selected].frame).c_str())) {
        for (int i = 0; i < int(capture.frames.size()); ++i)
            if (ImGui::Selectable(std::to_string(capture.frames[i].frame).c_str(), selected == i)) selected = i;
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(180); ImGui::SliderFloat("Timeline zoom", &zoom, 1, 16, "%.1fx");
    const auto& frame = capture.frames[selected];
    ImGui::Text("Frame %u: %.3f ms", frame.frame, frame.duration / 294912.0);
    std::map<std::string, std::pair<uint64_t, unsigned>> totals;
    for (auto& e : capture.events) if (e.frame == frame.frame) {
        auto& sum = totals[e.label]; sum.first += e.duration; ++sum.second;
    }
    std::vector<std::string> lanes{"Frame"};
    for (auto& [label, unused] : totals) if (label != "Frame") lanes.push_back(label);
    if (ImGui::BeginChild("Hardware timeline chart", ImVec2(0, 330), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        const auto origin = ImGui::GetCursorScreenPos();
        const float width = std::max(450.0f, ImGui::GetContentRegionAvail().x - 190) * zoom;
        const float height = 30 + float(lanes.size()) * 24;
        const double scale = width / std::max(1u, frame.duration);
        ImGui::InvisibleButton("Timeline canvas", ImVec2(width + 200, height));
        auto* dl = ImGui::GetWindowDrawList();
        const int ticks = std::max(5, int(zoom * 5));
        for (int i = 0; i <= ticks; ++i) {
            const float x = origin.x + 190 + width * i / ticks;
            char tick[32]; snprintf(tick, sizeof(tick), "%.2f ms", frame.duration * (double(i) / ticks) / 294912.0);
            dl->AddText(ImVec2(x, origin.y), IM_COL32(180,190,205,255), tick);
            dl->AddLine(ImVec2(x, origin.y + 20), ImVec2(x, origin.y + height), IM_COL32(65,75,90,255));
        }
        for (size_t i = 0; i < lanes.size(); ++i)
            dl->AddText(ImVec2(origin.x, origin.y + 30 + i * 24), IM_COL32(210,220,235,255), lanes[i].c_str());
        const Event* hovered = nullptr;
        for (auto& e : capture.events) if (e.frame == frame.frame) {
            auto lane = std::find(lanes.begin(), lanes.end(), e.label) - lanes.begin();
            ImVec2 a(origin.x + 190 + float((e.start - frame.start) * scale), origin.y + 30 + lane * 24);
            ImVec2 b(a.x + std::max(e.duration ? 0.1f : 2.0f, float(e.duration * scale)), a.y + 18);
            dl->AddRectFilled(a, b, !e.duration ? IM_COL32(220,165,70,255) : e.label.find("wait") != std::string::npos ? IM_COL32(225,120,105,255) : IM_COL32(80,175,215,255));
            if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b)) hovered = &e;
        }
        if (hovered) ImGui::SetTooltip("%s\nStart: %.4f ms\nDuration: %.4f ms\nValue: %u (0x%08X)", hovered->label.c_str(),
            (hovered->start - frame.start) / 294912.0, hovered->duration / 294912.0, hovered->value, hovered->value);
    }
    ImGui::EndChild();
    ImGui::TextDisabled("Inclusive totals for selected frame. Do not sum nested scopes.");
    if (ImGui::BeginTable("Hardware totals", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Scope"); ImGui::TableSetupColumn("Events"); ImGui::TableSetupColumn("Inclusive ms"); ImGui::TableHeadersRow();
        std::vector<std::pair<std::string, std::pair<uint64_t, unsigned>>> sorted(totals.begin(), totals.end());
        std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second.first > b.second.first; });
        for (auto& [label, sum] : sorted) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(label.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%u", sum.second);
            ImGui::TableNextColumn(); ImGui::Text("%.4f", sum.first / 294912.0);
        }
        ImGui::EndTable();
    }
}
}
