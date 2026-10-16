#include "widgets.hpp"

#include "theme.hpp"

#include <imgui_internal.h>

#include <cfloat>

namespace ui {
namespace {

ImFont* gBold = nullptr;
ImFont* gMono = nullptr;

}  // namespace

void setBoldFont(ImFont* f) { gBold = f; }
void setMonoFont(ImFont* f) { gMono = f; }
ImFont* boldFont() { return gBold; }
ImFont* monoFont() { return gMono; }

bool AxisDrag3(const char* label, float v[3], float speed, float vmin, float vmax,
               const char* fmt) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    // The same skeleton as ImGui::DragScalarN - a group of three unlabeled
    // drags under PushID(label), then the label as text - so the group's
    // LastItemData (Edited / Deactivated) and the --ui-script name are exactly
    // what a DragFloat3 gave the call sites this replaces.
    const ImGuiStyle& style = ImGui::GetStyle();
    const theme::Semantics& sem = theme::semantics();
    const ImVec4 axisCol[3] = {sem.axisX, sem.axisY, sem.axisZ};
    static const char* const kAxis[3] = {"X", "Y", "Z"};

    bool changed = false;
    ImGui::BeginGroup();
    ImGui::PushID(label);
    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        if (i > 0) ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        {
            FontScope mono(gMono);
            changed |= ImGui::DragFloat("", &v[i], speed, vmin, vmax, fmt);
        }
        // The axis letter sits in the left padding of the field ImGui just
        // drew. The value is centred, so the letter only meets it in a field
        // too narrow to read a number in anyway - and it is clipped to the
        // frame so it can never spill into the neighbour.
        const ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImFont* face = gBold ? gBold : ImGui::GetFont();
        const float fs = ImGui::GetFontSize() * 0.8f;
        const ImVec2 ts = face->CalcTextSizeA(fs, FLT_MAX, 0.0f, kAxis[i]);
        const ImVec2 at(r.Min.x + style.FramePadding.x * 0.75f,
                        r.Min.y + (r.GetHeight() - ts.y) * 0.5f);
        window->DrawList->PushClipRect(r.Min, r.Max, true);
        window->DrawList->AddText(face, fs, at, ImGui::GetColorU32(axisCol[i]), kAxis[i]);
        window->DrawList->PopClipRect();
        ImGui::PopID();
        ImGui::PopItemWidth();
    }
    ImGui::PopID();

    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    if (label != labelEnd) {
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::TextEx(label, labelEnd);
    }
    ImGui::EndGroup();
    return changed;
}

}  // namespace ui
