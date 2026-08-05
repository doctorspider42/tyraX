#pragma once

#include <string>
#include <vector>

// Menu stylesheets (docs/menu-styles.md): the look of a game menu as a
// CSS-shaped text file in <project>/menu-styles/*.menustyle, loaded into a
// global registry by loadForProject() the way custom flow nodes and custom
// screen effects are (flownode.cpp / screenfx.cpp).
//
// Why a file and not fields on GameMenu: a look is worth copying between
// projects and diffing in review, and the alternative is ~30 properties x 8
// elements x 3 states as struct members threaded through JSON, codegen,
// operator== and the collaboration wire. The Menu Editor edits the sheet with
// widgets (the Material Editor arrangement - the file on disk is the truth).
//
// This module is the MODEL and the PARSER only: no layout, no rasterization,
// no GL, no ImGui, no Project dependency. menulayout.cpp turns a sheet plus a
// GameMenu into positioned boxes; menubake.cpp draws them.
//
// It is NOT a CSS implementation and the docs say so - it is a CSS-shaped
// subset chosen so that everything in it is bakeable on the host (see
// docs/menu-styles.md "What this does not do").
namespace menustyle {

// --- values -----------------------------------------------------------------

struct Color {
    unsigned char r = 0, g = 0, b = 0, a = 255;
};

inline bool operator==(const Color& a, const Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
inline bool operator!=(const Color& a, const Color& b) { return !(a == b); }

// Per-side lengths (padding / margin), in pixels.
struct Edge {
    float t = 0, r = 0, b = 0, l = 0;
};

// A box background. Gradient interpolates a -> b along `angle` (0 = left to
// right, 90 = bottom to top, 180 = top to bottom - the CSS convention).
struct Fill {
    enum Kind { None = 0, Solid = 1, Gradient = 2 };
    int kind = None;
    Color a{}, b{};
    float angle = 180.0f;
};

// One parsed declaration value. Deliberately one flat struct rather than a
// variant: every property is one of a handful of shapes, the writer has to be
// able to print any of them, and the GUI edits them field by field.
struct Value {
    float n[4] = {0, 0, 0, 0};  // lengths / numbers, in declaration order
    Color c{};                  // primary colour
    Fill fill{};                // Background only
    std::string s;              // string, url path, font name, keyword
    int i = 0;                  // enum keyword / bool
};

// --- the vocabulary ---------------------------------------------------------

// Elements a rule can target. `List` is the row container (it owns scrolling),
// `Marker` the selection caret, `Image` the MenuImage blocks.
enum class Elem {
    Panel = 0,
    Title,
    List,
    Row,
    Value,
    Description,
    Hint,
    Marker,
    Image,
    Count
};

// Row states. A state rule only ever ADDS to the normal one (the cascade),
// which is what lets a sheet say "selected rows are white" without restating
// the font.
enum State { StateNormal = 0, StateSelected = 1, StateDisabled = 2, StateCount = 3 };

enum class Prop {
    // box
    Width = 0,
    Height,
    Padding,
    Margin,
    PaddingTop,
    PaddingRight,
    PaddingBottom,
    PaddingLeft,
    MarginTop,
    MarginRight,
    MarginBottom,
    MarginLeft,
    Background,
    BackgroundImage,
    Slice,
    BorderWidth,
    BorderColor,
    Radius,
    Shadow,
    Opacity,
    // text
    Font,
    FontSize,
    TextColor,
    Align,
    LetterSpacing,
    TextTransform,
    TextShadow,
    TextOutline,
    RuleBelow,
    Content,
    // rows / list
    TranslateX,
    IconSize,
    Marker,
    MarkerSide,
    Selectable,
    RowsVisible,
    ScrollMarker,
    // values
    Display,
    BarSize,
    BarFill,
    BarTrack,
    BackgroundAnim_,
    // panel-level
    Quant,
    Gap,
    Wrap,
    Area,
    Count
};

// What shape a property's value has - the parser, the writer and the Style
// tab's widget choice all read this.
enum class Kind {
    Length,   // 12px
    Color,    // #rrggbb / rgba() / var()
    Fill,     // colour or linear-gradient()
    Edge,     // 1-4 lengths
    Url,      // url(res/...) [+ trailing keyword or 9-slice N]
    Str,      // a bare name (font)
    Enum,     // one of a keyword list
    Border,   // <length> solid <color>
    Shadow,   // <x> <y> <blur> <color>
    Bool      // yes / no
};

struct PropSpec {
    Prop prop;
    const char* name;
    Kind kind;
    const char* keywords;  // Enum only: '|'-separated, index = Value::i
    const char* doc;       // the Style tab's tooltip
};

// The single property table: parser, writer, Style tab and the docs generator
// all read it. A new property is one row here plus one case in applyDecl().
const std::vector<PropSpec>& propSpecs();
const PropSpec* propSpec(Prop p);
const char* elemName(Elem e);
const char* stateName(int state);

// --- the sheet --------------------------------------------------------------

struct Decl {
    Prop prop = Prop::Count;
    Value value;
    int line = 0;
};

struct Rule {
    std::string menu;  // "" = every menu; else the GameMenu::name it scopes to
    Elem elem = Elem::Panel;
    std::string cls;   // "" = every row/element of that kind
    int state = StateNormal;
    std::vector<Decl> decls;
    int line = 0;

    // Higher wins. Menu scope dominates, then class, then state - the same
    // order the docs promise.
    int specificity() const {
        return (menu.empty() ? 0 : 8) + (cls.empty() ? 0 : 2) +
               (state != StateNormal ? 1 : 0);
    }
};

// An animation the RUNTIME plays (nothing here is baked): the sprite-level
// layer of docs/menu-styles.md. A Transition is a REACTION to something (the
// menu opened, the cursor moved, a value changed); an Animation below is a loop
// that never stops.
struct Transition {
    enum Which {
        Open = 0,
        Close = 1,
        Cursor = 2,
        Scroll = 3,  // a scrolling list easing to its new window
        Value = 4,   // the flash a Toggle/Choice value gives when it changes
        WhichCount = 5
    };
    int which = Open;
    float seconds = 0.0f;
    int ease = 1;          // 0 linear, 1 ease-out, 2 ease-in-out
    bool fade = false;     // ramp sprite alpha
    float translateX = 0;  // px the panel slides from
    float translateY = 0;
    float scale = 0;       // extra scale it grows from (0 = no scale)
};

// A continuous animation, one per target. Everything it can do is a sprite
// property - alpha, position, a texel offset - which is why a menu can be alive
// and still cost what a still one costs.
struct Animation {
    enum Which { Selected = 0, Marker = 1, Panel = 2, WhichCount = 3 };
    enum Kind {
        None = 0,
        Pulse,  // Selected: the highlight cell breathes (amount = alpha swing)
        Bob,    // Marker: the caret slides back and forth (amount = pixels)
        Sheen   // Panel: a soft band sweeps across it (amount = band width)
    };
    int which = Selected;
    int kind = None;
    float seconds = 1.0f;  // period of one cycle
    float amount = 0.0f;
    Color color{255, 255, 255, 40};  // Sheen only
};

// How an animated background layer moves. It is a layer of its OWN (one sprite,
// one texture) rather than part of the baked panel, because baked pixels cannot
// move: a gradient that slides is a texture whose OFFSET slides, and a flame is
// a strip of frames the offset jumps through. Both are free.
struct BackgroundAnim {
    enum Mode { Off = 0, Scroll = 1, Frames = 2 };
    int mode = Off;
    std::string image;    // res/... PNG (a tiling pattern, or a frame strip)
    float scrollX = 0;    // Scroll: pixels per second
    float scrollY = 0;
    int frames = 0;       // Frames: how many are stacked in the strip
    float seconds = 1.0f;  // Frames: how long the whole loop takes
};

struct Diag {
    int line = 0;
    std::string message;
};

struct Sheet {
    std::string name;   // @style "..." - the display name
    std::string key;    // file stem, the name GameMenu::style stores
    std::string path;   // project-relative ("menu-styles/neon.menustyle")
    std::vector<std::pair<std::string, Value>> vars;  // :root --name: value
    std::vector<Rule> rules;
    std::vector<Transition> transitions;
    std::vector<Animation> animations;
    std::vector<Diag> diags;  // parse errors/warnings, with line numbers
    bool builtin = false;     // shipped with the editor, installed on demand

    bool ok() const { return diags.empty(); }
};

// --- resolved style ---------------------------------------------------------

// Every property with a concrete value: what the layout engine and the baker
// read. Defaults reproduce the Classic look (the pre-stylesheet bake), which
// is what makes a project with no sheet render byte-identically.
struct Computed {
    // box
    float width = 0;   // 0 = auto (inherit the panel's content width)
    float height = 0;  // 0 = auto (font size + the element's own padding)
    Edge padding{}, margin{};
    Fill background{};
    std::string backgroundImage;
    float slice = 0;  // 9-slice inset, 0 = stretch
    // The moving background layer (see BackgroundAnim): its own sprite under
    // everything the panel bakes.
    BackgroundAnim bgAnim;
    float borderW = 0;
    Color borderColor{};
    float radius = 0;
    bool shadow = false;
    float shadowX = 0, shadowY = 0, shadowBlur = 0;
    Color shadowColor{};
    float opacity = 1.0f;
    // text
    std::string font;  // "" = the menu's font
    float fontSize = 15.0f;
    Color color{235, 240, 245, 255};
    int align = 0;  // 0 left, 1 center, 2 right
    float letterSpacing = 0;
    bool upper = false;
    bool textShadow = false;
    float textShadowX = 0, textShadowY = 0;
    Color textShadowColor{};
    float outlineW = 0;
    Color outlineColor{};
    float ruleBelow = 0;  // separator thickness under the element
    Color ruleColor{};
    // Literal text an element draws when the menu supplies none. The button
    // hint line is the only user of it (and the only way to localize it).
    std::string content;
    // rows / list
    float translateX = 0;
    float iconSize = 0;
    std::string marker;
    int markerSide = 0;  // 0 left, 1 right
    bool selectable = true;
    int rowsVisible = 0;  // 0 = all
    std::string scrollMarker;
    // values
    int display = 0;  // 0 text, 1 bar
    float barW = 0, barH = 0;
    Color barFill{}, barTrack{};
    // panel-level
    int quant = 0;  // 0 = project default, 1 = 4bit, 2 = 8bit, 3 = 32bit
    float gap = 0;
    bool wrap = false;
    int area = 0;  // Description: 0 below the rows, 1 to the right
};

// The built-in starting point for an element (before any rule applies).
Computed defaults(Elem e);

// Applies one declaration onto a Computed. Public because the Style tab
// previews a widget's value without writing the sheet first.
void applyDecl(Computed& c, const Decl& d);

// Resolves (menu, element, class, state) through the cascade. `base` is the
// element's inherited starting point: pass defaults(elem), or - for a state -
// the already-resolved normal Computed, which is what makes `row:selected`
// additive.
Computed compute(const Sheet& sheet, const std::string& menuName, Elem elem,
                 const std::string& cls, int state, const Computed& base);

// The common case: defaults -> element rules -> class rules -> state rules.
Computed computeFor(const Sheet& sheet, const std::string& menuName, Elem elem,
                    const std::string& cls, int state);

// True when a state actually changes anything about how a row is drawn - the
// bake only spends a state cell when it does (docs/menu-styles.md).
bool statePaints(const Sheet& sheet, const std::string& menuName,
                 const std::string& cls, int state);

const Transition* transition(const Sheet& sheet, int which);
const Animation* animation(const Sheet& sheet, int which);

// --- text I/O ---------------------------------------------------------------

// Parses sheet text. Never throws and never returns nothing usable: an
// unparsable line becomes a Diag and is skipped, so a half-edited file still
// previews (the .flownode contract).
Sheet parse(const std::string& text, const std::string& key,
            const std::string& path);

// Canonical text for a sheet. write(parse(t)) must be stable - the Style tab
// saves through this, so an unstable writer would silently rewrite a user's
// file on every click (checked by the harness).
std::string write(const Sheet& sheet);

// --- registry ---------------------------------------------------------------

// Loads <projectDir>/menu-styles/*.menustyle into the registry, replacing
// whatever a previous project left. Called by project::load BEFORE menus are
// read. Missing directory = the built-ins only.
void loadForProject(const std::string& projectDir);

// Every sheet the project can name, built-ins first.
const std::vector<Sheet>& sheets();

// A sheet by key ("" or unknown = the Classic built-in, which is never null).
const Sheet& find(const std::string& key);

// Writes a sheet to <projectDir>/menu-styles/<key>.menustyle (creating the
// directory) and refreshes the registry entry. False on an I/O failure.
bool save(const std::string& projectDir, const Sheet& sheet);

// Temporarily replaces a registry entry with an unsaved edit, so every consumer
// (layout, bake, preview) sees the sheet the Style tab is editing rather than
// the file on disk. stage/unstage are a PAIR around one bake - there is no
// second lookup path a preview could take, which is what keeps "what the editor
// shows" and "what the build bakes" the same function.
void stage(const Sheet& sheet);
void unstage();

// The built-in sheets, as text. `key` is the file stem a project gets when it
// installs one; "classic" is the compatibility sheet and is what find("")
// returns.
const std::vector<std::pair<std::string, std::string>>& builtinSources();

}  // namespace menustyle
