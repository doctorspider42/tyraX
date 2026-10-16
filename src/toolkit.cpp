#include "toolkit.hpp"

#include "theme.hpp"

#include <imgui_internal.h>

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
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w, h));
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
    return ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
}
// --- end assets-a ---
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
    const float y = p.y + (lineH - h) * 0.5f;
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

}  // namespace ui
// --- end screens-c ---
