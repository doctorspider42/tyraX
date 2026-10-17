#include "widgets.hpp"

#include "theme.hpp"

#include <imgui_internal.h>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
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
void setMonoFont(ImFont* f) { gMono = f; }
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
            if (dot && dot[2] >= '1' && dot[2] <= '9' && dot[3] == 'f') {
                for (int prec = dot[2] - '0' - 1;
                     prec >= 0 && ImGui::CalcTextSize(buf).x > room; --prec) {
                    std::string f(fmt);
                    f[dot - fmt + 2] = (char)('0' + prec);
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
static void noteSwitchRow(const ImRect& caption, float textW);

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
    if (row) noteSwitchRow(ImRect(pos, ImVec2(a.x, pos.y + rowH)), nameSize.x);
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
        case Glyph::Folder: {
            const ImVec2 f[6] = {P(-0.9f, -0.6f), P(-0.25f, -0.6f), P(-0.05f, -0.38f),
                                 P(0.9f, -0.38f), P(0.9f, 0.7f),    P(-0.9f, 0.7f)};
            dl->AddPolyline(f, 6, col, ImDrawFlags_Closed, th);
            break;
        }
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
    r.fieldW = ImMax(1.0f, avail - cw);
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
}

}  // namespace

// Used by Switch (defined above the row machinery).
static bool switchRowWidth(float* avail) {
    if (!rowScope()) return false;
    *avail = ImGui::GetContentRegionAvail().x;
    return true;
}

static void noteSwitchRow(const ImRect& caption, float textW) {
    ImGuiContext& g = *GImGui;
    RowScope& s = gRows[gRowDepth - 1];
    s.lastId = g.LastItemData.ID;
    s.lastRect = g.LastItemData.Rect;
    s.lastCaption = caption;
    s.lastCaptionTextW = textW;
    s.lastFrame = g.FrameCount;
}

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
static ImU32 gNextSliderFrom = 0, gNextSliderTo = 0;
void SetNextSliderGradient(ImU32 from, ImU32 to) {
    gNextSliderFrom = from;
    gNextSliderTo = to;
}

namespace {

// (verify-world) An inspector-row slider drawn the way every redesigned panel
// shows one: a thin track with a knob, the value in the mono face at the
// row's right end. ImGui's stock slider centred the value ON the grab, so a
// knob near the middle covered the number ("24 u" under the knob). The stock
// widget still does the work (same ID, ctrl-click typing, --ui-script drag,
// IsItemDeactivatedAfterEdit) with its frame, grab and text made transparent
// and its width cut short by the value column; while it is being typed into
// it is drawn stock. `show(buf, t)` formats the current value and its 0..1
// position; `widthOf` measures a value string for the column.
template <class W, class S>
bool rowSlider(const char* label, ImGuiSliderFlags flags, W&& widget, S&& show, const char* lo,
               const char* hi) {
    const ImU32 gradFrom = gNextSliderFrom, gradTo = gNextSliderTo;
    gNextSliderFrom = gNextSliderTo = 0;
    Row r;
    const bool row = beginRow(label, r);
    if (!row) {
        const bool res = widget();
        endRow(r);
        return res;
    }
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const ImGuiStyle& st = ImGui::GetStyle();
    const bool typing = ImGui::TempInputIsActive(w->GetID(label));
    char buf[64];
    float t = 0.0f;
    show(buf, &t);
    float valueW;
    {
        FontScope mono(gMono);
        valueW = ImMax(ImMax(ImGui::CalcTextSize(lo).x, ImGui::CalcTextSize(hi).x), ImGui::CalcTextSize(buf).x);
    }
    valueW = ImMin(valueW + ImGui::GetFontSize(), r.fieldW * 0.45f);
    if (!typing) {
        ImGui::SetNextItemWidth(ImMax(1.0f, r.fieldW - valueW));
        const ImVec4 clear(0, 0, 0, 0);
        for (ImGuiCol c : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab,
                           ImGuiCol_SliderGrabActive, ImGuiCol_Text, ImGuiCol_Border})
            ImGui::PushStyleColor(c, clear);
    }
    const bool res = widget();
    if (!typing) {
        ImGui::PopStyleColor(7);
        show(buf, &t);
        // The item rect includes the (clipped) label: the frame is the
        // width set above.
        const ImVec2 bmin = ImGui::GetItemRectMin();
        const ImRect bb(bmin, ImVec2(bmin.x + ImMax(1.0f, r.fieldW - valueW), ImGui::GetItemRectMax().y));
        const theme::Semantics& sem = theme::semantics();
        ImDrawList* dl = w->DrawList;
        const float grab = st.GrabMinSize;
        const float x0 = bb.Min.x + 2.0f + grab * 0.5f, x1 = ImMax(x0, bb.Max.x - 2.0f - grab * 0.5f);
        const float cy = IM_TRUNC((bb.Min.y + bb.Max.y) * 0.5f);
        const float th = ImMax(2.0f, IM_TRUNC(bb.GetHeight() * 0.14f));
        const float kx = x0 + (x1 - x0) * ImClamp(t, 0.0f, 1.0f);
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(x1, cy + th * 0.5f), ImGui::GetColorU32(ImVec4(sem.textDim.x, sem.textDim.y, sem.textDim.z, sem.textDim.w * 0.28f)),
                          th);
        if (gradFrom || gradTo)
            dl->AddRectFilledMultiColor(ImVec2(x0, cy - th * 0.5f), ImVec2(x1, cy + th * 0.5f),
                                        ImGui::GetColorU32(gradFrom), ImGui::GetColorU32(gradTo),
                                        ImGui::GetColorU32(gradTo), ImGui::GetColorU32(gradFrom));
        else
            dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(kx, cy + th * 0.5f),
                              ImGui::GetColorU32(sem.accent), th);
        dl->AddCircleFilled(ImVec2(kx, cy), bb.GetHeight() * (hot ? 0.30f : 0.26f), ImGui::GetColorU32(sem.text),
                            20);
        FontScope mono(gMono);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(r.fieldX + r.fieldW - ts.x, cy - ts.y * 0.5f), ImGui::GetColorU32(sem.text), buf);
    }
    endRow(r);
    return res;
}

}  // namespace

bool SliderFloat(const char* label, float* v, float vmin, float vmax, const char* fmt, ImGuiSliderFlags flags) {
    char lo[64], hi[64];
    ImGui::DataTypeFormatString(lo, IM_ARRAYSIZE(lo), ImGuiDataType_Float, &vmin, fmt);
    ImGui::DataTypeFormatString(hi, IM_ARRAYSIZE(hi), ImGuiDataType_Float, &vmax, fmt);
    return rowSlider(
        label, flags, [&] { return ImGui::SliderFloat(label, v, vmin, vmax, fmt, flags); },
        [&](char* buf, float* t) {
            ImGui::DataTypeFormatString(buf, 64, ImGuiDataType_Float, v, fmt);
            // Same mapping as ImGui's for a positive logarithmic range.
            if ((flags & ImGuiSliderFlags_Logarithmic) && vmin > 0.0f && vmax > vmin)
                *t = std::log(ImMax(*v, vmin) / vmin) / std::log(vmax / vmin);
            else
                *t = vmax != vmin ? (*v - vmin) / (vmax - vmin) : 0.0f;
        },
        lo, hi);
}
bool SliderInt(const char* label, int* v, int vmin, int vmax, const char* fmt, ImGuiSliderFlags flags) {
    char lo[64], hi[64];
    ImGui::DataTypeFormatString(lo, IM_ARRAYSIZE(lo), ImGuiDataType_S32, &vmin, fmt);
    ImGui::DataTypeFormatString(hi, IM_ARRAYSIZE(hi), ImGuiDataType_S32, &vmax, fmt);
    return rowSlider(
        label, flags, [&] { return ImGui::SliderInt(label, v, vmin, vmax, fmt, flags); },
        [&](char* buf, float* t) {
            ImGui::DataTypeFormatString(buf, 64, ImGuiDataType_S32, v, fmt);
            *t = vmax != vmin ? (float)(*v - vmin) / (float)(vmax - vmin) : 0.0f;
        },
        lo, hi);
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

}  // namespace ui
