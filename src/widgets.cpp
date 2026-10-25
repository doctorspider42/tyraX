#include "widgets.hpp"

#include "theme.hpp"
#include "toolkit.hpp"  // Segmented, for SegmentedRow

#include <imgui_internal.h>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>

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
void setMonoFont(ImFont* f) {
    gMono = f;
    ImGui::GetIO().SliderValueFont = f;  // a thin-track slider's value text (vendor/imgui-tyrax patch)
}
ImFont* boldFont() { return gBold; }
ImFont* monoFont() { return gMono; }

std::string ShortcutChord(const char* key, const char* macKey, const char* sep, bool keycap) {
#ifdef __APPLE__
    (void)sep;
    const bool glyph = ImGui::GetFont()->IsGlyphInFont((ImWchar)0x2318) ||
                       (keycap && monoFont() && monoFont()->IsGlyphInFont((ImWchar)0x2318));
    return std::string(glyph ? "\xE2\x8C\x98" : "Cmd ") + (macKey ? macKey : key);
#else
    (void)macKey;
    (void)keycap;
    return std::string("Ctrl") + sep + key;
#endif
}

float Keycap(ImDrawList* dl, ImVec2 p, float h, const char* text) {
    // The mono face unless it lacks a character the text uses (the Command
    // sign, the Return arrow): then the UI face, which the caller checked.
    ImFont* face = monoFont();
    for (const char* s = text; face && *s;) {
        unsigned int c = 0;
        s += ImTextCharFromUtf8(&c, s, nullptr);
        if (c >= 0x80 && !face->IsGlyphInFont((ImWchar)c)) face = nullptr;
    }
    FontScope mono(face);
    const float fs = ImGui::GetFontSize() * 0.8f;
    const float tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text).x;
    const float pad = h * 0.3f;
    const float w = tw + pad * 2.0f;
    if (dl) {
        const theme::Semantics& sem = theme::semantics();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), fadeCol(sem.surface, 0.8f), h * 0.25f);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), fadeCol(sem.border, 1.0f), h * 0.25f);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + pad, p.y + (h - fs) * 0.5f),
                    fadeCol(sem.textDim, 1.0f), text);
    }
    return w;
}

// The body; AxisDrag3 itself (adding the inspector-row layout) is defined with
// the inspector rows below. `caption` false = the row drew the label already.
static bool axisDrag3Body(const char* label, float v[3], float speed, float vmin, float vmax,
                          const char* fmt, bool caption) {
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
        // ImGui centres a drag's value, so in an inspector-width field (three
        // per row) the number ran into the axis letter ("x17.400"). Unless the
        // field is being typed into, its own text is hidden and the value is
        // drawn left-aligned AFTER the letter instead.
        const ImGuiID dragId = window->GetID("");
        const bool typing = ImGui::TempInputIsActive(dragId);
        {
            FontScope mono(gMono);
            if (!typing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
            changed |= ImGui::DragFloat("", &v[i], speed, vmin, vmax, fmt);
            if (!typing) ImGui::PopStyleColor();
        }
        // The axis letter sits in the left padding of the field ImGui just
        // drew, clipped to the frame so it can never spill into the neighbour.
        const ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImFont* face = gBold ? gBold : ImGui::GetFont();
        const float fs = ImGui::GetFontSize() * 0.8f;
        const ImVec2 ts = face->CalcTextSizeA(fs, FLT_MAX, 0.0f, kAxis[i]);
        const ImVec2 at(r.Min.x + style.FramePadding.x * 0.75f,
                        r.Min.y + (r.GetHeight() - ts.y) * 0.5f);
        window->DrawList->PushClipRect(r.Min, r.Max, true);
        window->DrawList->AddText(face, fs, at, ImGui::GetColorU32(axisCol[i]), kAxis[i]);
        if (!typing) {
            char buf[64];
            ImGui::DataTypeFormatString(buf, IM_ARRAYSIZE(buf), ImGuiDataType_Float, &v[i], fmt);
            FontScope mono(gMono);
            const float th = ImGui::GetTextLineHeight();
            const ImVec2 vt(at.x + ts.x + style.ItemInnerSpacing.x,
                            r.Min.y + (r.GetHeight() - th) * 0.5f);
            // Too wide for the field: show fewer decimals ("%.3f" -> "%.0f")
            // rather than a clipped number. Display only - the value and the
            // typed-in text keep full precision.
            const float room = r.Max.x - style.FramePadding.x * 0.5f - vt.x;
            const char* dot = std::strstr(fmt, "%.");
            if (dot && dot[2] >= '0' && dot[2] <= '9' && dot[3] == 'f') {
                for (int prec = dot[2] - '0' - 1;
                     prec >= 0 && ImGui::CalcTextSize(buf).x > room; --prec) {
                    std::string f(fmt);
                    f[dot - fmt + 2] = (char)('0' + prec);
                    ImGui::DataTypeFormatString(buf, IM_ARRAYSIZE(buf), ImGuiDataType_Float, &v[i],
                                                f.c_str());
                }
                // Still too wide: drop the unit suffix (" deg") - the row's
                // caption says what the numbers are.
                if (ImGui::CalcTextSize(buf).x > room) {
                    std::string f(fmt, (size_t)(dot - fmt) + 4);
                    f[dot - fmt + 2] = '0';
                    ImGui::DataTypeFormatString(buf, IM_ARRAYSIZE(buf), ImGuiDataType_Float, &v[i],
                                                f.c_str());
                }
            }
            window->DrawList->AddText(vt, ImGui::GetColorU32(ImGuiCol_Text), buf);
        }
        window->DrawList->PopClipRect();
        ImGui::PopID();
        ImGui::PopItemWidth();
    }
    ImGui::PopID();

    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    if (caption && label != labelEnd) {
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::TextEx(label, labelEnd);
    }
    ImGui::EndGroup();
    return changed;
}

// Row-layout hooks for Switch, defined with the inspector rows below.
static bool switchRowWidth(float* avail);
static void noteSwitchRow(const ImRect& caption, float textW, bool hasDesc);

bool Switch(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const char* labelEnd = ImGui::FindRenderedTextEnd(label);

    // "Name (what it does)": the name on the line, the parenthesis as a dim,
    // smaller second line. Same item, same label - only the drawing splits.
    const char* nameEnd = labelEnd;
    const char* desc = nullptr;
    if (labelEnd - label >= 4 && labelEnd[-1] == ')')
        for (const char* c = label + 1; c + 2 < labelEnd; ++c)
            if (c[0] == ' ' && c[1] == '(') {
                nameEnd = c;
                desc = c + 2;
                break;
            }
    const char* descEnd = desc ? labelEnd - 1 : nullptr;
    ImFont* font = ImGui::GetFont();
    const float descFs = IM_TRUNC(g.FontSize * 0.86f);
    const ImVec2 nameSize = ImGui::CalcTextSize(label, nameEnd, false);
    const ImVec2 descSize = desc ? font->CalcTextSizeA(descFs, FLT_MAX, 0.0f, desc, descEnd) : ImVec2(0, 0);

    // Track: a pill one text line tall-ish and ~1.8x as wide, vertically centred
    // on the row a checkbox would have occupied.
    const float rowH = ImGui::GetFrameHeight();
    const float trackH = IM_TRUNC(g.FontSize * 1.05f);
    const float trackW = IM_TRUNC(trackH * 1.8f);
    const ImVec2 pos = window->DC.CursorPos;
    const float descGap = IM_TRUNC(g.FontSize * 0.08f);
    const float textH = g.FontSize + (desc ? descGap + descSize.y : 0.0f);
    const float h = ImMax(rowH, textH + style.FramePadding.y * 2.0f);
    // Inside inspector rows a switch is a settings-list row: text on the left,
    // the switch at the row's right end, the whole row clickable.
    float avail = 0.0f;
    const bool row = nameSize.x > 0.0f && switchRowWidth(&avail);
    const float textW = ImMax(nameSize.x, descSize.x);
    const ImRect total(pos, ImVec2(pos.x + (row ? avail
                                                : trackW + (nameSize.x > 0.0f ? style.ItemInnerSpacing.x + textW : 0.0f)),
                                   pos.y + h));
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
    const ImVec2 a(row ? total.Max.x - trackW : pos.x, pos.y + (rowH - trackH) * 0.5f);
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

    if (nameSize.x > 0.0f) {
        const float tx = row ? pos.x : b.x + style.ItemInnerSpacing.x;
        const float tmax = row ? a.x - style.ItemInnerSpacing.x * 2.0f : total.Max.x;
        const ImVec2 tp(tx, pos.y + style.FramePadding.y);
        if (row)
            ImGui::RenderTextEllipsis(dl, tp, ImVec2(tmax, tp.y + nameSize.y + 1.0f), tmax, label, nameEnd,
                                      &nameSize);
        else
            ImGui::RenderText(tp, label, nameEnd);
        if (desc) {
            const ImVec2 dp(tx, tp.y + g.FontSize + descGap);  // before the push resizes g.FontSize
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::PushFont(nullptr, descFs);
            ImGui::RenderTextEllipsis(dl, dp, ImVec2(tmax, dp.y + descSize.y + 1.0f), tmax, desc, descEnd,
                                      &descSize);
            ImGui::PopFont();
            ImGui::PopStyleColor();
        }
    }

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable |
                                               (*v ? ImGuiItemStatusFlags_Checked : 0));
    if (row) noteSwitchRow(ImRect(pos, ImVec2(a.x, pos.y + rowH)), nameSize.x, desc != nullptr);
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
        case Glyph::Road:  // two edges narrowing to the horizon, a dashed centre
            dl->AddLine(P(-0.9f, 0.9f), P(-0.3f, -0.9f), col, th);
            dl->AddLine(P(0.9f, 0.9f), P(0.3f, -0.9f), col, th);
            dl->AddLine(P(0, 0.85f), P(0, 0.45f), col, th);
            dl->AddLine(P(0, 0.15f), P(0, -0.15f), col, th);
            dl->AddLine(P(0, -0.45f), P(0, -0.65f), col, th);
            break;
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
        case Glyph::Plus:
            dl->AddLine(P(-0.75f, 0), P(0.75f, 0), col, th);
            dl->AddLine(P(0, -0.75f), P(0, 0.75f), col, th);
            break;
        case Glyph::Eye:
        case Glyph::EyeOff: {
            // An almond of two quadratic arcs and a filled pupil; the "off"
            // eye is struck through.
            dl->PathLineTo(P(-0.95f, 0));
            dl->PathBezierQuadraticCurveTo(P(0, -0.95f), P(0.95f, 0));
            dl->PathBezierQuadraticCurveTo(P(0, 0.95f), P(-0.95f, 0));
            dl->PathStroke(col, ImDrawFlags_Closed, th);
            dl->AddCircleFilled(c, s * 0.26f, col);
            if (g == Glyph::EyeOff) dl->AddLine(P(-0.8f, 0.8f), P(0.8f, -0.8f), col, th);
            break;
        }
        case Glyph::PanelLeft:
        case Glyph::PanelLeftOff:
        case Glyph::PanelRight:
        case Glyph::PanelRightOff: {
            const bool left = g == Glyph::PanelLeft || g == Glyph::PanelLeftOff;
            const bool on = g == Glyph::PanelLeft || g == Glyph::PanelRight;
            const float x0 = left ? -0.9f : 0.3f, x1 = left ? -0.3f : 0.9f;
            if (on) dl->AddRectFilled(P(x0, -0.7f), P(x1, 0.7f), col, s * 0.08f);
            dl->AddLine(P(left ? x1 : x0, -0.7f), P(left ? x1 : x0, 0.7f), col, th);
            dl->AddRect(P(-0.9f, -0.7f), P(0.9f, 0.7f), col, s * 0.1f, 0, th);
            break;
        }
        case Glyph::Folder: {
            const ImVec2 f[6] = {P(-0.9f, -0.6f), P(-0.25f, -0.6f), P(-0.05f, -0.38f),
                                 P(0.9f, -0.38f), P(0.9f, 0.7f),    P(-0.9f, 0.7f)};
            dl->AddPolyline(f, 6, col, ImDrawFlags_Closed, th);
            break;
        }
        case Glyph::Power:  // a broken ring with a stem: on/off (bypass)
            dl->PathArcTo(c, s * 0.68f, 3.14159f * -0.30f, 3.14159f * 1.30f, 24);
            dl->PathStroke(col, 0, th);
            dl->AddLine(P(0, -0.95f), P(0, -0.15f), col, th);
            break;
        case Glyph::Help: {  // a ring with a question mark
            dl->AddCircle(c, s * 0.82f, col, 24, th);
            dl->PathArcTo(P(0, -0.22f), s * 0.26f, 3.14159f * 1.0f, 3.14159f * 2.35f, 12);
            dl->PathLineTo(P(0, 0.2f));
            dl->PathStroke(col, 0, th);
            dl->AddCircleFilled(P(0, 0.48f), th * 0.75f, col);
            break;
        }
        case Glyph::Funnel: {  // filters
            const ImVec2 f[6] = {P(-0.85f, -0.7f), P(0.85f, -0.7f), P(0.2f, 0.05f),
                                 P(0.2f, 0.8f),    P(-0.2f, 0.6f),  P(-0.2f, 0.05f)};
            dl->AddPolyline(f, 6, col, ImDrawFlags_Closed, th);
            break;
        }
        case Glyph::Copies:  // a square behind a square: repeat
            dl->AddRect(P(-0.85f, -0.35f), P(0.35f, 0.85f), col, s * 0.12f, 0, th);
            dl->AddLine(P(-0.45f, -0.75f), P(0.85f, -0.75f), col, th);
            dl->AddLine(P(0.85f, -0.75f), P(0.85f, 0.45f), col, th);
            break;
        case Glyph::Mask:  // a frame split by a soft edge: a 2D field
            dl->AddRect(P(-0.85f, -0.85f), P(0.85f, 0.85f), col, s * 0.18f, 0, th);
            dl->AddBezierCubic(P(-0.85f, 0.45f), P(-0.2f, 0.35f), P(0.1f, -0.6f), P(0.85f, -0.45f), col, th, 12);
            break;
        case Glyph::Nodes:  // two nodes and a wire: a graph
            dl->AddRect(P(-0.95f, -0.75f), P(-0.25f, -0.15f), col, s * 0.12f, 0, th);
            dl->AddRect(P(0.25f, 0.15f), P(0.95f, 0.75f), col, s * 0.12f, 0, th);
            dl->AddBezierCubic(P(-0.25f, -0.45f), P(0.15f, -0.45f), P(-0.15f, 0.45f), P(0.25f, 0.45f), col,
                               th, 12);
            break;
        case Glyph::Film:  // a film frame with sprocket holes
            dl->AddRect(P(-0.9f, -0.7f), P(0.9f, 0.7f), col, s * 0.1f, 0, th);
            dl->AddLine(P(-0.5f, -0.7f), P(-0.5f, 0.7f), col, th);
            dl->AddLine(P(0.5f, -0.7f), P(0.5f, 0.7f), col, th);
            for (int i = -1; i <= 1; ++i) {
                dl->AddCircleFilled(P(-0.7f, 0.42f * i), s * 0.07f, col);
                dl->AddCircleFilled(P(0.7f, 0.42f * i), s * 0.07f, col);
            }
            break;
        case Glyph::Screen:  // a TV screen with a HUD bar and a menu row
            dl->AddRect(P(-0.95f, -0.75f), P(0.95f, 0.55f), col, s * 0.12f, 0, th);
            dl->AddLine(P(-0.6f, -0.4f), P(-0.05f, -0.4f), col, th);
            dl->AddLine(P(-0.6f, 0.15f), P(0.6f, 0.15f), col, th);
            dl->AddLine(P(-0.35f, 0.9f), P(0.35f, 0.9f), col, th);
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

// --- viewport overlays (blur) ---
namespace {
ImTextureID gBackdrop = (ImTextureID)0;
ImVec2 gBackdropMin, gBackdropMax;
}  // namespace

void SetOverlayBackdrop(ImTextureID tex, ImVec2 imgMin, ImVec2 imgMax) {
    gBackdrop = tex;
    gBackdropMin = imgMin;
    gBackdropMax = imgMax;
}

void OverlayGlassPanel(ImDrawList* dl, ImVec2 min, ImVec2 max) {
    const float iw = gBackdropMax.x - gBackdropMin.x, ih = gBackdropMax.y - gBackdropMin.y;
    if (!gBackdrop || iw <= 0.0f || ih <= 0.0f) {
        OverlayPanel(dl, min, max);
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    const float r = ImGui::GetStyle().FrameRounding + OverlayPad();
    dl->AddRectFilled(ImVec2(min.x, min.y + 2.0f), ImVec2(max.x, max.y + 3.0f),
                      IM_COL32(0, 0, 0, 60), r);
    // The card's own rectangle of the blurred image; v flips (GL is bottom-up).
    const ImVec2 uv0((min.x - gBackdropMin.x) / iw, 1.0f - (min.y - gBackdropMin.y) / ih);
    const ImVec2 uv1((max.x - gBackdropMin.x) / iw, 1.0f - (max.y - gBackdropMin.y) / ih);
    dl->AddImageRounded(gBackdrop, min, max, uv0, uv1, IM_COL32_WHITE, r);
    ImVec4 tint = sem.ground;
    tint.w = 0.6f;
    dl->AddRectFilled(min, max, ImGui::GetColorU32(tint), r);
    dl->AddRect(min, max, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.09f)), r);
}

float OverlayMenuButtonWidth(const char* text) {
    return OverlayTextWidth(text) + ImGui::GetFontSize() * 0.7f;
}

bool OverlayMenuButton(const char* label, const char* text, bool active, bool enabled) {
    const theme::Semantics& sem = theme::semantics();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = OverlayMenuButtonWidth(text), h = OverlayRowHeight();
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
                               : fadeCol(sem.text, 0.86f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float padX = ImGui::GetStyle().FramePadding.x * 0.8f;
    dl->AddText(ImVec2(p.x + padX, p.y + (h - ts.y) * 0.5f), ink, text);
    // Caret: a small open chevron, dimmer than the text.
    const float cs = ImGui::GetFontSize() * 0.2f;  // half width
    const ImVec2 c(q.x - padX - cs, p.y + h * 0.5f + cs * 0.45f);
    const float th = ImMax(1.0f, ImGui::GetFontSize() * 0.075f);
    const ImVec2 pts[3] = {ImVec2(c.x - cs, c.y - cs), c, ImVec2(c.x + cs, c.y - cs)};
    dl->AddPolyline(pts, 3, fadeCol(sem.textDim, 0.95f), 0, th);
    return pressed;
}

void OverlayDivider(ImDrawList* dl, float x, float y0, float y1) {
    dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), IM_COL32(255, 255, 255, 28), 1.0f);
}
// --- end viewport overlays (blur) ---

// --- inspector rows (props) --------------------------------------------------
namespace {

struct RowScope {
    ImGuiWindow* window = nullptr;
    float minW = 0.0f, maxW = 0.0f;
    // The row submitted last, for RowHelp.
    ImGuiID lastId = 0;
    ImRect lastRect, lastCaption;
    float lastCaptionTextW = 0.0f;
    int lastFrame = -1;
    bool lastSwitch = false;   // the last row was a Switch (text spans the row)
    bool lastHasDesc = false;  // ...whose label already carries a "(what it does)" line
    bool meanings = false;     // RowMeanings: RowHelp also writes the tip's first sentence
};
RowScope gRows[4];
int gRowDepth = 0;

RowScope* rowScope() {
    if (gRowDepth <= 0) return nullptr;
    RowScope& s = gRows[gRowDepth - 1];
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w != s.window || w->SkipItems || w->DC.IsSameLine) return nullptr;
    return &s;
}

float captionWidth(const RowScope& s, float avail) {
    return IM_TRUNC(ImClamp(avail * 0.34f, s.minW, ImMax(s.minW, s.maxW)));
}

// One row in flight: the caption drawn, the cursor moved to the field column,
// the width set, the widget's own right-hand label clipped away.
struct Row {
    ImGuiWindow* window = nullptr;
    RowScope* scope = nullptr;
    ImRect caption;
    float textW = 0.0f;
    float fieldX = 0.0f, fieldW = 0.0f;
    bool clip = false;
};

void drawCaption(ImDrawList* dl, ImVec2 at, float maxX, const char* text, const char* end, ImVec4 col) {
    const ImVec2 ts = ImGui::CalcTextSize(text, end);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::RenderTextEllipsis(dl, at, ImVec2(maxX, at.y + ts.y + 1.0f), maxX, text, end, &ts);
    ImGui::PopStyleColor();
}

// SetNextRowFieldTrim: the next row's field leaves this much room on its right.
float gNextRowTrim = 0.0f;

bool beginRow(const char* label, Row& r, bool clipLabel = true) {
    RowScope* s = rowScope();
    if (!s) return false;
    const char* end = ImGui::FindRenderedTextEnd(label);
    if (end == label) return false;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* w = g.CurrentWindow;
    const ImGuiStyle& st = g.Style;
    const ImVec2 p = w->DC.CursorPos;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float cw = captionWidth(*s, avail);
    const float textMax = p.x + cw - st.ItemInnerSpacing.x * 2.0f;
    drawCaption(w->DrawList, ImVec2(p.x, p.y + st.FramePadding.y), textMax, label, end,
                theme::semantics().textDim);
    r.window = w;
    r.scope = s;
    r.caption = ImRect(p, ImVec2(p.x + cw, p.y + ImGui::GetFrameHeight()));
    r.textW = ImGui::CalcTextSize(label, end).x;
    // A caption cut short by the column says the whole of itself on hover.
    // Before the widget: a tooltip call moves ImGui's "last item", and the
    // caller asks the widget's IsItem* afterwards.
    if (ImGui::CalcTextSize(label, end).x > textMax - p.x &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::IsMouseHoveringRect(r.caption.Min, r.caption.Max))
        ImGui::SetTooltip("%.*s", (int)(end - label), label);
    r.fieldX = p.x + cw;
    r.fieldW = ImMax(1.0f, avail - cw - gNextRowTrim);
    gNextRowTrim = 0.0f;
    ImGui::SetCursorScreenPos(ImVec2(r.fieldX, p.y));
    ImGui::SetNextItemWidth(r.fieldW);
    if (clipLabel) {
        ImGui::PushClipRect(ImVec2(r.fieldX, w->ClipRect.Min.y), ImVec2(r.fieldX + r.fieldW, w->ClipRect.Max.y),
                            true);
        r.clip = true;
    }
    return true;
}

void endRow(Row& r) {
    if (!r.window) return;
    if (r.clip) {
        // A BeginCombo that opened leaves the POPUP current: pop the row's
        // window by hand, the way ImGui::PopClipRect would.
        if (ImGui::GetCurrentWindow() == r.window) {
            ImGui::PopClipRect();
        } else {
            r.window->DrawList->PopClipRect();
            r.window->ClipRect = r.window->DrawList->_ClipRectStack.back();
        }
    }
    ImGuiContext& g = *GImGui;
    r.scope->lastId = g.LastItemData.ID;
    r.scope->lastRect = g.LastItemData.Rect;
    r.scope->lastCaption = r.caption;
    r.scope->lastCaptionTextW = r.textW;
    r.scope->lastFrame = g.FrameCount;
    r.scope->lastSwitch = false;
    r.scope->lastHasDesc = false;
}

}  // namespace

// Used by Switch (defined above the row machinery).
static bool switchRowWidth(float* avail) {
    if (!rowScope()) return false;
    *avail = ImGui::GetContentRegionAvail().x;
    return true;
}

static void noteSwitchRow(const ImRect& caption, float textW, bool hasDesc) {
    ImGuiContext& g = *GImGui;
    RowScope& s = gRows[gRowDepth - 1];
    s.lastId = g.LastItemData.ID;
    s.lastRect = g.LastItemData.Rect;
    s.lastCaption = caption;
    s.lastCaptionTextW = textW;
    s.lastFrame = g.FrameCount;
    s.lastSwitch = true;
    s.lastHasDesc = hasDesc;
}

static void drawRowMeaning(RowScope& s, const char* tip);

namespace {

template <class F>
bool rowWidget(const char* label, F&& widget) {
    Row r;
    beginRow(label, r);
    const bool res = widget();
    endRow(r);
    return res;
}

}  // namespace

InspectorRows::InspectorRows(float minLabelW, float maxLabelW) {
    IM_ASSERT(gRowDepth < IM_ARRAYSIZE(gRows));
    RowScope& s = gRows[gRowDepth++];
    s = RowScope();
    s.window = ImGui::GetCurrentWindow();
    s.minW = minLabelW;
    s.maxW = maxLabelW;
}

InspectorRows::~InspectorRows() {
    if (gRowDepth > 0) --gRowDepth;
}

bool RowHelp(const char* tip) {
    if (gRowDepth <= 0) return false;
    RowScope& s = gRows[gRowDepth - 1];
    ImGuiContext& g = *GImGui;
    if (g.CurrentWindow != s.window || s.lastFrame != g.FrameCount || s.lastId != g.LastItemData.ID ||
        s.lastRect.Min.x != g.LastItemData.Rect.Min.x || s.lastRect.Min.y != g.LastItemData.Rect.Min.y)
        return false;
    s.lastFrame = -1;  // one help per row
    if (s.window->SkipItems) return true;
    // A dotted underline under the caption: "there is more to read here".
    const ImRect& c = s.lastCaption;
    const float y = c.Min.y + g.Style.FramePadding.y + g.FontSize + 1.0f;
    ImVec4 col = theme::semantics().textDim;
    col.w *= 0.7f;
    const ImU32 u = ImGui::GetColorU32(col);
    const float x1 = c.Min.x + ImMin(c.GetWidth() - g.Style.ItemInnerSpacing.x * 2.0f, s.lastCaptionTextW);
    for (float x = c.Min.x; x + 1.0f < x1; x += 3.0f)
        s.window->DrawList->AddRectFilled(ImVec2(x, y), ImVec2(x + 1.0f, y + 1.0f), u);
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::IsMouseHoveringRect(c.Min, c.Max))
        ImGui::SetTooltip("%s", tip);
    if (s.meanings && !s.lastHasDesc) drawRowMeaning(s, tip);
    return true;
}

bool AxisDrag3(const char* label, float v[3], float speed, float vmin, float vmax, const char* fmt) {
    Row r;
    const bool row = beginRow(label, r, false);
    const bool changed = axisDrag3Body(label, v, speed, vmin, vmax, fmt, !row);
    endRow(r);
    return changed;
}

bool InputText(const char* label, char* buf, size_t size, ImGuiInputTextFlags flags, ImGuiInputTextCallback cb,
               void* user) {
    return rowWidget(label, [&] { return ImGui::InputText(label, buf, size, flags, cb, user); });
}
bool BeginCombo(const char* label, const char* preview, ImGuiComboFlags flags) {
    return rowWidget(label, [&] { return ImGui::BeginCombo(label, preview, flags); });
}
bool Combo(const char* label, int* cur, const char* const items[], int count, int maxHeight) {
    return rowWidget(label, [&] { return ImGui::Combo(label, cur, items, count, maxHeight); });
}
PreviewNav PreviewNavigation() {
    PreviewNav n;
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActive()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) n.orbit = io.MouseDelta;
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) n.pan = io.MouseDelta;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    if (ImGui::IsItemHovered()) {
        n.wheel = io.MouseWheel;
        n.recentre = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle);
    }
    return n;
}

bool SegmentedRow(const char* label, const char* const items[], int count, int* cur) {
    return rowWidget(label, [&] { return Segmented(label, items, count, cur, ImGui::CalcItemWidth()); });
}
bool Combo(const char* label, int* cur, const char* itemsZeroSeparated, int maxHeight) {
    return rowWidget(label, [&] { return ImGui::Combo(label, cur, itemsZeroSeparated, maxHeight); });
}
bool DragFloat(const char* label, float* v, float speed, float vmin, float vmax, const char* fmt,
               ImGuiSliderFlags flags) {
    return rowWidget(label, [&] { return ImGui::DragFloat(label, v, speed, vmin, vmax, fmt, flags); });
}
bool DragFloat2(const char* label, float v[2], float speed, float vmin, float vmax, const char* fmt,
                ImGuiSliderFlags flags) {
    return rowWidget(label, [&] { return ImGui::DragFloat2(label, v, speed, vmin, vmax, fmt, flags); });
}
bool DragInt(const char* label, int* v, float speed, int vmin, int vmax, const char* fmt, ImGuiSliderFlags flags) {
    return rowWidget(label, [&] { return ImGui::DragInt(label, v, speed, vmin, vmax, fmt, flags); });
}
// The next slider paints its track as a gradient: the in-tree ImGui patch
// (style.SliderThinTrack, vendor/imgui-tyrax/TYRAX-PATCHES.md) draws every
// slider - inspector row or not - so the cue rides along on the context.
void SetNextRowFieldTrim(float px) { gNextRowTrim = px; }

void SetNextSliderGradient(ImU32 from, ImU32 to) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    g.SliderNextTrackFrom = from;
    g.SliderNextTrackTo = to;
}

bool SliderFloat(const char* label, float* v, float vmin, float vmax, const char* fmt, ImGuiSliderFlags flags) {
    return rowWidget(label, [&] { return ImGui::SliderFloat(label, v, vmin, vmax, fmt, flags); });
}
bool SliderInt(const char* label, int* v, int vmin, int vmax, const char* fmt, ImGuiSliderFlags flags) {
    return rowWidget(label, [&] { return ImGui::SliderInt(label, v, vmin, vmax, fmt, flags); });
}

bool ColorHex(const char* label, float col[3]) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& st = g.Style;
    const bool mixed = (g.CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0;
    Row r;
    const bool row = beginRow(label, r, false);
    const float fullW = row ? r.fieldW : ImGui::CalcItemWidth();
    const float h = ImGui::GetFrameHeight();
    const float swatchW = IM_TRUNC(h * 1.6f);
    bool changed = false;

    ImGui::BeginGroup();
    // The swatch carries the label's ID: it is "the" colour control.
    const ImVec4 c4(col[0], col[1], col[2], 1.0f);
    const bool swatch = ImGui::ColorButton(label, c4, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha,
                                           ImVec2(swatchW, h));
    ImGui::PushID(label);
    if (swatch) ImGui::OpenPopup("picker");
    if (ImGui::BeginPopup("picker")) {
        if (ImGui::ColorPicker3("##picker", col,
                                ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview |
                                    ImGuiColorEditFlags_DisplayRGB)) {
            changed = true;
            // Not every inner picker widget marks itself edited; the group
            // reports the end of the drag as an edit only if one did.
            if (g.ActiveId) {
                g.ActiveIdHasBeenEditedThisFrame = true;
                g.ActiveIdHasBeenEditedBefore = true;
            }
        }
        ImGui::EndPopup();
    }
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID openKey = ImGui::GetID("rgbopen");
    bool open = store->GetBool(openKey, false);
    ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
    char hex[16];
    if (mixed)
        hex[0] = 0;
    else
        std::snprintf(hex, sizeof hex, "#%02X%02X%02X", (int)(ImSaturate(col[0]) * 255.0f + 0.5f),
                      (int)(ImSaturate(col[1]) * 255.0f + 0.5f), (int)(ImSaturate(col[2]) * 255.0f + 0.5f));
    ImGui::SetNextItemWidth(ImMax(1.0f, fullW - swatchW - h - st.ItemInnerSpacing.x * 2.0f));
    {
        FontScope mono(gMono);
        if (ImGui::InputTextWithHint("##hex", mixed ? "mixed" : "", hex, sizeof hex,
                                     ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_AutoSelectAll)) {
            const char* s = hex[0] == '#' ? hex + 1 : hex;
            unsigned v = 0;
            int n = 0;
            for (; s[n]; ++n) {
                const char ch = s[n];
                const int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
                if (d < 0 || n >= 6) {
                    n = -1;
                    break;
                }
                v = v * 16 + (unsigned)d;
            }
            if (n == 6) {
                col[0] = (float)((v >> 16) & 255) / 255.0f;
                col[1] = (float)((v >> 8) & 255) / 255.0f;
                col[2] = (float)(v & 255) / 255.0f;
                changed = true;
            }
        }
    }
    ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
    if (ImGui::ArrowButton("##rgb", open ? ImGuiDir_Down : ImGuiDir_Right)) {
        open = !open;
        store->SetBool(openKey, open);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(open ? "Hide the 0-255 fields" : "Edit as 0-255 RGB");
    if (open) {
        ImGui::SetNextItemWidth(fullW);
        changed |= ImGui::ColorEdit3("##rgbfields", col,
                                     ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoPicker |
                                         ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoLabel |
                                         ImGuiColorEditFlags_NoOptions);
    }
    ImGui::PopID();
    if (!row) {
        // Stock layout: the caption after the control, like ColorEdit3.
        const char* end = ImGui::FindRenderedTextEnd(label);
        if (end != label) {
            ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
            ImGui::TextEx(label, end);
        }
    }
    ImGui::EndGroup();
    endRow(r);
    return changed;
}

bool BeginQuietTabBar(const char* id) {
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(1, 1, 1, 0.05f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TabDimmed, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, ImVec4(0, 0, 0, 0));
    const bool open = ImGui::BeginTabBar(id, ImGuiTabBarFlags_FittingPolicyShrink);
    if (open) {
        // The bar's own bottom line is drawn in TabSelected (now clear): a
        // hairline instead, the selected tab's underline drawn over it.
        const ImRect bar = GImGui->CurrentTabBar->BarRect;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(bar.Min.x, bar.Max.y - 0.5f), ImVec2(bar.Max.x, bar.Max.y - 0.5f),
                                            ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    } else {
        ImGui::PopStyleColor(7);
    }
    return open;
}

bool QuietTab(const char* label, bool selected, ImGuiTabItemFlags flags) {
    const theme::Semantics& sem = theme::semantics();
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? sem.text : sem.textDim);
    const bool open = ImGui::BeginTabItem(label, nullptr, flags);
    ImGui::PopStyleColor();
    if (open) {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const float th = ImMax(2.0f, IM_TRUNC(ImGui::GetFontSize() * 0.12f));
        const float inset = ImGui::GetStyle().FramePadding.x * 0.5f;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x + inset, b.y - th), ImVec2(b.x - inset, b.y),
                                                  ImGui::GetColorU32(sem.accent), th * 0.5f);
        ImGui::EndTabItem();
    }
    return open;
}

void EndQuietTabBar() {
    ImGui::EndTabBar();
    ImGui::PopStyleColor(7);
}

bool MoreButton(const char* label, float size) {
    const theme::Semantics& sem = theme::semantics();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(size, size));
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    const float r = ImGui::GetStyle().FrameRounding;
    const ImVec2 q(p.x + size, p.y + size);
    if (ImGui::IsItemActive())
        dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.14f)), r);
    else if (t > 0.0f)
        dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.08f * t)), r);
    const float dr = ImMax(1.5f, size * 0.06f), gap = size * 0.2f;
    const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
    for (int i = -1; i <= 1; ++i)
        dl->AddCircleFilled(ImVec2(c.x + gap * (float)i, c.y), dr, ImGui::GetColorU32(sem.text), 10);
    return pressed;
}

// --- panel headers / outliner ---------------------------------------------
namespace {
// The slots PanelHeader reserved for its buttons (one header at a time).
ImVec2 gSlotPos;
float gSlotSize = 0.0f, gSlotGap = 0.0f;
int gSlotNext = 0;

// While the panel is untabbed, dragging the item just submitted undocks the
// panel - the same call ImGui's own hidden-tab-bar triangle makes, so the
// panel then floats and docks anywhere like one dragged by its tab.
// Only while untabbed: a tabbed panel has its tab (which undocks that one
// window, not the whole node), and a floating one its title bar - ImGui only
// offers dock targets to a window dragged by its top frame-height
// (BeginDockableDragDropSource), so a header further down could move a
// floating panel but never dock it again.
void grabHandleBehaviour() {
    if (!PanelIsUntabbed()) return;
    ImGuiWindow* win = ImGui::GetCurrentWindow()->RootWindow;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        ImGui::StartMouseMovingWindowOrNode(win, win->DockNode, true);
}

float chipHeight() {
    return IM_TRUNC(ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 1.2f);
}
float chipPadX() { return IM_TRUNC(ImGui::GetFontSize() * 0.6f); }
}  // namespace

void PanelChromeNewFrame(const char* const* names, int count) {
    for (int i = 0; i < count; ++i) {
        ImGuiWindow* w = ImGui::FindWindowByName(names[i]);
        if (!w) continue;
        // The window class's override is merged into its node's flags by the
        // node update inside DockSpaceOverViewport - this frame, before the
        // panel's own Begin - and, unlike the node's own flags, never reaches
        // a saved layout. A second window docked in brings the tabs back.
        ImGuiDockNodeFlags& f = w->WindowClass.DockNodeFlagsOverrideSet;
        if (w->DockNode && w->DockNode->Windows.Size == 1)
            f |= ImGuiDockNodeFlags_NoTabBar;
        else
            f &= ~ImGuiDockNodeFlags_NoTabBar;
    }
}

bool PanelIsUntabbed() {
    const ImGuiWindow* w = ImGui::GetCurrentWindowRead();
    if (!w) return false;
    w = w->RootWindow;
    return w->DockIsActive && w->DockNode &&
           (w->DockNode->IsNoTabBar() || w->DockNode->IsHiddenTabBar());
}

void PanelHeader(const char* title, const char* count, int buttons) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const theme::Semantics& sem = theme::semantics();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float rowH = ImGui::GetFrameHeight();
    const float availW = ImGui::GetContentRegionAvail().x;
    gSlotSize = rowH;
    gSlotGap = IM_TRUNC(st.ItemInnerSpacing.x * 0.5f);
    gSlotNext = 0;
    const float slotsW = buttons > 0 ? buttons * gSlotSize + (buttons - 1) * gSlotGap : 0.0f;
    gSlotPos = ImVec2(p0.x + availW - slotsW, p0.y);

    ImGui::InvisibleButton(
        "Move panel##panel_grab",
        ImVec2(ImMax(1.0f, availW - slotsW - (buttons > 0 ? st.ItemSpacing.x : 0.0f)), rowH));
    grabHandleBehaviour();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = p0.x;
    {
        FontScope bold(gBold);
        const float ty = p0.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(x, ty), ImGui::GetColorU32(sem.text), title);
        x += ImGui::CalcTextSize(title).x;
    }
    if (count && *count) {
        const float ty = p0.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(x + st.ItemInnerSpacing.x * 1.5f, ty), fadeCol(sem.textDim, 0.85f), count);
    }
}

bool PanelHeaderButton(const char* label, Glyph g, const char* tooltip) {
    const theme::Semantics& sem = theme::semantics();
    // On the header's line (SameLine), so the row ends after the last button
    // and nothing has to put the cursor back.
    const ImVec2 p(gSlotPos.x + gSlotNext * (gSlotSize + gSlotGap), gSlotPos.y);
    ++gSlotNext;
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(p);
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(gSlotSize, gSlotSize));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hovered, 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + gSlotSize, p.y + gSlotSize);
    const float r = ImGui::GetStyle().FrameRounding;
    if (held) dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.16f)), r);
    else if (t > 0.0f) dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.08f * t)), r);
    const ImU32 ink = hovered || held ? ImGui::GetColorU32(sem.text) : fadeCol(sem.text, 0.72f);
    DrawGlyph(dl, g, ImVec2(p.x + gSlotSize * 0.5f, p.y + gSlotSize * 0.5f), gSlotSize * 0.46f, ink);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

void PanelInlineTitle(const char* title) {
    if (!PanelIsUntabbed()) return;
    const theme::Semantics& sem = theme::semantics();
    FontScope bold(gBold);
    const ImVec2 ts = ImGui::CalcTextSize(title);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::InvisibleButton("Move panel##panel_grab", ImVec2(ts.x, h));
    grabHandleBehaviour();
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x, p.y + (h - ts.y) * 0.5f),
                                        ImGui::GetColorU32(sem.text), title);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
}

float FilterChipWidth(const char* label, int count) {
    float w = ImGui::CalcTextSize(label, nullptr, true).x + chipPadX() * 2.0f;
    if (count >= 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", count);
        w += ImGui::CalcTextSize(buf).x + ImGui::GetStyle().ItemInnerSpacing.x;
    }
    return w;
}

bool FilterChip(const char* label, bool selected, int count) {
    const theme::Semantics& sem = theme::semantics();
    const float w = FilterChipWidth(label, count), h = chipHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    const ImGuiID id = ImGui::GetItemID();
    ImGuiContext& g = *GImGui;
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label,
                                g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable |
                                    (selected ? ImGuiItemStatusFlags_Checked : 0));
    const float t = theme::hoverAnim(id, ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const float r = h * 0.5f;
    if (selected) {
        dl->AddRectFilled(p, q, fadeCol(sem.accent, 0.22f), r);
        dl->AddRect(p, q, fadeCol(sem.accent, 0.55f), r);
    } else {
        if (t > 0.0f) dl->AddRectFilled(p, q, fadeCol(sem.surface, t), r);
        dl->AddRect(p, q, ImGui::GetColorU32(sem.border), r);
    }
    const char* end = ImGui::FindRenderedTextEnd(label);
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    float x = p.x + chipPadX();
    dl->AddText(ImVec2(x, ty), selected ? ImGui::GetColorU32(sem.text) : fadeCol(sem.text, 0.80f), label,
                end);
    if (count >= 0) {
        x += ImGui::CalcTextSize(label, end).x + ImGui::GetStyle().ItemInnerSpacing.x;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", count);
        dl->AddText(ImVec2(x, ty), fadeCol(sem.textDim, 0.85f), buf);
    }
    return pressed;
}

float LayerChipWidth(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + chipPadX() * 2.0f +
           ImGui::GetFontSize() * 0.6f + ImGui::GetStyle().ItemInnerSpacing.x;
}

bool LayerChip(const char* label, ImU32 colour, bool visible) {
    const theme::Semantics& sem = theme::semantics();
    const float w = LayerChipWidth(label), h = chipHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    const ImGuiID id = ImGui::GetItemID();
    ImGuiContext& g = *GImGui;
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label,
                                g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable |
                                    (visible ? ImGuiItemStatusFlags_Checked : 0));
    const float t = theme::hoverAnim(id, ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const float r = h * 0.5f;
    if (t > 0.0f) dl->AddRectFilled(p, q, fadeCol(sem.surface, t), r);
    dl->AddRect(p, q, ImGui::GetColorU32(sem.border), r);
    // The swatch is filled while the layer shows and an outline while it is
    // hidden, so the state reads without relying on the dimmed text alone.
    const float sq = IM_TRUNC(ImGui::GetFontSize() * 0.6f);
    const ImVec2 s0(p.x + chipPadX(), p.y + IM_TRUNC((h - sq) * 0.5f)), s1(s0.x + sq, s0.y + sq);
    if (visible) dl->AddRectFilled(s0, s1, colour, 2.0f);
    else dl->AddRect(s0, s1, colour, 2.0f, 0, 1.2f);
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(s1.x + ImGui::GetStyle().ItemInnerSpacing.x, ty),
                visible ? fadeCol(sem.text, 0.85f) : fadeCol(sem.textDim, 0.60f), label,
                ImGui::FindRenderedTextEnd(label));
    return pressed;
}

void ChipFlow(float w, bool first) {
    if (first) return;
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
}

bool EyeToggle(const char* label, bool visible, float size, bool show) {
    const theme::Semantics& sem = theme::semantics();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(size, size));
    const ImGuiID id = ImGui::GetItemID();
    ImGuiContext& g = *GImGui;
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label,
                                g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable |
                                    (visible ? ImGuiItemStatusFlags_Checked : 0));
    const bool hovered = ImGui::IsItemHovered();
    if (show || hovered) {
        const ImU32 ink = hovered ? ImGui::GetColorU32(sem.text)
                                  : fadeCol(sem.textDim, visible ? 0.9f : 0.75f);
        DrawGlyph(ImGui::GetWindowDrawList(), visible ? Glyph::Eye : Glyph::EyeOff,
                  ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.62f, ink);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(visible ? "Hide in the editor" : "Show in the editor");
    return pressed;
}

// --- verify-world (polish-3) ---
// RowMeanings: the one-line meaning under a row's caption, written from the
// first sentence of the help that row already has (RowHelp's tip), so the
// explanation that decides whether to touch a setting is on screen and the
// rest stays a hover. Hard line breaks are joined, a "(docs/...)" pointer is
// dropped, and the line is cut with an ellipsis at the caption column (a
// switch row's text spans the row, so there it may run to the switch).
RowMeanings::RowMeanings() {
    if (gRowDepth > 0) {
        prev_ = gRows[gRowDepth - 1].meanings;
        gRows[gRowDepth - 1].meanings = true;
    }
}

RowMeanings::~RowMeanings() {
    if (gRowDepth > 0) gRows[gRowDepth - 1].meanings = prev_;
}

// One sentence of the tip's first paragraph: line breaks joined, a "(docs/...)"
// pointer dropped. A sentence that opens in capitals is a headline for the
// long read ("WHAT BLOWS THE REDUCED RENDER BACK UP, ...") - the next one says
// what the setting does.
static std::string firstSentence(const char* tip) {
    const char* c = tip;
    for (int tries = 0; tries < 3 && *c; ++tries) {
        std::string out;
        for (; *c; ++c) {
            if (c[0] == '\n' && c[1] == '\n') break;  // a paragraph ends it too
            const char ch = *c == '\n' ? ' ' : *c;
            if (ch == ' ' && (out.empty() || out.back() == ' ')) continue;
            out += ch;
            if (ch == '.' && (c[1] == ' ' || c[1] == '\n' || c[1] == 0)) {
                ++c;
                break;
            }
        }
        for (size_t at = out.find(" (docs/"); at != std::string::npos; at = out.find(" (docs/")) {
            const size_t close = out.find(')', at);
            if (close == std::string::npos) break;
            out.erase(at, close - at + 1);
        }
        while (!out.empty() && (out.back() == ' ' || out.back() == '.' || out.back() == ':')) out.pop_back();
        int letters = 0, upper = 0;
        for (char ch : out) {
            if (letters >= 14) break;
            if (std::isalpha((unsigned char)ch)) {
                ++letters;
                upper += std::isupper((unsigned char)ch) ? 1 : 0;
            }
        }
        if (letters > 0 && upper * 4 < letters * 3) return out;
        if (c[0] == '\n' && c[1] == '\n') break;
    }
    return std::string();
}

static void drawRowMeaning(RowScope& s, const char* tip) {
    const std::string line = firstSentence(tip);
    if (line.empty()) return;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* w = s.window;
    const float fsz = IM_TRUNC(g.FontSize * 0.86f);
    const float x0 = s.lastCaption.Min.x;
    const float x1 = s.lastSwitch ? s.lastRect.Max.x - g.FontSize * 3.0f
                                  : s.lastCaption.Max.x - g.Style.ItemInnerSpacing.x * 2.0f;
    // Tucked under the caption: the row's frame padding is the gap.
    const float y = ImMax(s.lastCaption.Max.y, s.lastRect.Max.y) - g.Style.FramePadding.y * 0.6f;
    ImFont* font = ImGui::GetFont();
    const char* text = line.c_str();
    const char* end = text + line.size();
    const float maxW = ImMax(1.0f, x1 - x0);
    std::string shown = line;
    if (font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, text, end).x > maxW) {
        const float dotsW = font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, "...").x;
        size_t n = line.size();
        while (n > 0 && font->CalcTextSizeA(fsz, FLT_MAX, 0.0f, text, text + n).x + dotsW > maxW) --n;
        while (n > 0 && line[n - 1] == ' ') --n;
        shown = line.substr(0, n) + "...";
    }
    ImVec4 col = theme::semantics().textDim;
    col.w *= 0.85f;
    w->DrawList->AddText(font, fsz, ImVec2(x0, y), ImGui::GetColorU32(col), shown.c_str());
    // Room for it: the cursor is already below the row, so only the part of
    // the line that reaches past it is added.
    const float bottom = y + fsz + g.Style.ItemSpacing.y;
    if (bottom > w->DC.CursorPos.y) {
        w->DC.CursorPos.y = bottom;
        w->DC.CursorMaxPos.y = ImMax(w->DC.CursorMaxPos.y, bottom);
    }
}
void NoteRow(ImVec2 captionMin, ImVec2 captionMax, float captionTextW) {
    const ImRect caption(captionMin, captionMax);
    if (gRowDepth <= 0) return;
    RowScope& s = gRows[gRowDepth - 1];
    ImGuiContext& g = *GImGui;
    if (g.CurrentWindow != s.window) return;
    s.lastId = g.LastItemData.ID;
    s.lastRect = g.LastItemData.Rect;
    s.lastCaption = caption;
    s.lastCaptionTextW = captionTextW;
    s.lastFrame = g.FrameCount;
    s.lastSwitch = false;
    s.lastHasDesc = false;
}
// --- end verify-world (polish-3) ---

// --- node inspector kit ---
namespace {

// The caption every kit section starts with: a hairline, then the text in a
// smaller bold cut, upper-cased (stops at "##"). Returns the caption line's
// rectangle; `reserveRight` px at its end stay free for a control.
ImRect kitCaption(const char* text, const char* right, float reserveRight) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const ImGuiStyle& st = ImGui::GetStyle();
    const theme::Semantics& sem = theme::semantics();
    const float W = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float lineY = IM_TRUNC(p.y + st.ItemSpacing.y * 0.5f);
    w->DrawList->AddLine(ImVec2(p.x, lineY), ImVec2(p.x + W, lineY), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, st.ItemSpacing.y * 0.6f));
    p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    const float small = IM_TRUNC(ImGui::GetFontSize() * 0.80f);
    std::string up;
    for (const char* c = text; *c && !(c[0] == '#' && c[1] == '#'); ++c)
        up += (char)std::toupper((unsigned char)*c);
    ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
    {
        FontScope bold(gBold);
        ImGui::PushFont(nullptr, small);
        const ImVec2 ts = ImGui::CalcTextSize(up.c_str());
        w->DrawList->AddText(ImVec2(p.x, p.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), up.c_str());
        ImGui::PopFont();
    }
    if (right && *right) {
        const ImVec2 rs = ImGui::CalcTextSize(right);
        w->DrawList->AddText(ImVec2(p.x + W - reserveRight - rs.x, p.y + (h - rs.y) * 0.5f),
                             ImGui::GetColorU32(ImGuiCol_Text), right);
    }
    ImGui::PopStyleColor();
    return ImRect(p, ImVec2(p.x + W, p.y + h));
}

// Re-reports the item just submitted under `name`, so a field whose ImGui
// label is "##x" is still reachable by --ui-script.
void reportAs(const char* name, ImGuiItemStatusFlags extra = 0) {
    ImGuiContext& g = *GImGui;
    (void)g;
    (void)name;
    (void)extra;
    IMGUI_TEST_ENGINE_ITEM_INFO(g.LastItemData.ID, name, g.LastItemData.StatusFlags | extra);
}

}  // namespace

void KitSection(const char* text, const char* right) {
    if (ImGui::GetCurrentWindow()->SkipItems) return;
    const ImRect r = kitCaption(text, right, 0.0f);
    ImGui::SetCursorScreenPos(r.Min);
    ImGui::Dummy(r.GetSize());
}

bool KitSectionSwitch(const char* text, bool* on) {
    if (ImGui::GetCurrentWindow()->SkipItems) return false;
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float trackH = IM_TRUNC(fs * 0.95f), trackW = IM_TRUNC(trackH * 1.8f);
    const ImRect r = kitCaption(text, nullptr, trackW);
    ImGui::SetCursorScreenPos(ImVec2(r.Max.x - trackW, r.Min.y + (r.GetHeight() - trackH) * 0.5f));
    const bool pressed = ImGui::InvisibleButton(text, ImVec2(trackW, trackH));
    if (pressed) {
        *on = !*on;
        ImGui::MarkItemEdited(ImGui::GetItemID());
    }
    reportAs(text, ImGuiItemStatusFlags_Checkable | (*on ? ImGuiItemStatusFlags_Checked : 0));
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 off = sem.border;
    off = ImVec4(off.x * 1.25f, off.y * 1.25f, off.z * 1.25f, 1.0f);
    dl->AddRectFilled(a, b, ImGui::GetColorU32(*on ? sem.accent : off), trackH * 0.5f);
    const float kr = trackH * 0.5f - IM_TRUNC(trackH * 0.12f) - 0.5f;
    const float kx = *on ? b.x - trackH * 0.5f : a.x + trackH * 0.5f;
    dl->AddCircleFilled(ImVec2(kx, a.y + trackH * 0.5f), kr, IM_COL32(255, 255, 255, 255), 20);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %s", text, *on ? "on" : "off");
    ImGui::SetCursorScreenPos(ImVec2(r.Min.x, r.Max.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    return pressed;
}

bool DragRow(const char* label, const KitDrag* fields, int n) {
    if (ImGui::GetCurrentWindow()->SkipItems || n <= 0) return false;
    Row r;
    const bool row = beginRow(label, r, false);
    const float total = row ? r.fieldW : ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const float each = IM_TRUNC((total - gap * (float)(n - 1)) / (float)n);
    bool changed = false;
    ImGui::BeginGroup();
    ImGui::PushID(label);
    for (int i = 0; i < n; ++i) {
        const KitDrag& f = fields[i];
        if (i) ImGui::SameLine(0.0f, gap);
        ImGui::SetNextItemWidth(i == n - 1 ? ImMax(1.0f, total - (each + gap) * (float)(n - 1)) : each);
        char id[8];
        std::snprintf(id, sizeof id, "##f%d", i);
        float v = *f.v;
        if (ImGui::DragFloat(id, &v, f.speed, f.vmin, f.vmax, f.fmt,
                             f.vmin < f.vmax ? ImGuiSliderFlags_AlwaysClamp : 0)) {
            *f.v = f.isInt ? std::round(v) : v;
            changed = true;
        }
        char name[160];
        if (f.name) std::snprintf(name, sizeof name, "%s", f.name);
        else if (i == 0) std::snprintf(name, sizeof name, "%s", label);
        else std::snprintf(name, sizeof name, "%s#%d", label, i + 1);
        reportAs(name);
    }
    ImGui::PopID();
    ImGui::EndGroup();
    if (row) endRow(r);
    return changed;
}

bool RangeDrag(const char* label, float* lo, float* hi, const RangeSpec& s) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& st = g.Style;
    const theme::Semantics& sem = theme::semantics();
    Row r;
    const bool row = beginRow(label, r, false);
    const float fieldW = row ? r.fieldW : ImGui::GetContentRegionAvail().x;
    const float fs = g.FontSize;
    const char* nm = s.name ? s.name : label;
    bool changed = false;
    ImGui::BeginGroup();

    // The track: one control, either knob draggable (the nearer one wins).
    const float th = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(fieldW, th));
    reportAs(nm);
    const ImGuiID id = ImGui::GetItemID();
    const float span = s.viewMax > s.viewMin ? s.viewMax - s.viewMin : 1.0f;
    const float pad = IM_TRUNC(fs * 0.45f);  // knobs stay inside the field
    const float tw = ImMax(1.0f, fieldW - pad * 2.0f);
    auto xOf = [&](float v) { return p.x + pad + ImClamp((v - s.viewMin) / span, 0.0f, 1.0f) * tw; };
    auto vOf = [&](float x) { return s.viewMin + ImClamp((x - p.x - pad) / tw, 0.0f, 1.0f) * span; };
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID knobKey = id + 0x6bu;
    if (ImGui::IsItemActivated()) {
        const float mx = g.IO.MousePos.x;
        int k = std::fabs(mx - xOf(*lo)) <= std::fabs(mx - xOf(*hi)) ? 0 : 1;
        if (*lo == *hi) k = mx > xOf(*lo) ? 1 : 0;
        store->SetInt(knobKey, k);
    }
    const bool active = ImGui::IsItemActive();
    const int knob = store->GetInt(knobKey, 0);
    if (active) {
        float v = ImClamp(vOf(g.IO.MousePos.x), s.vmin, s.vmax);
        if (s.speed > 0.0f) v = std::round(v / s.speed) * s.speed;
        if (knob == 0) {
            v = ImMin(v, *hi);
            if (v != *lo) *lo = v, changed = true;
        } else {
            v = ImMax(v, *lo);
            if (v != *hi) *hi = v, changed = true;
        }
        if (changed) ImGui::MarkItemEdited(id);
    }
    ImDrawList* dl = w->DrawList;
    const float cy = p.y + th * 0.5f;
    const float rh = IM_TRUNC(fs * (s.gradient ? 0.30f : 0.16f));
    const ImVec2 a(p.x + pad, cy - rh), b(p.x + pad + tw, cy + rh);
    if (s.gradient) {
        const ImU32 k0 = IM_COL32(12, 14, 18, 255), k1 = IM_COL32(232, 234, 240, 255);
        dl->AddRectFilledMultiColor(a, b, k0, k1, k1, k0);
    } else {
        dl->AddRectFilled(a, b, ImGui::GetColorU32(sem.field), rh);
    }
    dl->AddRect(a, b, ImGui::GetColorU32(sem.border), s.gradient ? 2.0f : rh);
    const float x0 = xOf(*lo), x1 = xOf(*hi);
    const ImU32 acc = ImGui::GetColorU32(sem.accent);
    if (s.falloff && *s.falloff > 0.0f) {
        const ImU32 clear = fadeCol(sem.accent, 0.0f), half = fadeCol(sem.accent, 0.55f);
        const float f0 = xOf(*lo - *s.falloff), f1 = xOf(*hi + *s.falloff);
        const float ry = IM_TRUNC(rh * 0.5f);
        if (f0 < x0) dl->AddRectFilledMultiColor(ImVec2(f0, cy - ry), ImVec2(x0, cy + ry), clear, half, half, clear);
        if (f1 > x1) dl->AddRectFilledMultiColor(ImVec2(x1, cy - ry), ImVec2(f1, cy + ry), half, clear, clear, half);
    }
    if (s.gradient)
        dl->AddRect(ImVec2(x0, a.y - 1.0f), ImVec2(ImMax(x1, x0 + 1.0f), b.y + 1.0f), acc, 2.0f, 0, 2.0f);
    else
        dl->AddRectFilled(ImVec2(x0, a.y), ImVec2(ImMax(x1, x0 + 1.0f), b.y), acc, rh);
    const bool hov = ImGui::IsItemHovered();
    const float kr = IM_TRUNC(fs * 0.36f);
    for (int k = 0; k < 2; ++k) {
        const ImVec2 c(k == 0 ? x0 : x1, cy);
        const bool hot = (active && knob == k) || (hov && std::fabs(g.IO.MousePos.x - c.x) < kr * 2.0f);
        dl->AddCircleFilled(c, kr + 2.0f, ImGui::GetColorU32(ImGuiCol_WindowBg), 16);
        dl->AddCircleFilled(c, kr, hot ? acc : ImGui::GetColorU32(sem.text), 16);
    }
    if (hov && !active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    // The values: exact entry, and the only way to type one.
    const float gap = st.ItemInnerSpacing.x;
    const float fallW = s.falloff ? IM_TRUNC(fs * 4.2f) : 0.0f;
    const float vw = IM_TRUNC((fieldW - (s.falloff ? fallW + gap : 0.0f) - gap) * 0.5f);
    char name[160];
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(vw);
    if (ImGui::DragFloat("##lo", lo, s.speed, s.vmin, *hi, s.fmt, ImGuiSliderFlags_AlwaysClamp)) changed = true;
    std::snprintf(name, sizeof name, "%s min", nm);
    reportAs(name);
    ImGui::SameLine(0.0f, gap);
    ImGui::SetNextItemWidth(vw);
    if (ImGui::DragFloat("##hi", hi, s.speed, *lo, s.vmax, s.fmt, ImGuiSliderFlags_AlwaysClamp)) changed = true;
    std::snprintf(name, sizeof name, "%s max", nm);
    reportAs(name);
    if (s.falloff) {
        ImGui::SameLine(0.0f, gap);
        ImGui::SetNextItemWidth(ImMax(1.0f, fieldW - vw * 2.0f - gap * 2.0f));
        if (ImGui::DragFloat("##fo", s.falloff, s.speed, 0.0f, s.falloffMax, s.falloffFmt,
                             ImGuiSliderFlags_AlwaysClamp))
            changed = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Falloff: the soft edge on both ends");
        std::snprintf(name, sizeof name, "%s falloff", nm);
        reportAs(name);
    }
    ImGui::PopID();
    ImGui::EndGroup();
    if (row) endRow(r);
    return changed;
}

bool PercentSlider(const char* label, float* v01, const char* fmt) {
    float pct = *v01 * 100.0f;
    if (!SliderFloat(label, &pct, 0.0f, 100.0f, fmt, ImGuiSliderFlags_AlwaysClamp)) return false;
    *v01 = ImClamp(pct / 100.0f, 0.0f, 1.0f);
    return true;
}

float ToggleChipWidth(const char* label) { return FilterChipWidth(label); }

bool ToggleChip(const char* label, bool* v) {
    if (!FilterChip(label, *v)) return false;
    *v = !*v;
    ImGui::MarkItemEdited(ImGui::GetItemID());
    return true;
}

bool BeginOverrideRow(const char* label, bool* on, const char* unsetText, bool* flipped) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    *flipped = false;
    if (w->SkipItems) return false;
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    const RowScope* rs = rowScope();
    const float cw = rs ? captionWidth(*rs, avail) : IM_TRUNC(avail * 0.45f);
    const float h = ImGui::GetFrameHeight();
    // The whole caption cell is the checkbox: box + name.
    if (ImGui::InvisibleButton(label, ImVec2(cw, h))) {
        *on = !*on;
        *flipped = true;
        ImGui::MarkItemEdited(ImGui::GetItemID());
    }
    reportAs(label, ImGuiItemStatusFlags_Checkable | (*on ? ImGuiItemStatusFlags_Checked : 0));
    const bool hov = ImGui::IsItemHovered();
    const float box = IM_TRUNC(fs * 0.85f);
    const ImVec2 b0(p.x, p.y + IM_TRUNC((h - box) * 0.5f)), b1(b0.x + box, b0.y + box);
    if (*on) {
        w->DrawList->AddRectFilled(b0, b1, ImGui::GetColorU32(sem.accent), 3.0f);
        CheckMark(w->DrawList, ImVec2((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f), box * 0.7f,
                  ImGui::GetColorU32(sem.field));
    } else {
        w->DrawList->AddRect(b0, b1, hov ? ImGui::GetColorU32(sem.text) : ImGui::GetColorU32(sem.textDim), 3.0f,
                             0, 1.4f);
    }
    const char* end = ImGui::FindRenderedTextEnd(label);
    drawCaption(w->DrawList, ImVec2(b1.x + st.ItemInnerSpacing.x, p.y + st.FramePadding.y),
                p.x + cw - st.ItemInnerSpacing.x, label, end, *on ? sem.text : sem.textDim);
    ImGui::SetCursorScreenPos(ImVec2(p.x + cw, p.y));
    const float fieldW = ImMax(1.0f, avail - cw);
    if (*on) {
        ImGui::SetNextItemWidth(fieldW);
        ImGui::PushID(label);
        return true;
    }
    const char* t = unsetText ? unsetText : "";
    const ImVec2 ts = ImGui::CalcTextSize(t);
    w->DrawList->AddText(ImVec2(p.x + cw + fieldW - ts.x - st.FramePadding.x, p.y + st.FramePadding.y),
                         fadeCol(sem.textDim, 0.8f), t);
    ImGui::Dummy(ImVec2(fieldW, h));
    return false;
}

void EndOverrideRow() { ImGui::PopID(); }

bool Disclosure(const char* label, const char* summary) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return false;
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float W = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float lineY = IM_TRUNC(p.y + st.ItemSpacing.y * 0.5f);
    w->DrawList->AddLine(ImVec2(p.x, lineY), ImVec2(p.x + W, lineY), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, st.ItemSpacing.y * 0.6f));
    p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID(label);
    bool open = store->GetBool(key, false);
    if (ImGui::InvisibleButton(label, ImVec2(W, h))) {
        open = !open;
        store->SetBool(key, open);
    }
    reportAs(label, ImGuiItemStatusFlags_Openable | (open ? ImGuiItemStatusFlags_Opened : 0));
    const bool hov = ImGui::IsItemHovered();
    const ImU32 ink = hov ? ImGui::GetColorU32(sem.text) : ImGui::GetColorU32(sem.textDim);
    const float fs = ImGui::GetFontSize();
    ImGui::RenderArrow(w->DrawList, ImVec2(p.x, p.y + (h - fs * 0.7f) * 0.5f), ink,
                       open ? ImGuiDir_Down : ImGuiDir_Right, 0.7f);
    const char* end = ImGui::FindRenderedTextEnd(label);
    const float tx = p.x + fs * 0.9f;
    w->DrawList->AddText(ImVec2(tx, p.y + st.FramePadding.y), ink, label, end);
    if (summary && *summary && !open) {
        FontScope mono(gMono);
        const float lw = ImGui::CalcTextSize(label, end).x;
        const float maxX = p.x + W;
        const float minX = tx + lw + fs;
        const ImVec2 ss = ImGui::CalcTextSize(summary);
        const float sx = ImMax(minX, maxX - ss.x);
        ImGui::PushStyleColor(ImGuiCol_Text, fadeCol(sem.textDim, 0.85f));
        ImGui::RenderTextEllipsis(w->DrawList, ImVec2(sx, p.y + st.FramePadding.y),
                                  ImVec2(maxX, p.y + h), maxX, summary, nullptr, &ss);
        ImGui::PopStyleColor();
    }
    return open;
}

int NodeHeader(const char* title, const char* subtitle, Glyph icon, ImVec4 tint, const HeaderButton* buttons, int n,
               int* hovered) {
    if (hovered) *hovered = -1;
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return -1;
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    const float W = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float sq = IM_TRUNC(fs * 2.2f);
    ImDrawList* dl = w->DrawList;
    dl->AddRectFilled(p, ImVec2(p.x + sq, p.y + sq), fadeCol(tint, 0.16f), IM_TRUNC(sq * 0.24f));
    DrawGlyph(dl, icon, ImVec2(p.x + sq * 0.5f, p.y + sq * 0.5f), sq * 0.5f, ImGui::GetColorU32(tint));
    const float bsz = ImGui::GetFrameHeight();
    const float gap = IM_TRUNC(st.ItemInnerSpacing.x * 0.6f);
    const float bx = p.x + W - (float)n * bsz - (float)ImMax(0, n - 1) * gap;
    const float tx = p.x + sq + fs * 0.6f, tmax = bx - fs * 0.4f;
    const float subFs = IM_TRUNC(fs * 0.86f);
    const float textH = fs + (subtitle && *subtitle ? subFs + 1.0f : 0.0f);
    const float ty = p.y + (sq - textH) * 0.5f;
    {
        FontScope bold(gBold);
        const ImVec2 ts = ImGui::CalcTextSize(title);
        ImGui::RenderTextEllipsis(dl, ImVec2(tx, ty), ImVec2(tmax, ty + ts.y + 1.0f), tmax, title, nullptr, &ts);
    }
    if (subtitle && *subtitle) {
        ImGui::PushFont(nullptr, subFs);
        const ImVec2 ss = ImGui::CalcTextSize(subtitle);
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        const float sy = ty + fs + 1.0f;
        ImGui::RenderTextEllipsis(dl, ImVec2(tx, sy), ImVec2(tmax, sy + ss.y + 1.0f), tmax, subtitle, nullptr, &ss);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    int clicked = -1;
    for (int i = 0; i < n; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(bx + (float)i * (bsz + gap), p.y + (sq - bsz) * 0.5f));
        if (GlyphButton(buttons[i].label, buttons[i].glyph, bsz, buttons[i].label, buttons[i].active)) clicked = i;
        if (hovered && ImGui::IsItemHovered()) *hovered = i;
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(W, sq));
    return clicked;
}
// --- end node inspector kit ---

// --- picture inspector kit ---------------------------------------------------

bool Choice(const char* id, const char* const* labels, int count, int* cur, float width, bool enabled) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems || count <= 0) return false;
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float h = ImGui::GetFrameHeight();
    const float pad = IM_TRUNC(ImGui::GetFontSize() * 0.18f);
    float natural = pad * 2.0f;
    for (int i = 0; i < count; ++i)
        natural += ImGui::CalcTextSize(labels[i], ImGui::FindRenderedTextEnd(labels[i])).x + st.FramePadding.x * 2.0f;
    const float W = width > 0.0f ? width : (width < 0.0f ? ImGui::GetContentRegionAvail().x : natural);
    // Equal segments when the control is stretched and every label fits its
    // share (a row of three reads as three equal choices); widths in
    // proportion to the labels otherwise, so a long one is not cut first.
    float widest = 0.0f;
    for (int i = 0; i < count; ++i)
        widest = ImMax(widest, ImGui::CalcTextSize(labels[i], ImGui::FindRenderedTextEnd(labels[i])).x +
                                   st.FramePadding.x * 2.0f);
    const bool equal = W > natural && (W - pad * 2.0f) / (float)count >= widest;
    const float scale = (W - pad * 2.0f) / (natural - pad * 2.0f);
    ImGui::PushID(id);
    const ImVec2 base = ImGui::GetCursorScreenPos();
    ImDrawList* dl = w->DrawList;
    const float r = st.FrameRounding;
    dl->AddRectFilled(base, ImVec2(base.x + W, base.y + h), ImGui::GetColorU32(sem.field), r);
    dl->AddRect(base, ImVec2(base.x + W, base.y + h), ImGui::GetColorU32(sem.border), r);
    bool changed = false;
    float x = base.x + pad;
    if (!enabled) ImGui::BeginDisabled();
    for (int i = 0; i < count; ++i) {
        const char* end = ImGui::FindRenderedTextEnd(labels[i]);
        const float sw = equal ? (W - pad * 2.0f) / (float)count
                               : (ImGui::CalcTextSize(labels[i], end).x + st.FramePadding.x * 2.0f) * scale;
        ImGui::SetCursorScreenPos(ImVec2(x, base.y + pad));
        if (ImGui::InvisibleButton(labels[i], ImVec2(ImMax(1.0f, sw), h - pad * 2.0f)) && *cur != i) {
            *cur = i;
            changed = true;
            ImGui::MarkItemEdited(ImGui::GetItemID());
        }
        const bool on = *cur == i;
        reportAs(labels[i], ImGuiItemStatusFlags_Checkable | (on ? ImGuiItemStatusFlags_Checked : 0));
        const float t = theme::hoverAnim(ImGui::GetItemID(), enabled && ImGui::IsItemHovered() && !on, 12.0f);
        if (on || t > 0.0f)
            dl->AddRectFilled(ImVec2(x, base.y + pad), ImVec2(x + sw, base.y + h - pad),
                              fadeCol(sem.surface, on ? 1.0f : 0.5f * t), ImMax(0.0f, r - pad * 0.5f));
        const ImVec2 ts = ImGui::CalcTextSize(labels[i], end);
        ImVec4 ink = on ? sem.text : sem.textDim;
        if (!enabled) ink.w *= st.DisabledAlpha;
        ImGui::PushClipRect(ImVec2(x, base.y), ImVec2(x + sw, base.y + h), true);
        dl->AddText(ImVec2(x + ImMax(2.0f, (sw - ts.x) * 0.5f), base.y + (h - ts.y) * 0.5f),
                    ImGui::GetColorU32(ink), labels[i], end);
        ImGui::PopClipRect();
        x += sw;
    }
    if (!enabled) ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(base);
    ImGui::Dummy(ImVec2(W, h));
    return changed;
}

bool ChoiceRow(const char* label, const char* const* labels, int count, int* cur, bool enabled) {
    Row r;
    const bool row = beginRow(label, r, false);
    const bool changed = Choice(label, labels, count, cur, row ? r.fieldW : -1.0f, enabled);
    if (row) endRow(r);
    return changed;
}

bool OptionTile(const char* label, const char* text, TileDrawFn draw, bool selected, float width, float height,
                bool approx, bool enabled) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return false;
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float W = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const float H = height > 0.0f ? height : fs * 4.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (!enabled) ImGui::BeginDisabled();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(W, H));
    if (!enabled) ImGui::EndDisabled();
    reportAs(label, ImGuiItemStatusFlags_Checkable | (selected ? ImGuiItemStatusFlags_Checked : 0));
    const float t = theme::hoverAnim(ImGui::GetItemID(), enabled && ImGui::IsItemHovered() && !selected, 12.0f);
    ImDrawList* dl = w->DrawList;
    const ImVec2 q(p.x + W, p.y + H);
    const float r = ImGui::GetStyle().FrameRounding + 3.0f;
    if (selected) {
        dl->AddRectFilled(p, q, fadeCol(sem.accent, 0.13f), r);
        dl->AddRect(p, q, fadeCol(sem.accent, 0.85f), r, 0, 1.4f);
    } else {
        dl->AddRectFilled(p, q, fadeCol(sem.surface, 0.55f + 0.35f * t), r);
        dl->AddRect(p, q, approx ? fadeCol(sem.accent, 0.55f) : ImGui::GetColorU32(sem.border), r, 0,
                    approx ? 1.2f : 1.0f);
    }
    const char* tx = text ? text : label;
    const char* end = ImGui::FindRenderedTextEnd(tx);
    const ImVec2 ts = ImGui::CalcTextSize(tx, end);
    const float gap = IM_TRUNC(fs * 0.3f);
    const float padY = IM_TRUNC(fs * 0.45f);
    ImVec4 inkV = selected ? sem.text : sem.textDim;
    if (!enabled) inkV.w *= ImGui::GetStyle().DisabledAlpha;
    const ImU32 ink = ImGui::GetColorU32(inkV);
    const ImVec2 picMin(p.x + padY, p.y + padY);
    const ImVec2 picMax(q.x - padY, q.y - padY - ts.y - gap);
    if (draw && picMax.y > picMin.y + 2.0f)
        draw(dl, picMin, picMax, selected ? ImGui::GetColorU32(sem.accent) : ink, selected);
    dl->PushClipRect(p, q, true);
    dl->AddText(ImVec2(p.x + ImMax(2.0f, (W - ts.x) * 0.5f), q.y - padY - ts.y), ink, tx, end);
    dl->PopClipRect();
    return pressed && enabled;
}

bool AssetTile(const char* label, ImTextureID tex, Glyph fallback, const char* title, const char* sub, float width,
               float height, bool flipY) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return false;
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float W = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const float H = height > 0.0f ? height : IM_TRUNC(fs * 3.2f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(W, H));
    const bool hov = ImGui::IsItemHovered();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hov, 12.0f);
    ImDrawList* dl = w->DrawList;
    const ImVec2 q(p.x + W, p.y + H);
    const float r = ImGui::GetStyle().FrameRounding + 2.0f;
    dl->AddRectFilled(p, q, fadeCol(sem.surface, 0.55f + 0.35f * t), r);
    dl->AddRect(p, q, ImGui::GetColorU32(hov ? sem.textDim : sem.border), r);
    const float pad = IM_TRUNC(fs * 0.3f);
    const float s = H - pad * 2.0f;
    const ImVec2 a(p.x + pad, p.y + pad), b(a.x + s, a.y + s);
    if (tex) {
        dl->AddImageRounded(tex, a, b, ImVec2(0, flipY ? 1.0f : 0.0f), ImVec2(1, flipY ? 0.0f : 1.0f),
                            IM_COL32_WHITE, r - 1.0f);
    } else {
        dl->AddRectFilled(a, b, ImGui::GetColorU32(sem.field), r - 1.0f);
        DrawGlyph(dl, fallback, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), s * 0.45f,
                  fadeCol(sem.textDim, 0.8f));
    }
    const float tx = b.x + fs * 0.6f, tmax = q.x - pad;
    const float subFs = IM_TRUNC(fs * 0.86f);
    const bool hasSub = sub && *sub;
    const float textH = fs + (hasSub ? subFs + 2.0f : 0.0f);
    const float ty = p.y + (H - textH) * 0.5f;
    if (title && *title) {
        const ImVec2 ts = ImGui::CalcTextSize(title);
        ImGui::RenderTextEllipsis(dl, ImVec2(tx, ty), ImVec2(tmax, ty + ts.y + 1.0f), tmax, title, nullptr, &ts);
    }
    if (hasSub) {
        ImGui::PushFont(nullptr, subFs);
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        const ImVec2 ss = ImGui::CalcTextSize(sub);
        const float sy = ty + fs + 2.0f;
        ImGui::RenderTextEllipsis(dl, ImVec2(tx, sy), ImVec2(tmax, sy + ss.y + 1.0f), tmax, sub, nullptr, &ss);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    return pressed;
}

namespace {
struct InsetCard {
    ImDrawListSplitter split;
    ImGuiWindow* window = nullptr;
    ImVec2 p0;
    float width = 0.0f, pad = 0.0f;
    float workMaxX = 0.0f, contentMaxX = 0.0f;
    bool open = false;
};
InsetCard gCard;
}  // namespace

void BeginInsetCard(const char* id, float pad) {
    (void)id;  // names the card for the reader; no ID scope, see the header
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    IM_ASSERT(!gCard.open && "BeginInsetCard does not nest");
    gCard.open = !w->SkipItems;
    if (!gCard.open) return;
    const ImGuiStyle& st = ImGui::GetStyle();
    gCard.window = w;
    gCard.pad = pad > 0.0f ? pad : IM_TRUNC(ImGui::GetFontSize() * 0.7f);
    gCard.p0 = ImGui::GetCursorScreenPos();
    gCard.width = ImGui::GetContentRegionAvail().x;
    gCard.split.Split(w->DrawList, 2);
    gCard.split.SetCurrentChannel(w->DrawList, 1);
    // Narrow the region the contents lay out in: "fill" widths, the inspector
    // rows and SameLine(right-aligned) all read these.
    gCard.workMaxX = w->WorkRect.Max.x;
    gCard.contentMaxX = w->ContentRegionRect.Max.x;
    const float right = gCard.p0.x + gCard.width - gCard.pad;
    w->WorkRect.Max.x = right;
    w->ContentRegionRect.Max.x = right;
    ImGui::Indent(gCard.pad);
    ImGui::SetCursorScreenPos(ImVec2(w->DC.CursorPos.x, gCard.p0.y + gCard.pad));
    (void)st;
}

void EndInsetCard() {
    if (gCard.open) {
        ImGuiWindow* w = gCard.window;
        const ImGuiStyle& st = ImGui::GetStyle();
        const float bottom = w->DC.CursorPos.y - st.ItemSpacing.y + gCard.pad;
        ImGui::Unindent(gCard.pad);
        w->WorkRect.Max.x = gCard.workMaxX;
        w->ContentRegionRect.Max.x = gCard.contentMaxX;
        gCard.split.SetCurrentChannel(w->DrawList, 0);
        const theme::Semantics& sem = theme::semantics();
        const float r = st.FrameRounding + 4.0f;
        const ImVec2 q(gCard.p0.x + gCard.width, bottom);
        w->DrawList->AddRectFilled(gCard.p0, q, fadeCol(sem.surface, 0.55f), r);
        w->DrawList->AddRect(gCard.p0, q, ImGui::GetColorU32(sem.border), r);
        gCard.split.Merge(w->DrawList);
        ImGui::SetCursorScreenPos(ImVec2(gCard.p0.x, bottom));
        ImGui::Dummy(ImVec2(gCard.width, 0.0f));
        gCard.open = false;
    }
}

bool CardHeader(const char* label, const char* sub, bool* on, bool* open, ImU32 dot, bool onEnabled) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return false;
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    const float W = ImGui::GetContentRegionAvail().x;
    const bool hasSub = sub && *sub;
    const float subFs = IM_TRUNC(fs * 0.86f);
    const float H = ImMax(ImGui::GetFrameHeight(), fs + (hasSub ? subFs + 3.0f : 0.0f) + st.FramePadding.y);
    const float trackH = IM_TRUNC(fs * 1.05f), trackW = IM_TRUNC(trackH * 1.8f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const char* end = ImGui::FindRenderedTextEnd(label);
    const float leftW = ImMax(1.0f, W - (on ? trackW + st.ItemInnerSpacing.x * 2.0f : 0.0f));
    ImDrawList* dl = w->DrawList;
    bool hovFold = false;
    if (open) {
        const std::string fold = "Fold " + std::string(label, end) + "##fold";
        if (ImGui::InvisibleButton(fold.c_str(), ImVec2(leftW, H))) *open = !*open;
        reportAs(fold.c_str(), ImGuiItemStatusFlags_Openable | (*open ? ImGuiItemStatusFlags_Opened : 0));
        hovFold = ImGui::IsItemHovered();
    }
    float x = p.x;
    if (open) {
        ImGui::RenderArrow(dl, ImVec2(x, p.y + (H - fs * 0.7f) * 0.5f),
                           ImGui::GetColorU32(hovFold ? sem.text : sem.textDim), *open ? ImGuiDir_Down : ImGuiDir_Right,
                           0.7f);
        x += fs * 0.95f;
    }
    if (dot) {
        const float d = IM_TRUNC(fs * 0.5f);
        dl->AddCircleFilled(ImVec2(x + d * 0.5f, p.y + H * 0.5f), d * 0.5f, dot, 12);
        x += d + fs * 0.45f;
    }
    const float tmax = p.x + leftW - st.ItemInnerSpacing.x;
    const float ty = p.y + (H - (fs + (hasSub ? subFs + 3.0f : 0.0f))) * 0.5f;
    {
        FontScope bold(gBold);
        const ImVec2 ts = ImGui::CalcTextSize(label, end);
        ImGui::RenderTextEllipsis(dl, ImVec2(x, ty), ImVec2(tmax, ty + ts.y + 1.0f), tmax, label, end, &ts);
    }
    if (hasSub) {
        ImGui::PushFont(nullptr, subFs);
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        const ImVec2 ss = ImGui::CalcTextSize(sub);
        const float sy = ty + fs + 3.0f;
        ImGui::RenderTextEllipsis(dl, ImVec2(x, sy), ImVec2(tmax, sy + ss.y + 1.0f), tmax, sub, nullptr, &ss);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    bool toggled = false;
    if (on) {
        const ImVec2 a(p.x + W - trackW, p.y + (H - trackH) * 0.5f);
        ImGui::SetCursorScreenPos(a);
        if (!onEnabled) ImGui::BeginDisabled();
        if (ImGui::InvisibleButton(label, ImVec2(trackW, trackH))) {
            *on = !*on;
            toggled = true;
            ImGui::MarkItemEdited(ImGui::GetItemID());
        }
        if (!onEnabled) ImGui::EndDisabled();
        reportAs(label, ImGuiItemStatusFlags_Checkable | (*on ? ImGuiItemStatusFlags_Checked : 0));
        const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        ImGuiStorage* store = ImGui::GetStateStorage();
        const ImGuiID animId = ImGui::GetItemID() + 0x51u;
        float t = store->GetFloat(animId, *on ? 1.0f : 0.0f);
        const float target = *on ? 1.0f : 0.0f, step = ImGui::GetIO().DeltaTime * 10.0f;
        t = t < target ? ImMin(target, t + step) : ImMax(target, t - step);
        store->SetFloat(animId, t);
        ImVec4 off = sem.border;
        off = ImVec4(off.x * 1.25f, off.y * 1.25f, off.z * 1.25f, 1.0f);
        ImVec4 track(off.x + (sem.accent.x - off.x) * t, off.y + (sem.accent.y - off.y) * t,
                     off.z + (sem.accent.z - off.z) * t, 1.0f);
        if (hov && onEnabled)
            track = ImVec4(ImMin(1.0f, track.x + 0.06f), ImMin(1.0f, track.y + 0.06f), ImMin(1.0f, track.z + 0.06f), 1.0f);
        if (!onEnabled) track.w = st.DisabledAlpha;
        const ImVec2 b(a.x + trackW, a.y + trackH);
        dl->AddRectFilled(a, b, ImGui::GetColorU32(track), trackH * 0.5f);
        const float kr = trackH * 0.5f - IM_TRUNC(trackH * 0.12f) - 0.5f;
        dl->AddCircleFilled(ImVec2(a.x + trackH * 0.5f + (trackW - trackH) * t, a.y + trackH * 0.5f), kr,
                            IM_COL32(255, 255, 255, onEnabled ? 255 : 120), 20);
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(W, H));
    return toggled;
}

bool AddRow(const char* label, const char* hint) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return false;
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float W = ImGui::GetContentRegionAvail().x;
    const float H = IM_TRUNC(fs * 2.4f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(W, H));
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = w->DrawList;
    const ImVec2 q(p.x + W, p.y + H);
    const float r = ImGui::GetStyle().FrameRounding + 4.0f;
    if (t > 0.0f) dl->AddRectFilled(p, q, fadeCol(sem.surface, 0.5f * t), r);
    // Dashed outline: straight runs only (the corners stay open), which reads
    // as dashed at a glance and costs a handful of segments.
    const ImU32 dash = fadeCol(sem.textDim, 0.45f + 0.3f * t);
    const float dl0 = IM_TRUNC(fs * 0.45f), gap = IM_TRUNC(fs * 0.3f);
    for (float x = p.x + r; x < q.x - r; x += dl0 + gap) {
        const float x1 = ImMin(x + dl0, q.x - r);
        dl->AddLine(ImVec2(x, p.y + 0.5f), ImVec2(x1, p.y + 0.5f), dash);
        dl->AddLine(ImVec2(x, q.y - 0.5f), ImVec2(x1, q.y - 0.5f), dash);
    }
    for (float y = p.y + r; y < q.y - r; y += dl0 + gap) {
        const float y1 = ImMin(y + dl0, q.y - r);
        dl->AddLine(ImVec2(p.x + 0.5f, y), ImVec2(p.x + 0.5f, y1), dash);
        dl->AddLine(ImVec2(q.x - 0.5f, y), ImVec2(q.x - 0.5f, y1), dash);
    }
    const float cx = p.x + fs * 1.1f, cy = p.y + H * 0.5f, a = fs * 0.32f;
    const ImU32 acc = ImGui::GetColorU32(sem.accent);
    dl->AddLine(ImVec2(cx - a, cy), ImVec2(cx + a, cy), acc, 1.6f);
    dl->AddLine(ImVec2(cx, cy - a), ImVec2(cx, cy + a), acc, 1.6f);
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    dl->AddText(ImVec2(cx + fs * 0.8f, cy - ts.y * 0.5f), ImGui::GetColorU32(sem.text), label, end);
    if (hint && *hint) {
        const ImVec2 hs = ImGui::CalcTextSize(hint);
        const float hx = ImMax(cx + fs * 0.8f + ts.x + fs, q.x - fs * 0.8f - hs.x);
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::RenderTextEllipsis(dl, ImVec2(hx, cy - hs.y * 0.5f), ImVec2(q.x - fs * 0.6f, cy + hs.y), q.x - fs * 0.6f,
                                  hint, nullptr, &hs);
        ImGui::PopStyleColor();
    }
    return pressed;
}

float DetailChipWidth(const char* label, const char* detail) {
    float w = ImGui::CalcTextSize(label, nullptr, true).x + chipPadX() * 2.0f;
    if (detail && *detail) {
        FontScope mono(gMono);
        w += IM_TRUNC(ImGui::CalcTextSize(detail).x * 0.92f) + ImGui::GetStyle().ItemInnerSpacing.x;
    }
    return w;
}

bool DetailChip(const char* label, const char* detail, bool on, bool focused) {
    const theme::Semantics& sem = theme::semantics();
    const float w = DetailChipWidth(label, detail), h = chipHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    reportAs(label, ImGuiItemStatusFlags_Checkable | (on ? ImGuiItemStatusFlags_Checked : 0));
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const float r = h * 0.5f;
    if (on) dl->AddRectFilled(p, q, fadeCol(sem.accent, 0.16f + 0.06f * t), r);
    else if (t > 0.0f) dl->AddRectFilled(p, q, fadeCol(sem.surface, t), r);
    dl->AddRect(p, q, focused ? ImGui::GetColorU32(sem.accent) : on ? fadeCol(sem.accent, 0.5f) : ImGui::GetColorU32(sem.border),
                r, 0, focused ? 1.8f : 1.0f);
    const char* end = ImGui::FindRenderedTextEnd(label);
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    float x = p.x + chipPadX();
    dl->AddText(ImVec2(x, ty), on ? ImGui::GetColorU32(sem.text) : fadeCol(sem.textDim, 0.95f), label, end);
    if (detail && *detail) {
        x += ImGui::CalcTextSize(label, end).x + ImGui::GetStyle().ItemInnerSpacing.x;
        FontScope mono(gMono);
        const float dfs = IM_TRUNC(ImGui::GetFontSize() * 0.92f);
        dl->AddText(ImGui::GetFont(), dfs, ImVec2(x, p.y + (h - dfs) * 0.5f),
                    on ? ImGui::GetColorU32(sem.accent) : fadeCol(sem.textDim, 0.8f), detail);
    }
    return pressed;
}

void DimensionLine(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, ImU32 col) {
    const float fs = ImGui::GetFontSize();
    const bool vertical = std::fabs(b.x - a.x) < std::fabs(b.y - a.y);
    const float ah = IM_TRUNC(fs * 0.32f), aw = IM_TRUNC(fs * 0.22f);
    dl->AddLine(a, b, col, 1.0f);
    if (vertical) {
        const float s = b.y > a.y ? 1.0f : -1.0f;
        dl->AddTriangleFilled(a, ImVec2(a.x - aw, a.y + ah * s), ImVec2(a.x + aw, a.y + ah * s), col);
        dl->AddTriangleFilled(b, ImVec2(b.x - aw, b.y - ah * s), ImVec2(b.x + aw, b.y - ah * s), col);
    } else {
        const float s = b.x > a.x ? 1.0f : -1.0f;
        dl->AddTriangleFilled(a, ImVec2(a.x + ah * s, a.y - aw), ImVec2(a.x + ah * s, a.y + aw), col);
        dl->AddTriangleFilled(b, ImVec2(b.x - ah * s, b.y - aw), ImVec2(b.x - ah * s, b.y + aw), col);
    }
    if (!text || !*text) return;
    const theme::Semantics& sem = theme::semantics();
    FontScope mono(gMono);
    const float tfs = IM_TRUNC(fs * 0.85f);
    const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(tfs, FLT_MAX, 0.0f, text);
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float px = IM_TRUNC(tfs * 0.5f), py = IM_TRUNC(tfs * 0.15f);
    const ImVec2 m(c.x - ts.x * 0.5f - px, c.y - ts.y * 0.5f - py), n(c.x + ts.x * 0.5f + px, c.y + ts.y * 0.5f + py);
    dl->AddRectFilled(m, n, ImGui::GetColorU32(sem.surface), (n.y - m.y) * 0.5f);
    dl->AddText(ImGui::GetFont(), tfs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ImGui::GetColorU32(sem.text), text);
}

bool DragHandle(const char* label, ImVec2 c, float r, ImGuiMouseCursor cursor) {
    const theme::Semantics& sem = theme::semantics();
    const ImVec2 back = ImGui::GetCursorScreenPos();
    const float hit = r * 1.6f;
    ImGui::SetCursorScreenPos(ImVec2(c.x - hit, c.y - hit));
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton(label, ImVec2(hit * 2.0f, hit * 2.0f));
    const bool active = ImGui::IsItemActive();
    const bool hov = ImGui::IsItemHovered();
    if (hov || active) ImGui::SetMouseCursor(cursor);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(c, r, active || hov ? ImGui::GetColorU32(sem.accent) : ImGui::GetColorU32(ImGuiCol_WindowBg), 16);
    dl->AddCircle(c, r, ImGui::GetColorU32(sem.accent), 16, 1.6f);
    ImGui::SetCursorScreenPos(back);
    return active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f);
}
// --- end picture inspector kit -----------------------------------------------

}  // namespace ui
