#pragma once

// The main menus ARE the editor's action registry (docs/editor-theme.md,
// "Command palette"). Menu code calls menuact::BeginMenu / MenuItem / EndMenu
// instead of ImGui's: drawn normally they ARE ImGui's (same labels, same IDs,
// same return values - --ui-script and the docs keep naming every item), and
// in a HARVEST pass they draw nothing and record each item instead - its menu
// path, label, shortcut, enabled and checked state. The command palette runs a
// harvest to list every menu command, and runs a second one with an item's
// ordinal to execute exactly the code under that item's `if`.
//
// One table that two front ends read would have meant restating ~150 items,
// their enable rules and their bodies a second time, and keeping both in step
// by hand - the hand-written palette list this replaced was that, and it had
// drifted (eight commands, most of them already in a menu under another name).
//
// A harvest is run inside a window whose items are skipped (SkipItems), so the
// plain ImGui calls sprinkled through menu code - TextDisabled, Separator,
// SeparatorText, tooltips - are no-ops during it.

#include <string>
#include <vector>

namespace menuact {

struct Item {
    std::string path;      // the enclosing menus, "View > Theme"
    std::string label;     // what the menu shows (any "##id" stripped)
    std::string shortcut;  // "" when none
    bool enabled = true;
    bool selected = false;  // a checked toggle / the current radio choice
    int ordinal = 0;        // position in the harvest; what invoke() takes
    std::string hint;       // what the palette shows instead of the path ("" = path)
};

// Harvest mode. `out` collects every item; with invoke >= 0 the item with that
// ordinal returns true from MenuItem, so its code runs. Calls nest: the
// previous state is restored by endHarvest.
void beginHarvest(std::vector<Item>* out, int invoke = -1);
void endHarvest();
bool harvesting();

// Drop-in replacements for ImGui::BeginMenu / EndMenu / MenuItem in menu code.
// While harvesting, BeginMenu enters every ENABLED menu (a disabled one hides
// nothing a palette could run) and MenuItem records + returns false (true for
// the invoked one, when it is enabled).
bool BeginMenu(const char* label, bool enabled = true);
void EndMenu();
bool MenuItem(const char* label, const char* shortcut = nullptr, bool selected = false,
              bool enabled = true);

// The command palette's dim line for the item just declared, when its menu path
// says too little ("Lighting · probes and lightmaps for scene main"). Recorded
// while harvesting, a no-op when drawing (the menu shows only the label).
void Hint(const std::string& text);

}  // namespace menuact
