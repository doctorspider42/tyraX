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

void LogoMark(ImDrawList* dl, ImVec2 p, float size) {
    const theme::Semantics& sem = theme::semantics();
    const float r = size * 0.26f;
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), ImGui::GetColorU32(sem.surface), r);
    dl->AddRect(p, ImVec2(p.x + size, p.y + size), ImGui::GetColorU32(sem.border), r);
    const float th = ImMax(1.2f, size * 0.065f);  // glyph stroke
    const float q = size * 0.5f;                  // quadrant size
    const float g = q * 0.27f;                    // glyph half-extent
    auto centre = [&](int col, int row) {
        return ImVec2(p.x + q * (col + 0.5f) + (col ? -size * 0.03f : size * 0.03f),
                      p.y + q * (row + 0.5f) + (row ? -size * 0.03f : size * 0.03f));
    };
    // Same arrangement as on the pad: triangle top, circle right, cross
    // bottom, square left - folded into a 2x2 grid.
    const ImVec2 tri = centre(0, 0), cir = centre(1, 0), crs = centre(0, 1), sqr = centre(1, 1);
    const ImVec2 t[3] = {ImVec2(tri.x, tri.y - g), ImVec2(tri.x + g * 1.05f, tri.y + g * 0.8f),
                         ImVec2(tri.x - g * 1.05f, tri.y + g * 0.8f)};
    dl->AddPolyline(t, 3, ImGui::GetColorU32(sem.ok), ImDrawFlags_Closed, th);
    dl->AddCircle(cir, g, ImGui::GetColorU32(sem.danger), 0, th);
    const ImU32 cx = ImGui::GetColorU32(sem.accent);
    dl->AddLine(ImVec2(crs.x - g, crs.y - g), ImVec2(crs.x + g, crs.y + g), cx, th);
    dl->AddLine(ImVec2(crs.x + g, crs.y - g), ImVec2(crs.x - g, crs.y + g), cx, th);
    dl->AddRect(ImVec2(sqr.x - g * 0.9f, sqr.y - g * 0.9f), ImVec2(sqr.x + g * 0.9f, sqr.y + g * 0.9f),
                ImGui::GetColorU32(sem.special), size * 0.03f, 0, th);
}

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
