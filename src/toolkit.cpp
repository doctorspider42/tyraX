#include "toolkit.hpp"

#include "theme.hpp"
#include "uiscript.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui {
namespace {

ImU32 c32(ImVec4 c, float a = 1.0f) {
    c.w *= a;
    return ImGui::GetColorU32(c);
}

ImVec4 mixv(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

ImU32 kindCol(int kind) {
    const theme::Semantics& s = theme::semantics();
    switch (kind) {
        case 0: return c32(s.ok);
        case 1: return c32(s.warn);
        case 2: return c32(s.danger);
        case 3: return c32(s.accent);
        default: return c32(s.textDim);
    }
}

// The shared body of the three text buttons: an InvisibleButton named
// `label`, a fill chosen by `paint`, the label centred.
bool textButton(const char* label, float width, bool enabled, int style) {
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float h = ImGui::GetFrameHeight();
    float w = width > 0.0f ? width : ts.x + st.FramePadding.x * 2.6f;
    if (width < 0.0f) w = ImMax(ts.x + st.FramePadding.x * 2.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hov, 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const float r = st.FrameRounding;
    ImU32 ink = c32(sem.text);
    if (style == 0) {  // primary
        ImVec4 f = enabled ? sem.accent : sem.surface;
        if (enabled) f = mixv(f, ImVec4(1, 1, 1, 1), held ? -0.0f : 0.10f * t);
        if (held) f = mixv(f, ImVec4(0, 0, 0, 1), 0.15f);
        dl->AddRectFilled(p, q, c32(f), r);
        ink = enabled ? c32(sem.ground) : c32(sem.textDim, 0.6f);
    } else if (style == 1) {  // danger
        if (t > 0.0f || held) dl->AddRectFilled(p, q, c32(sem.danger, held ? 0.30f : 0.16f * t), r);
        dl->AddRect(p, q, c32(sem.danger, enabled ? 0.45f : 0.2f), r);
        ink = enabled ? c32(sem.danger) : c32(sem.textDim, 0.6f);
    } else {  // ghost
        if (t > 0.0f || held) dl->AddRectFilled(p, q, c32(sem.surface, held ? 1.0f : 0.8f * t), r);
        ink = enabled ? c32(sem.text, 0.9f) : c32(sem.textDim, 0.6f);
    }
    {
        FontScope bold(style == 0 ? boldFont() : nullptr);
        const ImVec2 bs = ImGui::CalcTextSize(label, end);
        dl->AddText(ImVec2(p.x + (w - bs.x) * 0.5f, p.y + (h - bs.y) * 0.5f), ink, label, end);
    }
    return pressed && enabled;
}

// A grab strip between two panes. Pattern (vertical):
//   BeginChild("list", {leftW, h}); ...; EndChild(); SameLine(0, 0);
//   VSplitter("split", &leftW, minL, minR, h);   // ends on the same line
//   BeginChild("detail", {0, h}); ...
bool splitter(const char* id, bool vertical, float* a, float minA, float minB, float cross) {
    const theme::Semantics& sem = theme::semantics();
    const float thick = ImMax(6.0f, ImGui::GetFontSize() * 0.45f);
    const float total = vertical ? ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x
                                 : ImGui::GetWindowContentRegionMax().y - ImGui::GetWindowContentRegionMin().y;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size = vertical ? ImVec2(thick, cross) : ImVec2(cross, thick);
    ImGui::InvisibleButton(id, size);
    const bool held = ImGui::IsItemActive();
    const bool hov = ImGui::IsItemHovered();
    if (held) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        *a += vertical ? d.x : d.y;
    }
    *a = ImClamp(*a, minA, ImMax(minA, total - minB - thick));
    if (hov || held) {
        ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (vertical)
            dl->AddLine(ImVec2(p.x + thick * 0.5f, p.y), ImVec2(p.x + thick * 0.5f, p.y + cross),
                        c32(sem.accent, held ? 0.9f : 0.5f), 2.0f);
        else
            dl->AddLine(ImVec2(p.x, p.y + thick * 0.5f), ImVec2(p.x + cross, p.y + thick * 0.5f),
                        c32(sem.accent, held ? 0.9f : 0.5f), 2.0f);
    }
    if (vertical) ImGui::SameLine(0.0f, 0.0f);
    return held;
}

}  // namespace

bool PrimaryButton(const char* label, float width, bool enabled) {
    return textButton(label, width, enabled, 0);
}
bool DangerButton(const char* label, float width, bool enabled) {
    return textButton(label, width, enabled, 1);
}
bool GhostButton(const char* label, float width, bool enabled) {
    return textButton(label, width, enabled, 2);
}

bool VSplitter(const char* id, float* leftW, float minL, float minR, float height) {
    return splitter(id, true, leftW, minL, minR, height);
}
bool HSplitter(const char* id, float* topH, float minT, float minB, float width) {
    return splitter(id, false, topH, minT, minB, width);
}

bool BeginCard(const char* id, ImVec2 size) {
    const theme::Semantics& sem = theme::semantics();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, sem.surface);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.06f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding + 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(ImGui::GetFontSize() * 0.9f, ImGui::GetFontSize() * 0.8f));
    ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (size.y == 0.0f) flags |= ImGuiChildFlags_AutoResizeY;
    const bool open = ImGui::BeginChild(id, size, flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    return open;
}

void EndCard() { ImGui::EndChild(); }

void Caption(const char* text, const char* count) {
    const theme::Semantics& sem = theme::semantics();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float fs = ImGui::GetFontSize() * 0.78f;
    ImFont* f = boldFont() ? boldFont() : ImGui::GetFont();
    // Small caps by hand: the caption upper-cased with a little tracking.
    char up[128];
    std::snprintf(up, sizeof(up), "%s", text);
    for (char* c = up; *c; ++c)
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
    float x = p.x;
    for (const char* c = up; *c; ++c) {
        const char s[2] = {*c, 0};
        dl->AddText(f, fs, ImVec2(x, p.y + 2.0f), c32(sem.textDim), s);
        x += f->CalcTextSizeA(fs, FLT_MAX, 0.0f, s).x + fs * 0.08f;
    }
    if (count) {
        x += fs * 0.6f;
        dl->AddText(ImGui::GetFont(), fs, ImVec2(x, p.y + 2.0f), c32(sem.textDim, 0.7f), count);
        x += ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, count).x;
    }
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float ly = p.y + 2.0f + fs * 0.55f;
    if (x + fs < right) dl->AddLine(ImVec2(x + fs * 0.8f, ly), ImVec2(right, ly), c32(sem.border), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, fs + 6.0f));
}

bool ListRow(const char* label, const char* sublabel, bool selected, Glyph glyph, ImU32 glyphCol,
             bool showGlyph, float height) {
    const theme::Semantics& sem = theme::semantics();
    const float h = height > 0.0f ? height : ImGui::GetFrameHeight();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(ImMax(1.0f, w), h));
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding;
    const ImVec2 q(p.x + w, p.y + h);
    if (selected) {
        dl->AddRectFilled(p, q, c32(sem.accent, 0.16f), r);
        dl->AddRect(p, q, c32(sem.accent, 0.30f), r);
    } else if (t > 0.0f) {
        dl->AddRectFilled(p, q, c32(sem.surface, 0.8f * t), r);
    }
    const float fs = ImGui::GetFontSize();
    float x = p.x + fs * 0.45f;
    if (showGlyph) {
        DrawGlyph(dl, glyph, ImVec2(x + fs * 0.5f, p.y + h * 0.5f), fs * 0.86f,
                  glyphCol ? glyphCol : c32(sem.textDim));
        x += fs + fs * 0.5f;
    }
    const char* end = ImGui::FindRenderedTextEnd(label);
    float subW = 0.0f;
    if (sublabel && *sublabel) {
        subW = ImGui::CalcTextSize(sublabel).x;
        dl->AddText(ImVec2(q.x - subW - fs * 0.5f, p.y + (h - fs) * 0.5f), c32(sem.textDim, 0.8f), sublabel);
    }
    dl->PushClipRect(p, ImVec2(q.x - subW - fs, q.y), true);
    dl->AddText(ImVec2(x, p.y + (h - fs) * 0.5f), c32(selected ? sem.text : mixv(sem.text, sem.textDim, 0.15f)),
                label, end);
    dl->PopClipRect();
    return pressed;
}

bool ThumbTile(const char* label, ImTextureID tex, Glyph fallback, bool selected, float size,
               const char* caption) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float capH = fs + 6.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(size, size + capH));
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding + 2.0f;
    const ImVec2 q(p.x + size, p.y + size);
    dl->AddRectFilled(p, q, c32(mixv(sem.field, sem.surface, 0.5f + 0.5f * t)), r);
    if (tex)
        dl->AddImageRounded(tex, ImVec2(p.x + 3, p.y + 3), ImVec2(q.x - 3, q.y - 3), ImVec2(0, 0),
                            ImVec2(1, 1), IM_COL32_WHITE, r - 2.0f);
    else
        DrawGlyph(dl, fallback, ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.36f,
                  c32(sem.textDim, 0.8f));
    if (selected) dl->AddRect(p, q, c32(sem.accent), r, 0, 2.0f);
    else dl->AddRect(p, q, c32(sem.border), r);
    const char* cap = caption ? caption : label;
    const char* end = caption ? nullptr : ImGui::FindRenderedTextEnd(label);
    dl->PushClipRect(ImVec2(p.x, q.y), ImVec2(q.x, q.y + capH), true);
    dl->AddText(ImVec2(p.x + 2.0f, q.y + 4.0f), c32(selected ? sem.text : sem.textDim), cap, end);
    dl->PopClipRect();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) &&
        ImGui::CalcTextSize(cap, end).x > size - 4.0f)
        ImGui::SetTooltip("%.*s", end ? (int)(end - cap) : (int)std::strlen(cap), cap);
    return pressed;
}

void TileFlow(float tileW, bool first) {
    if (first) return;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float lastR = ImGui::GetItemRectMax().x;
    if (lastR + spacing + tileW <= right) ImGui::SameLine(0.0f, spacing);
}

bool EmptyState(Glyph g, const char* title, const char* why, const char* action) {
    const theme::Semantics& sem = theme::semantics();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float fs = ImGui::GetFontSize();
    const float iconS = fs * 3.0f;
    const float wrapW = ImMin(avail.x - fs * 2.0f, fs * 26.0f);
    const ImVec2 whyS = ImGui::CalcTextSize(why ? why : "", nullptr, false, wrapW);
    const float blockH = iconS + fs * 1.2f + fs + 8.0f + whyS.y + (action ? fs * 3.0f : 0.0f);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float top = start.y + ImMax(0.0f, (avail.y - blockH) * 0.42f);
    const float cx = start.x + avail.x * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(cx, top + iconS * 0.5f), iconS * 0.62f, c32(sem.surface, 0.8f), 32);
    DrawGlyph(dl, g, ImVec2(cx, top + iconS * 0.5f), iconS * 0.5f, c32(sem.textDim));
    float y = top + iconS + fs * 1.2f;
    {
        FontScope bold(boldFont());
        const ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(cx - ts.x * 0.5f, y), c32(sem.text), title);
        y += ts.y + 8.0f;
    }
    if (why && *why) {
        // Centre each wrapped line: draw through a temporary text wrap.
        ImGui::SetCursorScreenPos(ImVec2(cx - wrapW * 0.5f, y));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::TextWrapped("%s", why);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        y += whyS.y;
    }
    bool clicked = false;
    if (action) {
        const float bw = ImGui::CalcTextSize(action).x + ImGui::GetStyle().FramePadding.x * 2.6f;
        ImGui::SetCursorScreenPos(ImVec2(cx - bw * 0.5f, y + fs));
        clicked = PrimaryButton(action, bw);
    }
    ImGui::SetCursorScreenPos(ImVec2(start.x, ImMax(y + (action ? fs * 3.0f : fs), start.y)));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    return clicked;
}

void ProgressInline(float fraction, const char* label, const char* detail) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (label) dl->AddText(p, c32(sem.text), label);
    if (detail) {
        FontScope mono(monoFont());
        const float dw = ImGui::CalcTextSize(detail).x;
        dl->AddText(ImVec2(p.x + w - dw, p.y), c32(sem.textDim), detail);
    }
    const float by = p.y + fs + 6.0f, bh = ImMax(3.0f, fs * 0.25f);
    dl->AddRectFilled(ImVec2(p.x, by), ImVec2(p.x + w, by + bh), c32(sem.field), bh);
    if (fraction >= 0.0f) {
        dl->AddRectFilled(ImVec2(p.x, by), ImVec2(p.x + w * ImClamp(fraction, 0.0f, 1.0f), by + bh),
                          c32(sem.accent), bh);
    } else {  // indeterminate: a segment sweeping across
        const float t = std::fmod((float)ImGui::GetTime() * 0.8f, 1.0f);
        const float seg = w * 0.25f;
        const float x0 = p.x - seg + (w + seg) * t;
        dl->PushClipRect(ImVec2(p.x, by), ImVec2(p.x + w, by + bh), true);
        dl->AddRectFilled(ImVec2(x0, by), ImVec2(x0 + seg, by + bh), c32(sem.accent), bh);
        dl->PopClipRect();
    }
    ImGui::Dummy(ImVec2(w, fs + 6.0f + bh + 4.0f));
}

void StatusLine(int kind, const char* text) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + fs * 0.3f, p.y + fs * 0.55f), fs * 0.22f,
                                                kindCol(kind));
    ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 0.9f, p.y));
    ImGui::TextUnformatted(text);
}

void Badge(const char* text, int kind) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize() * 0.82f;
    ImFont* f = ImGui::GetFont();
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    const float padX = fs * 0.5f, h = fs + 4.0f;
    const float lineH = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y = p.y + (lineH - h) * 0.5f;
    ImVec4 base = ImGui::ColorConvertU32ToFloat4(kindCol(kind));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + ts.x + padX * 2.0f, y + h),
                      c32(kind == 4 ? sem.surface : base, kind == 4 ? 1.0f : 0.16f), h * 0.5f);
    dl->AddText(f, fs, ImVec2(p.x + padX, y + 2.0f), kind == 4 ? c32(sem.textDim) : c32(base), text);
    ImGui::Dummy(ImVec2(ts.x + padX * 2.0f, lineH));
}

int UsedBy(const char* caption, const std::vector<std::string>& items) {
    const theme::Semantics& sem = theme::semantics();
    ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
    ImGui::TextUnformatted(caption ? caption : "Used by");
    ImGui::PopStyleColor();
    int clicked = -1;
    if (items.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("not used yet");
        return -1;
    }
    for (int i = 0; i < (int)items.size(); ++i) {
        const ImVec2 ts = ImGui::CalcTextSize(items[i].c_str());
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine();
        if (ImGui::GetCursorScreenPos().x + ts.x > right) ImGui::NewLine();
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Text, sem.accent);
        if (ImGui::TextLink(items[i].c_str())) clicked = i;
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    return clicked;
}

bool Segmented(const char* id, const char* const* labels, int count, int* selected, float width) {
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float h = ImGui::GetFrameHeight();
    const float pad = 3.0f;
    float natural = pad * 2.0f;
    for (int i = 0; i < count; ++i)
        natural += ImGui::CalcTextSize(labels[i], ImGui::FindRenderedTextEnd(labels[i])).x + st.FramePadding.x * 2.0f;
    const float w = width > 0.0f ? width : (width < 0.0f ? ImGui::GetContentRegionAvail().x : natural);
    const float scale = (w - pad * 2.0f) / (natural - pad * 2.0f);
    ImGui::PushID(id);
    const ImVec2 base = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = st.FrameRounding;
    dl->AddRectFilled(base, ImVec2(base.x + w, base.y + h), c32(sem.field), r);
    dl->AddRect(base, ImVec2(base.x + w, base.y + h), c32(sem.border), r);
    bool changed = false;
    float x = base.x + pad;
    for (int i = 0; i < count; ++i) {
        const char* end = ImGui::FindRenderedTextEnd(labels[i]);
        const float sw = (ImGui::CalcTextSize(labels[i], end).x + st.FramePadding.x * 2.0f) * scale;
        ImGui::SetCursorScreenPos(ImVec2(x, base.y + pad));
        if (ImGui::InvisibleButton(labels[i], ImVec2(sw, h - pad * 2.0f)) && *selected != i) {
            *selected = i;
            changed = true;
        }
        const bool on = *selected == i;
        const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered() && !on, 12.0f);
        if (on || t > 0.0f)
            dl->AddRectFilled(ImVec2(x, base.y + pad), ImVec2(x + sw, base.y + h - pad),
                              c32(sem.surface, on ? 1.0f : 0.5f * t), r - pad * 0.5f);
        const ImVec2 ts = ImGui::CalcTextSize(labels[i], end);
        dl->AddText(ImVec2(x + (sw - ts.x) * 0.5f, base.y + (h - ts.y) * 0.5f),
                    on ? c32(sem.text) : c32(sem.textDim), labels[i], end);
        x += sw;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(base);
    ImGui::Dummy(ImVec2(w, h));
    return changed;
}

// --- runtime-b ---------------------------------------------------------------

float StatusChipWidth(const char* text) {
    const float fs = ImGui::GetFontSize();
    return ImGui::CalcTextSize(text).x + fs * 1.6f;
}

bool StatusChip(const char* label, const char* text, int kind) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float w = StatusChipWidth(text), h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const ImVec4 tint = kind == 1 ? sem.warn : kind == 2 ? sem.danger : sem.surface;
    dl->AddRectFilled(p, q, c32(kind == 1 || kind == 2 ? mixv(sem.surface, tint, 0.18f) : sem.surface,
                                0.85f + 0.15f * t),
                      h * 0.5f);
    dl->AddRect(p, q, c32(kind == 1 || kind == 2 ? tint : sem.border, kind == 1 || kind == 2 ? 0.35f : 1.0f),
                h * 0.5f);
    dl->AddCircleFilled(ImVec2(p.x + fs * 0.6f, p.y + h * 0.5f), fs * 0.18f, kindCol(kind));
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(p.x + fs * 0.95f, p.y + (h - ts.y) * 0.5f),
                c32(kind == 1 ? sem.warn : kind == 2 ? sem.danger : sem.text, kind == 4 ? 0.7f : 0.92f),
                text);
    return pressed;
}

void Spinner(ImDrawList* dl, ImVec2 c, float radius) {
    const theme::Semantics& sem = theme::semantics();
    const float a0 = (float)ImGui::GetTime() * 6.0f;
    dl->PathArcTo(c, radius, a0, a0 + IM_PI * 1.4f, 24);
    dl->PathStroke(c32(sem.accent), 0, ImMax(1.5f, radius * 0.28f));
}

void CheckMark(ImDrawList* dl, ImVec2 c, float size, ImU32 col) {
    const float s = size * 0.5f;
    const ImVec2 pts[3] = {ImVec2(c.x - s * 0.55f, c.y + s * 0.02f), ImVec2(c.x - s * 0.15f, c.y + s * 0.42f),
                           ImVec2(c.x + s * 0.6f, c.y - s * 0.4f)};
    dl->AddPolyline(pts, 3, col, 0, ImMax(1.4f, size * 0.13f));
}

void Monogram(ImDrawList* dl, ImVec2 c, float radius, const char* initials, ImU32 col) {
    ImVec4 base = ImGui::ColorConvertU32ToFloat4(col);
    dl->AddCircleFilled(c, radius, c32(base, 0.18f), 32);
    dl->AddCircle(c, radius, col, 32, ImMax(1.0f, radius * 0.08f));
    ImFont* f = boldFont() ? boldFont() : ImGui::GetFont();
    const float fs = radius * 0.78f;
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, initials);
    dl->AddText(f, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, initials);
}

bool ChoiceCard(const char* label, bool selected, float height) {
    const theme::Semantics& sem = theme::semantics();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(ImMax(1.0f, w), height));
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding + 4.0f;
    const ImVec2 q(p.x + w, p.y + height);
    if (selected) {
        dl->AddRectFilled(p, q, c32(mixv(sem.surface, sem.accent, 0.10f)), r);
        dl->AddRect(ImVec2(p.x - 2, p.y - 2), ImVec2(q.x + 2, q.y + 2), c32(sem.accent, 0.14f), r + 2.0f, 0, 3.0f);
        dl->AddRect(p, q, c32(sem.accent, 0.6f), r);
    } else {
        dl->AddRectFilled(p, q, c32(mixv(sem.surface, sem.text, 0.03f * t)), r);
        dl->AddRect(p, q, c32(sem.border), r);
    }
    return pressed;
}
// --- assets-a ---
bool PrimaryButtonText(const char* id, const char* text, float width, bool enabled) {
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    ImFont* bf = boldFont() ? boldFont() : ImGui::GetFont();
    const float fsz = ImGui::GetFontSize();
    const ImVec2 ts = bf->CalcTextSizeA(fsz, FLT_MAX, 0.0f, text);
    const float h = ImGui::GetFrameHeight();
    float w = width > 0.0f ? width : ts.x + st.FramePadding.x * 2.6f;
    if (width < 0.0f) w = ImMax(ts.x + st.FramePadding.x * 2.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hov, 12.0f);
    ImVec4 f = enabled ? sem.accent : sem.surface;
    if (enabled) f = mixv(f, ImVec4(1, 1, 1, 1), 0.10f * t);
    if (held) f = mixv(f, ImVec4(0, 0, 0, 1), 0.15f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), c32(f), st.FrameRounding);
    dl->AddText(bf, fsz, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f),
                enabled ? c32(sem.ground) : c32(sem.textDim, 0.6f), text);
    return pressed && enabled;
}

bool NoteCard(int kind, const char* text) {
    const float fs = ImGui::GetFontSize();
    const float w = ImGui::GetContentRegionAvail().x;
    const float wrap = w - fs * 2.2f;
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, wrap);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ts.y + fs * 1.2f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = kindCol(kind);
    const ImVec4 base = ImGui::ColorConvertU32ToFloat4(col);
    const float r = ImGui::GetStyle().FrameRounding + 2.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), c32(base, 0.10f), r);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), c32(base, 0.35f), r);
    dl->AddCircleFilled(ImVec2(p.x + fs * 0.8f, p.y + fs * 0.6f + fs * 0.5f), fs * 0.22f, col);
    ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 1.5f, p.y + fs * 0.6f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    return ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
}
// --- end assets-a ---

// --- assets-c ---
bool SecondaryButton(const char* label, float width, bool enabled) {
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float h = ImGui::GetFrameHeight();
    float w = width > 0.0f ? width : ts.x + st.FramePadding.x * 2.6f;
    if (width < 0.0f) w = ImMax(ts.x + st.FramePadding.x * 2.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hov, 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const float r = st.FrameRounding;
    const ImVec4 f = mixv(sem.surface, sem.text, held ? 0.12f : 0.06f * t);
    dl->AddRectFilled(p, q, c32(f, enabled ? 1.0f : 0.5f), r);
    dl->AddRect(p, q, c32(sem.border), r);
    const ImU32 ink = enabled ? c32(sem.text) : c32(sem.textDim, 0.6f);
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), ink, label, end);
    return pressed && enabled;
}

bool HelpButton(const char* label, float size, const char* tooltip) {
    const theme::Semantics& sem = theme::semantics();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(size, size));
    const bool hov = ImGui::IsItemHovered();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hov, 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
    if (t > 0.0f)
        dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), c32(sem.surface, 0.9f * t),
                          ImGui::GetStyle().FrameRounding);
    const ImU32 ink = hov ? c32(sem.text) : c32(sem.textDim);
    dl->AddCircle(c, size * 0.3f, ink, 20, 1.3f);
    const float fs = ImGui::GetFontSize() * 0.72f;
    const ImVec2 qs = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, "?");
    dl->AddText(ImGui::GetFont(), fs, ImVec2(c.x - qs.x * 0.5f, c.y - qs.y * 0.5f), ink, "?");
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

bool ThumbTileEx(const char* label, ImTextureID tex, bool flipY, Glyph fallback, bool selected,
                 float size, const char* caption, const char* note) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float capH = fs + 6.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(size, size + capH));
    const bool hov = ImGui::IsItemHovered();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hov, 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding + 2.0f;
    const ImVec2 q(p.x + size, p.y + size);
    if (selected)
        dl->AddRectFilled(ImVec2(p.x - 3, p.y - 3), ImVec2(q.x + 3, q.y + capH), c32(sem.accent, 0.10f),
                          r + 2);
    dl->AddRectFilled(p, q, c32(mixv(sem.ground, sem.field, 0.5f + 0.5f * t)), r);
    if (tex)
        dl->AddImageRounded(tex, ImVec2(p.x + 3, p.y + 3), ImVec2(q.x - 3, q.y - 3),
                            ImVec2(0, flipY ? 1.0f : 0.0f), ImVec2(1, flipY ? 0.0f : 1.0f),
                            IM_COL32_WHITE, r - 2.0f);
    else
        DrawGlyph(dl, fallback, ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.36f,
                  c32(sem.textDim, 0.8f));
    if (selected) dl->AddRect(p, q, c32(sem.accent), r, 0, 2.0f);
    else dl->AddRect(p, q, c32(sem.border), r);
    const char* cap = caption ? caption : label;
    const char* end = caption ? nullptr : ImGui::FindRenderedTextEnd(label);
    float noteW = 0.0f;
    if (note && *note) {
        FontScope mono(monoFont());
        noteW = ImGui::CalcTextSize(note).x;
        dl->AddText(ImVec2(q.x - noteW - 2.0f, q.y + 4.0f), c32(sem.textDim, 0.8f), note);
        noteW += fs * 0.5f;
    }
    dl->PushClipRect(ImVec2(p.x, q.y), ImVec2(q.x - noteW, q.y + capH), true);
    {
        FontScope bold(selected ? boldFont() : nullptr);
        dl->AddText(ImVec2(p.x + 2.0f, q.y + 4.0f),
                    c32(selected ? sem.text : mixv(sem.text, sem.textDim, 0.3f)), cap, end);
    }
    dl->PopClipRect();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) &&
        ImGui::CalcTextSize(cap, end).x > size - noteW - 4.0f)
        ImGui::SetTooltip("%.*s", end ? (int)(end - cap) : (int)std::strlen(cap), cap);
    return pressed;
}

void BadgeFlow(const char* text, int kind, bool first, const char* tooltip) {
    const float fs = ImGui::GetFontSize() * 0.82f;
    const float w = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text).x + fs;
    if (!first) {
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        if (ImGui::GetItemRectMax().x + spacing + w <= right) ImGui::SameLine(0.0f, spacing);
    }
    Badge(text, kind);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(tooltip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Callout(int kind, const char* text, float width) {
    const float fs = ImGui::GetFontSize();
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const float padX = fs * 0.75f, padY = fs * 0.55f;
    const float wrapW = ImMax(10.0f, w - padX * 2.0f - fs);
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, wrapW);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ts.y + padY * 2.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = kindCol(kind);
    const ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
    const float r = ImGui::GetStyle().FrameRounding + 2.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), c32(cv, 0.10f), r);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), c32(cv, 0.35f), r);
    dl->AddCircle(ImVec2(p.x + padX + fs * 0.3f, p.y + padY + fs * 0.5f), fs * 0.3f, col, 16, 1.4f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + padX + fs, p.y + padY));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
}

void SwatchDot(ImU32 col, float d, bool glow) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(d, d));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
    if (glow) {
        const ImVec4 cv = ImGui::ColorConvertU32ToFloat4(col);
        for (int i = 4; i >= 1; --i)
            dl->AddCircleFilled(c, d * 0.5f * (0.55f + 0.12f * (float)i), c32(cv, 0.12f), 24);
    }
    dl->AddCircleFilled(c, d * (glow ? 0.3f : 0.5f), col, 24);
}
// --- end assets-c ---


}  // namespace ui

// --- screens-c ---
namespace ui {
namespace {

ImU32 pillKindCol(int kind) {
    if (kind == 5) return c32(theme::semantics().special);
    return kindCol(kind);
}

float pillPadX() { return ImGui::GetFontSize() * 0.6f; }

}  // namespace

float PillTabWidth(const char* label, const char* detail) {
    float w = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x + pillPadX() * 2.0f;
    if (detail && *detail) {
        FontScope mono(monoFont());
        w += ImGui::CalcTextSize(detail).x + ImGui::GetStyle().ItemInnerSpacing.x;
    }
    return w;
}

bool PillTab(const char* label, const char* detail, bool selected) {
    const theme::Semantics& sem = theme::semantics();
    const float w = PillTabWidth(label, detail), h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    const ImGuiID id = ImGui::GetItemID();
    ImGuiContext& g = *GImGui;
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label,
                                g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable |
                                    (selected ? ImGuiItemStatusFlags_Checked : 0));
    (void)g;
    const float t = theme::hoverAnim(id, ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const float r = h * 0.5f;
    if (selected) {
        dl->AddRectFilled(p, q, c32(sem.accent, 0.16f), r);
        dl->AddRect(p, q, c32(sem.accent, 0.45f), r);
    } else {
        dl->AddRectFilled(p, q, c32(sem.surface, 0.6f + 0.4f * t), r);
        dl->AddRect(p, q, c32(sem.border), r);
    }
    const char* end = ImGui::FindRenderedTextEnd(label);
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    float x = p.x + pillPadX();
    dl->AddText(ImVec2(x, ty), c32(selected ? sem.text : mixv(sem.text, sem.textDim, 0.25f)), label, end);
    if (detail && *detail) {
        x += ImGui::CalcTextSize(label, end).x + ImGui::GetStyle().ItemInnerSpacing.x;
        FontScope mono(monoFont());
        dl->AddText(ImVec2(x, ty), c32(sem.textDim, 0.9f), detail);
    }
    return pressed;
}

float ChipButtonWidth(const char* label) {
    return ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x + pillPadX() * 2.0f;
}

bool ChipButton(const char* label, bool active, float width, bool enabled) {
    const theme::Semantics& sem = theme::semantics();
    const float w = width > 0.0f ? width : ChipButtonWidth(label);
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    ImGui::EndDisabled();
    const float t = theme::hoverAnim(ImGui::GetItemID(), enabled && ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const float r = h * 0.5f;
    if (active) {
        dl->AddRectFilled(p, q, c32(sem.accent, 0.16f + 0.08f * t), r);
        dl->AddRect(p, q, c32(sem.accent, 0.4f), r);
    } else {
        dl->AddRectFilled(p, q, c32(mixv(sem.surface, sem.text, 0.04f + 0.06f * t)), r);
        dl->AddRect(p, q, c32(sem.border), r);
    }
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const ImU32 ink = !enabled ? c32(sem.textDim, 0.6f)
                               : active ? c32(mixv(sem.accent, sem.text, 0.45f))
                                        : c32(mixv(sem.text, sem.textDim, 0.15f));
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), ink, label, end);
    return pressed && enabled;
}

bool ToolIconButton(const char* label, ToolIcon icon, float size, bool enabled) {
    const theme::Semantics& sem = theme::semantics();
    const float s = size > 0.0f ? size : ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(s, s));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hov, 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + s, p.y + s);
    const float r = ImGui::GetStyle().FrameRounding;
    dl->AddRectFilled(p, q, c32(mixv(sem.surface, sem.text, 0.03f + 0.07f * t)), r);
    const ImU32 ink = enabled ? c32(sem.text, 0.9f) : c32(sem.textDim, 0.5f);
    const ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
    const float u = s * 0.18f, th = ImMax(1.2f, s * 0.06f);
    switch (icon) {
        case ToolIcon::Play:
            dl->AddTriangleFilled(ImVec2(c.x - u * 0.8f, c.y - u * 1.1f), ImVec2(c.x - u * 0.8f, c.y + u * 1.1f),
                                  ImVec2(c.x + u * 1.1f, c.y), ink);
            break;
        case ToolIcon::Pause:
            dl->AddRectFilled(ImVec2(c.x - u * 0.9f, c.y - u), ImVec2(c.x - u * 0.25f, c.y + u), ink, 1.0f);
            dl->AddRectFilled(ImVec2(c.x + u * 0.25f, c.y - u), ImVec2(c.x + u * 0.9f, c.y + u), ink, 1.0f);
            break;
        case ToolIcon::Restart: {
            const float a0 = 3.14159f * 1.3f, a1 = 3.14159f * 2.95f;
            dl->PathArcTo(c, u * 1.1f, a0, a1, 16);
            dl->PathStroke(ink, 0, th);
            const ImVec2 a(c.x + std::cos(a0) * u * 1.1f, c.y + std::sin(a0) * u * 1.1f);
            dl->AddTriangleFilled(ImVec2(a.x - u * 0.6f, a.y - u * 0.1f), ImVec2(a.x + u * 0.3f, a.y - u * 0.55f),
                                  ImVec2(a.x + u * 0.15f, a.y + u * 0.5f), ink);
            break;
        }
        case ToolIcon::PopOut:
            dl->AddRect(ImVec2(c.x - u * 1.1f, c.y - u * 0.7f), ImVec2(c.x + u * 0.7f, c.y + u * 1.1f), ink, 1.5f,
                        0, th);
            dl->AddLine(ImVec2(c.x - u * 0.1f, c.y + u * 0.1f), ImVec2(c.x + u * 1.2f, c.y - u * 1.2f), ink, th);
            dl->AddLine(ImVec2(c.x + u * 0.4f, c.y - u * 1.2f), ImVec2(c.x + u * 1.2f, c.y - u * 1.2f), ink, th);
            dl->AddLine(ImVec2(c.x + u * 1.2f, c.y - u * 1.2f), ImVec2(c.x + u * 1.2f, c.y - u * 0.4f), ink, th);
            break;
        case ToolIcon::Up:
        case ToolIcon::Down: {
            const float d = icon == ToolIcon::Up ? -1.0f : 1.0f;
            dl->AddLine(ImVec2(c.x - u, c.y - d * u * 0.45f), ImVec2(c.x, c.y + d * u * 0.55f), ink, th);
            dl->AddLine(ImVec2(c.x, c.y + d * u * 0.55f), ImVec2(c.x + u, c.y - d * u * 0.45f), ink, th);
            break;
        }
        case ToolIcon::Plus:
            dl->AddLine(ImVec2(c.x - u, c.y), ImVec2(c.x + u, c.y), ink, th);
            dl->AddLine(ImVec2(c.x, c.y - u), ImVec2(c.x, c.y + u), ink, th);
            break;
    }
    if (hov && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        const char* end = ImGui::FindRenderedTextEnd(label);
        ImGui::SetTooltip("%.*s", (int)(end - label), label);
    }
    return pressed && enabled;
}

void Meter(float fraction, float width, float warnAt, float dangerAt) {
    const theme::Semantics& sem = theme::semantics();
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const float lineH = ImGui::GetTextLineHeight();
    const float h = ImMax(3.0f, ImGui::GetFontSize() * 0.28f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    // Centred on the line's TEXT, as Text() places it: after SameLine on a
    // frame-padded line (AlignTextToFramePadding) the text sits lower than
    // the cursor, and the bar floated above it (Save Editor header).
    const float y = p.y + ImGui::GetCurrentWindowRead()->DC.CurrLineTextBaseOffset + (lineH - h) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), c32(sem.field), h);
    const float f = ImClamp(fraction, 0.0f, 1.0f);
    const ImVec4 col = fraction >= dangerAt ? sem.danger : fraction >= warnAt ? sem.warn : sem.accent;
    if (f > 0.0f) dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + ImMax(h, w * f), y + h), c32(col), h);
    ImGui::Dummy(ImVec2(w, lineH));
}

float PillWidth(const char* text) {
    const float fs = ImGui::GetFontSize() * 0.82f;
    return ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text).x + fs * 1.1f;
}

void Pill(const char* text, int kind) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize() * 0.82f;
    ImFont* f = ImGui::GetFont();
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    const float padX = fs * 0.55f, h = fs + 5.0f;
    const float lineH = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y = p.y + (lineH - h) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 base = ImGui::ColorConvertU32ToFloat4(pillKindCol(kind));
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + ts.x + padX * 2.0f, y + h),
                      c32(kind == 4 ? sem.surface : base, kind == 4 ? 1.0f : 0.14f), h * 0.5f);
    dl->AddText(f, fs, ImVec2(p.x + padX, y + 2.5f),
                kind == 4 ? c32(sem.textDim) : c32(mixv(base, sem.text, 0.2f)), text);
    ImGui::Dummy(ImVec2(ts.x + padX * 2.0f, lineH));
}

// --- runtime-a ---
void DrawRtIcon(ImDrawList* dl, RtIcon icon, ImVec2 c, float size, ImU32 col) {
    const float s = size * 0.5f;
    const float th = ImMax(1.3f, size * 0.09f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    const float pi = 3.14159265f;
    switch (icon) {
        case RtIcon::Play:
            dl->AddTriangleFilled(P(-0.55f, -0.75f), P(0.8f, 0.0f), P(-0.55f, 0.75f), col);
            break;
        case RtIcon::Pause:
            dl->AddRectFilled(P(-0.6f, -0.72f), P(-0.15f, 0.72f), col, s * 0.1f);
            dl->AddRectFilled(P(0.15f, -0.72f), P(0.6f, 0.72f), col, s * 0.1f);
            break;
        case RtIcon::StepFrame: {
            const ImVec2 t[3] = {P(-0.7f, -0.7f), P(0.35f, 0.0f), P(-0.7f, 0.7f)};
            dl->AddPolyline(t, 3, col, ImDrawFlags_Closed, th);
            dl->AddLine(P(0.72f, -0.72f), P(0.72f, 0.72f), col, th);
            break;
        }
        case RtIcon::StepNode:
            dl->AddRect(P(-0.95f, -0.38f), P(-0.25f, 0.38f), col, s * 0.15f, 0, th);
            dl->AddLine(P(-0.25f, 0.0f), P(0.45f, 0.0f), col, th);
            dl->AddLine(P(0.2f, -0.28f), P(0.48f, 0.0f), col, th);
            dl->AddLine(P(0.2f, 0.28f), P(0.48f, 0.0f), col, th);
            dl->AddCircle(P(0.78f, 0.0f), s * 0.2f, col, 12, th);
            break;
        case RtIcon::Camera:
            dl->AddRect(P(-0.85f, -0.45f), P(0.85f, 0.7f), col, s * 0.15f, 0, th);
            dl->AddLine(P(-0.35f, -0.45f), P(-0.2f, -0.72f), col, th);
            dl->AddLine(P(-0.2f, -0.72f), P(0.2f, -0.72f), col, th);
            dl->AddLine(P(0.2f, -0.72f), P(0.35f, -0.45f), col, th);
            dl->AddCircle(P(0.0f, 0.12f), s * 0.3f, col, 16, th);
            break;
        case RtIcon::More:
            for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(P(i * 0.62f, 0.0f), s * 0.14f, col, 8);
            break;
        case RtIcon::Eye:
            dl->PathArcTo(P(0.0f, 0.9f), s * 1.35f, pi * 1.18f, pi * 1.82f, 16);
            dl->PathStroke(col, 0, th);
            dl->PathArcTo(P(0.0f, -0.9f), s * 1.35f, pi * 0.18f, pi * 0.82f, 16);
            dl->PathStroke(col, 0, th);
            dl->AddCircle(c, s * 0.28f, col, 12, th);
            break;
        case RtIcon::Nodes:
            dl->AddRect(P(-0.85f, -0.75f), P(-0.2f, -0.2f), col, s * 0.12f, 0, th);
            dl->AddRect(P(0.2f, 0.2f), P(0.85f, 0.75f), col, s * 0.12f, 0, th);
            dl->AddLine(P(-0.2f, -0.48f), P(0.52f, -0.48f), col, th);
            dl->AddLine(P(0.52f, -0.48f), P(0.52f, 0.2f), col, th);
            break;
        case RtIcon::Breakpoint:
            dl->AddCircle(c, s * 0.7f, col, 20, th);
            dl->AddCircleFilled(c, s * 0.3f, c32(theme::semantics().danger), 12);
            break;
        case RtIcon::Bolt: {
            const ImVec2 pts[7] = {P(0.15f, -0.95f), P(-0.6f, 0.15f), P(-0.05f, 0.15f), P(-0.2f, 0.95f),
                                   P(0.6f, -0.15f), P(0.05f, -0.15f), P(0.15f, -0.95f)};
            dl->AddPolyline(pts, 7, col, 0, th);
            break;
        }
        case RtIcon::Cube: {
            const ImVec2 hex[6] = {P(0, -0.85f), P(0.75f, -0.45f), P(0.75f, 0.45f),
                                   P(0, 0.85f),  P(-0.75f, 0.45f), P(-0.75f, -0.45f)};
            dl->AddPolyline(hex, 6, col, ImDrawFlags_Closed, th);
            dl->AddLine(P(-0.75f, -0.45f), P(0, -0.05f), col, th);
            dl->AddLine(P(0.75f, -0.45f), P(0, -0.05f), col, th);
            dl->AddLine(P(0, -0.05f), P(0, 0.85f), col, th);
            break;
        }
        case RtIcon::Monitor:
            dl->AddRect(P(-0.85f, -0.65f), P(0.85f, 0.4f), col, s * 0.15f, 0, th);
            dl->AddLine(P(0, 0.4f), P(0, 0.8f), col, th);
            dl->AddLine(P(-0.38f, 0.8f), P(0.38f, 0.8f), col, th);
            break;
        case RtIcon::Bars:
            dl->AddLine(P(-0.55f, 0.75f), P(-0.55f, -0.1f), col, th);
            dl->AddLine(P(0.0f, 0.75f), P(0.0f, -0.65f), col, th);
            dl->AddLine(P(0.55f, 0.75f), P(0.55f, 0.2f), col, th);
            dl->AddLine(P(-0.9f, 0.8f), P(0.9f, 0.8f), col, th);
            break;
        case RtIcon::Lines:
            dl->AddLine(P(-0.75f, -0.55f), P(0.75f, -0.55f), col, th);
            dl->AddLine(P(-0.75f, 0.0f), P(0.25f, 0.0f), col, th);
            dl->AddLine(P(-0.75f, 0.55f), P(-0.15f, 0.55f), col, th);
            break;
        case RtIcon::Timeline:
            dl->AddLine(P(-0.85f, -0.55f), P(-0.15f, -0.55f), col, th);
            dl->AddLine(P(-0.5f, 0.0f), P(0.55f, 0.0f), col, th);
            dl->AddLine(P(-0.15f, 0.55f), P(0.85f, 0.55f), col, th);
            break;
        case RtIcon::Chip:
            dl->AddRect(P(-0.5f, -0.5f), P(0.5f, 0.5f), col, s * 0.12f, 0, th);
            for (int i = -1; i <= 1; i += 2) {
                const float o = i * 0.22f;
                dl->AddLine(P(o, -0.5f), P(o, -0.85f), col, th);
                dl->AddLine(P(o, 0.5f), P(o, 0.85f), col, th);
                dl->AddLine(P(-0.5f, o), P(-0.85f, o), col, th);
                dl->AddLine(P(0.5f, o), P(0.85f, o), col, th);
            }
            break;
        case RtIcon::Rewind:
        case RtIcon::History:
            dl->PathArcTo(c, s * 0.72f, pi * 1.05f, pi * 2.75f, 24);
            dl->PathStroke(col, 0, th);
            dl->AddLine(P(-0.72f, 0.0f), P(-0.98f, -0.32f), col, th);
            dl->AddLine(P(-0.72f, 0.0f), P(-0.4f, -0.22f), col, th);
            if (icon == RtIcon::History) {
                dl->AddLine(c, P(0.0f, -0.42f), col, th);
                dl->AddLine(c, P(0.3f, 0.2f), col, th);
            }
            break;
        case RtIcon::Timer:
            dl->AddCircle(P(0.0f, 0.12f), s * 0.72f, col, 20, th);
            dl->AddLine(P(0.0f, 0.12f), P(0.0f, -0.3f), col, th);
            dl->AddLine(P(-0.25f, -0.9f), P(0.25f, -0.9f), col, th);
            break;
        case RtIcon::Copy:
            dl->AddRect(P(-0.25f, -0.25f), P(0.8f, 0.8f), col, s * 0.15f, 0, th);
            dl->AddLine(P(0.4f, -0.25f), P(0.4f, -0.8f), col, th);
            dl->AddLine(P(0.4f, -0.8f), P(-0.8f, -0.8f), col, th);
            dl->AddLine(P(-0.8f, -0.8f), P(-0.8f, 0.4f), col, th);
            dl->AddLine(P(-0.8f, 0.4f), P(-0.25f, 0.4f), col, th);
            break;
        case RtIcon::Plus:
            dl->AddLine(P(0, -0.75f), P(0, 0.75f), col, th);
            dl->AddLine(P(-0.75f, 0), P(0.75f, 0), col, th);
            break;
        case RtIcon::Sparkle: {
            ImVec2 pts[8];
            for (int i = 0; i < 8; ++i) {
                const float a = -pi * 0.5f + i * pi * 0.25f;
                const float r = (i % 2 == 0) ? s * 0.92f : s * 0.3f;
                pts[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
            }
            dl->AddPolyline(pts, 8, col, ImDrawFlags_Closed, th);
            break;
        }
        case RtIcon::Send:
            dl->AddLine(P(0, 0.75f), P(0, -0.7f), col, th * 1.2f);
            dl->AddLine(P(-0.55f, -0.15f), P(0, -0.72f), col, th * 1.2f);
            dl->AddLine(P(0.55f, -0.15f), P(0, -0.72f), col, th * 1.2f);
            break;
        case RtIcon::Stop:
            dl->AddRectFilled(P(-0.55f, -0.55f), P(0.55f, 0.55f), col, s * 0.15f);
            break;
        case RtIcon::Record:
            dl->AddCircleFilled(c, s * 0.55f, col, 16);
            break;
        case RtIcon::Keyboard:
            dl->AddRect(P(-0.95f, -0.55f), P(0.95f, 0.55f), col, s * 0.15f, 0, th);
            for (int i = 0; i < 4; ++i)
                dl->AddCircleFilled(P(-0.6f + i * 0.4f, -0.15f), s * 0.07f, col, 6);
            dl->AddLine(P(-0.45f, 0.25f), P(0.45f, 0.25f), col, th);
            break;
        case RtIcon::Undo:
            dl->PathArcTo(P(0.1f, 0.15f), s * 0.55f, -pi * 0.5f, pi * 0.5f, 16);
            dl->PathStroke(col, 0, th);
            dl->AddLine(P(0.1f, -0.4f), P(-0.75f, -0.4f), col, th);
            dl->AddLine(P(-0.75f, -0.4f), P(-0.4f, -0.75f), col, th);
            dl->AddLine(P(-0.75f, -0.4f), P(-0.4f, -0.05f), col, th);
            dl->AddLine(P(0.1f, 0.7f), P(-0.3f, 0.7f), col, th);
            break;
        case RtIcon::ChevronUp:
            dl->AddLine(P(-0.6f, 0.3f), P(0, -0.3f), col, th);
            dl->AddLine(P(0.6f, 0.3f), P(0, -0.3f), col, th);
            break;
        case RtIcon::ChevronDown:
            dl->AddLine(P(-0.6f, -0.3f), P(0, 0.3f), col, th);
            dl->AddLine(P(0.6f, -0.3f), P(0, 0.3f), col, th);
            break;
        case RtIcon::ChevronRight:
            dl->AddLine(P(-0.3f, -0.6f), P(0.3f, 0), col, th);
            dl->AddLine(P(-0.3f, 0.6f), P(0.3f, 0), col, th);
            break;
        case RtIcon::Check:
            dl->AddLine(P(-0.7f, 0.0f), P(-0.2f, 0.55f), col, th * 1.2f);
            dl->AddLine(P(-0.2f, 0.55f), P(0.75f, -0.55f), col, th * 1.2f);
            break;
        case RtIcon::Close:
            dl->AddLine(P(-0.6f, -0.6f), P(0.6f, 0.6f), col, th);
            dl->AddLine(P(0.6f, -0.6f), P(-0.6f, 0.6f), col, th);
            break;
        case RtIcon::Question:
            dl->AddCircle(c, s * 0.85f, col, 20, th);
            dl->PathArcTo(P(0, -0.22f), s * 0.3f, pi, pi * 2.3f, 12);
            dl->PathStroke(col, 0, th);
            dl->AddLine(P(0.05f, 0.05f), P(0.0f, 0.25f), col, th);
            dl->AddCircleFilled(P(0.0f, 0.52f), th * 0.7f, col, 6);
            break;
        case RtIcon::Pencil:
            dl->AddLine(P(-0.7f, 0.7f), P(0.5f, -0.5f), col, th * 1.6f);
            dl->AddLine(P(-0.85f, 0.85f), P(-0.7f, 0.4f), col, th);
            break;
    }
}

bool IconButton(const char* label, RtIcon icon, float size, const char* tooltip, bool enabled,
                bool active) {
    const theme::Semantics& sem = theme::semantics();
    const float sz = size > 0.0f ? size : ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(sz, sz));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hov, 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + sz, p.y + sz);
    const float r = ImGui::GetStyle().FrameRounding;
    if (active) dl->AddRectFilled(p, q, c32(sem.accent, 0.18f), r);
    else if (t > 0.0f) dl->AddRectFilled(p, q, c32(sem.surface, t), r);
    const ImU32 ink = !enabled ? c32(sem.textDim, 0.45f)
                      : active ? c32(sem.accent)
                               : c32(mixv(sem.textDim, sem.text, t));
    DrawRtIcon(dl, icon, ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f), sz * 0.52f, ink);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        const char* end = ImGui::FindRenderedTextEnd(label);
        if (tooltip) ImGui::SetTooltip("%s", tooltip);
        else ImGui::SetTooltip("%.*s", (int)(end - label), label);
    }
    return pressed && enabled;
}

float StatePillWidth(const char* text) {
    const float fs = ImGui::GetFontSize();
    FontScope bold(boldFont());
    return ImGui::CalcTextSize(text).x + fs * 1.9f;
}

void StatePill(const char* text, int kind, bool withIcon, RtIcon icon) {
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight();
    const float w = StatePillWidth(text);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 base = ImGui::ColorConvertU32ToFloat4(kindCol(kind));
    const ImVec2 q(p.x + w, p.y + h);
    dl->AddRectFilled(p, q, c32(base, kind == 4 ? 0.08f : 0.12f), h * 0.5f);
    dl->AddRect(p, q, c32(base, kind == 4 ? 0.25f : 0.38f), h * 0.5f);
    const ImVec2 ic(p.x + fs * 0.75f, p.y + h * 0.5f);
    if (withIcon) {
        DrawRtIcon(dl, icon, ic, fs * 0.72f, c32(base));
    } else {
        dl->AddCircleFilled(ic, fs * 0.42f, c32(base, 0.2f), 12);
        dl->AddCircleFilled(ic, fs * 0.25f, c32(base), 12);
    }
    {
        FontScope bold(boldFont());
        dl->AddText(ImVec2(p.x + fs * 1.35f, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), c32(base),
                    text);
    }
    ImGui::Dummy(ImVec2(w, h));
}

namespace {
float transportItemW(const TransportItem& it) {
    const float fs = ImGui::GetFontSize();
    const char* text = it.text ? it.text : it.label;
    const char* end = it.text ? nullptr : ImGui::FindRenderedTextEnd(it.label);
    float tw;
    {
        FontScope bold(it.primary && it.enabled ? boldFont() : nullptr);
        tw = ImGui::CalcTextSize(text, end).x;
    }
    float w = fs * 0.8f + fs + fs * 0.45f + tw;
    if (it.key) w += fs * 0.45f + Keycap(nullptr, ImVec2(0, 0), fs * 1.15f, it.key);
    return w;
}
}  // namespace

float TransportBarWidth(const TransportItem* items, int count) {
    float w = 6.0f;
    for (int i = 0; i < count; ++i) w += transportItemW(items[i]) + (i ? 2.0f : 0.0f);
    return w;
}

int TransportBar(const char* id, const TransportItem* items, int count) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight() + 6.0f;
    const float pad = 3.0f;
    const float w = TransportBarWidth(items, count);
    const ImVec2 base = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding + 2.0f;
    dl->AddRectFilled(base, ImVec2(base.x + w, base.y + h), c32(sem.field), r);
    dl->AddRect(base, ImVec2(base.x + w, base.y + h), c32(sem.border), r);
    int clicked = -1;
    ImGui::PushID(id);
    float x = base.x + pad;
    for (int i = 0; i < count; ++i) {
        const TransportItem& it = items[i];
        const float iw = transportItemW(it);
        const ImVec2 p(x, base.y + pad), q(x + iw, base.y + h - pad);
        ImGui::SetCursorScreenPos(p);
        ImGui::BeginDisabled(!it.enabled);
        if (ImGui::InvisibleButton(it.label, ImVec2(iw, q.y - p.y)) && it.enabled) clicked = i;
        ImGui::EndDisabled();
        const bool hov = it.enabled && ImGui::IsItemHovered();
        const float t = theme::hoverAnim(ImGui::GetItemID(), hov, 12.0f);
        if (it.tooltip &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", it.tooltip);
        ImU32 ink;
        if (it.primary && it.enabled) {
            dl->AddRectFilled(p, q, c32(mixv(sem.accent, ImVec4(1, 1, 1, 1), 0.1f * t)), r - pad * 0.5f);
            ink = c32(sem.ground);
        } else {
            if (t > 0.0f) dl->AddRectFilled(p, q, c32(sem.surface, t), r - pad * 0.5f);
            ink = it.enabled ? c32(mixv(sem.textDim, sem.text, 0.55f + 0.45f * t))
                             : c32(sem.textDim, 0.5f);
        }
        float cx = x + fs * 0.4f;
        DrawRtIcon(dl, it.icon, ImVec2(cx + fs * 0.5f, (p.y + q.y) * 0.5f), fs * 0.78f, ink);
        cx += fs + fs * 0.45f;
        const char* text = it.text ? it.text : it.label;
        const char* end = it.text ? nullptr : ImGui::FindRenderedTextEnd(it.label);
        {
            FontScope bold(it.primary && it.enabled ? boldFont() : nullptr);
            dl->AddText(ImVec2(cx, (p.y + q.y - ImGui::GetTextLineHeight()) * 0.5f), ink, text, end);
            cx += ImGui::CalcTextSize(text, end).x;
        }
        if (it.key) {
            const float kh = fs * 1.15f;
            Keycap(dl, ImVec2(cx + fs * 0.45f, (p.y + q.y - kh) * 0.5f), kh, it.key);
        }
        x += iw + 2.0f;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(base);
    ImGui::Dummy(ImVec2(w, h));
    return clicked;
}

bool RailRow(const char* label, RtIcon icon, const char* count, int countKind, bool selected,
             bool compact) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight() + 4.0f;
    const float w = compact ? h : ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(ImMax(1.0f, w), h));
    const ImGuiID id = ImGui::GetItemID();
    ImGuiContext& g = *GImGui;
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label,
                                g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable |
                                    (selected ? ImGuiItemStatusFlags_Checked : 0));
    const bool hov = ImGui::IsItemHovered();
    const float t = theme::hoverAnim(id, hov, 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const float r = ImGui::GetStyle().FrameRounding;
    if (selected) {
        dl->AddRectFilled(p, q, c32(sem.surface), r);
        dl->AddRectFilled(p, ImVec2(p.x + 2.0f, q.y), c32(sem.accent));
    } else if (t > 0.0f) {
        dl->AddRectFilled(p, q, c32(sem.surface, 0.7f * t), r);
    }
    const ImU32 iconCol = selected ? c32(sem.accent) : c32(mixv(sem.textDim, sem.text, t));
    const char* end = ImGui::FindRenderedTextEnd(label);
    if (compact) {
        DrawRtIcon(dl, icon, ImVec2(p.x + w * 0.5f, p.y + h * 0.5f), fs * 0.9f, iconCol);
        if (count && *count && countKind != 4)
            dl->AddCircleFilled(ImVec2(q.x - fs * 0.3f, p.y + fs * 0.35f), fs * 0.18f, kindCol(countKind), 8);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%.*s%s%s", (int)(end - label), label, count && *count ? "  " : "",
                              count ? count : "");
        return pressed;
    }
    DrawRtIcon(dl, icon, ImVec2(p.x + fs * 1.05f, p.y + h * 0.5f), fs * 0.9f, iconCol);
    float cw = 0.0f;
    if (count && *count) {
        FontScope mono(monoFont());
        const float cfs = ImGui::GetFontSize() * 0.85f;
        cw = ImGui::GetFont()->CalcTextSizeA(cfs, FLT_MAX, 0.0f, count).x;
        dl->AddText(ImGui::GetFont(), cfs, ImVec2(q.x - cw - fs * 0.6f, p.y + (h - cfs) * 0.5f),
                    countKind == 4 ? c32(sem.textDim, 0.8f) : kindCol(countKind), count);
    }
    dl->PushClipRect(p, ImVec2(q.x - cw - fs, q.y), true);
    dl->AddText(ImVec2(p.x + fs * 2.0f, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                selected ? c32(sem.text) : c32(mixv(sem.text, sem.textDim, 0.25f * (1.0f - t))), label,
                end);
    dl->PopClipRect();
    return pressed;
}

void KpiTile(const char* label, const char* value, const char* sub, ImU32 valueCol, float w) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float h = fs * 4.4f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding + 3.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), c32(sem.field), r);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), c32(sem.border), r);
    const float x = p.x + fs * 0.8f;
    dl->PushClipRect(p, ImVec2(p.x + w - fs * 0.4f, p.y + h), true);
    dl->AddText(ImGui::GetFont(), fs * 0.88f, ImVec2(x, p.y + fs * 0.55f), c32(sem.textDim), label);
    {
        ImFont* f = monoFont() ? monoFont() : ImGui::GetFont();
        dl->AddText(f, fs * 1.4f, ImVec2(x, p.y + fs * 1.5f), valueCol ? valueCol : c32(sem.text), value);
    }
    if (sub)
        dl->AddText(ImGui::GetFont(), fs * 0.82f, ImVec2(x, p.y + fs * 3.1f), c32(sem.textDim, 0.75f), sub);
    dl->PopClipRect();
    ImGui::Dummy(ImVec2(w, h));
}

void Meter(const float* fracs, const ImU32* cols, int n, float height) {
    const theme::Semantics& sem = theme::semantics();
    const float h = height > 0.0f ? height : ImMax(6.0f, ImGui::GetFontSize() * 0.5f);
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), c32(sem.field), h * 0.5f);
    dl->PushClipRect(p, ImVec2(p.x + w, p.y + h), true);
    float x = p.x;
    for (int i = 0; i < n; ++i) {
        const float sw = w * ImClamp(fracs[i], 0.0f, 1.0f);
        if (sw <= 0.0f) continue;
        dl->AddRectFilled(ImVec2(x, p.y), ImVec2(ImMin(x + sw, p.x + w), p.y + h), cols[i],
                          i == 0 ? h * 0.5f : 0.0f, i == 0 ? ImDrawFlags_RoundCornersLeft : 0);
        x += sw;
    }
    dl->PopClipRect();
    ImGui::Dummy(ImVec2(w, h));
}

void LegendSwatch(ImU32 col, const char* text) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float sw = fs * 0.55f;
    const float y = p.y + (ImGui::GetTextLineHeight() - sw) * 0.5f;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + sw, y + sw), col, 2.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + sw + fs * 0.4f, p.y));
    ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}
// --- end runtime-a ---

// --- screens-a ---------------------------------------------------------------

float StatusPillWidth(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetFontSize() * 1.9f;
}

bool StatusPill(const char* label, int kind) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float w = StatusPillWidth(label), h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const ImVec4 base = ImGui::ColorConvertU32ToFloat4(kindCol(kind));
    const float r = h * 0.5f;
    if (kind == 4) {
        dl->AddRectFilled(p, q, c32(sem.surface, 0.6f + 0.4f * t), r);
        dl->AddRect(p, q, c32(sem.border), r);
    } else {
        dl->AddRectFilled(p, q, c32(base, 0.12f + 0.08f * t), r);
        dl->AddRect(p, q, c32(base, 0.45f), r);
    }
    dl->AddCircleFilled(ImVec2(p.x + fs * 0.75f, p.y + h * 0.5f), fs * 0.2f, kindCol(kind));
    const char* end = ImGui::FindRenderedTextEnd(label);
    dl->AddText(ImVec2(p.x + fs * 1.2f, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                kind == 4 ? c32(sem.textDim) : c32(mixv(base, sem.text, 0.25f)), label, end);
    return pressed;
}

bool GlyphButton(const char* label, Glyph g, float size, const char* tooltip, bool active) {
    const theme::Semantics& sem = theme::semantics();
    const float s = size > 0.0f ? size : ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(s, s));
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + s, p.y + s);
    const float r = ImGui::GetStyle().FrameRounding;
    if (active) dl->AddRectFilled(p, q, c32(sem.accent, 0.22f), r);
    else if (t > 0.0f) dl->AddRectFilled(p, q, c32(sem.surface, 0.9f * t), r);
    DrawGlyph(dl, g, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.55f,
              active ? c32(sem.accent) : c32(mixv(sem.textDim, sem.text, t)));
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

void ValueChip(const char* text, bool dim) {
    const theme::Semantics& sem = theme::semantics();
    FontScope mono(monoFont());
    const float fs = ImGui::GetFontSize();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float padX = fs * 0.4f, h = ImGui::GetTextLineHeight() + fs * 0.2f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + ts.x + padX * 2.0f, p.y + h), c32(sem.field), fs * 0.25f);
    dl->AddText(ImVec2(p.x + padX, p.y + (h - ts.y) * 0.5f), dim ? c32(sem.textDim) : c32(sem.text), text);
    ImGui::Dummy(ImVec2(ts.x + padX * 2.0f, h));
}

bool RecordButton(const char* label, bool recording, float width, bool enabled) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight() * 1.45f;
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(ImMax(1.0f, w), h));
    ImGui::EndDisabled();
    const float t = theme::hoverAnim(ImGui::GetItemID(), enabled && ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const float r = ImGui::GetStyle().FrameRounding + 2.0f;
    float a = enabled ? 0.85f + 0.15f * t : 0.35f;
    if (recording) a = 0.55f + 0.3f * (0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 5.0f));
    dl->AddRectFilled(p, q, c32(sem.danger, a), r);
    const char* end = ImGui::FindRenderedTextEnd(label);
    FontScope bold(boldFont());
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float mark = fs * 0.62f, gap = fs * 0.5f;
    const float x = p.x + (w - ts.x - mark - gap) * 0.5f, cy = p.y + h * 0.5f;
    const ImU32 ink = c32(sem.ground);
    if (recording)
        dl->AddRectFilled(ImVec2(x, cy - mark * 0.5f), ImVec2(x + mark, cy + mark * 0.5f), ink, 2.0f);
    else
        dl->AddCircleFilled(ImVec2(x + mark * 0.5f, cy), mark * 0.5f, ink);
    dl->AddText(ImVec2(x + mark + gap, cy - ts.y * 0.5f), ink, label, end);
    return pressed && enabled;
}

// --- end screens-a -----------------------------------------------------------

// --- world-a ---

bool ToolTile(const char* label, Glyph glyph, bool active, const char* key, float width, float height,
              IconDrawFn draw) {
    const theme::Semantics& sem = theme::semantics();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const float h = height > 0.0f ? height : ImGui::GetFrameHeight() * 2.4f;
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    {
        ImGuiContext& g = *GImGui;  // the hook macro reads `g`
        IMGUI_TEST_ENGINE_ITEM_INFO(g.LastItemData.ID, label,
                                    g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable |
                                        (active ? ImGuiItemStatusFlags_Checked : 0));
        (void)g;
    }
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const float r = ImGui::GetStyle().FrameRounding + 3.0f;
    if (active) {
        dl->AddRectFilled(p, q, c32(sem.accent, 0.14f), r);
        dl->AddRect(p, q, c32(sem.accent, 0.45f), r);
    } else {
        dl->AddRectFilled(p, q, c32(mixv(sem.field, sem.surface, 0.6f * t)), r);
        dl->AddRect(p, q, c32(sem.border), r);
    }
    const float fs = ImGui::GetFontSize();
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float iconS = fs * 1.15f;
    const float blockH = iconS + fs * 0.35f + ts.y;
    const float y0 = p.y + (h - blockH) * 0.5f;
    const ImU32 ink = active ? c32(sem.text) : c32(sem.textDim);
    const ImVec2 ic(p.x + w * 0.5f, y0 + iconS * 0.5f);
    if (draw) draw(dl, ic, iconS, active ? c32(sem.accent) : ink);
    else DrawGlyph(dl, glyph, ic, iconS, active ? c32(sem.accent) : ink);
    dl->PushClipRect(p, q, true);
    dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, y0 + iconS + fs * 0.35f), ink, label, end);
    dl->PopClipRect();
    if (key && *key) {
        const float kh = fs * 0.95f;
        const float kw = Keycap(nullptr, ImVec2(0, 0), kh, key);
        Keycap(dl, ImVec2(q.x - kw - 4.0f, p.y + 4.0f), kh, key);
    }
    return pressed;
}

void StatTile(const char* caption, const char* value, int kind, float width) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const float h = fs * 2.0f + fs * 1.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding + 2.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), c32(sem.field), r);
    dl->PushClipRect(p, ImVec2(p.x + w, p.y + h), true);
    dl->AddText(ImGui::GetFont(), fs * 0.82f, ImVec2(p.x + fs * 0.6f, p.y + fs * 0.45f), c32(sem.textDim),
                caption);
    {
        FontScope mono(monoFont());
        dl->AddText(ImVec2(p.x + fs * 0.6f, p.y + fs * 0.45f + fs * 0.95f),
                    kind == 4 ? c32(sem.text) : kindCol(kind), value);
    }
    dl->PopClipRect();
    ImGui::Dummy(ImVec2(w, h));
}

bool Note(int kind, const char* text, const char* action) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float w = ImGui::GetContentRegionAvail().x;
    const float pad = fs * 0.7f;
    const float textX = pad + fs * 1.1f;
    const float wrapW = ImMax(fs * 4.0f, w - textX - pad);
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, wrapW);
    const float linkH = action ? fs + 6.0f : 0.0f;
    const float h = ts.y + linkH + pad * 2.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = kindCol(kind);
    const ImVec4 colV = ImGui::ColorConvertU32ToFloat4(col);
    const float r = ImGui::GetStyle().FrameRounding + 2.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), c32(colV, 0.10f), r);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), c32(colV, 0.35f), r);
    dl->AddCircle(ImVec2(p.x + pad + fs * 0.4f, p.y + pad + fs * 0.5f), fs * 0.38f, col, 16, 1.5f);
    dl->AddLine(ImVec2(p.x + pad + fs * 0.4f, p.y + pad + fs * 0.3f),
                ImVec2(p.x + pad + fs * 0.4f, p.y + pad + fs * 0.58f), col, 1.5f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + textX, p.y + pad));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
    ImGui::PushStyleColor(ImGuiCol_Text, mixv(sem.text, colV, 0.25f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    bool clicked = false;
    if (action) {
        ImGui::SetCursorScreenPos(ImVec2(p.x + textX, p.y + pad + ts.y + 4.0f));
        clicked = Link(action);
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    return clicked;
}

bool Link(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_TextLink, theme::semantics().accent);
    const bool clicked = ImGui::TextLink(label);
    ImGui::PopStyleColor();
    return clicked;
}

void Heading(const char* title, const char* sub) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = p.x;
    {
        ImFont* f = boldFont() ? boldFont() : ImGui::GetFont();
        const float hs = fs * 1.15f;
        dl->AddText(f, hs, p, c32(sem.text), title);
        x += f->CalcTextSizeA(hs, FLT_MAX, 0.0f, title).x;
    }
    if (sub && *sub)
        dl->AddText(ImGui::GetFont(), fs * 0.9f, ImVec2(x + fs * 0.6f, p.y + fs * 0.2f), c32(sem.textDim), sub);
    ImGui::Dummy(ImVec2(1.0f, fs * 1.15f + fs * 0.45f));
}
// --- end world-a ---

// --- content ---
bool RangeSlider(const char* label, float* lo, float* hi, float vmin, float vmax, const char* fmt,
                 float width) {
    const theme::Semantics& sem = theme::semantics();
    const float w = width > 0.0f ? width : ImGui::CalcItemWidth();
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    (void)pressed;
    const ImGuiID id = ImGui::GetItemID();
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();

    char a[32], b[32], txt[72];
    std::snprintf(a, sizeof a, fmt, *lo);
    std::snprintf(b, sizeof b, fmt, *hi);
    std::snprintf(txt, sizeof txt, "%s - %s", a, b);
    ImFont* mono = monoFont();
    const float fsz = ImGui::GetFontSize();
    ImGui::PushFont(mono, 0.0f);
    const ImVec2 ts = ImGui::CalcTextSize(txt);
    ImGui::PopFont();
    const float valW = std::min(ts.x + fsz * 0.8f, w * 0.45f);
    const float trackR = p.x + w - valW;
    const float knob = h * 0.3f;
    const float x0 = p.x + knob + 1.0f, x1 = std::max(x0 + 1.0f, trackR - knob - 1.0f);
    const float span = vmax - vmin != 0.0f ? vmax - vmin : 1.0f;
    auto toX = [&](float v) { return x0 + (x1 - x0) * std::clamp((v - vmin) / span, 0.0f, 1.0f); };

    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID key = id ^ 0x52414e47u;  // which handle the drag holds
    const float mx = ImGui::GetIO().MousePos.x;
    if (ImGui::IsItemActivated()) {
        const float dl = std::fabs(mx - toX(*lo)), dh = std::fabs(mx - toX(*hi));
        st->SetInt(key, (dh < dl || (dh == dl && mx > toX(*hi))) ? 1 : 0);
    }
    bool changed = false;
    if (active) {
        const float v = vmin + std::clamp((mx - x0) / (x1 - x0), 0.0f, 1.0f) * span;
        if (st->GetInt(key, 0) == 0) {
            const float nv = std::min(v, *hi);
            if (nv != *lo) *lo = nv, changed = true;
        } else {
            const float nv = std::max(v, *lo);
            if (nv != *hi) *hi = nv, changed = true;
        }
        if (changed) ImGui::MarkItemEdited(id);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding;
    const float t = theme::hoverAnim(id, hovered || active, 12.0f);
    dl->AddRectFilled(p, ImVec2(trackR, p.y + h), c32(mixv(sem.field, sem.text, 0.03f * t)), r);
    const float cy = p.y + h * 0.5f;
    const float xl = toX(*lo), xh = toX(*hi);
    dl->AddRectFilled(ImVec2(xl, cy - h * 0.16f), ImVec2(xh, cy + h * 0.16f), c32(sem.accent, 0.55f),
                      h * 0.16f);
    const int which = active ? st->GetInt(key, 0) : -1;
    for (int k = 0; k < 2; ++k) {
        const ImVec2 c(k == 0 ? xl : xh, cy);
        dl->AddCircleFilled(c, knob, c32(sem.ground), 16);
        dl->AddCircleFilled(c, knob - 1.5f, which == k ? c32(sem.accent) : c32(sem.text), 16);
    }
    dl->AddText(mono, mono ? fsz : 0.0f, ImVec2(p.x + w - ts.x, p.y + (h - ts.y) * 0.5f),
                c32(sem.textDim), txt);
    return changed;
}
// --- end content ---

// --- drone ---
void RowLabel(const char* text, const char* tip, float labelW) {
    const theme::Semantics& sem = theme::semantics();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    if (tip) {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (float x = a.x; x < b.x; x += 3.0f)
            dl->AddRectFilled(ImVec2(x, b.y - 1.0f), ImVec2(x + 1.0f, b.y), c32(sem.textDim));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    }
    ImGui::SameLine(labelW);
}

bool TrackSlider(const char* id, float* v, float lo, float hi, const std::string& shown, ImU32 fromCol,
                 ImU32 toCol, const char* detail, float valueW, bool enabled) {
    const theme::Semantics& sem = theme::semantics();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    const float fs = ImGui::GetFontSize();
    ImGui::BeginDisabled(!enabled);
    ImGui::InvisibleButton(id, ImVec2(w, h));
    ImGui::EndDisabled();
    float textW, detailW = 0.0f, gap = 0.0f;
    if (detail) {
        textW = ImGui::CalcTextSize(shown.c_str()).x;
        if (*detail) {
            ui::FontScope mono(ui::monoFont());
            detailW = ImGui::CalcTextSize(detail).x * 0.85f;
            gap = fs * 0.35f;
        }
    } else {
        ui::FontScope mono(ui::monoFont());
        textW = ImGui::CalcTextSize(shown.c_str()).x;
        valueW = ImMax(valueW, ImGui::CalcTextSize("00:00-00:00").x * 0.6f);
    }
    const float tw = ImMax(valueW, textW + gap + detailW) + 10.0f;
    const float x0 = p.x + 7.0f, x1 = p.x + w - tw - 7.0f;
    bool changed = false;
    if (enabled && ImGui::IsItemActive() && x1 > x0) {
        const float t = ImClamp((ImGui::GetIO().MousePos.x - x0) / (x1 - x0), 0.0f, 1.0f);
        const float nv = lo + (hi - lo) * t;
        if (nv != *v) *v = nv, changed = true;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f;
    const float t = hi > lo ? ImClamp((*v - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    const float kx = x0 + (x1 - x0) * t;
    const float alpha = enabled ? 1.0f : 0.4f;
    if (fromCol || toCol) {
        auto fade = [alpha](ImU32 c) {
            return (c & ~IM_COL32_A_MASK) | ((ImU32)(((c >> IM_COL32_A_SHIFT) & 0xFF) * alpha) << IM_COL32_A_SHIFT);
        };
        dl->AddRectFilledMultiColor(ImVec2(x0, cy - 2), ImVec2(x1, cy + 2), fade(fromCol), fade(toCol), fade(toCol),
                                    fade(fromCol));
    } else {
        dl->AddRectFilled(ImVec2(x0, cy - 2), ImVec2(x1, cy + 2), ImGui::GetColorU32(ImGuiCol_FrameBg), 2.0f);
        dl->AddRectFilled(ImVec2(x0, cy - 2), ImVec2(kx, cy + 2), c32(sem.accent, alpha), 2.0f);
    }
    const bool hot = enabled && (ImGui::IsItemHovered() || ImGui::IsItemActive());
    dl->AddCircleFilled(ImVec2(kx, cy), hot ? 8.0f : 7.0f, c32(sem.text, 0.96f * alpha), 20);
    if (detail) {
        const float right = p.x + w;
        if (detailW > 0.0f) {
            ui::FontScope mono(ui::monoFont());
            const float dfs = ImGui::GetFontSize() * 0.85f;
            dl->AddText(ImGui::GetFont(), dfs, ImVec2(right - detailW, cy - dfs * 0.5f), c32(sem.textDim, alpha),
                        detail);
        }
        dl->AddText(ImVec2(right - detailW - gap - textW, cy - fs * 0.5f), c32(sem.text, alpha), shown.c_str());
    } else {
        ui::FontScope mono(ui::monoFont());
        const ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
        dl->AddText(ImVec2(p.x + w - ts.x, cy - ts.y * 0.5f), c32(sem.text, alpha), shown.c_str());
    }
    return changed;
}

bool SegmentedApprox(const char* id, const char* const* labels, int count, int* selected, int approx, float width,
                     bool enabled, bool* hovered) {
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float h = ImGui::GetFrameHeight();
    const float pad = 3.0f;
    float natural = pad * 2.0f;
    for (int i = 0; i < count; ++i)
        natural += ImGui::CalcTextSize(labels[i], ImGui::FindRenderedTextEnd(labels[i])).x + st.FramePadding.x * 2.0f;
    const float w = width > 0.0f ? width : (width < 0.0f ? ImGui::GetContentRegionAvail().x : natural);
    const float scale = (w - pad * 2.0f) / (natural - pad * 2.0f);
    ImGui::PushID(id);
    const ImVec2 base = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = st.FrameRounding;
    const float alpha = enabled ? 1.0f : 0.45f;
    dl->AddRectFilled(base, ImVec2(base.x + w, base.y + h), c32(sem.field), r);
    dl->AddRect(base, ImVec2(base.x + w, base.y + h), c32(sem.border), r);
    bool clicked = false, hov = false;
    float x = base.x + pad;
    ImGui::BeginDisabled(!enabled);
    for (int i = 0; i < count; ++i) {
        const char* end = ImGui::FindRenderedTextEnd(labels[i]);
        const float sw = (ImGui::CalcTextSize(labels[i], end).x + st.FramePadding.x * 2.0f) * scale;
        ImGui::SetCursorScreenPos(ImVec2(x, base.y + pad));
        if (ImGui::InvisibleButton(labels[i], ImVec2(sw, h - pad * 2.0f))) {
            *selected = i;
            clicked = true;
        }
        hov |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        const bool on = *selected == i;
        const bool near = !on && approx == i;
        uiscript::markLastItemChecked(on);
        const float t = theme::hoverAnim(ImGui::GetItemID(), enabled && ImGui::IsItemHovered() && !on, 12.0f);
        const ImVec2 a(x, base.y + pad), b(x + sw, base.y + h - pad);
        if (on || t > 0.0f) dl->AddRectFilled(a, b, c32(sem.surface, on ? 1.0f : 0.5f * t), r - pad * 0.5f);
        if (near) dl->AddRect(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f), c32(sem.accent, 0.8f),
                              r - pad * 0.5f, 0, 1.0f);
        const ImVec2 ts = ImGui::CalcTextSize(labels[i], end);
        const ImU32 tc = near ? c32(sem.accent, alpha) : on ? c32(sem.text, alpha) : c32(sem.textDim, alpha);
        dl->AddText(ImVec2(x + (sw - ts.x) * 0.5f, base.y + (h - ts.y) * 0.5f), tc, labels[i], end);
        x += sw;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(base);
    ImGui::Dummy(ImVec2(w, h));
    if (hovered) *hovered = hov;
    return clicked;
}
// --- end drone ---

}  // namespace ui
// --- end screens-c ---

// --- verify-polish-2 ---------------------------------------------------------
namespace ui {

bool GoButton(const char* label, float width, bool enabled) {
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImFont* bf = boldFont() ? boldFont() : ImGui::GetFont();
    const float fsz = ImGui::GetFontSize();
    const ImVec2 ts = bf->CalcTextSizeA(fsz, FLT_MAX, 0.0f, label, end);
    const float icon = IM_TRUNC(fsz * 0.62f), gap = IM_TRUNC(fsz * 0.4f);
    const float h = ImGui::GetFrameHeight();
    const float w = width > 0.0f ? width : ts.x + icon + gap + st.FramePadding.x * 2.6f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hov, 12.0f);
    ImVec4 f = enabled ? sem.ok : sem.surface;
    if (enabled) f = mixv(f, ImVec4(1, 1, 1, 1), 0.10f * t);
    if (held) f = mixv(f, ImVec4(0, 0, 0, 1), 0.15f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), c32(f), st.FrameRounding);
    const ImU32 ink = enabled ? c32(sem.ground) : c32(sem.textDim, 0.6f);
    // the play triangle, then the label, centred as one group
    const float gx = p.x + (w - (icon + gap + ts.x)) * 0.5f;
    const float cy = p.y + h * 0.5f;
    dl->AddTriangleFilled(ImVec2(gx, cy - icon * 0.5f), ImVec2(gx, cy + icon * 0.5f),
                          ImVec2(gx + icon * 0.85f, cy), ink);
    dl->AddText(bf, fsz, ImVec2(gx + icon + gap, p.y + (h - ts.y) * 0.5f), ink, label, end);
    return pressed && enabled;
}


bool KeyButton(const char* label, const char* key, bool primary, bool enabled) {
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImFont* face = primary && boldFont() ? boldFont() : ImGui::GetFont();
    const float fsz = ImGui::GetFontSize();
    const ImVec2 ts = face->CalcTextSizeA(fsz, FLT_MAX, 0.0f, label, end);
    const float h = ImGui::GetFrameHeight();
    // the key hint: mono, small, on a tile a little shorter than the text line
    ImFont* mono = monoFont() ? monoFont() : ImGui::GetFont();
    const float kfs = fsz * 0.72f;
    const ImVec2 ks = key && *key ? mono->CalcTextSizeA(kfs, FLT_MAX, 0.0f, key) : ImVec2(0, 0);
    const float kpad = kfs * 0.35f, kh = kfs + kpad * 1.2f;
    const float kw = ks.x > 0.0f ? ks.x + kpad * 2.0f : 0.0f;
    const float gap = kw > 0.0f ? fsz * 0.55f : 0.0f;
    const float w = ts.x + gap + kw + st.FramePadding.x * 2.6f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hov, 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + h);
    const float r = st.FrameRounding;
    ImU32 ink, kink, kbg;
    if (primary) {
        ImVec4 f = enabled ? sem.accent : sem.surface;
        if (enabled) f = mixv(f, ImVec4(1, 1, 1, 1), 0.10f * t);
        if (held) f = mixv(f, ImVec4(0, 0, 0, 1), 0.15f);
        dl->AddRectFilled(p, q, c32(f), r);
        ink = enabled ? c32(sem.ground) : c32(sem.textDim, 0.6f);
        kink = enabled ? c32(sem.ground, 0.7f) : c32(sem.textDim, 0.5f);
        kbg = c32(sem.ground, 0.14f);
    } else {
        const ImVec4 f = mixv(sem.surface, sem.text, held ? 0.12f : 0.06f * t);
        dl->AddRectFilled(p, q, c32(f, enabled ? 1.0f : 0.5f), r);
        dl->AddRect(p, q, c32(sem.border), r);
        ink = enabled ? c32(sem.text) : c32(sem.textDim, 0.6f);
        kink = c32(sem.textDim);
        kbg = c32(sem.ground, 0.5f);
    }
    const float x0 = p.x + st.FramePadding.x * 1.3f;
    dl->AddText(face, fsz, ImVec2(x0, p.y + (h - ts.y) * 0.5f), ink, label, end);
    if (kw > 0.0f) {
        const ImVec2 k0(x0 + ts.x + gap, p.y + (h - kh) * 0.5f);
        dl->AddRectFilled(k0, ImVec2(k0.x + kw, k0.y + kh), kbg, kh * 0.25f);
        dl->AddText(mono, kfs, ImVec2(k0.x + kpad, k0.y + (kh - ks.y) * 0.5f), kink, key);
    }
    return pressed && enabled;
}

}  // namespace ui
// --- end verify-polish-2 -----------------------------------------------------
