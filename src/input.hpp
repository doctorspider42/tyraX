#pragma once

#include <string>
#include <vector>

// Configurable input bindings (docs/input-bindings.md).
//
// Everything the game reads goes through a named *action* ("jump", "sprint",
// "use") instead of a hardcoded pad button: the generated walkers, the menus,
// the keyboard folding and the flow-graph On Action trigger all ask "is the
// jump action pressed this frame". A project authors one or more *presets* -
// named binding sets - and the player may override single bindings in an
// in-game controls menu (a MenuEntry::RebindKey row), which persists in a save
// value like every other menu state. So: presets per project, overrides per
// player.
//
// The chain: InputMap (this file, project-wide) -> inc/input_map.gen.hpp +
// src/gen/input_map.gen.cpp (templates.cpp) -> inputPressed()/inputClicked()
// in the generated game.

// One rebindable action. `name` is the identity referenced by flow nodes and
// menu rows; `role` ties the action to a built-in engine behavior so the
// generated game keeps working while the buttons move.
struct InputAction {
    std::string name;   // "jump" - stable identity, referenced by name
    std::string label;  // "Jump" - what an in-game rebind row shows
    // Built-in behavior this action drives. RoleNone = a custom action only
    // the flow graphs read. At most one action per role reaches codegen (the
    // first one wins) - the roles are single slots in the generated runtime.
    enum Role {
        RoleNone = 0,
        RoleJump,     // walkers: jump (was BTN_JUMP)
        RoleUse,      // usable objects / pick up + drop (was BTN_USE)
        RoleThrow,    // throw a carried pickable (was BTN_THROW)
        RoleSprint,   // NEW: multiplies walk speed while held
        RoleFlyUp,    // noclip ascend (was BTN_FLY_UP)
        RoleFlyDown,  // noclip descend (was BTN_FLY_DOWN)
        RoleConfirm,  // menus: select (was Cross)
        RoleBack,     // menus: back / close (was Triangle)
        RoleMenu,     // open/close the pause menu (was Start)
        RoleAlt,      // save menu: load slot (was Circle)
        RoleMenuUp,   // menu navigation (was the d-pad)
        RoleMenuDown,
        RoleMenuLeft,
        RoleMenuRight,
        RoleMoveForward,  // stick deflection when driven from a keyboard
        RoleMoveBack,
        RoleMoveLeft,
        RoleMoveRight,
        // The driver's seat (docs/vehicles.md). Steering itself stays the
        // analog stick - an axis is not an action - but every button of the
        // drive is a role, which is what makes a rebind (or a wheel) possible
        // at all. Append-only, like everything in this enum.
        RoleVehThrottle,   // held: accelerate (was hardcoded Cross)
        RoleVehBrake,      // held: brake (was L1)
        RoleVehHandbrake,  // held: the drift button (was Circle)
        RoleVehNitrous,    // held: nitrous while the tank lasts (was R1)
        RoleVehCamera,     // clicked: cycle chase/bumper/far (was Triangle)
        RoleVehRearView,   // held: the look-back mirror (was R3)
        RoleCount,
    };
    int role = RoleNone;
    // May the player rebind it in an in-game controls menu? Off for actions
    // that would lock the player out of the menus if misbound.
    bool rebindable = true;
};

inline bool operator==(const InputAction& a, const InputAction& b) {
    return a.name == b.name && a.label == b.label && a.role == b.role &&
           a.rebindable == b.rebindable;
}

// What one action is bound to inside one preset. All three may be set at once
// (a pad button AND a key AND a mouse button all trigger the action); an
// unset slot is pad "" / key 0 / mouse 0.
struct InputBinding {
    std::string action;  // InputAction::name
    std::string pad;     // Tyra PadButtons field name ("Cross"); "" = none
    int key = 0;         // USB HID usage code (usb.org HID Usage Tables)
    int mouse = 0;       // 0 none, 1 left, 2 right, 3 middle
};

inline bool operator==(const InputBinding& a, const InputBinding& b) {
    return a.action == b.action && a.pad == b.pad && a.key == b.key &&
           a.mouse == b.mouse;
}

// A named binding set ("Default", "Southpaw", "Keyboard"). Actions with no
// entry here are simply unbound in this preset.
struct InputPreset {
    std::string name = "Default";
    std::vector<InputBinding> bindings;

    const InputBinding* find(const std::string& action) const {
        for (const InputBinding& b : bindings)
            if (b.action == action) return &b;
        return nullptr;
    }
    InputBinding& at(const std::string& action) {
        for (InputBinding& b : bindings)
            if (b.action == action) return b;
        bindings.push_back(InputBinding{action, "", 0, 0});
        return bindings.back();
    }
};

inline bool operator==(const InputPreset& a, const InputPreset& b) {
    return a.name == b.name && a.bindings == b.bindings;
}

// The project's whole input configuration (Tools > Input Map). Project-wide
// like fonts and menus, part of undo/redo through the normal commitChange().
struct InputMap {
    std::vector<InputAction> actions;
    std::vector<InputPreset> presets;
    // The preset the game boots with. A menu Choice row bound to
    // "Input preset" switches presets at runtime.
    int activePreset = 0;
    // Master switch for the in-game rebind rows: off makes every RebindKey
    // row read-only (the row still shows the current binding).
    bool allowRebind = true;

    const InputAction* findAction(const std::string& name) const {
        for (const InputAction& a : actions)
            if (a.name == name) return &a;
        return nullptr;
    }
    int actionIndex(const std::string& name) const {
        for (size_t i = 0; i < actions.size(); ++i)
            if (actions[i].name == name) return (int)i;
        return -1;
    }
    // First action carrying `role`, or -1. Roles are single slots in the
    // generated runtime, so a duplicate role never reaches the game.
    int roleIndex(int role) const {
        for (size_t i = 0; i < actions.size(); ++i)
            if (actions[i].role == role) return (int)i;
        return -1;
    }
    const InputPreset* activeOrNull() const {
        if (presets.empty()) return nullptr;
        const int i = (activePreset < 0 || activePreset >= (int)presets.size())
                          ? 0
                          : activePreset;
        return &presets[i];
    }
    // The binding an action has in the active preset (all-unset when absent).
    InputBinding resolve(const std::string& action) const {
        if (const InputPreset* p = activeOrNull())
            if (const InputBinding* b = p->find(action)) return *b;
        return InputBinding{action, "", 0, 0};
    }
};

inline bool operator==(const InputMap& a, const InputMap& b) {
    return a.actions == b.actions && a.presets == b.presets &&
           a.activePreset == b.activePreset && a.allowRebind == b.allowRebind;
}

// ---------------------------------------------------------------------------
// Pad buttons, in Tyra::PadButtons declaration order (pad.hpp). The index is
// what the generated tables store; the name is what the .tyra file and
// controls.hpp use, so reordering this list would silently remap saved
// projects - append only.
inline const char* const kPadButtonNames[16] = {
    "Cross", "Square", "Triangle", "Circle", "DpadUp", "DpadDown",
    "DpadLeft", "DpadRight", "L1", "L2", "L3", "R1", "R2", "R3",
    "Start", "Select"};

inline int padButtonIndex(const std::string& name) {
    for (int i = 0; i < 16; ++i)
        if (name == kPadButtonNames[i]) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Keyboard keys offered by the editor's pickers and by the in-game rebind
// rows: USB HID usage codes with a printable label. Not the full HID table -
// the keys a PS2 game plausibly binds (the ps2kbd driver only speaks the HID
// boot protocol anyway).
struct InputKeyName {
    int code;
    const char* label;
};

inline const std::vector<InputKeyName>& inputKeyNames() {
    static const std::vector<InputKeyName> v = {
        {0x04, "A"}, {0x05, "B"}, {0x06, "C"}, {0x07, "D"}, {0x08, "E"},
        {0x09, "F"}, {0x0A, "G"}, {0x0B, "H"}, {0x0C, "I"}, {0x0D, "J"},
        {0x0E, "K"}, {0x0F, "L"}, {0x10, "M"}, {0x11, "N"}, {0x12, "O"},
        {0x13, "P"}, {0x14, "Q"}, {0x15, "R"}, {0x16, "S"}, {0x17, "T"},
        {0x18, "U"}, {0x19, "V"}, {0x1A, "W"}, {0x1B, "X"}, {0x1C, "Y"},
        {0x1D, "Z"},
        {0x1E, "1"}, {0x1F, "2"}, {0x20, "3"}, {0x21, "4"}, {0x22, "5"},
        {0x23, "6"}, {0x24, "7"}, {0x25, "8"}, {0x26, "9"}, {0x27, "0"},
        {0x28, "Enter"}, {0x29, "Esc"}, {0x2A, "Backspace"}, {0x2B, "Tab"},
        {0x2C, "Space"}, {0x2D, "Minus"}, {0x2E, "Equals"},
        {0x3A, "F1"}, {0x3B, "F2"}, {0x3C, "F3"}, {0x3D, "F4"}, {0x3E, "F5"},
        {0x3F, "F6"}, {0x40, "F7"}, {0x41, "F8"}, {0x42, "F9"}, {0x43, "F10"},
        {0x44, "F11"}, {0x45, "F12"},
        {0x4F, "Right"}, {0x50, "Left"}, {0x51, "Down"}, {0x52, "Up"},
        {0xE0, "Left Ctrl"}, {0xE1, "Left Shift"}, {0xE2, "Left Alt"},
        {0xE4, "Right Ctrl"}, {0xE5, "Right Shift"}, {0xE6, "Right Alt"},
    };
    return v;
}

inline const char* inputKeyLabel(int code) {
    for (const InputKeyName& k : inputKeyNames())
        if (k.code == code) return k.label;
    return "";
}

inline int inputKeyCode(const std::string& label) {
    for (const InputKeyName& k : inputKeyNames())
        if (label == k.label) return k.code;
    return 0;
}

inline const char* inputMouseLabel(int mouse) {
    return mouse == 1   ? "Mouse Left"
           : mouse == 2 ? "Mouse Right"
           : mouse == 3 ? "Mouse Middle"
                        : "";
}

// ---------------------------------------------------------------------------
// The rebind code space: one dense index per physical input a player can pick
// in an in-game rebind row. The bound code is stored as this index in the
// row's save value (0 = "the project's preset binding"), and the *same* table
// drives codegen, so editor and game always agree on what code 37 means.
// Append only - the numbers live in players' memory-card saves.
struct InputCode {
    const char* label;
    int pad;    // kPadButtonNames index, -1 = not a pad button
    int key;    // USB HID code, 0 = not a key
    int mouse;  // 1/2/3, 0 = not a mouse button
};

inline const std::vector<InputCode>& inputCodes() {
    static const std::vector<InputCode> v = [] {
        std::vector<InputCode> out;
        out.push_back({"Default", -1, 0, 0});  // index 0: use the preset
        for (int i = 0; i < 16; ++i)
            out.push_back({kPadButtonNames[i], i, 0, 0});
        for (const InputKeyName& k : inputKeyNames())
            out.push_back({k.label, -1, k.code, 0});
        out.push_back({"Mouse Left", -1, 0, 1});
        out.push_back({"Mouse Right", -1, 0, 2});
        out.push_back({"Mouse Middle", -1, 0, 3});
        return out;
    }();
    return v;
}

// Human-readable binding, e.g. "Cross / Space" or "(unbound)".
inline std::string inputBindingLabel(const InputBinding& b) {
    std::string s = b.pad;
    if (b.key != 0) {
        const char* k = inputKeyLabel(b.key);
        if (*k == '\0') return s.empty() ? std::string("(unbound)") : s;
        s += s.empty() ? k : std::string(" / ") + k;
    }
    if (b.mouse != 0) {
        const char* m = inputMouseLabel(b.mouse);
        s += s.empty() ? m : std::string(" / ") + m;
    }
    return s.empty() ? std::string("(unbound)") : s;
}
