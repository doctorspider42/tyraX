#include "menustyle.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace menustyle {

namespace {

// The Classic palette - the literals the pre-stylesheet bake carried in
// menubake.cpp. They live here now, which is what makes "no sheet" and "the
// classic sheet" the same thing rather than two code paths.
constexpr Color kClassicBg{10, 14, 28, 225};
constexpr Color kClassicText{235, 240, 245, 255};
constexpr Color kClassicDim{150, 160, 175, 255};
constexpr Color kClassicRule{70, 90, 120, 255};

std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

}  // namespace

// --- the property table ------------------------------------------------------

const std::vector<PropSpec>& propSpecs() {
    static const std::vector<PropSpec> v = {
        {Prop::Width, "width", Kind::Length, nullptr,
         "Box width in pixels. `auto` follows the panel; on `panel` itself the\n"
         "value must be 128, 256 or 512 (a PS2 texture axis), or `screen` for a\n"
         "full-width panel built from crisp tiles."},
        {Prop::Height, "height", Kind::Length, nullptr,
         "Box height in pixels. `auto` = the text size plus the element's own\n"
         "padding, which is what makes a row pitch follow its font size."},
        {Prop::Padding, "padding", Kind::Edge, nullptr,
         "Space INSIDE the box, before its text: 1-4 lengths (all / vertical\n"
         "horizontal / top right bottom left)."},
        {Prop::Margin, "margin", Kind::Edge, nullptr,
         "Space OUTSIDE the box. On `title` the left/right margin is also the\n"
         "inset of its separator rule."},
        {Prop::PaddingTop, "padding-top", Kind::Length, nullptr,
         "One side of `padding`."},
        {Prop::PaddingRight, "padding-right", Kind::Length, nullptr,
         "One side of `padding`."},
        {Prop::PaddingBottom, "padding-bottom", Kind::Length, nullptr,
         "One side of `padding`."},
        {Prop::PaddingLeft, "padding-left", Kind::Length, nullptr,
         "One side of `padding` - on `row` this is the label's left inset."},
        {Prop::MarginTop, "margin-top", Kind::Length, nullptr,
         "One side of `margin`."},
        {Prop::MarginRight, "margin-right", Kind::Length, nullptr,
         "One side of `margin` - on `value` this is its right inset."},
        {Prop::MarginBottom, "margin-bottom", Kind::Length, nullptr,
         "One side of `margin`."},
        {Prop::MarginLeft, "margin-left", Kind::Length, nullptr,
         "One side of `margin`."},
        {Prop::Background, "background", Kind::Fill, nullptr,
         "A colour, or linear-gradient(<angle>deg, <from>, <to>). Baked, so a\n"
         "gradient costs nothing at runtime."},
        {Prop::BackgroundImage, "background-image", Kind::Url, nullptr,
         "url(res/hud/x.png), optionally `9-slice <n>px` so the corners keep\n"
         "their shape while the middle stretches. Composited into the bake."},
        {Prop::BackgroundAnim_, "background-anim", Kind::Url, nullptr,
         "A MOVING background layer, drawn as its own sprite under the panel -\n"
         "which is how you animate what a gradient cannot be: baked pixels never\n"
         "move, a texture's offset does.\n"
         "  url(res/hud/bg.png) scroll 12px/s 4px/s   - tiles and slides\n"
         "  url(res/hud/flame.png) frames 8 1.2s      - a vertical frame strip\n"
         "Costs one texture and one sprite, and nothing per frame."},
        {Prop::Slice, "slice", Kind::Length, nullptr,
         "9-slice inset for background-image when written separately."},
        {Prop::BorderWidth, "border", Kind::Border, nullptr,
         "<width> solid <colour> - e.g. `2px solid #78d1ff`."},
        {Prop::BorderColor, "border-color", Kind::Color, nullptr,
         "Border colour on its own (the menu's accent by default)."},
        {Prop::Radius, "border-radius", Kind::Length, nullptr,
         "Corner radius in pixels. Baked, and it rounds the border with the\n"
         "background."},
        {Prop::Shadow, "shadow", Kind::Shadow, nullptr,
         "<x> <y> <blur> <colour> - a soft drop shadow baked under the box.\n"
         "Costs texture margin, not frames."},
        {Prop::Opacity, "opacity", Kind::Length, nullptr,
         "0..1, multiplied into everything the box draws."},
        {Prop::Font, "font", Kind::Str, nullptr,
         "A Tools > Font Manager entry name. Unset = the menu's own font."},
        {Prop::FontSize, "font-size", Kind::Length, nullptr,
         "Text height in pixels. On `title`/`row` unset means the menu's own\n"
         "Title size / Row size."},
        {Prop::TextColor, "color", Kind::Color, nullptr, "Text colour."},
        {Prop::Align, "align", Kind::Enum, "left|center|right",
         "Horizontal alignment of the element's text inside its box."},
        {Prop::LetterSpacing, "letter-spacing", Kind::Length, nullptr,
         "Extra pixels between glyphs. The cheapest way to make a title read\n"
         "as a title."},
        {Prop::TextTransform, "text-transform", Kind::Enum, "none|uppercase",
         "`uppercase` bakes the text upper-cased, leaving the label as typed."},
        {Prop::TextShadow, "text-shadow", Kind::Shadow, nullptr,
         "<x> <y> <blur> <colour> behind the glyphs. Icons are skipped (they\n"
         "carry their own colours)."},
        {Prop::TextOutline, "text-outline", Kind::Border, nullptr,
         "<width> solid <colour> around the glyphs - what keeps text readable\n"
         "over busy art."},
        {Prop::RuleBelow, "rule-below", Kind::Border, nullptr,
         "<width> solid <colour>: a separator line under the element, inset by\n"
         "its left/right margin."},
        {Prop::Content, "content", Kind::Str, nullptr,
         "Literal text for an element the menu gives none - the button hint\n"
         "line. Understands {{icons}}."},
        {Prop::TranslateX, "translate-x", Kind::Length, nullptr,
         "Horizontal offset. On row:selected this is the classic \"the\n"
         "highlighted row steps out\" move."},
        {Prop::IconSize, "icon-size", Kind::Length, nullptr,
         "Box for a row's own icon, left of its label. 0 = no icon column."},
        {Prop::Marker, "marker", Kind::Url, "left|right",
         "url(...) for the selection caret, plus `left` or `right`. Unset uses\n"
         "the built-in cursor sprite; `none` draws no caret at all, which is what\n"
         "a style whose selected row paints a full-width plate wants (the caret\n"
         "would sit on top of it)."},
        {Prop::MarkerSide, "marker-side", Kind::Enum, "left|right",
         "Which side of the row the caret sits on."},
        {Prop::Selectable, "selectable", Kind::Bool, nullptr,
         "`no` makes rows of this class headers/spacers the cursor skips."},
        {Prop::RowsVisible, "rows-visible", Kind::Length, nullptr,
         "On `list`: how many rows are on screen at once. More entries than\n"
         "that and the list scrolls (one sprite, an offset - no extra cost)."},
        {Prop::ScrollMarker, "scroll-marker", Kind::Url, nullptr,
         "url(...) drawn at the top/bottom of a scrolling list to say there is\n"
         "more."},
        {Prop::Display, "display", Kind::Enum, "text|bar",
         "On `value`: `bar` draws a slider for numeric option rows instead of\n"
         "the option label."},
        {Prop::BarSize, "bar-size", Kind::Edge, nullptr,
         "<width> <height> of a value bar."},
        {Prop::BarFill, "bar-fill", Kind::Color, nullptr, "Filled part of a bar."},
        {Prop::BarTrack, "bar-track", Kind::Color, nullptr, "Empty part of a bar."},
        {Prop::Quant, "quant", Kind::Enum, "default|4bit|8bit|32bit",
         "Colour depth the panel ships at. 4bit is ~1/8 the VRAM and menu art\n"
         "palettizes almost losslessly - it is what makes a full-screen menu\n"
         "affordable (docs/gs-vram.md)."},
        {Prop::Gap, "gap", Kind::Length, nullptr,
         "Space between stacked children (image blocks, rows)."},
        {Prop::Wrap, "wrap", Kind::Bool, nullptr,
         "On `description`: wrap the text to the box width."},
        {Prop::Area, "area", Kind::Enum, "below|right",
         "On `description`: where the selected row's description sits."},
    };
    return v;
}

const PropSpec* propSpec(Prop p) {
    for (const PropSpec& s : propSpecs())
        if (s.prop == p) return &s;
    return nullptr;
}

const char* elemName(Elem e) {
    switch (e) {
        case Elem::Panel: return "panel";
        case Elem::Title: return "title";
        case Elem::List: return "list";
        case Elem::Row: return "row";
        case Elem::Value: return "value";
        case Elem::Description: return "description";
        case Elem::Hint: return "hint";
        case Elem::Marker: return "marker";
        case Elem::Image: return "image";
        case Elem::Count: break;
    }
    return "?";
}

namespace {
struct MotionInfo {
    const char* key;
    const char* label;
    const char* help;
};
// Index-aligned with Transition::Which. A static_assert keeps it that way.
const MotionInfo kTransitions[] = {
    {"open", "Open",
     "The panel arrives: it can fade in and slide from an offset."},
    {"close", "Close",
     "And leaves the same way. The menu keeps drawing until this finishes -\n"
     "only plain dismissals animate, never a scene switch."},
    {"cursor", "Caret",
     "How long the selection caret takes to reach the row you moved to."},
    {"scroll", "List scroll",
     "A list longer than its window settles into the new position instead of\n"
     "jumping. The rows, the highlight and the values move together."},
    {"value", "Value change",
     "The row whose Toggle/Choice value just changed flashes for this long -\n"
     "how a player sees that a press did something on a row that never moves."},
};
static_assert(sizeof(kTransitions) / sizeof(kTransitions[0]) ==
                  (size_t)Transition::WhichCount,
              "every Transition::Which needs a name here");

// Index-aligned with Animation::Which.
const MotionInfo kAnimations[] = {
    {"selected", "Selected row",
     "`pulse <period> <amount>` - the highlight breathes. The amount is how far\n"
     "its alpha swings (0.2 is a gentle one)."},
    {"marker", "Caret",
     "`bob <period> <pixels>` - the caret drifts back and forth."},
    {"panel", "Panel sheen",
     "`sheen <period> <width> <colour>` - a soft band sweeps across the panel,\n"
     "added rather than blended, and cropped to the panel's edges."},
};
static_assert(sizeof(kAnimations) / sizeof(kAnimations[0]) ==
                  (size_t)Animation::WhichCount,
              "every Animation::Which needs a name here");
}  // namespace

const char* transitionName(int w) {
    return w >= 0 && w < Transition::WhichCount ? kTransitions[w].key : "";
}
const char* transitionLabel(int w) {
    return w >= 0 && w < Transition::WhichCount ? kTransitions[w].label : "";
}
const char* transitionHelp(int w) {
    return w >= 0 && w < Transition::WhichCount ? kTransitions[w].help : "";
}
const char* animationName(int w) {
    return w >= 0 && w < Animation::WhichCount ? kAnimations[w].key : "";
}
const char* animationLabel(int w) {
    return w >= 0 && w < Animation::WhichCount ? kAnimations[w].label : "";
}
const char* animationHelp(int w) {
    return w >= 0 && w < Animation::WhichCount ? kAnimations[w].help : "";
}

const char* stateName(int state) {
    switch (state) {
        case StateSelected: return "selected";
        case StateDisabled: return "disabled";
        default: return "";
    }
}

// --- defaults (the Classic look) ---------------------------------------------

Computed defaults(Elem e) {
    Computed c;
    switch (e) {
        case Elem::Panel:
            c.background = Fill{Fill::Solid, kClassicBg, kClassicBg, 180.0f};
            c.borderW = 2;
            c.borderColor = Color{120, 209, 255, 255};  // overridden by accent
            c.padding = Edge{8, 0, 0, 0};
            c.quant = 0;
            c.gap = 8;
            break;
        case Elem::Title:
            // The classic title block: text, a 5px gap, a 1px rule inset 16px,
            // then 12px of air - together the old `titleSize + 18`.
            c.fontSize = 18;
            c.align = 1;
            c.padding = Edge{0, 0, 5, 0};
            c.margin = Edge{0, 16, 12, 16};
            c.ruleBelow = 1;
            c.ruleColor = kClassicRule;
            break;
        case Elem::List:
            c.rowsVisible = 0;
            break;
        case Elem::Row:
            c.fontSize = 15;
            c.color = kClassicText;
            c.padding = Edge{2, 0, 7, 56};  // 2 + size + 7 = the old pitch
            break;
        case Elem::Value:
            c.fontSize = 15;
            c.color = kClassicText;
            c.align = 2;
            // Same 2px as a row label: the cell is drawn ON the row, so the two
            // baselines have to agree.
            c.padding = Edge{2, 0, 0, 0};
            c.margin = Edge{0, 28, 0, 0};
            c.barW = 90;
            c.barH = 8;
            c.barFill = Color{120, 209, 255, 255};
            c.barTrack = Color{255, 255, 255, 40};
            break;
        case Elem::Description:
            c.fontSize = 12;
            c.color = kClassicDim;
            c.height = 0;  // no description area unless a sheet asks for one
            c.wrap = true;
            c.padding = Edge{2, 12, 2, 12};
            break;
        case Elem::Hint:
            c.fontSize = 11;
            c.color = kClassicDim;
            c.align = 1;
            c.height = 22;
            c.padding = Edge{4, 0, 0, 0};
            c.content = "X OK    \xE2\x96\xB2 BACK";
            break;
        case Elem::Marker:
            c.markerSide = 0;
            c.translateX = 32;  // the classic cursor x inside the panel
            break;
        case Elem::Image:
            c.gap = 8;
            break;
        case Elem::Count: break;
    }
    return c;
}

// --- applying declarations ---------------------------------------------------

void applyDecl(Computed& c, const Decl& d) {
    const Value& v = d.value;
    switch (d.prop) {
        case Prop::Width: c.width = v.n[0]; break;
        case Prop::Height: c.height = v.n[0]; break;
        case Prop::Padding: c.padding = Edge{v.n[0], v.n[1], v.n[2], v.n[3]}; break;
        case Prop::Margin: c.margin = Edge{v.n[0], v.n[1], v.n[2], v.n[3]}; break;
        case Prop::PaddingTop: c.padding.t = v.n[0]; break;
        case Prop::PaddingRight: c.padding.r = v.n[0]; break;
        case Prop::PaddingBottom: c.padding.b = v.n[0]; break;
        case Prop::PaddingLeft: c.padding.l = v.n[0]; break;
        case Prop::MarginTop: c.margin.t = v.n[0]; break;
        case Prop::MarginRight: c.margin.r = v.n[0]; break;
        case Prop::MarginBottom: c.margin.b = v.n[0]; break;
        case Prop::MarginLeft: c.margin.l = v.n[0]; break;
        case Prop::Background: c.background = v.fill; break;
        case Prop::BackgroundImage:
            c.backgroundImage = v.s;
            if (v.n[0] > 0) c.slice = v.n[0];
            break;
        case Prop::Slice: c.slice = v.n[0]; break;
        case Prop::BackgroundAnim_:
            c.bgAnim.image = v.s;
            c.bgAnim.mode = v.i;
            c.bgAnim.scrollX = v.n[0];
            c.bgAnim.scrollY = v.n[1];
            c.bgAnim.frames = (int)v.n[2];
            c.bgAnim.seconds = v.n[3] > 0 ? v.n[3] : 1.0f;
            break;
        case Prop::BorderWidth:
            c.borderW = v.n[0];
            c.borderColor = v.c;
            break;
        case Prop::BorderColor: c.borderColor = v.c; break;
        case Prop::Radius: c.radius = v.n[0]; break;
        case Prop::Shadow:
            c.shadow = v.n[2] > 0 || v.n[0] != 0 || v.n[1] != 0;
            c.shadowX = v.n[0];
            c.shadowY = v.n[1];
            c.shadowBlur = v.n[2];
            c.shadowColor = v.c;
            break;
        case Prop::Opacity: c.opacity = v.n[0]; break;
        case Prop::Font: c.font = v.s; break;
        case Prop::FontSize: c.fontSize = v.n[0]; break;
        case Prop::TextColor: c.color = v.c; break;
        case Prop::Align: c.align = v.i; break;
        case Prop::LetterSpacing: c.letterSpacing = v.n[0]; break;
        case Prop::TextTransform: c.upper = v.i == 1; break;
        case Prop::TextShadow:
            c.textShadow = true;
            c.textShadowX = v.n[0];
            c.textShadowY = v.n[1];
            c.textShadowColor = v.c;
            break;
        case Prop::TextOutline:
            c.outlineW = v.n[0];
            c.outlineColor = v.c;
            break;
        case Prop::RuleBelow:
            c.ruleBelow = v.n[0];
            c.ruleColor = v.c;
            break;
        case Prop::Content: c.content = v.s; break;
        case Prop::TranslateX: c.translateX = v.n[0]; break;
        case Prop::IconSize: c.iconSize = v.n[0]; break;
        case Prop::Marker:
            // "none" is a sentinel, not a path: it turns the caret off. The
            // layout, the preview and codegen all read it the same way.
            c.marker = v.s;
            if (v.i >= 0) c.markerSide = v.i;
            break;
        case Prop::MarkerSide: c.markerSide = v.i; break;
        case Prop::Selectable: c.selectable = v.i != 0; break;
        case Prop::RowsVisible: c.rowsVisible = (int)v.n[0]; break;
        case Prop::ScrollMarker: c.scrollMarker = v.s; break;
        case Prop::Display: c.display = v.i; break;
        case Prop::BarSize:
            c.barW = v.n[0];
            c.barH = v.n[1];
            break;
        case Prop::BarFill: c.barFill = v.c; break;
        case Prop::BarTrack: c.barTrack = v.c; break;
        case Prop::Quant: c.quant = v.i; break;
        case Prop::Gap: c.gap = v.n[0]; break;
        case Prop::Wrap: c.wrap = v.i != 0; break;
        case Prop::Area: c.area = v.i; break;
        case Prop::Count: break;
    }
}

Computed compute(const Sheet& sheet, const std::string& menuName, Elem elem,
                 const std::string& cls, int state, const Computed& base) {
    // Collect the rules that match, then apply them by specificity and, within
    // one specificity, in source order - the documented cascade.
    std::vector<const Rule*> matching;
    for (const Rule& r : sheet.rules) {
        if (r.elem != elem) continue;
        if (!r.menu.empty() && r.menu != menuName) continue;
        if (!r.cls.empty() && r.cls != cls) continue;
        if (r.state != StateNormal && r.state != state) continue;
        matching.push_back(&r);
    }
    std::stable_sort(matching.begin(), matching.end(),
                     [](const Rule* a, const Rule* b) {
                         return a->specificity() < b->specificity();
                     });
    Computed c = base;
    for (const Rule* r : matching)
        for (const Decl& d : r->decls) applyDecl(c, d);
    return c;
}

Computed computeFor(const Sheet& sheet, const std::string& menuName, Elem elem,
                    const std::string& cls, int state) {
    Computed normal = compute(sheet, menuName, elem, cls, StateNormal, defaults(elem));
    if (state == StateNormal) return normal;
    return compute(sheet, menuName, elem, cls, state, normal);
}

bool statePaints(const Sheet& sheet, const std::string& menuName,
                 const std::string& cls, int state) {
    if (state == StateNormal) return false;
    for (const Rule& r : sheet.rules) {
        if (r.state != state || r.decls.empty()) continue;
        if (r.elem != Elem::Row && r.elem != Elem::Value) continue;
        if (!r.menu.empty() && r.menu != menuName) continue;
        if (!r.cls.empty() && r.cls != cls) continue;
        return true;
    }
    return false;
}

const Transition* transition(const Sheet& sheet, int which) {
    for (const Transition& t : sheet.transitions)
        if (t.which == which) return &t;
    return nullptr;
}

const Animation* animation(const Sheet& sheet, int which) {
    for (const Animation& a : sheet.animations)
        if (a.which == which) return &a;
    return nullptr;
}

// --- parsing -----------------------------------------------------------------

namespace {

// Comments out, newlines kept so every diagnostic can name a line.
std::string stripComments(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '*') {
            i += 2;
            while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) {
                if (in[i] == '\n') out += '\n';
                ++i;
            }
            i = i + 2 <= in.size() ? i + 2 : in.size();
            continue;
        }
        if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            while (i < in.size() && in[i] != '\n') ++i;
            continue;
        }
        out += in[i++];
    }
    return out;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// One raw declaration before its value is understood.
struct RawDecl {
    std::string prop, value;
    int line = 0;
};

struct RawRule {
    std::string menu, selector;
    std::vector<RawDecl> decls;
    int line = 0;
};

// The structural pass: `@` items, :root, menu#name scopes and selector blocks,
// values kept as text (so pass two can resolve var() whatever the file order).
struct Scanner {
    const std::string& s;
    size_t i = 0;
    int line = 1;
    std::vector<Diag>* diags;

    void skipWs() {
        while (i < s.size() && isspace((unsigned char)s[i])) {
            if (s[i] == '\n') ++line;
            ++i;
        }
    }
    bool eof() {
        skipWs();
        return i >= s.size();
    }
    char peek() { return i < s.size() ? s[i] : '\0'; }
    char get() {
        const char c = s[i++];
        if (c == '\n') ++line;
        return c;
    }
    void err(const std::string& m) { diags->push_back(Diag{line, m}); }

    // Reads until one of `stops`, ignoring stops nested in parentheses or
    // inside a quoted string. The quotes matter: a hint's `content` is
    // "{{cross}} OK", and stopping at the first '}' truncated the value AND ate
    // the declaration after it (found by the harness, not by eye).
    std::string readUntil(const char* stops) {
        std::string out;
        int depth = 0;
        bool quoted = false;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '"') quoted = !quoted;
            if (!quoted) {
                if (c == '(') ++depth;
                if (c == ')') --depth;
                if (depth <= 0 && std::strchr(stops, c)) break;
            }
            out += get();
        }
        return out;
    }

    std::string readIdent() {
        std::string out;
        while (i < s.size() &&
               (isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_' ||
                s[i] == '.' || s[i] == ':' || s[i] == '#'))
            out += get();
        return out;
    }

    std::string readString() {
        std::string out;
        if (peek() != '"') return out;
        get();
        while (i < s.size() && peek() != '"') out += get();
        if (i < s.size()) get();
        return out;
    }

    // decls of one block; nested blocks are handed back to the caller through
    // `nested` (menu#name scopes are the only nesting the format has).
    void readDecls(std::vector<RawDecl>& out) {
        while (!eof() && peek() != '}') {
            const int declLine = line;
            std::string name = trim(readUntil(":;}{"));
            if (name.empty()) {
                if (!eof() && peek() != '}') get();
                continue;
            }
            skipWs();
            if (peek() != ':') {
                err("expected ':' after '" + name + "'");
                readUntil(";}");
                if (peek() == ';') get();
                continue;
            }
            get();  // ':'
            std::string value = trim(readUntil(";}"));
            if (peek() == ';') get();
            out.push_back(RawDecl{lower(name), value, declLine});
        }
        if (peek() == '}') get();
    }
};

// A length, with the two keywords the format allows.
bool parseLength(const std::string& in, float& out) {
    const std::string t = lower(trim(in));
    if (t.empty()) return false;
    if (t == "auto" || t == "none") {
        out = 0;
        return true;
    }
    if (t == "screen") {
        out = -1;  // the layout engine reads -1 as "as wide as the screen"
        return true;
    }
    char* end = nullptr;
    const double d = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) return false;
    out = (float)d;
    return true;
}

bool parseColorText(const std::string& in, Color& out) {
    std::string t = trim(in);
    if (t.empty()) return false;
    const std::string tl = lower(t);
    if (tl == "transparent" || tl == "none") {
        out = Color{0, 0, 0, 0};
        return true;
    }
    if (t[0] == '#') {
        const std::string h = t.substr(1);
        auto hx = [&](size_t k) { return hexVal(h[k]); };
        if (h.size() == 3) {
            for (size_t k = 0; k < 3; ++k)
                if (hx(k) < 0) return false;
            out = Color{(unsigned char)(hx(0) * 17), (unsigned char)(hx(1) * 17),
                        (unsigned char)(hx(2) * 17), 255};
            return true;
        }
        if (h.size() == 6 || h.size() == 8) {
            for (size_t k = 0; k < h.size(); ++k)
                if (hx(k) < 0) return false;
            out = Color{(unsigned char)(hx(0) * 16 + hx(1)),
                        (unsigned char)(hx(2) * 16 + hx(3)),
                        (unsigned char)(hx(4) * 16 + hx(5)),
                        h.size() == 8 ? (unsigned char)(hx(6) * 16 + hx(7))
                                      : (unsigned char)255};
            return true;
        }
        return false;
    }
    if (tl.rfind("rgb", 0) == 0) {
        const size_t o = t.find('(');
        const size_t c = t.rfind(')');
        if (o == std::string::npos || c == std::string::npos || c < o) return false;
        std::string body = t.substr(o + 1, c - o - 1);
        for (char& ch : body)
            if (ch == ',') ch = ' ';
        std::istringstream is(body);
        float v[4] = {0, 0, 0, 1};
        int n = 0;
        while (n < 4 && (is >> v[n])) ++n;
        if (n < 3) return false;
        auto b = [](float f) {
            const int k = (int)(f + 0.5f);
            return (unsigned char)(k < 0 ? 0 : k > 255 ? 255 : k);
        };
        // The alpha is 0..1 like CSS, but a stray 0..255 is what people type -
        // accept both rather than silently making the box invisible.
        const float af = n >= 4 ? (v[3] > 1.0f ? v[3] / 255.0f : v[3]) : 1.0f;
        out = Color{b(v[0]), b(v[1]), b(v[2]), b(af * 255.0f)};
        return true;
    }
    return false;
}

// url(path) plus an optional trailing keyword or `9-slice N`.
void parseUrl(const std::string& in, Value& v, const char* keywords) {
    const size_t o = in.find('(');
    const size_t c = in.find(')', o == std::string::npos ? 0 : o);
    if (o != std::string::npos && c != std::string::npos && c > o) {
        v.s = trim(in.substr(o + 1, c - o - 1));
        if (!v.s.empty() && (v.s.front() == '"' || v.s.front() == '\''))
            v.s = v.s.substr(1, v.s.size() - 2);
    } else {
        v.s = trim(in);
    }
    v.i = -1;
    const std::string rest = lower(c == std::string::npos ? "" : in.substr(c + 1));
    const size_t sl = rest.find("9-slice");
    if (sl != std::string::npos) {
        float f = 0;
        if (parseLength(trim(rest.substr(sl + 7)), f)) v.n[0] = f;
    }
    if (keywords) {
        int idx = 0;
        std::string kw;
        for (const char* p = keywords;; ++p) {
            if (*p == '|' || *p == '\0') {
                if (!kw.empty() && rest.find(kw) != std::string::npos) {
                    v.i = idx;
                    break;
                }
                ++idx;
                kw.clear();
                if (*p == '\0') break;
                continue;
            }
            kw += *p;
        }
    }
}

// "12px/s" -> "12", "1.2s" -> "1.2". A unit is documentation, not data.
std::string stripUnit(const std::string& in, const char* unit) {
    const size_t at = in.find(unit);
    return at == std::string::npos ? in : in.substr(0, at);
}

bool parseEnum(const std::string& in, const char* keywords, int& out) {
    const std::string t = lower(trim(in));
    int idx = 0;
    std::string kw;
    for (const char* p = keywords;; ++p) {
        if (*p == '|' || *p == '\0') {
            if (kw == t) {
                out = idx;
                return true;
            }
            ++idx;
            kw.clear();
            if (*p == '\0') return false;
            continue;
        }
        kw += *p;
    }
}

std::vector<std::string> splitWs(const std::string& in) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : in) {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (isspace((unsigned char)c) && depth == 0) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Splits on top-level commas (gradient stops, rgb() args stay intact).
std::vector<std::string> splitCommas(const std::string& in) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : in) {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(trim(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    out.push_back(trim(cur));
    return out;
}

bool parseFill(const std::string& in, Fill& out) {
    const std::string t = trim(in);
    const std::string tl = lower(t);
    if (tl.rfind("linear-gradient", 0) == 0) {
        const size_t o = t.find('(');
        const size_t c = t.rfind(')');
        if (o == std::string::npos || c == std::string::npos) return false;
        const auto parts = splitCommas(t.substr(o + 1, c - o - 1));
        Fill f;
        f.kind = Fill::Gradient;
        size_t at = 0;
        f.angle = 180.0f;
        if (!parts.empty() && lower(parts[0]).find("deg") != std::string::npos) {
            float a = 0;
            if (parseLength(parts[0].substr(0, lower(parts[0]).find("deg")), a))
                f.angle = a;
            at = 1;
        }
        Color ca{}, cb{};
        if (parts.size() <= at || !parseColorText(parts[at], ca)) return false;
        cb = ca;
        if (parts.size() > at + 1) parseColorText(parts[at + 1], cb);
        f.a = ca;
        f.b = cb;
        out = f;
        return true;
    }
    Color c{};
    if (!parseColorText(t, c)) return false;
    out = Fill{c.a == 0 ? Fill::None : Fill::Solid, c, c, 180.0f};
    return true;
}

// A value, per the property's Kind. False = unparsable (the caller diags it).
bool parseValue(const PropSpec& spec, const std::string& text, Value& v) {
    switch (spec.kind) {
        case Kind::Length: return parseLength(text, v.n[0]);
        case Kind::Color: return parseColorText(text, v.c);
        case Kind::Fill: return parseFill(text, v.fill);
        case Kind::Edge: {
            const auto parts = splitWs(text);
            float f[4] = {0, 0, 0, 0};
            int n = 0;
            for (const std::string& p : parts) {
                if (n >= 4) break;
                if (!parseLength(p, f[n])) return false;
                ++n;
            }
            if (n == 0) return false;
            // CSS shorthand: 1 = all, 2 = v/h, 3 = t/h/b, 4 = t/r/b/l
            if (n == 1) v.n[0] = v.n[1] = v.n[2] = v.n[3] = f[0];
            else if (n == 2) {
                v.n[0] = v.n[2] = f[0];
                v.n[1] = v.n[3] = f[1];
            } else if (n == 3) {
                v.n[0] = f[0];
                v.n[1] = v.n[3] = f[1];
                v.n[2] = f[2];
            } else {
                v.n[0] = f[0];
                v.n[1] = f[1];
                v.n[2] = f[2];
                v.n[3] = f[3];
            }
            return true;
        }
        case Kind::Url:
            parseUrl(text, v, spec.keywords);
            if (spec.prop == Prop::BackgroundAnim_) {
                // `scroll <x>px/s <y>px/s` or `frames <n> <sec>` after the url.
                const auto words = splitWs(lower(text));
                for (size_t k = 0; k < words.size(); ++k) {
                    if (words[k] == "scroll") {
                        v.i = BackgroundAnim::Scroll;
                        float f = 0;
                        if (k + 1 < words.size() &&
                            parseLength(stripUnit(words[k + 1], "px/s"), f))
                            v.n[0] = f;
                        if (k + 2 < words.size() &&
                            parseLength(stripUnit(words[k + 2], "px/s"), f))
                            v.n[1] = f;
                    } else if (words[k] == "frames") {
                        v.i = BackgroundAnim::Frames;
                        float f = 0;
                        if (k + 1 < words.size() && parseLength(words[k + 1], f))
                            v.n[2] = f;
                        if (k + 2 < words.size() &&
                            parseLength(stripUnit(words[k + 2], "s"), f))
                            v.n[3] = f;
                    }
                }
            }
            return !v.s.empty();
        case Kind::Str: {
            std::string t = trim(text);
            if (t.size() >= 2 && (t.front() == '"' || t.front() == '\''))
                t = t.substr(1, t.size() - 2);
            v.s = t;
            return true;
        }
        case Kind::Enum: return parseEnum(text, spec.keywords, v.i);
        case Kind::Border: {
            const auto parts = splitWs(text);
            bool gotW = false;
            v.c = Color{255, 255, 255, 255};
            for (const std::string& p : parts) {
                if (lower(p) == "solid") continue;
                Color c{};
                if (parseColorText(p, c)) {
                    v.c = c;
                    continue;
                }
                float f = 0;
                if (parseLength(p, f)) {
                    v.n[0] = f;
                    gotW = true;
                }
            }
            return gotW;
        }
        case Kind::Shadow: {
            const auto parts = splitWs(text);
            int n = 0;
            v.c = Color{0, 0, 0, 160};
            for (const std::string& p : parts) {
                Color c{};
                if (parseColorText(p, c)) {
                    v.c = c;
                    continue;
                }
                float f = 0;
                if (n < 3 && parseLength(p, f)) v.n[n++] = f;
            }
            return n > 0;
        }
        case Kind::Bool: {
            const std::string t = lower(trim(text));
            if (t == "yes" || t == "true" || t == "1") {
                v.i = 1;
                return true;
            }
            if (t == "no" || t == "false" || t == "0") {
                v.i = 0;
                return true;
            }
            return false;
        }
    }
    return false;
}

// `elem`, `elem.class`, `elem:state` (in any order after the element).
bool parseSelector(const std::string& sel, Elem& elem, std::string& cls,
                   int& state) {
    std::string base = trim(sel);
    cls.clear();
    state = StateNormal;
    const size_t colon = base.find(':');
    if (colon != std::string::npos) {
        const std::string st = lower(trim(base.substr(colon + 1)));
        if (st == "selected") state = StateSelected;
        else if (st == "disabled") state = StateDisabled;
        else return false;
        base = trim(base.substr(0, colon));
    }
    const size_t dot = base.find('.');
    if (dot != std::string::npos) {
        cls = trim(base.substr(dot + 1));
        base = trim(base.substr(0, dot));
    }
    const std::string el = lower(base);
    for (int e = 0; e < (int)Elem::Count; ++e)
        if (el == elemName((Elem)e)) {
            elem = (Elem)e;
            return true;
        }
    return false;
}

void parseTransition(const std::string& name, const std::vector<RawDecl>& decls,
                     const std::string& body, Sheet& sheet, int line) {
    Transition t;
    const std::string n = lower(trim(name));
    int which = -1;
    for (int w = 0; w < Transition::WhichCount; ++w)
        if (n == transitionName(w)) which = w;
    if (which < 0) {
        sheet.diags.push_back(Diag{line, "unknown transition '" + name + "'"});
        return;
    }
    t.which = which;
    (void)decls;
    // A transition block is a space/semicolon separated list of terms rather
    // than key: value pairs - `fade 200ms ease-out; translate-y 12px`.
    std::string cur;
    std::vector<std::string> terms;
    for (char c : body) {
        if (c == ';' || c == '\n') {
            if (!trim(cur).empty()) terms.push_back(trim(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!trim(cur).empty()) terms.push_back(trim(cur));
    for (const std::string& term : terms) {
        const auto words = splitWs(term);
        for (size_t k = 0; k < words.size(); ++k) {
            const std::string w = lower(words[k]);
            if (w == "fade") {
                t.fade = true;
            } else if (w == "ease-out") {
                t.ease = 1;
            } else if (w == "ease-in-out") {
                t.ease = 2;
            } else if (w == "linear") {
                t.ease = 0;
            } else if (w == "translate-x" || w == "translate-y" || w == "scale") {
                float f = 0;
                if (k + 1 < words.size() && parseLength(words[k + 1], f)) {
                    if (w == "translate-x") t.translateX = f;
                    else if (w == "translate-y") t.translateY = f;
                    else t.scale = f;
                    ++k;
                }
            } else if (w.size() > 2 && w.compare(w.size() - 2, 2, "ms") == 0) {
                float f = 0;
                if (parseLength(w.substr(0, w.size() - 2), f)) t.seconds = f / 1000.0f;
            } else if (!w.empty() && w.back() == 's') {
                float f = 0;
                if (parseLength(w.substr(0, w.size() - 1), f)) t.seconds = f;
            }
        }
    }
    for (Transition& ex : sheet.transitions)
        if (ex.which == t.which) {
            ex = t;
            return;
        }
    sheet.transitions.push_back(t);
}

void parseAnimation(const std::string& name, const std::string& body, Sheet& sheet,
                    int line) {
    Animation a;
    const std::string n = lower(trim(name));
    int which = -1;
    for (int w = 0; w < Animation::WhichCount; ++w)
        if (n == animationName(w)) which = w;
    if (n == "caret") which = Animation::Marker;  // a friendlier spelling
    if (which < 0) {
        sheet.diags.push_back(
            Diag{line, "unknown animation target '" + name + "'"});
        return;
    }
    a.which = which;
    // Space/semicolon separated terms, like @transition: `pulse 1.6s 0.25`.
    std::string cur;
    std::vector<std::string> terms;
    for (char c : body) {
        if (c == ';' || c == '\n') {
            if (!trim(cur).empty()) terms.push_back(trim(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!trim(cur).empty()) terms.push_back(trim(cur));
    for (const std::string& term : terms) {
        const auto words = splitWs(term);
        for (size_t k = 0; k < words.size(); ++k) {
            const std::string w = lower(words[k]);
            if (w == "pulse") a.kind = Animation::Pulse;
            else if (w == "bob") a.kind = Animation::Bob;
            else if (w == "sheen") a.kind = Animation::Sheen;
            else if (!w.empty() && w.back() == 's' &&
                     (w.size() < 2 || w[w.size() - 2] != 'p')) {
                float f = 0;
                if (w.size() > 2 && w.compare(w.size() - 2, 2, "ms") == 0) {
                    if (parseLength(w.substr(0, w.size() - 2), f))
                        a.seconds = f / 1000.0f;
                } else if (parseLength(w.substr(0, w.size() - 1), f)) {
                    a.seconds = f;
                }
            } else {
                Color c{};
                if (parseColorText(words[k], c)) {
                    a.color = c;
                    continue;
                }
                float f = 0;
                if (parseLength(w, f)) a.amount = f;
            }
        }
    }
    if (a.kind == Animation::None) {
        sheet.diags.push_back(
            Diag{line, "@animate " + n + " says nothing to animate (pulse / bob / sheen)"});
        return;
    }
    for (Animation& ex : sheet.animations)
        if (ex.which == a.which) {
            ex = a;
            return;
        }
    sheet.animations.push_back(a);
}

}  // namespace

Sheet parse(const std::string& textIn, const std::string& key,
            const std::string& path) {
    Sheet sheet;
    sheet.key = key;
    sheet.path = path;
    sheet.name = key;
    const std::string text = stripComments(textIn);

    // --- pass 1: structure, values as text -----------------------------------
    std::vector<RawRule> raw;
    std::vector<RawDecl> rootVars;
    struct RawTransition {
        std::string name, body;
        int line;
    };
    std::vector<RawTransition> transitions, animations;

    Scanner sc{text, 0, 1, &sheet.diags};
    std::string menuScope;
    int scopeDepth = 0;
    while (!sc.eof()) {
        if (sc.peek() == '}') {
            sc.get();
            if (scopeDepth > 0) {
                --scopeDepth;
                menuScope.clear();
            }
            continue;
        }
        if (sc.peek() == '@') {
            sc.get();
            const int line = sc.line;
            const std::string word = lower(sc.readIdent());
            if (word == "style") {
                sc.skipWs();
                const std::string n = sc.readString();
                if (!n.empty()) sheet.name = n;
                sc.skipWs();
                if (sc.peek() == ';') sc.get();
            } else if (word == "animate" || word == "transition") {
                sc.skipWs();
                const std::string n = sc.readIdent();
                sc.skipWs();
                if (sc.peek() == '{') {
                    sc.get();
                    const std::string body = sc.readUntil("}");
                    if (sc.peek() == '}') sc.get();
                    (word == "animate" ? animations : transitions)
                        .push_back(RawTransition{n, body, line});
                } else {
                    sc.err("expected '{' after @" + word);
                }
            } else {
                sc.err("unknown @" + word);
                sc.readUntil("}");
                if (sc.peek() == '}') sc.get();
            }
            continue;
        }
        const int line = sc.line;
        std::string head = trim(sc.readUntil("{};"));
        if (head.empty()) {
            if (!sc.eof()) sc.get();
            continue;
        }
        sc.skipWs();
        if (sc.peek() != '{') {
            sc.err("expected '{' after '" + head + "'");
            if (!sc.eof()) sc.get();
            continue;
        }
        sc.get();  // '{'
        const std::string headLower = lower(head);
        if (headLower == ":root") {
            sc.readDecls(rootVars);
            continue;
        }
        if (headLower.rfind("menu#", 0) == 0) {
            menuScope = trim(head.substr(5));
            ++scopeDepth;
            continue;
        }
        std::vector<RawDecl> decls;
        sc.readDecls(decls);
        for (const std::string& sel : splitCommas(head))
            raw.push_back(RawRule{menuScope, sel, decls, line});
    }

    // --- pass 2: variables ---------------------------------------------------
    std::map<std::string, std::string> varText;
    for (const RawDecl& d : rootVars) {
        if (d.prop.rfind("--", 0) != 0) {
            sheet.diags.push_back(
                Diag{d.line, "only --variables belong in :root ('" + d.prop + "')"});
            continue;
        }
        varText[d.prop] = d.value;
        Value v;
        v.s = d.value;
        parseColorText(d.value, v.c);
        parseLength(d.value, v.n[0]);
        sheet.vars.push_back({d.prop, v});
    }
    // var() substitution is textual and one level deep: a variable holding
    // another var() is resolved here, but a cycle is cut rather than chased.
    auto substitute = [&](std::string in, int line) {
        for (int pass = 0; pass < 4; ++pass) {
            const size_t at = in.find("var(");
            if (at == std::string::npos) break;
            const size_t close = in.find(')', at);
            if (close == std::string::npos) break;
            const std::string name = trim(in.substr(at + 4, close - at - 4));
            auto it = varText.find(name);
            if (it == varText.end()) {
                sheet.diags.push_back(Diag{line, "unknown variable " + name});
                in = in.substr(0, at) + in.substr(close + 1);
                continue;
            }
            in = in.substr(0, at) + it->second + in.substr(close + 1);
        }
        return in;
    };

    // --- pass 3: rules -------------------------------------------------------
    for (const RawRule& rr : raw) {
        Rule rule;
        rule.menu = rr.menu;
        rule.line = rr.line;
        if (!parseSelector(rr.selector, rule.elem, rule.cls, rule.state)) {
            sheet.diags.push_back(
                Diag{rr.line, "unknown selector '" + trim(rr.selector) + "'"});
            continue;
        }
        for (const RawDecl& d : rr.decls) {
            const PropSpec* spec = nullptr;
            for (const PropSpec& s : propSpecs())
                if (d.prop == s.name) {
                    spec = &s;
                    break;
                }
            if (!spec) {
                sheet.diags.push_back(Diag{d.line, "unknown property '" + d.prop + "'"});
                continue;
            }
            Decl decl;
            decl.prop = spec->prop;
            decl.line = d.line;
            if (!parseValue(*spec, substitute(d.value, d.line), decl.value)) {
                sheet.diags.push_back(
                    Diag{d.line, std::string("bad value for ") + spec->name + ": '" +
                                     d.value + "'"});
                continue;
            }
            rule.decls.push_back(decl);
        }
        sheet.rules.push_back(std::move(rule));
    }

    for (const RawTransition& t : transitions)
        parseTransition(t.name, {}, t.body, sheet, t.line);
    for (const RawTransition& a : animations)
        parseAnimation(a.name, a.body, sheet, a.line);
    return sheet;
}

// --- writing -----------------------------------------------------------------

namespace {

std::string fmtNum(float f) {
    char buf[32];
    if (f == (float)(long long)f)
        std::snprintf(buf, sizeof(buf), "%lld", (long long)f);
    else
        std::snprintf(buf, sizeof(buf), "%.3g", (double)f);
    return buf;
}

std::string fmtPx(float f) {
    if (f == -1.0f) return "screen";
    return fmtNum(f) + "px";
}

std::string fmtColor(Color c) {
    char buf[40];
    if (c.a == 0) return "transparent";
    if (c.a == 255) {
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "rgba(%d, %d, %d, %.3g)", c.r, c.g, c.b,
                  c.a / 255.0);
    return buf;
}

std::string fmtValue(const PropSpec& spec, const Value& v) {
    switch (spec.kind) {
        case Kind::Length: return fmtPx(v.n[0]);
        case Kind::Color: return fmtColor(v.c);
        case Kind::Fill:
            if (v.fill.kind == Fill::Gradient)
                return "linear-gradient(" + fmtNum(v.fill.angle) + "deg, " +
                       fmtColor(v.fill.a) + ", " + fmtColor(v.fill.b) + ")";
            if (v.fill.kind == Fill::None) return "transparent";
            return fmtColor(v.fill.a);
        case Kind::Edge: {
            if (v.n[0] == v.n[1] && v.n[1] == v.n[2] && v.n[2] == v.n[3])
                return fmtPx(v.n[0]);
            if (v.n[0] == v.n[2] && v.n[1] == v.n[3])
                return fmtPx(v.n[0]) + " " + fmtPx(v.n[1]);
            return fmtPx(v.n[0]) + " " + fmtPx(v.n[1]) + " " + fmtPx(v.n[2]) + " " +
                   fmtPx(v.n[3]);
        }
        case Kind::Url: {
            std::string out = "url(" + v.s + ")";
            if (v.n[0] > 0) out += " 9-slice " + fmtPx(v.n[0]);
            if (spec.keywords && v.i >= 0) {
                int idx = 0;
                std::string kw;
                for (const char* p = spec.keywords;; ++p) {
                    if (*p == '|' || *p == '\0') {
                        if (idx == v.i) {
                            out += " " + kw;
                            break;
                        }
                        ++idx;
                        kw.clear();
                        if (*p == '\0') break;
                        continue;
                    }
                    kw += *p;
                }
            }
            return out;
        }
        case Kind::Str: return "\"" + v.s + "\"";
        case Kind::Enum: {
            int idx = 0;
            std::string kw;
            for (const char* p = spec.keywords;; ++p) {
                if (*p == '|' || *p == '\0') {
                    if (idx == v.i) return kw;
                    ++idx;
                    kw.clear();
                    if (*p == '\0') break;
                    continue;
                }
                kw += *p;
            }
            return "0";
        }
        case Kind::Border:
            return fmtPx(v.n[0]) + " solid " + fmtColor(v.c);
        case Kind::Shadow:
            return fmtPx(v.n[0]) + " " + fmtPx(v.n[1]) + " " + fmtPx(v.n[2]) + " " +
                   fmtColor(v.c);
        case Kind::Bool: return v.i ? "yes" : "no";
    }
    return "";
}

std::string selectorText(const Rule& r) {
    std::string s = elemName(r.elem);
    if (!r.cls.empty()) s += "." + r.cls;
    if (r.state != StateNormal) s += std::string(":") + stateName(r.state);
    return s;
}

void writeRules(std::ostringstream& out, const Sheet& sheet,
                const std::string& menu, const char* indent) {
    for (const Rule& r : sheet.rules) {
        if (r.menu != menu || r.decls.empty()) continue;
        out << indent << selectorText(r) << " {\n";
        for (const Decl& d : r.decls) {
            const PropSpec* spec = propSpec(d.prop);
            if (!spec) continue;
            out << indent << "  " << spec->name << ": "
                << fmtValue(*spec, d.value) << ";\n";
        }
        out << indent << "}\n";
    }
}

}  // namespace

std::string write(const Sheet& sheet) {
    std::ostringstream out;
    out << "@style \"" << sheet.name << "\"\n";
    if (!sheet.vars.empty()) {
        out << "\n:root {\n";
        for (const auto& v : sheet.vars) out << "  " << v.first << ": " << v.second.s << ";\n";
        out << "}\n";
    }
    out << "\n";
    writeRules(out, sheet, "", "");
    // Menu-scoped blocks last, in first-appearance order, so the file reads
    // general-to-specific the way the cascade resolves.
    std::vector<std::string> menus;
    for (const Rule& r : sheet.rules) {
        if (r.menu.empty()) continue;
        if (std::find(menus.begin(), menus.end(), r.menu) == menus.end())
            menus.push_back(r.menu);
    }
    for (const std::string& m : menus) {
        out << "\nmenu#" << m << " {\n";
        writeRules(out, sheet, m, "  ");
        out << "}\n";
    }
    for (const Transition& t : sheet.transitions) {
        if (t.which < 0 || t.which >= Transition::WhichCount) continue;
        out << "\n@transition " << transitionName(t.which) << " {\n  ";
        if (t.seconds > 0) out << fmtNum(t.seconds * 1000.0f) << "ms ";
        out << (t.ease == 0 ? "linear" : t.ease == 2 ? "ease-in-out" : "ease-out");
        if (t.fade) out << "; fade";
        if (t.translateX != 0) out << "; translate-x " << fmtPx(t.translateX);
        if (t.translateY != 0) out << "; translate-y " << fmtPx(t.translateY);
        if (t.scale != 0) out << "; scale " << fmtNum(t.scale);
        out << ";\n}\n";
    }
    static const char* kKind[] = {"", "pulse", "bob", "sheen"};
    for (const Animation& a : sheet.animations) {
        if (a.which < 0 || a.which >= Animation::WhichCount) continue;
        if (a.kind <= 0 || a.kind > Animation::Sheen) continue;
        out << "\n@animate " << animationName(a.which) << " {\n  " << kKind[a.kind] << " "
            << fmtNum(a.seconds) << "s " << fmtNum(a.amount);
        if (a.kind == Animation::Sheen) out << " " << fmtColor(a.color);
        out << ";\n}\n";
    }
    return out.str();
}

// --- the built-in sheets -----------------------------------------------------

const std::vector<std::pair<std::string, std::string>>& builtinSources() {
    static const std::vector<std::pair<std::string, std::string>> v = {
        // "classic" is deliberately EMPTY of rules: the built-in defaults ARE
        // the classic look, so an existing project renders identically whether
        // it names this sheet or nothing at all.
        {"classic",
         "@style \"Classic\"\n"
         "\n"
         "/* The look TyraX menus had before stylesheets existed - and it is\n"
         "   deliberately EMPTY. Every value it would list is already the\n"
         "   built-in default, and the ones it must NOT list are the menu's own:\n"
         "   accent, title size, row size, panel width and font are the BASE of\n"
         "   the cascade (menulayout::baseFor). A rule restating one of them here\n"
         "   would freeze it for every menu using this sheet - which is what the\n"
         "   first pixel diff against the old baker found.\n"
         "\n"
         "   Start from a copy of another sheet (Neon, Blade, Parchment,\n"
         "   Minimal) or add rules here one at a time; the Style tab lists every\n"
         "   default next to the field that overrides it. */\n"},

        {"neon",
         "@style \"Neon\"\n"
         "\n"
         ":root {\n"
         "  --accent: #78d1ff;\n"
         "  --ink: #eaf6ff;\n"
         "  --dim: #7d90a8;\n"
         "}\n"
         "\n"
         "panel {\n"
         "  /* A styled menu ships a second texture (the row-state cells), so it\n"
         "     palettizes: 256 colours are plenty for text over a gradient and it\n"
         "     is 4x less VRAM (docs/gs-vram.md). */\n"
         "  quant: 8bit;\n"
         "  background: linear-gradient(180deg, rgba(6, 12, 26, 0.95), rgba(10, 20, 42, 0.8));\n"
         "  border: 1px solid var(--accent);\n"
         "  border-radius: 8px;\n"
         "  padding: 14px 0 0 0;\n"
         "  shadow: 0px 3px 10px rgba(0, 0, 0, 0.65);\n"
         "}\n"
         "title {\n"
         "  color: var(--accent);\n"
         "  letter-spacing: 3px;\n"
         "  text-transform: uppercase;\n"
         "  text-shadow: 0px 2px 0px rgba(0, 8, 24, 0.9);\n"
         "  rule-below: 1px solid rgba(120, 209, 255, 0.35);\n"
         "}\n"
         "row { color: var(--ink); padding: 3px 0 8px 44px; }\n"
         "row:selected {\n"
         "  color: #ffffff;\n"
         "  background: linear-gradient(0deg, rgba(120, 209, 255, 0.42), rgba(120, 209, 255, 0.05));\n"
         "  translate-x: 6px;\n"
         "}\n"
         "/* The selected row is a full-width plate, so the built-in caret would\n"
         "   only sit on top of it. */\n"
         "marker { marker: none; }\n"
         "row:disabled { color: #4d5a6b; }\n"
         "value { color: var(--accent); }\n"
         "hint { color: var(--dim); letter-spacing: 1px; }\n"
         "\n"
         "/* Motion. All of it is sprite properties - an alpha, a position, a\n"
         "   texel offset - so the console pays nothing for any of it\n"
         "   (docs/menu-styles.md \"Motion\"). A moving background layer would go\n"
         "   here too, and is the one way to animate what a baked gradient\n"
         "   cannot:\n"
         "     panel { background-anim: url(res/hud/stars.png) scroll 8px/s -3px/s; }\n"
         "*/\n"
         "@transition open   { 180ms ease-out; fade; translate-y 10px; }\n"
         "@transition close  { 140ms ease-in; fade; translate-y 6px; }\n"
         "@transition cursor { 110ms ease-out; }\n"
         "@transition scroll { 120ms ease-out; }\n"
         "@transition value  { 180ms; }\n"
         "@animate selected  { pulse 1.8s 0.22; }\n"
         "@animate panel     { sheen 3.4s 52px rgba(255, 255, 255, 0.18); }\n"},

        {"blade",
         "@style \"Blade\"\n"
         "\n"
         ":root {\n"
         "  --edge: #ff9a3c;\n"
         "  --steel: #d7dee8;\n"
         "}\n"
         "\n"
         "panel {\n"
         "  quant: 8bit;\n"
         "  background: linear-gradient(90deg, rgba(12, 12, 14, 0.95), rgba(28, 30, 36, 0.85));\n"
         "  border: 0px solid var(--edge);\n"
         "  padding: 16px 0 0 0;\n"
         "}\n"
         "title {\n"
         "  color: var(--steel);\n"
         "  letter-spacing: 6px;\n"
         "  text-transform: uppercase;\n"
         "  align: left;\n"
         "  margin: 0 20px 14px 20px;\n"
         "  rule-below: 2px solid var(--edge);\n"
         "}\n"
         "row { color: var(--steel); padding: 3px 0 9px 24px; letter-spacing: 1px; }\n"
         "row:selected {\n"
         "  color: #14161a;\n"
         "  background: linear-gradient(90deg, var(--edge), rgba(255, 154, 60, 0.15));\n"
         "}\n"
         "marker { marker: none; }  /* the plate IS the selection */\n"
         "row:disabled { color: #5d646f; }\n"
         "value { color: var(--edge); }\n"
         "hint { color: #6f7783; align: right; margin-right: 20px; }\n"
         "\n"
         "/* Faster and harder than Neon: the plate is already loud, so it does\n"
         "   not breathe - the motion is the slide and one quick edge of light. */\n"
         "@transition open   { 140ms ease-out; translate-x -14px; fade; }\n"
         "@transition close  { 100ms ease-in; translate-x -10px; fade; }\n"
         "@transition cursor { 90ms linear; }\n"
         "@transition scroll { 90ms linear; }\n"
         "@transition value  { 120ms; }\n"
         "@animate panel     { sheen 2.2s 34px rgba(255, 154, 60, 0.22); }\n"},

        {"parchment",
         "@style \"Parchment\"\n"
         "\n"
         ":root {\n"
         "  --ink: #2e2118;\n"
         "  --gold: #7a5a1e;\n"
         "}\n"
         "\n"
         "panel {\n"
         "  quant: 8bit;\n"
         "  background: linear-gradient(180deg, #efe0c0, #d8c49a);\n"
         "  border: 3px solid var(--gold);\n"
         "  border-radius: 4px;\n"
         "  padding: 12px 0 0 0;\n"
         "  shadow: 0px 2px 8px rgba(20, 12, 0, 0.5);\n"
         "}\n"
         "title { color: var(--ink); letter-spacing: 2px; rule-below: 1px solid var(--gold); }\n"
         "row { color: #3b2b1e; padding: 3px 0 8px 48px; }\n"
         "row:selected { color: #1b1208; background: rgba(122, 90, 30, 0.28); }\n"
         "marker { marker: none; }\n"
         "row:disabled { color: #9c8a72; }\n"
         "value { color: var(--gold); }\n"
         "hint { color: #6b5636; }\n"
         "\n"
         "/* Slow and soft, like the paper it is pretending to be. */\n"
         "@transition open   { 260ms ease-in-out; fade; }\n"
         "@transition close  { 180ms ease-in-out; fade; }\n"
         "@transition cursor { 120ms ease-out; }\n"
         "@transition scroll { 160ms ease-in-out; }\n"
         "@animate selected  { pulse 2.6s 0.12; }\n"},

        {"minimal",
         "@style \"Minimal\"\n"
         "\n"
         "panel {\n"
         "  quant: 8bit;\n"
         "  background: rgba(0, 0, 0, 0.55);\n"
         "  border: 0px solid #ffffff;\n"
         "  padding: 10px 0 0 0;\n"
         "}\n"
         "title { color: #ffffff; align: left; margin: 0 24px 10px 24px; rule-below: 0px solid #ffffff; }\n"
         "row { color: rgba(255, 255, 255, 0.72); padding: 2px 0 8px 24px; }\n"
         "row:selected { color: #ffffff; translate-x: 4px; }\n"
         "row:disabled { color: rgba(255, 255, 255, 0.25); }\n"
         "value { color: rgba(255, 255, 255, 0.72); }\n"
         "hint { color: rgba(255, 255, 255, 0.4); }\n"
         "\n"
         "/* A minimal style earns its motion: a fade, a caret that keeps up,\n"
         "   and a caret that drifts because here it IS the selection. */\n"
         "@transition open   { 120ms ease-out; fade; }\n"
         "@transition close  { 90ms ease-out; fade; }\n"
         "@transition cursor { 100ms ease-out; }\n"
         "@animate marker    { bob 1.4s 2px; }\n"},
    };
    return v;
}

// --- the registry ------------------------------------------------------------

namespace {

std::vector<Sheet>& registry() {
    static std::vector<Sheet> v;
    return v;
}

void seedBuiltins() {
    registry().clear();
    for (const auto& b : builtinSources()) {
        Sheet s = parse(b.second, b.first, "");
        s.builtin = true;
        registry().push_back(std::move(s));
    }
}

}  // namespace

const std::vector<Sheet>& sheets() {
    if (registry().empty()) seedBuiltins();
    return registry();
}

void loadForProject(const std::string& projectDir) {
    seedBuiltins();
    if (projectDir.empty()) return;
    const std::filesystem::path dir =
        std::filesystem::path(projectDir) / "menu-styles";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        if (lower(e.path().extension().string()) != ".menustyle") continue;
        files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        if (!in) continue;
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string key = f.stem().string();
        Sheet s = parse(ss.str(), key, "menu-styles/" + f.filename().string());
        // A project file replaces the built-in of the same name: that is how
        // someone edits "neon" without losing the ability to start from it.
        bool replaced = false;
        for (Sheet& ex : registry())
            if (ex.key == key) {
                ex = s;
                replaced = true;
                break;
            }
        if (!replaced) registry().push_back(std::move(s));
    }
}

namespace {
// The staged override (see stage/unstage). At most one is active, and only for
// the duration of one bake on the UI thread.
Sheet& stagedSheet() {
    static Sheet s;
    return s;
}
bool& staged() {
    static bool b = false;
    return b;
}
}  // namespace

void stage(const Sheet& sheet) {
    stagedSheet() = sheet;
    staged() = true;
}

void unstage() { staged() = false; }

const Sheet& find(const std::string& key) {
    if (staged() && stagedSheet().key == key) return stagedSheet();
    const std::vector<Sheet>& all = sheets();
    for (const Sheet& s : all)
        if (s.key == key) return s;
    return all[0];  // "classic" - seedBuiltins guarantees it exists
}

bool save(const std::string& projectDir, const Sheet& sheet) {
    if (projectDir.empty() || sheet.key.empty()) return false;
    const std::filesystem::path dir =
        std::filesystem::path(projectDir) / "menu-styles";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path file = dir / (sheet.key + ".menustyle");
    const std::string text = write(sheet);
    {
        std::ofstream out(file, std::ios::binary);
        if (!out) return false;
        out << text;
        if (!out) return false;
    }
    Sheet stored = parse(text, sheet.key, "menu-styles/" + sheet.key + ".menustyle");
    for (Sheet& ex : registry())
        if (ex.key == stored.key) {
            ex = stored;
            return true;
        }
    registry().push_back(std::move(stored));
    return true;
}

}  // namespace menustyle
