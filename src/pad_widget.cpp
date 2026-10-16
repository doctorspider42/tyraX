// ui::PadDiagram - the one DualShock 2 drawing (toolkit.hpp "pad" section,
// docs/editor-theme.md "Controller").
//
// The geometry is a straight port of the approved generator (the design
// round's padsvg.py, revision 4): a 1000 x 630 unit box, y down, every value
// below in those units. One table, so Remote Pad, Input Map, the pickers and
// the AI cards cannot drift apart again - before this the editor drew the pad
// twice with different coordinates and three hand-written button lists.
//
// ImDrawList only. Colours come from theme::padColors() (body, keys, the real
// symbol colours) and theme::semantics() (accent = pressed / selected, warn =
// latched / conflict). Strokes are in pixels, not units, so a 120 px card pad
// and a 760 px Remote Pad have the same hairlines.
#include "toolkit.hpp"

#include "input.hpp"
#include "theme.hpp"
#include "uiscript.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ui {
namespace {

// --- geometry (units) ---------------------------------------------------------

constexpr float kW = 1000.0f, kH = 630.0f;
constexpr float kTop = 4.0f;  // the box's margin above y = 0 (the L2 tab's top edge)
constexpr float kPi = 3.14159265358979f;

enum Btn { Cross, Square, Triangle, Circle, Up, Down, Left, Right, L1, L2, L3, R1, R2, R3, Start, Select };
const char* const kItemName[16] = {"Cross", "Square", "Triangle", "Circle", "Up", "Down", "Left", "Right",
                                   "L1",    "L2",     "L3",       "R1",     "R2", "R3",   "Start", "Select"};
// Submission order: the order the spec lists the items in.
const int kOrder[16] = {L2, L1, R1, R2, Up, Left, Right, Down, Triangle, Square, Circle, Cross, Select, Start, L3, R3};

const ImVec2 kLobeL(202, 215), kLobeR(798, 215);
constexpr float kLobeRad = 159, kPlateR = 116, kBridgeY = 100, kUTop = 446;
const ImVec2 kGripR(905, 532);
constexpr float kGripRad = 94;

const ImVec2 kDpadC = kLobeL;
const ImVec2 kArm[7] = {{-28, -98}, {0, -112}, {28, -98}, {28, -36}, {13, -20}, {-13, -20}, {-28, -36}};
const ImVec2 kEmboss[3] = {{0, -134}, {8, -122}, {-8, -122}};
constexpr float kDipHalfLen = 104, kDipHalfW = 32;
int dpadDeg(int b) { return b == Up ? 0 : b == Right ? 90 : b == Down ? 180 : 270; }

constexpr float kFaceR = 35, kFaceWell = 41;
ImVec2 faceC(int b) {
    switch (b) {
        case Triangle: return ImVec2(798, 143);
        case Circle: return ImVec2(870, 215);
        case Cross: return ImVec2(798, 287);
        default: return ImVec2(726, 215);  // Square
    }
}

const ImVec2 kStickL(350, 392), kStickR(650, 392);
constexpr float kWellR = 80, kCapR = 64, kDishR = 52, kTravel = 18, kDotTravel = 66;

constexpr float kSelX = 407, kSelY = 234, kSelW = 52, kSelH = 30;
const ImVec2 kStartPts[5] = {{562, 219}, {608, 219}, {624, 234}, {608, 249}, {562, 249}};
constexpr float kAnaX = 500, kAnaY = 330, kAnaW = 56, kAnaH = 24;
constexpr float kLedX = 500, kLedY = 366, kLedW = 40, kLedH = 10;
constexpr float kBadgeY = 66;

// Shoulder tabs (left; right mirrors): x0 x1 y0 y1, and where they stop being
// visible above the body.
constexpr float kL2Tab[4] = {124, 270, 0, 70}, kL1Tab[4] = {110, 284, 28, 110};
constexpr float kTabR = 24, kL2Vis = 28, kL1Vis = 66;

struct Rect4 {
    float x0, y0, x1, y1;
};
Rect4 tabRect(int b) {
    const float* t = (b == L2 || b == R2) ? kL2Tab : kL1Tab;
    Rect4 r{t[0], t[2], t[1], t[3]};
    if (b == R1 || b == R2) r = Rect4{kW - t[1], t[2], kW - t[0], t[3]};
    return r;
}
float tabVisBottom(int b) { return (b == L2 || b == R2) ? kL2Vis : kL1Vis; }

ImVec2 rot(ImVec2 p, int deg, ImVec2 c = kDpadC) {
    const float a = deg * kPi / 180.0f, cs = std::cos(a), sn = std::sin(a);
    return ImVec2(c.x + p.x * cs - p.y * sn, c.y + p.x * sn + p.y * cs);
}
ImVec2 arcPt(ImVec2 c, float r, float deg) {
    const float a = deg * kPi / 180.0f;
    return ImVec2(c.x + r * std::cos(a), c.y + r * std::sin(a));
}

// The outline in units: the right half clockwise from the bridge top centre
// (cubics and arcs flattened), the left half its mirror walked back.
const std::vector<ImVec2>& bodyOutline() {
    static std::vector<ImVec2> pts;
    if (!pts.empty()) return pts;
    std::vector<ImVec2> r;
    auto cubic = [&](ImVec2 c1, ImVec2 c2, ImVec2 e) {
        const ImVec2 s = r.back();
        const int n = 24;
        for (int i = 1; i <= n; ++i) {
            const float t = (float)i / n, u = 1.0f - t;
            const float w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
            r.push_back(ImVec2(w0 * s.x + w1 * c1.x + w2 * c2.x + w3 * e.x,
                               w0 * s.y + w1 * c1.y + w2 * c2.y + w3 * e.y));
        }
    };
    auto arc = [&](ImVec2 c, float rad, float a0, float a1) {
        const int n = std::max(6, (int)std::ceil(std::fabs(a1 - a0) / 4.0f));
        for (int i = 1; i <= n; ++i) r.push_back(arcPt(c, rad, a0 + (a1 - a0) * i / n));
    };
    const ImVec2 f = arcPt(kLobeR, kLobeRad, -125);
    const ImVec2 e = arcPt(kGripR, kGripRad, 160);
    r.push_back(ImVec2(500, kBridgeY));
    cubic(ImVec2(640, kBridgeY), ImVec2(f.x - 16, f.y + 11), f);
    arc(kLobeR, kLobeRad, -125, 0);
    cubic(ImVec2(kLobeR.x + kLobeRad, 300), ImVec2(kGripR.x + kGripRad, 420), ImVec2(kGripR.x + kGripRad, kGripR.y));
    arc(kGripR, kGripRad, 0, 160);
    cubic(ImVec2(e.x - 0.342f * 60, e.y - 0.94f * 60), ImVec2(700, kUTop + 6), ImVec2(640, kUTop + 4));
    cubic(ImVec2(590, kUTop + 2), ImVec2(545, kUTop), ImVec2(500, kUTop));
    pts = r;
    for (int i = (int)r.size() - 2; i >= 1; --i) pts.push_back(ImVec2(kW - r[i].x, r[i].y));
    return pts;
}

// --- drawing context --------------------------------------------------------------

struct Frame {
    ImVec2 o;  // screen position of unit (0, 0)
    float s;   // px per unit
    ImVec2 P(float x, float y) const { return ImVec2(o.x + x * s, o.y + y * s); }
    ImVec2 P(ImVec2 u) const { return P(u.x, u.y); }
    float L(float units) const { return units * s; }
};

float g_alpha = 1.0f;  // the current element's opacity (Annotated 40%, Compact 28%)

ImU32 col(const ImVec4& c, float a = 1.0f) {
    ImVec4 v = c;
    v.w *= a * g_alpha;
    return ImGui::GetColorU32(v);
}
ImVec4 mixv(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

// A UI pixel: the editor's 15 px face is 100%.
float upx(float v) { return v * ImGui::GetFontSize() / 15.0f; }

// A linear gradient over the vertices [v0, end) of `dl`, keeping their alpha.
void shade(ImDrawList* dl, int v0, ImVec2 a, ImVec2 b, const ImVec4& c0, const ImVec4& c1) {
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, a, b, col(c0), col(c1));
}

// A mesh gradient: rings from `focus` (colour at t = 0) out to the ellipse
// (`c`, `r`) (t = 1); `clampFn` may move a point (unit space) back inside an
// allowed region. Used for the lobe highlights and the concave stick dishes.
template <class ColFn, class ClampFn>
void gradientDisc(ImDrawList* dl, const Frame& f, ImVec2 focus, ImVec2 c, ImVec2 r, ColFn colAt, ClampFn clampFn,
                  int rings = 6, int segs = 48) {
    const int vtx = 1 + rings * segs, idx = segs * 3 + (rings - 1) * segs * 6;
    dl->PrimReserve(idx, vtx);
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    dl->PrimWriteVtx(f.P(clampFn(focus)), uv, colAt(0.0f));
    for (int k = 1; k <= rings; ++k) {
        const float t = (float)k / rings;
        for (int i = 0; i < segs; ++i) {
            const float a = 2.0f * kPi * i / segs;
            const ImVec2 rim(c.x + r.x * std::cos(a), c.y + r.y * std::sin(a));
            const ImVec2 p(focus.x + (rim.x - focus.x) * t, focus.y + (rim.y - focus.y) * t);
            dl->PrimWriteVtx(f.P(clampFn(p)), uv, colAt(t));
        }
    }
    for (int i = 0; i < segs; ++i) {
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
        dl->PrimWriteIdx((ImDrawIdx)(base + 1 + (i + 1) % segs));
    }
    for (int k = 1; k < rings; ++k) {
        const int r0 = 1 + (k - 1) * segs, r1 = 1 + k * segs;
        for (int i = 0; i < segs; ++i) {
            const int j = (i + 1) % segs;
            dl->PrimWriteIdx((ImDrawIdx)(base + r0 + i));
            dl->PrimWriteIdx((ImDrawIdx)(base + r1 + i));
            dl->PrimWriteIdx((ImDrawIdx)(base + r1 + j));
            dl->PrimWriteIdx((ImDrawIdx)(base + r0 + i));
            dl->PrimWriteIdx((ImDrawIdx)(base + r1 + j));
            dl->PrimWriteIdx((ImDrawIdx)(base + r0 + j));
        }
    }
}

// A dashed closed polyline (screen space).
void dashedLoop(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 c, float thick, float dash, float gap) {
    if (pts.size() < 2) return;
    bool on = true;
    float left = dash;
    for (size_t i = 0; i < pts.size(); ++i) {
        ImVec2 a = pts[i];
        const ImVec2 b = pts[(i + 1) % pts.size()];
        float segLen = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        while (segLen > 0.0f) {
            const float step = std::min(left, segLen);
            const float t = step / segLen;
            const ImVec2 m(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
            if (on) dl->AddLine(a, m, c, thick);
            a = m;
            segLen -= step;
            left -= step;
            if (left <= 0.0f) {
                on = !on;
                left = on ? dash : gap;
            }
        }
    }
}
std::vector<ImVec2> circlePts(ImVec2 c, float r, int n = 64) {
    std::vector<ImVec2> v;
    for (int i = 0; i < n; ++i) {
        const float a = 2.0f * kPi * i / n;
        v.push_back(ImVec2(c.x + r * std::cos(a), c.y + r * std::sin(a)));
    }
    return v;
}
std::vector<ImVec2> roundRectPts(ImDrawList* dl, ImVec2 a, ImVec2 b, float r) {
    dl->PathRect(a, b, r);
    std::vector<ImVec2> v(dl->_Path.Data, dl->_Path.Data + dl->_Path.Size);
    dl->PathClear();
    return v;
}

// Text centred on `c` (x and y), with `spacing` px between letters.
void textC(ImDrawList* dl, ImFont* font, float size, ImVec2 c, ImU32 colour, const char* t, float spacing = 0.0f) {
    if (!font) font = ImGui::GetFont();
    float w = 0.0f;
    const int n = (int)std::strlen(t);
    if (spacing == 0.0f) {
        w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t).x;
        dl->AddText(font, size, ImVec2(IM_ROUND(c.x - w * 0.5f), IM_ROUND(c.y - size * 0.5f)), colour, t);
        return;
    }
    for (int i = 0; i < n; ++i) w += font->CalcTextSizeA(size, FLT_MAX, 0.0f, t + i, t + i + 1).x;
    w += spacing * (n - 1);
    float x = c.x - w * 0.5f;
    for (int i = 0; i < n; ++i) {
        dl->AddText(font, size, ImVec2(IM_ROUND(x), IM_ROUND(c.y - size * 0.5f)), colour, t + i, t + i + 1);
        x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, t + i, t + i + 1).x + spacing;
    }
}

// The face symbol (unit-space size: the generator's 100-unit-free form x scale).
void faceSymbol(ImDrawList* dl, int b, ImVec2 c, float k, ImU32 colour, float thick) {
    switch (b) {
        case Triangle:
            dl->AddTriangle(ImVec2(c.x, c.y - 11 * k), ImVec2(c.x + 11.5f * k, c.y + 8 * k),
                            ImVec2(c.x - 11.5f * k, c.y + 8 * k), colour, thick);
            break;
        case Circle: dl->AddCircle(c, 10.5f * k, colour, 32, thick); break;
        case Cross:
            dl->AddLine(ImVec2(c.x - 9.5f * k, c.y - 9.5f * k), ImVec2(c.x + 9.5f * k, c.y + 9.5f * k), colour, thick);
            dl->AddLine(ImVec2(c.x + 9.5f * k, c.y - 9.5f * k), ImVec2(c.x - 9.5f * k, c.y + 9.5f * k), colour, thick);
            break;
        default: dl->AddRect(ImVec2(c.x - 9 * k, c.y - 9 * k), ImVec2(c.x + 9 * k, c.y + 9 * k), colour, 0.0f, 0, thick);
    }
}
const ImVec4& faceCol(const theme::PadColors& pc, int b) {
    return b == Triangle ? pc.triangle : b == Circle ? pc.circle : b == Cross ? pc.cross : pc.square;
}

// Hit boxes (units).
Rect4 hitRect(int b) {
    if (b == L1 || b == L2 || b == R1 || b == R2) {
        Rect4 r = tabRect(b);
        r.y1 = tabVisBottom(b);
        return r;
    }
    if (b >= Up && b <= Right) {
        Rect4 r{1e9f, 1e9f, -1e9f, -1e9f};
        for (const ImVec2& p : kArm) {
            const ImVec2 q = rot(p, dpadDeg(b));
            r.x0 = std::min(r.x0, q.x);
            r.y0 = std::min(r.y0, q.y);
            r.x1 = std::max(r.x1, q.x);
            r.y1 = std::max(r.y1, q.y);
        }
        return r;
    }
    if (b <= Circle) {
        const ImVec2 c = faceC(b);
        return Rect4{c.x - kFaceR, c.y - kFaceR, c.x + kFaceR, c.y + kFaceR};
    }
    if (b == L3 || b == R3) {
        const ImVec2 c = b == L3 ? kStickL : kStickR;
        return Rect4{c.x - kWellR, c.y - kWellR, c.x + kWellR, c.y + kWellR};
    }
    if (b == Select) return Rect4{kSelX - kSelW / 2, kSelY - kSelH / 2, kSelX + kSelW / 2, kSelY + kSelH / 2};
    return Rect4{562, 219, 624, 249};  // Start
}

// Where a keycap sits (units), Interactive with the keyboard on.
bool keycapPos(int b, ImVec2* p) {
    switch (b) {
        case L2: *p = ImVec2(96, 14); return true;
        case L1: *p = ImVec2(82, 52); return true;
        case R2: *p = ImVec2(904, 14); return true;
        case R1: *p = ImVec2(918, 52); return true;
        case Triangle: *p = ImVec2(850, 116); return true;
        case Circle: *p = ImVec2(928, 170); return true;
        case Cross: *p = ImVec2(852, 330); return true;
        case Square: *p = ImVec2(676, 170); return true;
        case Select: *p = ImVec2(362, 234); return true;
        case Start: *p = ImVec2(646, 234); return true;
        default: return false;
    }
}

// Callout anchors (units) and their column: 0 left, 1 right, 2 top row.
struct Anchor {
    ImVec2 p;
    int side;
    float under;  // > 0: the under-route's minimum y (units)
    float recess; // the recess bottom the route keeps 7 px clear of
};
Anchor anchorOf(int b) {
    switch (b) {
        case L2: return {ImVec2(kL2Tab[0], 14), 0, 0, 0};
        case L1: return {ImVec2(kL1Tab[0], 46), 0, 0, 0};
        case R2: return {ImVec2(kW - kL2Tab[0], 14), 1, 0, 0};
        case R1: return {ImVec2(kW - kL1Tab[0], 46), 1, 0, 0};
        case Up: return {ImVec2(kDpadC.x - 28, kDpadC.y - 66), 0, 0, 0};
        case Left: return {ImVec2(kDpadC.x - 112, kDpadC.y), 0, 0, 0};
        case Down: return {ImVec2(kDpadC.x - 28, kDpadC.y + 66), 0, 0, 0};
        case Right: return {ImVec2(kDpadC.x + 60, kDpadC.y + 28), 0, kDpadC.y + 112 + 10, kDpadC.y + 112};
        case L3: return {ImVec2(kStickL.x - kWellR, kStickL.y), 0, 0, 0};
        case Triangle: return {ImVec2(faceC(Triangle).x + kFaceR, faceC(Triangle).y), 1, 0, 0};
        case Circle: return {ImVec2(faceC(Circle).x + kFaceR, faceC(Circle).y), 1, 0, 0};
        case Cross: return {ImVec2(faceC(Cross).x + kFaceR, faceC(Cross).y), 1, 0, 0};
        case Square: {
            const float rb = faceC(Cross).y + kFaceWell;
            return {ImVec2(faceC(Square).x, faceC(Square).y + kFaceR), 1, rb + 7, rb};
        }
        case R3: return {ImVec2(kStickR.x + kWellR, kStickR.y), 1, 0, 0};
        case Select: return {ImVec2(kSelX, kSelY - 15), 2, 0, 0};
        default: return {ImVec2(589, 219), 2, 0, 0};  // Start
    }
}

// Per-element flags for one draw.
struct El {
    bool pressed = false, latched = false, bound = false, sel = false, conflict = false;
    float hover = 0.0f;  // 0..1 ramp
};

struct Painter {
    ImDrawList* dl;
    Frame f;
    PadMode mode;
    bool compact, annotated, big;
    const theme::PadColors& pc;
    const theme::Semantics& sem;
    El el[16];
    int focus;
    ImVec4 bg;  // what the halos punch out to (the window)

    float opac(int b) const {
        if (compact) return (b == focus || el[b].pressed) ? 1.0f : 0.28f;
        if (annotated && !(el[b].bound || el[b].sel || el[b].conflict)) return 0.4f;
        return 1.0f;
    }
    void keyStyle(int b, ImVec4* fill, ImVec4* edge, float* ew) const {
        const El& e = el[b];
        *fill = mixv(pc.key, pc.keyHover, e.hover);
        *edge = mixv(pc.keyEdge, pc.glyph, e.hover);
        *ew = 1.0f;
        if (e.pressed || e.latched || (compact && b == focus)) {
            *edge = sem.accent;
            *ew = 1.6f;
        }
    }
    // Selected / conflict rings and the latch pin, around a circle...
    void ringsCircle(int b, ImVec2 cu, float ru) {
        const El& e = el[b];
        const ImVec2 c = f.P(cu);
        const float r = f.L(ru);
        if (e.sel) dl->AddCircle(c, r + upx(4), col(sem.accent), 64, upx(2.2f));
        if (e.conflict) dashedLoop(dl, circlePts(c, r + upx(4)), col(sem.warn), upx(2), upx(4), upx(3));
        if (e.latched) {
            const ImVec2 pin(c.x + r * 0.72f, c.y - r * 0.72f);
            const float pr = std::max(upx(3.5f), f.L(6));
            dl->AddCircleFilled(pin, pr, col(sem.warn), 16);
            dl->AddCircle(pin, pr, col(pc.body), 16, upx(1.5f));
        }
    }
    // ...or a rounded rect (units).
    void ringsRect(int b, Rect4 r, float rxu) {
        const El& e = el[b];
        const ImVec2 a = f.P(r.x0, r.y0), z = f.P(r.x1, r.y1);
        const float rx = f.L(rxu), off = upx(4);
        if (e.sel)
            dl->AddRect(ImVec2(a.x - off, a.y - off), ImVec2(z.x + off, z.y + off), col(sem.accent), rx + off, 0,
                        upx(2.2f));
        if (e.conflict)
            dashedLoop(dl, roundRectPts(dl, ImVec2(a.x - off, a.y - off), ImVec2(z.x + off, z.y + off), rx + off),
                       col(sem.warn), upx(2), upx(4), upx(3));
        if (e.latched) {
            const ImVec2 pin(z.x - upx(2), a.y + upx(2));
            const float pr = std::max(upx(3.5f), f.L(6));
            dl->AddCircleFilled(pin, pr, col(sem.warn), 16);
            dl->AddCircle(pin, pr, col(pc.body), 16, upx(1.5f));
        }
    }
    void halo(int b, ImVec2 cu, float ru) {
        if (el[b].pressed) dl->AddCircleFilled(f.P(cu), f.L(ru) + upx(6), col(sem.accent, 0.22f), 48);
    }
    // A recess: `fill` the shape (vertices recorded), then the vertical
    // wellLow -> well gradient down to 75% of its height.
    template <class F>
    void recess(float y0u, float y1u, float alpha, F&& fill) {
        const int v0 = dl->VtxBuffer.Size;
        fill(col(pc.well, alpha));
        shade(dl, v0, f.P(0, y0u), f.P(0, y0u + (y1u - y0u) * 0.75f), pc.wellLow, pc.well);
    }

    void tab(int b) {
        g_alpha = opac(b);
        const Rect4 r = tabRect(b);
        ImVec4 fill, edge;
        float ew;
        keyStyle(b, &fill, &edge, &ew);
        const ImVec2 a = f.P(r.x0, r.y0), z = f.P(r.x1, r.y1);
        const float rr = f.L(kTabR);
        dl->AddRectFilled(a, z, col(fill), rr);
        dl->AddRect(a, z, col(edge), rr, 0, upx(ew));
        if (!compact)
            dl->AddLine(ImVec2(f.P(r.x0 + kTabR, 0).x, a.y + upx(1.5f)), ImVec2(f.P(r.x1 - kTabR, 0).x, a.y + upx(1.5f)),
                        col(pc.bodyHi), upx(1));
        if (el[b].pressed) dl->AddRectFilled(a, z, col(sem.accent, 0.38f), rr);
        Rect4 vis = r;
        vis.y1 = tabVisBottom(b);
        ringsRect(b, vis, 14);
        g_alpha = 1.0f;
    }

    void body(bool capture) {
        const std::vector<ImVec2>& u = bodyOutline();
        std::vector<ImVec2> p(u.size());
        for (size_t i = 0; i < u.size(); ++i) p[i] = f.P(u[i]);
        if (capture) {
            // 1.2 s pulse between 0.35 and 1.
            const float t = (float)ImGui::GetTime();
            const float k = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * 2.0f * kPi / 1.2f));
            dl->AddPolyline(p.data(), (int)p.size(), col(sem.accent, 0.16f * k), ImDrawFlags_Closed, upx(9));
            dl->AddPolyline(p.data(), (int)p.size(), col(sem.accent, 0.5f * k), ImDrawFlags_Closed, upx(3));
        }
        const int v0 = dl->VtxBuffer.Size;
        dl->AddConcavePolyFilled(p.data(), (int)p.size(), col(pc.body));
        shade(dl, v0, f.P(0, kBridgeY + (kH - kBridgeY) * 0.45f), f.P(0, kH), pc.body, pc.bodyLow);
        // A soft highlight on each lobe's top, kept inside the silhouette.
        for (int side = 0; side < 2; ++side) {
            const ImVec2 lc = side ? kLobeR : kLobeL;
            const ImVec2 hc(lc.x, lc.y - 70);
            auto clampFn = [&](ImVec2 q) {
                const float dx = q.x - lc.x, dy = q.y - lc.y;
                const float d = std::sqrt(dx * dx + dy * dy);
                const bool inner = side ? q.x <= lc.x : q.x >= lc.x;  // towards the bridge
                if (d <= kLobeRad || q.y >= lc.y || (q.y >= kBridgeY && inner)) return q;
                return ImVec2(lc.x + dx / d * kLobeRad, lc.y + dy / d * kLobeRad);
            };
            const ImVec4 hi = pc.bodyHi;
            gradientDisc(dl, f, hc, hc, ImVec2(150, 105),
                         [&](float t) { return col(hi, 1.0f - t); }, clampFn);
        }
        dl->AddPolyline(p.data(), (int)p.size(), col(pc.bodyEdge), ImDrawFlags_Closed, upx(1.2f));
        if (!compact)
            for (int side = 0; side < 2; ++side) {
                const ImVec2 lc = side ? kLobeR : kLobeL;
                dl->AddCircleFilled(f.P(lc), f.L(kPlateR), col(pc.plate), 96);
                dl->AddCircle(f.P(lc), f.L(kPlateR), col(pc.wellLow, 0.8f), 96, upx(1.2f));
                dl->PathArcTo(f.P(lc), f.L(kPlateR) + upx(1.2f), -160 * kPi / 180, -20 * kPi / 180, 48);
                dl->PathStroke(col(pc.bodyHi), 0, upx(1));
            }
    }

    void dpad() {
        const float wo = compact ? 0.6f : 1.0f;
        const ImVec2 c = kDpadC;
        recess(c.y - kDipHalfW, c.y + kDipHalfW, wo, [&](ImU32 k) {
            dl->AddRectFilled(f.P(c.x - kDipHalfLen, c.y - kDipHalfW), f.P(c.x + kDipHalfLen, c.y + kDipHalfW), k,
                              f.L(kDipHalfW));
        });
        recess(c.y - kDipHalfLen, c.y + kDipHalfLen, wo, [&](ImU32 k) {
            dl->AddRectFilled(f.P(c.x - kDipHalfW, c.y - kDipHalfLen), f.P(c.x + kDipHalfW, c.y + kDipHalfLen), k,
                              f.L(kDipHalfW));
        });
        if (!compact)
            for (int b = Up; b <= Right; ++b) {
                ImVec2 t[3];
                for (int i = 0; i < 3; ++i) t[i] = f.P(rot(kEmboss[i], dpadDeg(b)));
                dl->AddPolyline(t, 3, col(pc.bodyEdge), ImDrawFlags_Closed, upx(1));
            }
        for (int b = Up; b <= Right; ++b) {
            g_alpha = opac(b);
            ImVec2 q[7];
            for (int i = 0; i < 7; ++i) q[i] = f.P(rot(kArm[i], dpadDeg(b)));
            ImVec4 fill, edge;
            float ew;
            keyStyle(b, &fill, &edge, &ew);
            dl->AddConvexPolyFilled(q, 7, col(fill));
            if (el[b].pressed) dl->AddConvexPolyFilled(q, 7, col(sem.accent, 0.35f));
            dl->AddPolyline(q, 7, col(edge), ImDrawFlags_Closed, upx(ew));
            if (!compact)
                dl->AddLine(f.P(rot(ImVec2(-21, -96), dpadDeg(b))), f.P(rot(ImVec2(21, -96), dpadDeg(b))),
                            col(pc.bodyHi), upx(1));
            ringsRect(b, hitRect(b), 10);
            g_alpha = 1.0f;
        }
    }

    void face() {
        const float wo = compact ? 0.6f : 1.0f;
        for (int b = Cross; b <= Circle; ++b) {
            const ImVec2 c = faceC(b);
            recess(c.y - kFaceWell, c.y + kFaceWell, wo,
                   [&](ImU32 k) { dl->AddCircleFilled(f.P(c), f.L(kFaceWell), k, 48); });
        }
        for (int b : {Triangle, Circle, Cross, Square}) {
            g_alpha = opac(b);
            const ImVec2 cu = faceC(b);
            const ImVec2 c = f.P(cu);
            ImVec4 fill, edge;
            float ew;
            keyStyle(b, &fill, &edge, &ew);
            halo(b, cu, kFaceR);
            dl->AddCircleFilled(c, f.L(kFaceR), col(fill), 48);
            dl->AddCircle(c, f.L(kFaceR), col(edge), 48, upx(ew));
            if (!compact) {
                dl->PathArcTo(c, f.L(kFaceR - 3), -160 * kPi / 180, -20 * kPi / 180, 24);
                dl->PathStroke(col(pc.bodyHi), 0, upx(1));
            }
            if (el[b].pressed) dl->AddCircleFilled(c, f.L(kFaceR), col(faceCol(pc, b), 0.30f), 48);
            const float thick = std::max(upx(1.25f), f.L(compact ? 6.0f : 3.4f));
            faceSymbol(dl, b, c, f.s * 1.35f, col(el[b].pressed ? sem.text : faceCol(pc, b)), thick);
            ringsCircle(b, cu, kFaceR);
            g_alpha = 1.0f;
        }
    }

    void centre(bool ledOn) {
        {
            g_alpha = opac(Select);
            ImVec4 fill, edge;
            float ew;
            keyStyle(Select, &fill, &edge, &ew);
            const ImVec2 a = f.P(kSelX - kSelW / 2, kSelY - kSelH / 2), z = f.P(kSelX + kSelW / 2, kSelY + kSelH / 2);
            dl->AddRectFilled(a, z, col(fill), f.L(10));
            if (el[Select].pressed) dl->AddRectFilled(a, z, col(sem.accent, 0.4f), f.L(10));
            dl->AddRect(a, z, col(edge), f.L(10), 0, upx(ew));
            ringsRect(Select, hitRect(Select), 10);
        }
        {
            g_alpha = opac(Start);
            ImVec4 fill, edge;
            float ew;
            keyStyle(Start, &fill, &edge, &ew);
            ImVec2 q[5];
            for (int i = 0; i < 5; ++i) q[i] = f.P(kStartPts[i]);
            dl->AddConvexPolyFilled(q, 5, col(fill));
            if (el[Start].pressed) dl->AddConvexPolyFilled(q, 5, col(sem.accent, 0.4f));
            dl->AddPolyline(q, 5, col(edge), ImDrawFlags_Closed, upx(ew));
            ringsRect(Start, hitRect(Start), 10);
        }
        // Analog button + LED: drawn, never an item.
        g_alpha = (compact || annotated) ? 0.28f : 1.0f;
        dl->AddRectFilled(f.P(kAnaX - kAnaW / 2, kAnaY - kAnaH / 2), f.P(kAnaX + kAnaW / 2, kAnaY + kAnaH / 2),
                          col(pc.key), f.L(5));
        dl->AddRect(f.P(kAnaX - kAnaW / 2, kAnaY - kAnaH / 2), f.P(kAnaX + kAnaW / 2, kAnaY + kAnaH / 2),
                    col(pc.keyEdge), f.L(5), 0, upx(1));
        if (ledOn && !compact)
            dl->AddRectFilled(f.P(kLedX - kLedW / 2 - 5, kLedY - kLedH / 2 - 4), f.P(kLedX + kLedW / 2 + 5, kLedY + kLedH / 2 + 4),
                              col(pc.led, 0.2f), f.L(kLedH / 2 + 4));
        dl->AddRectFilled(f.P(kLedX - kLedW / 2, kLedY - kLedH / 2), f.P(kLedX + kLedW / 2, kLedY + kLedH / 2),
                          col(ledOn ? pc.led : pc.ledOff), f.L(3));
        g_alpha = 1.0f;
        if (big) {
            ImFont* bold = boldFont() ? boldFont() : ImGui::GetFont();
            const float fs = std::max(f.L(13), upx(8));
            const ImU32 lc = col(pc.label);
            // SVG text sits on its baseline; centre the cap height there.
            textC(dl, bold, fs, f.P(kSelX, 270 - 4.5f), lc, "SELECT", f.L(1.5f));
            textC(dl, bold, fs, f.P(593, 270 - 4.5f), lc, "START", f.L(1.5f));
            textC(dl, bold, fs, f.P(500, 309 - 4.5f), lc, "ANALOG", f.L(1.5f));
        }
    }

    void stick(int side, ImVec2 v, float dz, bool gauge) {
        const int b = side ? R3 : L3;
        const ImVec2 cu = side ? kStickR : kStickL;
        g_alpha = opac(b);
        const float wo = compact ? 0.6f : 1.0f;
        const bool moved = v.x != 0.0f || v.y != 0.0f;
        const ImVec2 kc(cu.x + v.x * kTravel, cu.y + v.y * kTravel);
        ImVec4 fill, edge;
        float ew;
        keyStyle(b, &fill, &edge, &ew);
        const bool hot = moved || el[b].pressed;
        if (!compact) dl->AddCircleFilled(f.P(cu.x, cu.y + 6), f.L(kWellR + 2), col(pc.shadow), 64);
        dl->AddCircleFilled(f.P(cu), f.L(kWellR), col(pc.base, wo), 64);
        dl->AddCircle(f.P(cu), f.L(kWellR), col(pc.baseEdge, wo), 64, upx(1));
        halo(b, kc, kCapR);
        const float op = gauge ? 0.85f : 1.0f;
        const ImVec2 c = f.P(kc);
        dl->AddCircleFilled(c, f.L(kCapR), col(mixv(pc.cap, pc.keyHover, el[b].hover * 0.6f), op), 64);
        dl->AddCircle(c, f.L(kCapR), col(hot ? sem.accent : edge, op), 64, upx(hot ? 1.4f : ew));
        if (!compact) {
            // The ridged rim: short radial dashes around the cap.
            const float rm = f.L((kCapR + kDishR) / 2), hw = f.L(kCapR - kDishR - 4) * 0.5f;
            const float circ = 2.0f * kPi * (kCapR + kDishR) / 2;
            const int n = (int)(circ / 5.4f);
            const ImU32 rc = col(pc.capRing, 0.55f * op);
            for (int i = 0; i < n; ++i) {
                const float a0 = 2.0f * kPi * i / n, a1 = a0 + 2.0f * kPi * (2.2f / 5.4f) / n;
                const ImVec2 d0(std::cos(a0), std::sin(a0)), d1(std::cos(a1), std::sin(a1));
                dl->AddQuadFilled(ImVec2(c.x + d0.x * (rm - hw), c.y + d0.y * (rm - hw)),
                                  ImVec2(c.x + d0.x * (rm + hw), c.y + d0.y * (rm + hw)),
                                  ImVec2(c.x + d1.x * (rm + hw), c.y + d1.y * (rm + hw)),
                                  ImVec2(c.x + d1.x * (rm - hw), c.y + d1.y * (rm - hw)), rc);
            }
        }
        // The concave dish: darker centre a little above the middle.
        {
            const ImVec4 lo = pc.capLow, hi = pc.cap;
            gradientDisc(dl, f, ImVec2(kc.x, kc.y - kDishR * 0.16f), kc, ImVec2(kDishR, kDishR),
                         [&](float t) { return col(mixv(lo, hi, std::min(1.0f, t / 0.8f)), op); },
                         [](ImVec2 q) { return q; }, 5, 48);
            dl->AddCircle(c, f.L(kDishR), col(pc.capRing, op), 64, upx(1));
        }
        if (!compact) {
            dl->PathArcTo(c, f.L(kDishR - 3), 20 * kPi / 180, 160 * kPi / 180, 24);
            dl->PathStroke(col(pc.bodyHi), 0, upx(1));
        }
        if (el[b].pressed) {
            dl->AddCircleFilled(c, f.L(kDishR), col(sem.accent, 0.3f), 48);
            if (!compact) {
                ImFont* bold = boldFont() ? boldFont() : ImGui::GetFont();
                textC(dl, bold, std::max(f.L(18), upx(10)), c, col(sem.text), side ? "R3" : "L3");
            }
        }
        if (gauge) {
            const ImVec2 cc = f.P(cu);
            const float tr = f.L(kDotTravel);
            dl->AddLine(ImVec2(cc.x, cc.y - tr), ImVec2(cc.x, cc.y + tr), col(pc.capRing, 0.8f), upx(1));
            dl->AddLine(ImVec2(cc.x - tr, cc.y), ImVec2(cc.x + tr, cc.y), col(pc.capRing, 0.8f), upx(1));
            dashedLoop(dl, circlePts(cc, std::max(dz * tr, f.L(8)), 48), col(pc.glyph, 0.9f), upx(1), upx(2),
                       upx(2));
            const ImVec2 dot(cc.x + v.x * tr, cc.y + v.y * tr);
            if (moved) dl->AddLine(cc, dot, col(sem.accent), upx(2));
            const float dr = std::max(upx(4), f.L(7));
            dl->AddCircleFilled(dot, dr, col(moved ? sem.accent : pc.glyph), 24);
            dl->AddCircle(dot, dr, col(bg), 24, upx(1.5f));
        }
        ringsCircle(b, cu, kWellR);
        g_alpha = 1.0f;
    }

    void keycaps(const PadState& s) {
        ImFont* mono = monoFont() ? monoFont() : ImGui::GetFont();
        for (int b = 0; b < 16; ++b) {
            ImVec2 pu;
            if (!s.keycaps[b] || !keycapPos(b, &pu)) continue;
            const float fs = std::max(f.L(14), upx(8.5f));
            const float tw = mono->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.keycaps[b]).x;
            const float w = tw + fs * 0.9f, h = fs * 1.5f;
            const ImVec2 c = f.P(pu);
            dl->AddRectFilled(ImVec2(c.x - w / 2, c.y - fs * 0.75f), ImVec2(c.x + w / 2, c.y + fs * 0.75f), col(bg),
                              fs * 0.3f);
            dl->AddRect(ImVec2(c.x - w / 2, c.y - fs * 0.75f), ImVec2(c.x + w / 2, c.y + fs * 0.75f), col(pc.keyEdge),
                        fs * 0.3f, 0, upx(1));
            (void)h;
            textC(dl, mono, fs, c, col(pc.label), s.keycaps[b]);
        }
    }

    void shoulderLabels() {
        ImFont* bold = boldFont() ? boldFont() : ImGui::GetFont();
        for (int b : {L2, L1, R1, R2}) {
            const Rect4 r = tabRect(b);
            const float yy = (b == L2 || b == R2) ? 15 : 46;
            // 16-20 units, never under 9 px.
            const float fs = std::min(f.L(20), std::max(f.L(16), upx(9)));
            textC(dl, bold, fs, f.P((r.x0 + r.x1) / 2, yy + 1),
                  col(el[b].pressed ? sem.text : pc.glyph), kItemName[b]);
        }
    }

    void badge(int port) {
        const ImVec4& pcol = port == 1 ? pc.port1 : pc.port2;
        const float bw = std::max(f.L(64), upx(28)), bh = std::max(f.L(30), upx(16));
        const ImVec2 c = f.P(500, kBadgeY);
        const ImVec2 a(c.x - bw / 2, c.y - bh / 2), z(c.x + bw / 2, c.y + bh / 2);
        dl->AddRectFilled(a, z, col(pcol, 0.16f), bh / 2);
        dl->AddRect(a, z, col(pcol, 0.6f), bh / 2, 0, upx(1));
        ImFont* mono = monoFont() ? monoFont() : ImGui::GetFont();
        textC(dl, mono, bh * 0.62f, ImVec2(c.x, c.y + upx(0.5f)), col(pcol), port == 1 ? "P1" : "P2");
    }
};

float padUnitsH() { return kH + kTop; }

}  // namespace

const char* PadItemName(int button) { return (button >= 0 && button < 16) ? kItemName[button] : nullptr; }

float PadDiagramHeight(PadMode mode, float width) {
    if (mode != PadMode::Annotated) return width / kW * padUnitsH();
    // Mirrors PadDiagram's Annotated layout.
    const float gap = upx(16), colW = std::clamp((width - 2 * gap - upx(300)) * 0.5f, upx(84), upx(128));
    const float padW = std::clamp(width - 2 * (colW + gap), upx(160), upx(420));
    return upx(40) + padW / kW * padUnitsH() + upx(4);
}

bool PadDiagram(const char* id, PadMode mode, float width, const PadState& s, PadDiagramOut* outp,
                const PadCallout* callouts, int calloutCount, int focus) {
    PadDiagramOut dummy;
    PadDiagramOut& out = outp ? *outp : dummy;
    out = PadDiagramOut();
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool annotated = mode == PadMode::Annotated, compact = mode == PadMode::Compact;

    // Layout: Annotated puts the pad between two callout columns, under a
    // 40 px top row; the other modes are just the pad.
    float padW = width, colW = 0, gap = 0, topH = 0, totalW = width;
    if (annotated) {
        gap = upx(16);
        colW = std::clamp((width - 2 * gap - upx(300)) * 0.5f, upx(84), upx(128));
        padW = std::clamp(width - 2 * (colW + gap), upx(160), upx(420));
        topH = upx(40);
    }
    const float sc = padW / kW;
    const float padH = padUnitsH() * sc;
    const float totalH = annotated ? topH + padH + upx(4) : padH;
    // Annotated: centre the composition when the pad hit its 420 px cap.
    const float compW = annotated ? padW + 2 * (colW + gap) : padW;
    const float x0 = origin.x + std::max(0.0f, (totalW - compW) * 0.5f);
    const ImVec2 padMin(x0 + (annotated ? colW + gap : 0.0f), origin.y + topH);
    Frame f{ImVec2(padMin.x, padMin.y + kTop * sc), sc};
    out.rectMin = padMin;
    out.rectMax = ImVec2(padMin.x + padW, padMin.y + padH);

    const theme::PadColors& pc = theme::padColors();
    const theme::Semantics& sem = theme::semantics();
    Painter pt{win->DrawList, f, mode, compact, annotated, mode == PadMode::Interactive && padW >= upx(420),
               pc, sem, {}, focus, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)};
    if (s.backdrop.w > 0.0f) pt.bg = s.backdrop;
    pt.bg.w = 1.0f;
    for (int b = 0; b < 16; ++b) {
        const unsigned m = 1u << b;
        El& e = pt.el[b];
        e.pressed = ((s.pressed | s.latched) & m) != 0;
        e.latched = (s.latched & m) != 0;
        e.bound = (s.bound & m) != 0;
        e.sel = (s.selected & m) != 0;
        e.conflict = (s.conflict & m) != 0;
    }
    for (int i = 0; i < calloutCount; ++i) {
        const int b = callouts[i].button;
        if (b < 0 || b > 15) continue;
        pt.el[b].bound = true;
        if (callouts[i].kind == PadCallout::Selected) pt.el[b].sel = true;
        if (callouts[i].kind == PadCallout::Conflict) pt.el[b].conflict = true;
    }

    // --- items first (hover must be known before drawing), no PushID ------------
    const bool items = mode != PadMode::Compact || s.picker;
    if (items && !win->SkipItems) {
        const ImGuiID storeId = ImGui::GetID(id);
        for (int b : kOrder) {
            const Rect4 r = hitRect(b);
            const ImVec2 a = f.P(r.x0, r.y0), z = f.P(r.x1, r.y1);
            ImGui::SetCursorScreenPos(a);
            const bool released = ImGui::InvisibleButton(kItemName[b], ImVec2(std::max(1.0f, z.x - a.x),
                                                                              std::max(1.0f, z.y - a.y)));
            // Checked = pressed (Interactive: what the game is sent); in
            // Annotated mode = the selected action's button.
            uiscript::markLastItemChecked(annotated ? pt.el[b].sel : pt.el[b].pressed);
            const bool hov = ImGui::IsItemHovered();
            if (hov) out.hovered = b;
            pt.el[b].hover = theme::hoverAnim(ImGui::GetItemID(), hov && !s.disabled, 12.0f);
            const bool isStick = b == L3 || b == R3;
            if (isStick && mode == PadMode::Interactive) {
                const int side = b == R3 ? 1 : 0;
                ImGuiStorage* st = ImGui::GetStateStorage();
                const ImGuiID key = ImHashData(&side, sizeof(side), storeId);
                bool dragged = st->GetBool(key, false);
                if (ImGui::IsItemActive()) {
                    const ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
                    if (std::fabs(d.x) + std::fabs(d.y) > 3.0f) dragged = true;
                    if (dragged) {
                        const ImVec2 c = f.P(side ? kStickR : kStickL);
                        const ImVec2 mp = ImGui::GetMousePos();
                        float x = (mp.x - c.x) / f.L(kDotTravel), y = (mp.y - c.y) / f.L(kDotTravel);
                        const float len = std::sqrt(x * x + y * y);
                        if (len > 1.0f) {
                            x /= len;
                            y /= len;
                        }
                        out.stickActive = side;
                        out.stickValue[side] = ImVec2(x, y);
                    }
                }
                if (ImGui::IsItemDeactivated()) {
                    if (dragged) out.stickReleased[side] = true;
                    else out.stickClicked[side] = true;
                    dragged = false;
                }
                if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) out.stickDoubleClicked[side] = true;
                st->SetBool(key, dragged);
            } else {
                if (released) {
                    out.clicked = b;
                    out.shiftClicked = ImGui::GetIO().KeyShift;
                }
                if (ImGui::IsItemActive()) out.held = b;
            }
        }
    }
    if (compact && out.hovered >= 0) pt.focus = out.hovered;  // picker: preview the hovered button

    // --- draw ---------------------------------------------------------------------
    ImDrawList* dl = win->DrawList;
    for (int b : {L2, R2, L1, R1}) pt.tab(b);
    pt.body(s.capture);
    pt.dpad();
    pt.face();
    pt.centre(s.led && !s.disabled);
    const bool gauge = mode == PadMode::Interactive || s.gauge;
    for (int side = 0; side < 2; ++side) pt.stick(side, s.stick[side], s.deadzone[side], gauge);
    if (mode == PadMode::Interactive && padW >= upx(320)) pt.keycaps(s);
    if (pt.big) pt.shoulderLabels();
    if (s.port == 1 || s.port == 2) {
        if (!compact || padW >= upx(64)) pt.badge(s.port);
    }
    // Disabled = the whole drawing at 45%. Per-primitive alpha would let the
    // shoulder tabs show through the body, so the drawing is drawn opaque and
    // then veiled with what it sits on (an SVG group's opacity, in effect).
    if (s.disabled) dl->AddRectFilled(out.rectMin, out.rectMax, col(pt.bg, 0.55f));

    // --- Annotated: callouts in fixed slots ----------------------------------------
    if (annotated && calloutCount > 0) {
        ImFont* font = ImGui::GetFont();
        const float cfs = IM_TRUNC(ImGui::GetFontSize() * 0.8f);
        const float ph = std::max(upx(24), cfs + upx(10)), pitch = ph + upx(5);
        struct Slot {
            const PadCallout* c;
            ImVec2 a;    // anchor, px
            float want;  // slot y (px)
            float under; // under-route y (px), 0 = straight
        };
        std::vector<Slot> col3[3];
        for (int i = 0; i < calloutCount; ++i) {
            const PadCallout& c = callouts[i];
            if (c.button < 0 || c.button > 15 || !c.text || !*c.text) continue;
            const Anchor an = anchorOf(c.button);
            Slot sl{&c, f.P(an.p), f.P(an.p).y, 0.0f};
            if (an.under > 0) {
                sl.under = std::max(f.P(0, an.under).y, f.P(0, an.recess).y + upx(7));
                sl.want = sl.under;
            }
            col3[an.side].push_back(sl);
        }
        const ImVec4 bgc = pt.bg;
        auto kindCol = [&](const PadCallout& c) {
            return c.kind == PadCallout::Selected ? sem.accent : c.kind == PadCallout::Conflict ? sem.warn : pc.lead;
        };
        auto leader = [&](const std::vector<ImVec2>& path, const PadCallout& c, ImVec2 anchor) {
            const bool strong = c.kind == PadCallout::Selected || c.kind == PadCallout::Conflict;
            const float wd = upx(strong ? 1.6f : 1.0f);
            dl->AddPolyline(path.data(), (int)path.size(), col(bgc, 0.55f), 0, wd + upx(3));
            dl->AddPolyline(path.data(), (int)path.size(), col(kindCol(c)), 0, wd);
            dl->AddCircleFilled(anchor, upx(2.6f), col(strong ? kindCol(c) : pc.glyph), 12);
        };
        auto pill = [&](float x, float y, float w, const PadCallout& c) {
            const ImVec2 a(x, y - ph / 2), z(x + w, y + ph / 2);
            const bool sel = c.kind == PadCallout::Selected, warn = c.kind == PadCallout::Conflict;
            dl->AddRectFilled(a, z, col(pc.callBg), upx(6));
            if (sel) dl->AddRectFilled(a, z, col(sem.accent, 0.16f), upx(6));
            dl->AddRect(a, z, col(sel ? sem.accent : warn ? sem.warn : pc.callEdge), upx(6), 0,
                        upx(sel || warn ? 1.4f : 1.0f));
            ImVec4 tc = sel || c.kind == PadCallout::Normal ? sem.text : warn ? sem.warn : sem.textDim;
            if (c.kind == PadCallout::Normal && c.button <= Circle) tc = faceCol(pc, c.button);
            // Ellipsize to the pill; the full text is the tooltip.
            const float room = w - upx(18) + 0.5f;
            std::string t = c.text;
            bool cut = false;
            while (t.size() > 1 && font->CalcTextSizeA(cfs, FLT_MAX, 0.0f, t.c_str()).x > room) {
                t.pop_back();
                while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();
                cut = true;
            }
            if (cut) t += "\xe2\x80\xa6";
            ImFont* tf = font;  // one face: the width was measured in it
            dl->AddText(tf, cfs, ImVec2(IM_ROUND(a.x + upx(9)), IM_ROUND(y - cfs * 0.5f)), col(tc), t.c_str());
            if (cut && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, z)) ImGui::SetTooltip("%s", c.text);
        };
        auto textW = [&](const char* t) { return font->CalcTextSizeA(cfs, FLT_MAX, 0.0f, t).x + upx(18); };
        const float limit = origin.y + totalH - ph / 2;
        for (int side = 0; side < 2; ++side) {
            std::vector<Slot>& v = col3[side];
            std::sort(v.begin(), v.end(), [](const Slot& a, const Slot& b) { return a.want < b.want; });
            std::vector<float> ys;
            for (const Slot& sl : v) ys.push_back(sl.want);
            for (size_t i = 1; i < ys.size(); ++i) ys[i] = std::max(ys[i], ys[i - 1] + pitch);
            for (int i = (int)ys.size() - 1; i >= 0; --i) {
                ys[i] = std::min(ys[i], limit);
                if (i < (int)ys.size() - 1) ys[i] = std::min(ys[i], ys[i + 1] - pitch);
            }
            for (size_t i = 0; i < v.size(); ++i) {
                const Slot& sl = v[i];
                const float y = ys[i];
                const float w = std::min(colW, textW(sl.c->text));
                const float xe = side == 0 ? x0 + colW : padMin.x + padW + gap;
                const float px0 = side == 0 ? xe - w : xe;
                const float dir = side == 0 ? 1.0f : -1.0f;
                const float kx = xe + upx(8) * dir;
                std::vector<ImVec2> path;
                path.push_back(ImVec2(xe, y));
                path.push_back(ImVec2(kx, y));
                if (sl.under > 0) {
                    path.push_back(ImVec2(kx + upx(10) * dir, sl.under));
                    path.push_back(ImVec2(sl.a.x, sl.under));
                    path.push_back(sl.a);
                } else {
                    path.push_back(ImVec2(sl.a.x - upx(10) * dir, sl.a.y));
                    path.push_back(sl.a);
                }
                leader(path, *sl.c, sl.a);
                pill(px0, y, w, *sl.c);
            }
        }
        for (const Slot& sl : col3[2]) {
            const bool isSel = sl.c->button == Select;
            const float w = std::min(padW * 0.42f, textW(sl.c->text));
            const float cx = padMin.x + padW / 2;
            const float y = origin.y + upx(4) + ph / 2;
            const float px0 = isSel ? cx - upx(8) - w : cx + upx(8);
            const float lx = isSel ? px0 + w - upx(14) : px0 + upx(14);
            std::vector<ImVec2> path = {ImVec2(lx, y + ph / 2), ImVec2(lx, y + ph / 2 + upx(8)),
                                        ImVec2(sl.a.x, sl.a.y - upx(10)), sl.a};
            leader(path, *sl.c, sl.a);
            pill(px0, y, w, *sl.c);
        }
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(totalW, totalH));
    return out.clicked >= 0 || out.stickClicked[0] || out.stickClicked[1];
}

// --- glyph -------------------------------------------------------------------------

float PadButtonGlyphAt(ImDrawList* dl, ImVec2 p, int button, float h, bool selected) {
    if (button < 0 || button > 15) return 0.0f;
    const theme::PadColors& pc = theme::padColors();
    const theme::Semantics& sem = theme::semantics();
    const float k = h / 100.0f;
    const ImU32 edge = col(selected ? sem.accent : pc.keyEdge);
    const ImVec2 c(p.x + h * 0.5f, p.y + h * 0.5f);
    if (button <= Circle) {
        if (dl) {
            dl->AddCircleFilled(c, 46 * k, col(pc.key), 24);
            dl->AddCircle(c, 46 * k, edge, 24, std::max(1.0f, 5 * k));
            const float t = std::max(1.0f, 9 * k);
            const ImU32 fc = col(faceCol(pc, button));
            switch (button) {
                case Triangle:
                    dl->AddTriangle(ImVec2(c.x, c.y - 20 * k), ImVec2(c.x + 21 * k, c.y + 15 * k),
                                    ImVec2(c.x - 21 * k, c.y + 15 * k), fc, t);
                    break;
                case Circle: dl->AddCircle(c, 19 * k, fc, 20, t); break;
                case Cross:
                    dl->AddLine(ImVec2(c.x - 17 * k, c.y - 17 * k), ImVec2(c.x + 17 * k, c.y + 17 * k), fc, t);
                    dl->AddLine(ImVec2(c.x + 17 * k, c.y - 17 * k), ImVec2(c.x - 17 * k, c.y + 17 * k), fc, t);
                    break;
                default: dl->AddRect(ImVec2(c.x - 16 * k, c.y - 16 * k), ImVec2(c.x + 16 * k, c.y + 16 * k), fc, 0, 0, t);
            }
        }
        return h;
    }
    if (button >= Up && button <= Right) {
        if (dl) {
            for (int b = Up; b <= Right; ++b) {
                // The arm rect (38,6)-(62,40) of a 100 box, rotated about its centre.
                const ImVec2 q0 = rot(ImVec2(-12, -44), dpadDeg(b), ImVec2(0, 0));
                const ImVec2 q1 = rot(ImVec2(12, -10), dpadDeg(b), ImVec2(0, 0));
                const ImVec2 a(c.x + std::min(q0.x, q1.x) * k, c.y + std::min(q0.y, q1.y) * k);
                const ImVec2 z(c.x + std::max(q0.x, q1.x) * k, c.y + std::max(q0.y, q1.y) * k);
                dl->AddRectFilled(a, z, col(b == button ? sem.accent : pc.keyEdge), 5 * k);
            }
            dl->AddRectFilled(ImVec2(c.x - 12 * k, c.y - 12 * k), ImVec2(c.x + 12 * k, c.y + 12 * k), col(pc.keyEdge));
        }
        return h;
    }
    ImFont* bold = boldFont() ? boldFont() : ImGui::GetFont();
    if (button == L3 || button == R3) {
        if (dl) {
            dl->AddCircleFilled(c, 46 * k, col(pc.cap), 24);
            dl->AddCircle(c, 46 * k, edge, 24, std::max(1.0f, 5 * k));
            dl->AddCircle(c, 30 * k, col(pc.capRing), 24, std::max(1.0f, 5 * k));
            textC(dl, bold, 34 * k, c, col(pc.glyph), kItemName[button]);
        }
        return h;
    }
    const char* t = button == Select ? "SEL" : button == Start ? "START" : kItemName[button];
    const float fs = h * 0.5f;
    const float w = std::max(h, bold->CalcTextSizeA(fs, FLT_MAX, 0.0f, t).x + h * 0.56f);
    if (dl) {
        const ImVec2 z(p.x + w, p.y + h);
        dl->AddRectFilled(p, z, col(pc.key), h * 0.26f);
        dl->AddRect(p, z, edge, h * 0.26f, 0, 1.0f);
        textC(dl, bold, fs, ImVec2(p.x + w * 0.5f, p.y + h * 0.5f), col(pc.glyph), t);
    }
    return w;
}

float PadButtonGlyph(int button, float height, bool selected) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = PadButtonGlyphAt(ImGui::GetWindowDrawList(), p, button, height, selected);
    ImGui::Dummy(ImVec2(w, height));
    return w;
}

// --- picker combo --------------------------------------------------------------------

bool PadButtonCombo(const char* label, std::string* name, bool allowNone, const char* tip, const char* noneLabel,
                    float popupFontSize) {
    if (!noneLabel) noneLabel = "<none>";
    const int cur = padButtonIndex(*name);
    bool changed = false;
    const bool open = ui::BeginCombo(label, "", (ImGuiComboFlags)ImGuiComboFlags_CustomPreview | ImGuiComboFlags_HeightLargest);
    if (open) {
        if (popupFontSize > 0.0f) ImGui::PushFont(nullptr, popupFontSize);
        // The picker: a Compact pad whose elements are clickable.
        PadState ps;
        ps.picker = true;
        if (cur >= 0) ps.selected = 1u << cur;
        PadDiagramOut o;
        const float pw = upx(240);
        PadDiagram("##padpick", PadMode::Compact, pw, ps, &o, nullptr, 0, cur);
        if (o.clicked >= 0) {
            *name = kPadButtonNames[o.clicked];
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        {
            const int show = o.hovered >= 0 ? o.hovered : cur;
            const char* t = show >= 0 ? kPadButtonNames[show] : (allowNone ? noneLabel : "");
            ImGui::TextDisabled("%s", t);
        }
        ImGui::Separator();
        // The 16 names in two columns, never scrolled: every row is on screen,
        // so a script's `click DpadUp` lands where the dump says it is.
        const float rowH = ImGui::GetTextLineHeight() + upx(6);
        const float colW = std::max(pw, ImGui::GetContentRegionAvail().x) * 0.5f - upx(2);
        if (allowNone) {
            if (ImGui::Selectable(noneLabel, name->empty(), 0, ImVec2(0, rowH))) {
                changed = !name->empty();
                name->clear();
            }
        }
        for (int i = 0; i < 16; ++i) {
            // Column-major: Cross..DpadRight on the left, L1..Select on the right.
            const int row = i / 2;
            const int bi = (i % 2 == 0) ? row : 8 + row;
            if (i % 2) ImGui::SameLine(0.0f, upx(4));
            ImGui::PushID(bi);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool sel = cur == bi;
            if (ImGui::Selectable("##padrow", sel, 0, ImVec2(colW, rowH))) {
                if (*name != kPadButtonNames[bi]) changed = true;
                *name = kPadButtonNames[bi];
            }
            uiscript::nameItem(ImGui::GetItemID(), kPadButtonNames[bi]);
            const float gh = ImGui::GetTextLineHeight();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float gw = PadButtonGlyphAt(dl, ImVec2(p.x + upx(4), p.y + (rowH - gh) * 0.5f), bi, gh, sel);
            dl->AddText(ImVec2(p.x + upx(4) + std::max(gw, gh * 1.6f) + upx(8), p.y + (rowH - gh) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text), kPadButtonNames[bi]);
            ImGui::PopID();
        }
        if (popupFontSize > 0.0f) ImGui::PopFont();
        ImGui::EndCombo();
    }
    if (ImGui::BeginComboPreview()) {
        const float gh = ImGui::GetTextLineHeight();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (cur >= 0) {
            const float gw = PadButtonGlyphAt(ImGui::GetWindowDrawList(), p, cur, gh);
            ImGui::SetCursorScreenPos(ImVec2(p.x + gw + upx(6), p.y));
            ImGui::TextUnformatted(kPadButtonNames[cur]);
        } else {
            ImGui::TextDisabled("%s", name->empty() ? noneLabel : name->c_str());
        }
        ImGui::EndComboPreview();
    }
    if (tip && !open && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tip);
    return changed;
}

// --- port segmented ----------------------------------------------------------------------

bool PadPortSegmented(const char* id, int* port, float width, bool enabled) {
    const theme::Semantics& sem = theme::semantics();
    const theme::PadColors& pc = theme::padColors();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float h = ImGui::GetFrameHeight(), pad = 3.0f, r = st.FrameRounding;
    const char* labels[2] = {"Pad 1", "Pad 2"};
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const ImVec2 base = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(base, ImVec2(base.x + width, base.y + h), col(sem.field), r);
    dl->AddRect(base, ImVec2(base.x + width, base.y + h), col(sem.border), r);
    bool changed = false;
    const float sw = (width - pad * 2) * 0.5f;
    for (int i = 0; i < 2; ++i) {
        const float x = base.x + pad + sw * i;
        ImGui::SetCursorScreenPos(ImVec2(x, base.y + pad));
        if (ImGui::InvisibleButton(labels[i], ImVec2(sw, h - pad * 2)) && *port != i) {
            *port = i;
            changed = true;
        }
        const bool on = *port == i;
        uiscript::markLastItemChecked(on);
        const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered() && !on, 12.0f);
        if (on || t > 0.0f)
            dl->AddRectFilled(ImVec2(x, base.y + pad), ImVec2(x + sw, base.y + h - pad),
                              col(sem.surface, on ? 1.0f : 0.5f * t), r - pad * 0.5f);
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        const float dot = upx(7), gapX = upx(6);
        const float cx = x + (sw - ts.x - dot - gapX) * 0.5f;
        dl->AddCircleFilled(ImVec2(cx + dot * 0.5f, base.y + h * 0.5f), dot * 0.5f,
                            col(i == 0 ? pc.port1 : pc.port2, on ? 1.0f : 0.6f), 12);
        dl->AddText(ImVec2(cx + dot + gapX, base.y + (h - ts.y) * 0.5f), on ? col(sem.text) : col(sem.textDim),
                    labels[i]);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(base);
    ImGui::Dummy(ImVec2(width, h));
    return changed;
}

}  // namespace ui
