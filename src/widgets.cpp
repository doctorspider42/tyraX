#include "widgets.hpp"

#include "theme.hpp"

#include <imgui_internal.h>

#include <cfloat>
#include <cmath>

namespace ui {
namespace {

ImU32 fadeCol(ImVec4 c, float a) {
    c.w *= a;
    return ImGui::GetColorU32(c);
}

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

bool Switch(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, labelEnd, false);

    // Track: a pill one text line tall-ish and ~1.8x as wide, vertically centred
    // on the row a checkbox would have occupied, so a switch drops into a
    // checkbox's place without moving anything around it.
    const float rowH = ImGui::GetFrameHeight();
    const float trackH = IM_TRUNC(g.FontSize * 1.05f);
    const float trackW = IM_TRUNC(trackH * 1.8f);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total(pos, ImVec2(pos.x + trackW + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f),
                                   pos.y + rowH));
    ImGui::ItemSize(total, style.FramePadding.y);
    if (!ImGui::ItemAdd(total, id)) {
        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable |
                                                   (*v ? ImGuiItemStatusFlags_Checked : 0));
        return false;
    }
    const bool mixed = (g.LastItemData.ItemFlags & ImGuiItemFlags_MixedValue) != 0;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total, id, &hovered, &held);
    if (pressed) {
        *v = !*v;
        ImGui::MarkItemEdited(id);
    }

    const theme::Semantics& sem = theme::semantics();
    // Ease the knob between the two ends instead of jumping: state stored per
    // item like hoverAnim's.
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID animId = id + 0x51u;
    float t = store->GetFloat(animId, *v ? 1.0f : 0.0f);
    const float target = mixed ? 0.5f : (*v ? 1.0f : 0.0f);
    const float step = g.IO.DeltaTime * 10.0f;
    t = t < target ? ImMin(target, t + step) : ImMax(target, t - step);
    store->SetFloat(animId, t);

    ImDrawList* dl = window->DrawList;
    const ImVec2 a(pos.x, pos.y + (rowH - trackH) * 0.5f);
    const ImVec2 b(a.x + trackW, a.y + trackH);
    ImVec4 off = sem.border;
    off = ImVec4(off.x * 1.25f, off.y * 1.25f, off.z * 1.25f, 1.0f);
    ImVec4 on = sem.accent;
    if (mixed) on = sem.textDim;
    ImVec4 track(off.x + (on.x - off.x) * t, off.y + (on.y - off.y) * t, off.z + (on.z - off.z) * t, 1.0f);
    if (hovered) track = ImVec4(ImMin(1.0f, track.x + 0.06f), ImMin(1.0f, track.y + 0.06f),
                                ImMin(1.0f, track.z + 0.06f), 1.0f);
    if (g.LastItemData.ItemFlags & ImGuiItemFlags_Disabled) track.w = style.DisabledAlpha;
    dl->AddRectFilled(a, b, ImGui::GetColorU32(track), trackH * 0.5f);
    const float kr = trackH * 0.5f - IM_TRUNC(trackH * 0.12f) - 0.5f;
    const float kx = a.x + trackH * 0.5f + (trackW - trackH) * t;
    dl->AddCircleFilled(ImVec2(kx, a.y + trackH * 0.5f), kr, IM_COL32(255, 255, 255, 255), 20);
    ImGui::RenderNavCursor(total, id);

    if (labelSize.x > 0.0f)
        ImGui::RenderText(ImVec2(b.x + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label, labelEnd);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable |
                                               (*v ? ImGuiItemStatusFlags_Checked : 0));
    return pressed;
}

bool Section(const char* label, ImGuiTreeNodeFlags flags) {
    // The section's top edge: a hairline across the window, so sections still
    // read as blocks without the tinted band.
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window->SkipItems) {
        const float y = window->DC.CursorPos.y + IM_TRUNC(ImGui::GetStyle().ItemSpacing.y * 0.5f);
        window->DrawList->AddLine(ImVec2(window->WorkRect.Min.x, y), ImVec2(window->WorkRect.Max.x, y),
                                  ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y * 0.5f));
    }
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.04f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1, 1, 1, 0.07f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    FontScope bold(gBold);
    const bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return open;
}

void DrawGlyph(ImDrawList* dl, Glyph g, ImVec2 c, float size, ImU32 col) {
    const float s = size * 0.5f;            // half extent
    const float th = ImMax(1.3f, size * 0.085f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    auto head = [&](ImVec2 tip, float dx, float dy) {  // arrowhead pointing (dx,dy)
        const float l = s * 0.38f;
        const float nx = -dy, ny = dx;
        dl->AddTriangleFilled(tip, ImVec2(tip.x - dx * l + nx * l * 0.6f, tip.y - dy * l + ny * l * 0.6f),
                              ImVec2(tip.x - dx * l - nx * l * 0.6f, tip.y - dy * l - ny * l * 0.6f), col);
    };
    switch (g) {
        case Glyph::Move:
            dl->AddLine(P(-0.8f, 0), P(0.8f, 0), col, th);
            dl->AddLine(P(0, -0.8f), P(0, 0.8f), col, th);
            head(P(1.0f, 0), 1, 0);
            head(P(-1.0f, 0), -1, 0);
            head(P(0, 1.0f), 0, 1);
            head(P(0, -1.0f), 0, -1);
            break;
        case Glyph::Rotate: {
            const float r = s * 0.72f;
            dl->PathArcTo(c, r, 3.14159f * 0.15f, 3.14159f * 1.62f, 24);
            dl->PathStroke(col, 0, th);
            const float a = 3.14159f * 1.62f;
            const ImVec2 tip(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
            head(ImVec2(tip.x + s * 0.12f, tip.y + s * 0.02f), 1.0f, 0.25f);
            break;
        }
        case Glyph::Scale:
            dl->AddRect(P(-0.85f, 0.15f), P(-0.15f, 0.85f), col, s * 0.08f, 0, th);
            dl->AddLine(P(-0.05f, 0.05f), P(0.7f, -0.7f), col, th);
            dl->AddLine(P(0.2f, -0.85f), P(0.85f, -0.85f), col, th);
            dl->AddLine(P(0.85f, -0.85f), P(0.85f, -0.2f), col, th);
            break;
        case Glyph::Sculpt: {
            const ImVec2 pts[5] = {P(-0.95f, 0.7f), P(-0.35f, -0.35f), P(0.0f, 0.15f),
                                   P(0.4f, -0.6f), P(0.95f, 0.7f)};
            dl->AddPolyline(pts, 5, col, 0, th);
            dl->AddLine(P(-0.95f, 0.7f), P(0.95f, 0.7f), col, th);
            break;
        }
        case Glyph::Paint:
            dl->AddLine(P(0.85f, -0.85f), P(-0.05f, 0.05f), col, th * 1.1f);
            dl->AddCircleFilled(P(-0.38f, 0.42f), s * 0.36f, col, 16);
            break;
        case Glyph::Measure: {
            const ImVec2 a = P(-0.95f, -0.35f), b = P(0.95f, 0.35f);
            dl->AddRect(a, b, col, s * 0.08f, 0, th);
            for (int i = 1; i < 5; ++i) {
                const float x = -0.95f + 1.9f * i / 5.0f;
                dl->AddLine(P(x, -0.35f), P(x, i % 2 ? -0.02f : 0.1f), col, th * 0.8f);
            }
            break;
        }
        case Glyph::Snap:
            dl->AddLine(P(0, -0.9f), P(0, 0.2f), col, th);
            head(P(0, 0.42f), 0, 1);
            dl->AddLine(P(-0.9f, 0.78f), P(0.9f, 0.78f), col, th);
            break;
        case Glyph::Box: {  // an isometric cube: the generic "scene object"
            const ImVec2 hex[6] = {P(0, -0.9f), P(0.82f, -0.45f), P(0.82f, 0.45f),
                                   P(0, 0.9f),  P(-0.82f, 0.45f), P(-0.82f, -0.45f)};
            dl->AddPolyline(hex, 6, col, ImDrawFlags_Closed, th);
            dl->AddLine(P(0, 0), P(-0.82f, -0.45f), col, th);
            dl->AddLine(P(0, 0), P(0.82f, -0.45f), col, th);
            dl->AddLine(P(0, 0), P(0, 0.9f), col, th);
            break;
        }
        case Glyph::Person:
            dl->AddCircle(P(0, -0.45f), s * 0.36f, col, 16, th);
            dl->PathArcTo(P(0, 0.95f), s * 0.8f, 3.14159f * 1.08f, 3.14159f * 1.92f, 16);
            dl->PathStroke(col, 0, th);
            break;
        case Glyph::Bulb:
            dl->AddCircle(P(0, -0.25f), s * 0.55f, col, 18, th);
            dl->AddLine(P(-0.28f, 0.55f), P(0.28f, 0.55f), col, th);
            dl->AddLine(P(-0.2f, 0.85f), P(0.2f, 0.85f), col, th);
            break;
        case Glyph::Camera:
            dl->AddRect(P(-0.9f, -0.45f), P(0.35f, 0.55f), col, s * 0.1f, 0, th);
            {
                const ImVec2 t[3] = {P(0.35f, 0.05f), P(0.9f, -0.35f), P(0.9f, 0.45f)};
                dl->AddPolyline(t, 3, col, ImDrawFlags_Closed, th);
            }
            break;
        case Glyph::Speaker: {
            const ImVec2 b[6] = {P(-0.85f, -0.3f), P(-0.4f, -0.3f), P(0.1f, -0.75f),
                                 P(0.1f, 0.75f),   P(-0.4f, 0.3f),  P(-0.85f, 0.3f)};
            dl->AddPolyline(b, 6, col, ImDrawFlags_Closed, th);
            dl->PathArcTo(P(0.1f, 0), s * 0.6f, -0.8f, 0.8f, 10);
            dl->PathStroke(col, 0, th);
            break;
        }
        case Glyph::Area: {  // a dashed square: a volume, not a solid
            const float e = 0.85f, g0 = 0.22f;
            const float sides[4][4] = {{-e, -e, e, -e}, {e, -e, e, e}, {e, e, -e, e}, {-e, e, -e, -e}};
            for (const auto& sd : sides) {
                const float mx = (sd[0] + sd[2]) * 0.5f, my = (sd[1] + sd[3]) * 0.5f;
                const float dx = (sd[2] - sd[0]) * 0.5f, dy = (sd[3] - sd[1]) * 0.5f;
                const float k = g0 / e;
                dl->AddLine(P(sd[0], sd[1]), P(mx - dx * k, my - dy * k), col, th);
                dl->AddLine(P(mx + dx * k, my + dy * k), P(sd[2], sd[3]), col, th);
            }
            break;
        }
        case Glyph::Comment: {
            const ImVec2 b[7] = {P(-0.85f, -0.7f), P(0.85f, -0.7f), P(0.85f, 0.35f), P(-0.1f, 0.35f),
                                 P(-0.5f, 0.8f),   P(-0.5f, 0.35f), P(-0.85f, 0.35f)};
            dl->AddPolyline(b, 7, col, ImDrawFlags_Closed, th);
            break;
        }
        case Glyph::Sparkle: {
            const ImVec2 b[8] = {P(0, -0.95f), P(0.22f, -0.22f), P(0.95f, 0), P(0.22f, 0.22f),
                                 P(0, 0.95f),  P(-0.22f, 0.22f), P(-0.95f, 0), P(-0.22f, -0.22f)};
            dl->AddPolyline(b, 8, col, ImDrawFlags_Closed, th);
            break;
        }
        case Glyph::Flag:
            dl->AddLine(P(-0.6f, -0.9f), P(-0.6f, 0.9f), col, th);
            {
                const ImVec2 f[3] = {P(-0.6f, -0.85f), P(0.8f, -0.45f), P(-0.6f, -0.05f)};
                dl->AddPolyline(f, 3, col, 0, th);
            }
            break;
        case Glyph::List:
            for (int i = 0; i < 3; ++i) {
                const float y = -0.6f + 0.6f * i;
                dl->AddCircleFilled(P(-0.75f, y), s * 0.09f, col);
                dl->AddLine(P(-0.4f, y), P(0.9f, y), col, th);
            }
            break;
        case Glyph::Layers:
            for (int i = 0; i < 3; ++i) {
                const float y = -0.45f + 0.45f * i;
                const ImVec2 d[4] = {P(0, y - 0.45f), P(0.9f, y), P(0, y + 0.45f), P(-0.9f, y)};
                if (i == 0) dl->AddPolyline(d, 4, col, ImDrawFlags_Closed, th);
                else {
                    dl->AddLine(d[1], d[2], col, th);
                    dl->AddLine(d[2], d[3], col, th);
                }
            }
            break;
        case Glyph::Music:
            dl->AddLine(P(-0.3f, 0.55f), P(-0.3f, -0.8f), col, th);
            dl->AddLine(P(0.7f, 0.35f), P(0.7f, -0.95f), col, th);
            dl->AddLine(P(-0.3f, -0.8f), P(0.7f, -0.95f), col, th);
            dl->AddCircleFilled(P(-0.55f, 0.6f), s * 0.25f, col);
            dl->AddCircleFilled(P(0.45f, 0.4f), s * 0.25f, col);
            break;
        case Glyph::Code:
            dl->AddLine(P(-0.35f, -0.6f), P(-0.9f, 0), col, th);
            dl->AddLine(P(-0.9f, 0), P(-0.35f, 0.6f), col, th);
            dl->AddLine(P(0.35f, -0.6f), P(0.9f, 0), col, th);
            dl->AddLine(P(0.9f, 0), P(0.35f, 0.6f), col, th);
            dl->AddLine(P(0.15f, -0.85f), P(-0.15f, 0.85f), col, th);
            break;
        case Glyph::Sliders:
            dl->AddLine(P(-0.9f, -0.45f), P(0.9f, -0.45f), col, th);
            dl->AddLine(P(-0.9f, 0.45f), P(0.9f, 0.45f), col, th);
            dl->AddCircleFilled(P(0.35f, -0.45f), s * 0.22f, col);
            dl->AddCircleFilled(P(-0.35f, 0.45f), s * 0.22f, col);
            break;
        case Glyph::Gear: {
            const float r = s * 0.5f;
            dl->AddCircle(c, r, col, 16, th);
            for (int i = 0; i < 8; ++i) {
                const float t = (float)i / 8.0f * 6.2831853f;
                dl->AddLine(ImVec2(c.x + std::cos(t) * r, c.y + std::sin(t) * r),
                            ImVec2(c.x + std::cos(t) * s * 0.85f, c.y + std::sin(t) * s * 0.85f), col,
                            th);
            }
            break;
        }
    }
}

float OverlayRowHeight() { return ImGui::GetFrameHeight(); }
float OverlayPad() { return IM_TRUNC(ImGui::GetFontSize() * 0.22f); }
float OverlayTextWidth(const char* text) {
    return ImGui::CalcTextSize(text).x + ImGui::GetStyle().FramePadding.x * 1.6f;
}

void OverlayPanel(ImDrawList* dl, ImVec2 min, ImVec2 max) {
    const theme::Semantics& sem = theme::semantics();
    const float r = ImGui::GetStyle().FrameRounding + OverlayPad();
    // No blur in ImGui: a near-opaque ground keeps text readable over any
    // scene, a soft shadow lifts the card off it.
    dl->AddRectFilled(ImVec2(min.x, min.y + 2.0f), ImVec2(max.x, max.y + 3.0f),
                      IM_COL32(0, 0, 0, 70), r);
    ImVec4 bg = sem.ground;
    bg.w = 0.88f;
    dl->AddRectFilled(min, max, ImGui::GetColorU32(bg), r);
    dl->AddRect(min, max, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.08f)), r);
}

bool OverlayIconButton(const char* label, Glyph g, float size, bool active, bool enabled,
                       const char* key) {
    const theme::Semantics& sem = theme::semantics();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(size, size)) && enabled;
    const bool held = enabled && ImGui::IsItemActive();
    const float t = theme::hoverAnim(ImGui::GetItemID(), enabled && ImGui::IsItemHovered(), 12.0f);
    const float r = ImGui::GetStyle().FrameRounding;
    const ImVec2 q(p.x + size, p.y + size);
    if (active)
        dl->AddRectFilled(p, q, ImGui::GetColorU32(sem.accent), r);
    else if (held)
        dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.16f)), r);
    else if (t > 0.0f)
        dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.08f * t)), r);
    const ImU32 ink = !enabled ? fadeCol(sem.textDim, 0.45f)
                      : active ? ImGui::GetColorU32(sem.ground)
                               : ImGui::GetColorU32(sem.text);
    DrawGlyph(dl, g, ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.46f, ink);
    if (key && enabled) {
        ImFont* f = gMono ? gMono : ImGui::GetFont();
        const float fs = ImGui::GetFontSize() * 0.6f;
        const ImVec2 ks = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, key);
        ImVec4 kc = active ? sem.ground : sem.textDim;
        kc.w *= 0.85f;
        dl->AddText(f, fs, ImVec2(q.x - ks.x - size * 0.08f, q.y - ks.y - size * 0.04f),
                    ImGui::GetColorU32(kc), key);
    }
    return pressed;
}

bool OverlayTextButton(const char* label, const char* text, bool active, bool enabled) {
    const theme::Semantics& sem = theme::semantics();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = OverlayTextWidth(text), h = OverlayRowHeight();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h)) && enabled;
    const bool held = enabled && ImGui::IsItemActive();
    const float t = theme::hoverAnim(ImGui::GetItemID(), enabled && ImGui::IsItemHovered(), 12.0f);
    const float r = ImGui::GetStyle().FrameRounding;
    const ImVec2 q(p.x + w, p.y + h);
    if (active)
        dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(sem.accent.x, sem.accent.y, sem.accent.z, 0.22f)), r);
    if (held)
        dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.16f)), r);
    else if (t > 0.0f)
        dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.08f * t)), r);
    const ImU32 ink = !enabled ? fadeCol(sem.textDim, 0.5f)
                      : active ? ImGui::GetColorU32(sem.text)
                               : fadeCol(sem.text, 0.82f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), ink, text);
    return pressed;
}

}  // namespace ui
