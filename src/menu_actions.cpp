#include "menu_actions.hpp"

#include <imgui.h>

namespace menuact {

namespace {

struct State {
    std::vector<Item>* out = nullptr;  // null = drawing normally
    int invoke = -1;
    int next = 0;                      // ordinal of the next MenuItem
    std::vector<std::string> path;     // enclosing menus while harvesting
};
State g;
std::vector<State> saved;

// "Recent Projects##x" -> "Recent Projects"; "Tools###Menu" -> "Tools".
std::string visible(const char* label) {
    std::string s = label ? label : "";
    const size_t h = s.find("##");
    if (h != std::string::npos) s.resize(h);
    return s;
}

}  // namespace

void beginHarvest(std::vector<Item>* out, int invoke) {
    saved.push_back(g);
    g = State{};
    g.out = out;
    g.invoke = invoke;
}

void endHarvest() {
    if (saved.empty()) {
        g = State{};
        return;
    }
    g = saved.back();
    saved.pop_back();
}

bool harvesting() { return g.out != nullptr; }

bool BeginMenu(const char* label, bool enabled) {
    if (!g.out) return ImGui::BeginMenu(label, enabled);
    if (!enabled) return false;
    g.path.push_back(visible(label));
    return true;
}

void EndMenu() {
    if (!g.out) {
        ImGui::EndMenu();
        return;
    }
    if (!g.path.empty()) g.path.pop_back();
}

bool MenuItem(const char* label, const char* shortcut, bool selected, bool enabled) {
    if (!g.out) return ImGui::MenuItem(label, shortcut, selected, enabled);
    Item it;
    for (size_t i = 0; i < g.path.size(); ++i) it.path += (i ? " > " : "") + g.path[i];
    it.label = visible(label);
    it.shortcut = shortcut ? shortcut : "";
    it.enabled = enabled;
    it.selected = selected;
    it.ordinal = g.next++;
    const bool run = enabled && it.ordinal == g.invoke;
    g.out->push_back(std::move(it));
    return run;
}

void Hint(const std::string& text) {
    if (g.out && !g.out->empty()) g.out->back().hint = text;
}

}  // namespace menuact
