#include "uiscript.hpp"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <unordered_map>

#include <imgui.h>
#include <imgui_internal.h>

// ---------------------------------------------------------------------------
// The item registry.
//
// These four functions are declared `extern` by imgui_internal.h under
// IMGUI_ENABLE_TEST_ENGINE and are deliberately left for somebody else to
// define - normally imgui_test_engine. We define them, which buys the entire
// "what is on screen and where" question for the cost of a branch per widget
// (ImGui only calls them when `g.TestEngineHookItems` is set, and we only set it
// while a script runs).
//
// ItemAdd comes first with the bounding box, ItemInfo later with the label, both
// keyed by the item's ImGuiID - so the two are joined by id.
// ---------------------------------------------------------------------------
namespace {

std::vector<uiscript::Item> g_items;
std::unordered_map<uint32_t, size_t> g_byId;
// Labels that arrived BEFORE their bounding box. ImGui usually calls ItemAdd
// first, but a TAB ITEM is the other way round (imgui_widgets.cpp: TabItemEx
// reports its label up front, then adds the box further down), so a tab used to
// end up in the map unnamed - and every tabbed panel in the editor was
// unreachable from a script by name. Holding the label until the box shows up
// fixes the whole class.
std::unordered_map<uint32_t, std::pair<std::string, ImGuiItemStatusFlags>> g_pending;
bool g_enabled = false;

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

/** The part of an ImGui label a person actually sees: everything before "##"
 * (which is also everything before "###"). "Mix##delay" is typed as "Mix". */
std::string displayLabel(const char* label) {
    if (!label) return "";
    const std::string s(label);
    const size_t hash = s.find("##");
    return hash == std::string::npos ? s : s.substr(0, hash);
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool startsWithCI(const std::string& hay, const std::string& needle) {
    return lower(hay).rfind(lower(needle), 0) == 0;
}

}  // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb,
                                 const ImGuiLastItemData* item_data) {
    if (!g_enabled || id == 0) return;
    (void)item_data;
    uiscript::Item it;
    it.id = (uint32_t)id;
    it.x0 = bb.Min.x;
    it.y0 = bb.Min.y;
    it.x1 = bb.Max.x;
    it.y1 = bb.Max.y;
    if (ctx && ctx->CurrentWindow && ctx->CurrentWindow->Name) {
        it.window = ctx->CurrentWindow->Name;
        it.windowId = (uint32_t)ctx->CurrentWindow->ID;
        // ImGui registers each window as an item under the window's own id.
        it.isWindow = ctx->CurrentWindow->ID == id;
    }
    // A label reported ahead of the box (tab items - see g_pending).
    auto pend = g_pending.find(it.id);
    if (pend != g_pending.end()) {
        it.label = displayLabel(pend->second.first.c_str());
        const ImGuiItemStatusFlags f = pend->second.second;
        it.checkable = (f & ImGuiItemStatusFlags_Checkable) != 0;
        it.checked = (f & ImGuiItemStatusFlags_Checked) != 0;
        it.openable = (f & ImGuiItemStatusFlags_Openable) != 0;
        it.opened = (f & ImGuiItemStatusFlags_Opened) != 0;
        it.inputable = (f & ImGuiItemStatusFlags_Inputable) != 0;
        g_pending.erase(pend);
    }
    auto found = g_byId.find(it.id);
    if (found != g_byId.end()) {
        // Same id twice in a frame (a re-submitted item): keep the newest box
        // but do not lose a label ItemInfo already gave us.
        const std::string label = g_items[found->second].label;
        g_items[found->second] = it;
        g_items[found->second].label = label;
        return;
    }
    g_byId.emplace(it.id, g_items.size());
    g_items.push_back(it);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label,
                                  ImGuiItemStatusFlags flags) {
    if (!g_enabled || id == 0) return;
    (void)ctx;
    auto found = g_byId.find((uint32_t)id);
    if (found == g_byId.end()) {
        // No box yet: remember it for the ItemAdd that follows (tab items).
        if (label) g_pending[(uint32_t)id] = {label, flags};
        return;
    }
    uiscript::Item& it = g_items[found->second];
    it.label = displayLabel(label);
    it.checkable = (flags & ImGuiItemStatusFlags_Checkable) != 0;
    it.checked = (flags & ImGuiItemStatusFlags_Checked) != 0;
    it.openable = (flags & ImGuiItemStatusFlags_Openable) != 0;
    it.opened = (flags & ImGuiItemStatusFlags_Opened) != 0;
    it.inputable = (flags & ImGuiItemStatusFlags_Inputable) != 0;
}

void ImGuiTestEngineHook_Log(ImGuiContext* ctx, const char* fmt, ...) {
    // ImGui routes its debug log here when asked to; we have our own log.
    (void)ctx;
    (void)fmt;
}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* ctx, ImGuiID id) {
    (void)ctx;
    auto found = g_byId.find((uint32_t)id);
    if (found == g_byId.end()) return nullptr;
    const std::string& s = g_items[found->second].label;
    return s.empty() ? nullptr : s.c_str();
}

namespace uiscript {

void setEnabled(bool on) {
    g_enabled = on;
    if (ImGuiContext* g = ImGui::GetCurrentContext()) g->TestEngineHookItems = on;
    if (!on) {
        g_items.clear();
        g_byId.clear();
        g_pending.clear();
    }
}

bool enabled() { return g_enabled; }

void beginFrame() {
    g_items.clear();
    g_byId.clear();
    g_pending.clear();
}

const std::vector<Item>& items() { return g_items; }

const Item* find(const std::string& target, bool clickable) {
    const std::string t = trim(target);
    if (t.empty()) return nullptr;

    // Widgets ImGui never reports a label for - every combo, because BeginCombo
    // does not call the ItemInfo hook - can still be found by recomputing the id
    // ImGui itself would have produced: the widget's id is the label hashed with
    // its window's id as the seed. Only exact labels resolve this way (a hash
    // has no prefixes), and only for widgets submitted at a window's own scope,
    // which is where combos in this editor live.
    auto byIdHash = [&](const std::string& wantWindow,
                        const std::string& wantLabel) -> const Item* {
        for (const Item& it : g_items) {
            if (it.windowId == 0 || it.isWindow) continue;
            if (!wantWindow.empty() && !startsWithCI(it.window, wantWindow))
                continue;
            if (it.id == (uint32_t)ImHashStr(wantLabel.c_str(), 0, it.windowId))
                return &it;
        }
        return nullptr;
    };

    // One "window/label" split attempt.
    auto trySplit = [&](const std::string& wantWindow,
                        const std::string& wantLabel) -> const Item* {
        auto usable = [&](const Item& it) {
            if (clickable && it.isWindow) return false;
            return wantWindow.empty() || startsWithCI(it.window, wantWindow);
        };
        // Exact label first, then a prefix - so a menu entry authored as
        // "Remote Pad..." is reachable as "Remote Pad" without the ellipsis.
        for (const Item& it : g_items)
            if (usable(it) && lower(it.label) == lower(wantLabel)) return &it;
        for (const Item& it : g_items)
            if (usable(it) && !it.label.empty() &&
                startsWithCI(it.label, wantLabel))
                return &it;
        return nullptr;
    };

    // A WINDOW name can itself contain '/': ImGui child regions are registered
    // as "Project/##objects_DC0BCE04", and that is exactly how `dump` and the
    // failure message spell them. Splitting on the FIRST '/' therefore could not
    // resolve the names the tool prints itself - it looked for a label called
    // "##objects_DC0BCE04/the-cube". So every split point is tried, LONGEST
    // window prefix first, and the first one that resolves wins. Strictly more
    // permissive than the old single split, so a target that worked still does;
    // it additionally makes a label containing '/' reachable.
    std::vector<size_t> slashes;
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i] == '/') slashes.push_back(i);
    for (auto it = slashes.rbegin(); it != slashes.rend(); ++it)
        if (const Item* hit = trySplit(trim(t.substr(0, *it)),
                                       trim(t.substr(*it + 1))))
            return hit;
    // No window qualifier at all.
    if (const Item* hit = trySplit("", t)) return hit;

    // Nothing carried that label: try the id hash (unnamed widgets - combos).
    for (auto it = slashes.rbegin(); it != slashes.rend(); ++it)
        if (const Item* hit = byIdHash(trim(t.substr(0, *it)),
                                      trim(t.substr(*it + 1))))
            return hit;
    if (const Item* hit = byIdHash("", t)) return hit;

    // Last resort: the WINDOW itself (ImGui registers each window as an item),
    // so `expect "Remote Pad"` answers before anything inside it is known. Never
    // for a click - see the note on the declaration.
    if (!clickable)
        for (const Item& item : g_items)
            if (lower(item.window) == lower(t)) return &item;
    return nullptr;
}

std::string dumpText() {
    std::ostringstream out;
    out << g_items.size() << " item(s) on screen\n";
    for (const Item& it : g_items) {
        out << "  [" << it.window << "] " << (it.label.empty() ? "-" : it.label)
            << "  @ " << (int)it.x0 << "," << (int)it.y0 << " " << (int)(it.x1 - it.x0)
            << "x" << (int)(it.y1 - it.y0);
        if (it.checkable) out << (it.checked ? "  [checked]" : "  [unchecked]");
        if (it.openable) out << (it.opened ? "  [open]" : "  [closed]");
        if (it.inputable) out << "  [input]";
        out << "\n";
    }
    return out.str();
}

// ------------------------------------------------------------------ parser ---

namespace {

enum : unsigned char { kInQuotes = 1, kQuoteDelim = 2 };

/** Marks, for every character of `s`, whether it sits INSIDE a quoted run and
 * whether it is one of the quote characters delimiting one.
 *
 * All three places that scan a script for structure - the `;` / newline
 * statement split, the `#` comment strip, and the whitespace tokenizer - go
 * through this, because they have to agree that a quoted widget name is opaque.
 * They did not: `#` was cut from a line before quotes were considered at all,
 * and ImGui ids routinely contain `##` (`"Project/##objects_DC0BCE04"`), so the
 * target was silently truncated to `Project/` - which then prefix-matched a
 * DIFFERENT widget and clicked it. A script that asserts nothing and still
 * reports success is the one failure mode a test tool must not have. A label
 * containing `;` was cut the same way.
 *
 * A double quote always opens a run. A single quote opens one only at a token
 * boundary (start of line, or after whitespace or a separator), so an
 * apostrophe inside a label - "Player's hat" - stays an ordinary character
 * while `click 'Remote Pad'` works. Both spellings are used in the docs. */
std::vector<unsigned char> scanQuotes(const std::string& s) {
    std::vector<unsigned char> f(s.size(), 0);
    char open = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (open) {
            if (c == open) {
                f[i] = kQuoteDelim;
                open = 0;
            } else {
                f[i] = kInQuotes;
            }
            continue;
        }
        const bool boundary = i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t' ||
                              s[i - 1] == '\n' || s[i - 1] == ';';
        if (c == '"' || (c == '\'' && boundary)) {
            open = c;
            f[i] = kQuoteDelim;
        }
    }
    return f;
}

/** Splits a command line into tokens, honoring quotes so a target with spaces
 * can be written `click "Live Link"` or `click 'Live Link'`. */
std::vector<std::string> tokenize(const std::string& line) {
    const std::vector<unsigned char> f = scanQuotes(line);
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;  // this token had a quoted run, so "" is still a token
    for (size_t i = 0; i < line.size(); ++i) {
        if (f[i] & kQuoteDelim) {
            quoted = true;
            continue;
        }
        const char c = line[i];
        if (!(f[i] & kInQuotes) && (c == ' ' || c == '\t')) {
            if (!cur.empty() || quoted) {
                out.push_back(cur);
                cur.clear();
                quoted = false;
            }
            continue;
        }
        cur += c;
    }
    if (!cur.empty() || quoted) out.push_back(cur);
    return out;
}

bool toNumber(const std::string& s, double& out) {
    try {
        out = std::stod(s);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

bool parseScript(const std::string& text, std::vector<Step>& out,
                 std::string& err) {
    out.clear();
    err.clear();

    // Quote-aware: a `;` inside a quoted widget name is part of the name, not a
    // statement separator (see scanQuotes).
    const std::vector<unsigned char> qf = scanQuotes(text);
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if ((c == '\n' || c == ';') && !(qf[i] & kInQuotes)) {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    lines.push_back(cur);

    int lineNo = 0;
    auto fail = [&](const std::string& why) {
        err = "line " + std::to_string(lineNo) + ": " + why;
        out.clear();
        return false;
    };

    for (const std::string& raw : lines) {
        ++lineNo;
        std::string line = raw;
        // A `#` starts a comment only OUTSIDE quotes: an ImGui id is full of
        // them ("Flow Graph/##canvas"), and cutting the line there truncated
        // the target instead of commenting anything.
        const std::vector<unsigned char> lf = scanQuotes(line);
        for (size_t i = 0; i < line.size(); ++i)
            if (line[i] == '#' && !(lf[i] & kInQuotes)) {
                line = line.substr(0, i);
                break;
            }
        line = trim(line);
        if (line.empty()) continue;
        const std::vector<std::string> tok = tokenize(line);
        if (tok.empty()) continue;
        const std::string cmd = lower(tok[0]);

        Step s;
        s.source = line;
        auto needArg = [&](size_t count) { return tok.size() == count; };

        if (cmd == "click" || cmd == "rightclick" || cmd == "right-click" ||
            cmd == "doubleclick" || cmd == "hover" ||
            cmd == "expect" || cmd == "expect-not" || cmd == "expectnot" ||
            cmd == "expect-checked" || cmd == "expect-unchecked") {
            if (!needArg(2)) return fail(cmd + " needs one target");
            s.kind = cmd == "click"             ? Step::Click
                     : cmd == "rightclick"      ? Step::RightClick
                     : cmd == "right-click"     ? Step::RightClick
                     : cmd == "doubleclick"     ? Step::DoubleClick
                     : cmd == "hover"           ? Step::Hover
                     : cmd == "expect"          ? Step::Expect
                     : cmd == "expect-checked"  ? Step::ExpectChecked
                     : cmd == "expect-unchecked" ? Step::ExpectUnchecked
                                                 : Step::ExpectNot;
            s.arg = tok[1];
        } else if (cmd == "hold") {
            if (tok.size() < 2 || tok.size() > 3)
                return fail("hold needs a target and optional seconds");
            s.kind = Step::HoldClick;
            s.arg = tok[1];
            s.seconds = 0.5;
            if (tok.size() == 3 && !toNumber(tok[2], s.seconds))
                return fail("bad duration '" + tok[2] + "'");
        } else if (cmd == "drag") {
            if (!needArg(4)) return fail("drag needs a target and dx dy");
            double dx = 0, dy = 0;
            if (!toNumber(tok[2], dx) || !toNumber(tok[3], dy))
                return fail("drag dx/dy must be numbers");
            s.kind = Step::Drag;
            s.arg = tok[1];
            s.dx = (float)dx;
            s.dy = (float)dy;
        } else if (cmd == "wheel" || cmd == "scroll") {
            if (!needArg(3)) return fail("wheel needs a target and notches");
            double dy = 0;
            if (!toNumber(tok[2], dy)) return fail("wheel notches must be a number");
            s.kind = Step::Wheel;
            s.arg = tok[1];
            s.dy = (float)dy;
        } else if (cmd == "key") {
            if (!needArg(2)) return fail("key needs a chord, e.g. ctrl+n");
            s.kind = Step::Key;
            s.arg = tok[1];
        } else if (cmd == "text" || cmd == "type") {
            // Everything after the verb, verbatim (quotes already stripped).
            if (tok.size() < 2) return fail("text needs something to type");
            s.kind = Step::Text;
            for (size_t i = 1; i < tok.size(); ++i)
                s.arg += (i > 1 ? " " : "") + tok[i];
        } else if (cmd == "wait" || cmd == "sleep") {
            if (!needArg(2)) return fail("wait needs seconds");
            if (!toNumber(tok[1], s.seconds) || s.seconds < 0)
                return fail("bad duration '" + tok[1] + "'");
            s.kind = Step::Wait;
        } else if (cmd == "frames") {
            double n = 0;
            if (!needArg(2) || !toNumber(tok[1], n) || n < 1)
                return fail("frames needs a count");
            s.kind = Step::Frames;
            s.n = (int)n;
        } else if (cmd == "shot") {
            if (!needArg(2)) return fail("shot needs a .png path");
            s.kind = Step::Shot;
            s.arg = tok[1];
        } else if (cmd == "dump") {
            s.kind = Step::Dump;
        } else if (cmd == "log") {
            s.kind = Step::Log;
            for (size_t i = 1; i < tok.size(); ++i)
                s.arg += (i > 1 ? " " : "") + tok[i];
        } else if (cmd == "quit" || cmd == "exit") {
            s.kind = Step::Quit;
        } else {
            return fail("unknown command '" + tok[0] + "'");
        }
        out.push_back(s);
    }
    // A script that never quits would leave the editor up forever, which for an
    // unattended run means a hung test rather than a failing one.
    bool hasQuit = false;
    for (const Step& s : out) hasQuit = hasQuit || s.kind == Step::Quit;
    if (!hasQuit) {
        Step q;
        q.kind = Step::Quit;
        q.source = "(implicit quit)";
        out.push_back(q);
    }
    return true;
}

bool parseChord(const std::string& chord, int& key, bool& ctrl, bool& shift,
                bool& alt) {
    key = ImGuiKey_None;
    ctrl = shift = alt = false;
    // "ctrl+shift+n": modifiers first, the key last.
    std::vector<std::string> parts;
    std::string cur;
    for (char c : chord) {
        if (c == '+' && !cur.empty()) {
            parts.push_back(lower(cur));
            cur.clear();
        } else if (c != '+') {
            cur += c;
        }
    }
    if (!cur.empty()) parts.push_back(lower(cur));
    if (parts.empty()) return false;

    struct Named {
        const char* name;
        int key;
    };
    static const Named kNamed[] = {
        {"escape", ImGuiKey_Escape},      {"esc", ImGuiKey_Escape},
        {"enter", ImGuiKey_Enter},        {"return", ImGuiKey_Enter},
        {"tab", ImGuiKey_Tab},            {"space", ImGuiKey_Space},
        {"backspace", ImGuiKey_Backspace}, {"delete", ImGuiKey_Delete},
        {"del", ImGuiKey_Delete},         {"insert", ImGuiKey_Insert},
        {"home", ImGuiKey_Home},          {"end", ImGuiKey_End},
        {"pageup", ImGuiKey_PageUp},      {"pagedown", ImGuiKey_PageDown},
        {"up", ImGuiKey_UpArrow},         {"down", ImGuiKey_DownArrow},
        {"left", ImGuiKey_LeftArrow},     {"right", ImGuiKey_RightArrow},
        {"minus", ImGuiKey_Minus},        {"equal", ImGuiKey_Equal},
        {"comma", ImGuiKey_Comma},        {"period", ImGuiKey_Period},
    };

    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string& p = parts[i];
        const bool last = i + 1 == parts.size();
        if (!last || parts.size() == 1) {
            if (p == "ctrl" || p == "control") {
                ctrl = true;
                continue;
            }
            if (p == "shift") {
                shift = true;
                continue;
            }
            if (p == "alt") {
                alt = true;
                continue;
            }
            if (!last) return false;  // an unknown modifier
        }
        // The key itself.
        if (p.size() == 1 && p[0] >= 'a' && p[0] <= 'z')
            key = ImGuiKey_A + (p[0] - 'a');
        else if (p.size() == 1 && p[0] >= '0' && p[0] <= '9')
            key = ImGuiKey_0 + (p[0] - '0');
        else if (p.size() >= 2 && p[0] == 'f' && std::isdigit((unsigned char)p[1])) {
            const int n = std::atoi(p.c_str() + 1);
            if (n < 1 || n > 12) return false;
            key = ImGuiKey_F1 + (n - 1);
        } else {
            for (const Named& nm : kNamed)
                if (p == nm.name) key = nm.key;
        }
    }
    return key != ImGuiKey_None;
}

}  // namespace uiscript
