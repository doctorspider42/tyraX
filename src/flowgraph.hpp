#pragma once

#include <string>
#include <vector>

// Visual logic graph (CryEngine-FlowGraph-like). Every scene object can carry
// its own graph; the graph is stored inside the object in the .tyra file and
// compiled into src/scripts/flow_graph.gen.cpp on every build (one script
// class per object graph).
//
// Link kinds:
//  - exec links (trigger "then" -> action "do"): execution flow
//  - object links (square pins): pass an object reference between nodes
//  - position links (triangle pins): pass XYZ coordinates between nodes
//  - bool links (circle pins): per-frame conditions for the logic gates
//  - text links (circle pins): strings for Log Message / Set Save Text.
// Object-referencing nodes resolve their target in this order:
//  1. incoming object link  ->  the source node's resolved object
//  2. explicit object name (str)
//  3. the object owning the graph ("self")
// Position inputs override a node's own X/Y/Z params when linked.
// Copying an object copies its graph; self-references follow the copy, so a
// graph built around "self" works as a reusable component.

struct FlowNode {
    int id = 0;
    std::string type;      // key into flowNodeTypes()
    float pos[2] = {0, 0};  // node editor position
    std::string str;       // string param (object name / text / button / track)
    std::string str2;      // second string param (Set Save Text: the value)
    float num[4] = {0, 0, 0, 0};  // numeric params (radius, color, volume...)
};

enum FlowLinkKind {
    FlowLinkExec = 0,    // trigger "then" -> action "do"
    FlowLinkObject = 1,  // object id out -> object id in
    FlowLinkPos = 2,     // position out -> position in
    FlowLinkBool = 3,    // boolean value out -> boolean value in (logic gates)
    FlowLinkText = 4,    // text value out -> text value in (Log / Save text)
};

struct FlowLink {
    int id = 0;
    int fromNode = 0;  // output side
    int toNode = 0;    // input side
    int kind = FlowLinkExec;
};

struct FlowGraph {
    std::vector<FlowNode> nodes;
    std::vector<FlowLink> links;
    int nextId = 1;

    bool empty() const { return nodes.empty() && links.empty(); }
};

inline bool operator==(const FlowNode& a, const FlowNode& b) {
    return a.id == b.id && a.type == b.type && a.pos[0] == b.pos[0] &&
           a.pos[1] == b.pos[1] && a.str == b.str && a.str2 == b.str2 &&
           a.num[0] == b.num[0] && a.num[1] == b.num[1] &&
           a.num[2] == b.num[2] && a.num[3] == b.num[3];
}

inline bool operator==(const FlowLink& a, const FlowLink& b) {
    return a.id == b.id && a.fromNode == b.fromNode && a.toNode == b.toNode &&
           a.kind == b.kind;
}

inline bool operator==(const FlowGraph& a, const FlowGraph& b) {
    return a.nextId == b.nextId && a.nodes == b.nodes && a.links == b.links;
}

// ---------------------------------------------------------------------------

enum class FlowParamKind {
    None,
    Text,
    ObjectName,
    Button,
    Color,
    MusicTrack,
    SoundTrack,
    SceneName,
    SaveValue,  // name of a Project::saveValues entry
    MenuName,   // name of a Project::menus entry
    VarName,    // name of a flow variable (free text; created on first use)
    SaveText,   // name of a Project::saveTexts entry
    GradingName,  // name of a Project::gradings preset ("" = neutral/off)
};

struct FlowNodeType {
    const char* key;
    const char* title;
    const char* category;  // add-menu submenu ("Triggers", "Object", ...)
    bool trigger;  // true = has exec output, false = has exec input (action)
    FlowParamKind strKind;   // meaning of FlowNode::str
    int numCount;            // how many of num[] are used
    const char* numLabels[4];
    FlowParamKind numKind;   // Color = show a color picker for num[0..2]
    bool idIn;   // accepts an object id from a data link (object-param nodes)
    bool idOut;  // exposes its resolved object as an id output
    bool posIn = false;   // accepts XYZ coordinates from a position link
    bool posOut = false;  // exposes XYZ coordinates as a position output
    bool pure = false;    // data-only node: no exec pins, never "runs"
    bool boolIn = false;  // accepts boolean value(s) from bool link(s)
    bool boolOut = false; // exposes a per-frame boolean condition as a bool output
    bool textIn = false;  // accepts text value(s) from text link(s)
    bool textOut = false; // exposes a text value as a text output
    // action that ALSO has an exec output fired later (Delay's "after >")
    bool execThrough = false;
};

inline const std::vector<FlowNodeType>& flowNodeTypes() {
    static const std::vector<FlowNodeType> types = {
        // Triggers. Some expose their watched object as an object output
        // (Near/Used/Anim Finished); the per-frame conditions for the logic
        // gates come from the pure bool sources (Value At Least, Get Bool,
        // Is Visible, ...) instead.
        {"OnStart", "On Start", "Triggers", true, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false},
        {"OnButton", "On Button", "Triggers", true, FlowParamKind::Button, 0, {},
         FlowParamKind::None, false, false},
        {"NearObject", "Near Object", "Triggers", true, FlowParamKind::ObjectName, 1,
         {"Radius"}, FlowParamKind::None, true, true},
        // Fires when the player presses BTN_USE (controls.hpp) while looking
        // at the target object up close. The object must be marked "usable".
        {"OnUsed", "On Used", "Triggers", true, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None, true, true},
        {"EverySeconds", "Every N Seconds", "Triggers", true, FlowParamKind::None, 1,
         {"Seconds"}, FlowParamKind::None, false, false},
        // Fires the frame the watched object's animation clip reaches its
        // last frame (one-shot clips: once; looping clips: every wrap).
        // Only animated (.glb) model objects ever fire it.
        {"OnAnimFinished", "On Animation Finished", "Triggers", true,
         FlowParamKind::ObjectName, 0, {}, FlowParamKind::None, true, true},
        // Time: exec passes through after N seconds (fractions fine). Armed
        // by its exec input, fires its "after" exec output once the timer
        // runs out; re-arming while counting restarts the timer.
        {"Delay", "Delay", "Time", false, FlowParamKind::None, 1, {"Seconds"},
         FlowParamKind::None, false, false, false, false, false, false, false,
         false, false, true},
        // Self: pure data node exposing the graph's owner as an object output
        // (and its live position). Object params already default to self when
        // empty; this makes the reference explicit and wireable into any pin.
        {"Self", "Self", "Object", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, true, false, true, true},
        // Object actions (id in = target, id out = the same target)
        {"ShowObject", "Show Object", "Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None, true, true},
        {"HideObject", "Hide Object", "Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None, true, true},
        {"ToggleObject", "Toggle Object", "Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None, true, true},
        {"MoveObjectBy", "Move Object By", "Object", false, FlowParamKind::ObjectName, 3,
         {"dX", "dY", "dZ"}, FlowParamKind::None, true, true},
        // Glides the target toward X/Y/Z (or a linked position, re-read every
        // frame) at Speed units/s until it arrives. Re-triggering re-arms it.
        {"MoveObjectTo", "Move Object To", "Object", false, FlowParamKind::ObjectName, 4,
         {"X", "Y", "Z", "Speed"}, FlowParamKind::None, true, true, true, false},
        {"SetObjectColor", "Set Object Color", "Object", false, FlowParamKind::ObjectName,
         3, {}, FlowParamKind::Color, true, true},
        // Position plumbing: Get Position is a pure data node (no exec pins);
        // Set Object Position uses its X/Y/Z params unless a position link
        // feeds it. Both pass the object through and expose the position.
        {"GetPosition", "Get Position", "Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None, true, true, false, true, true},
        // Pure bool getter: is the target object visible this frame?
        {"IsVisible", "Is Visible", "Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None, true, true, false, false, true, false, true},
        {"SetPosition", "Set Object Position", "Object", false, FlowParamKind::ObjectName,
         3, {"X", "Y", "Z"}, FlowParamKind::None, true, true, true, true, false},
        // Animation (animated .glb model objects; no-ops on anything else).
        // Play Animation: str = clip name ("" = the model's first clip); the
        // target object comes from an object link or defaults to self (the
        // str slot holds the clip, not an object name). Loop: 1 = loop,
        // 0 = play once. Speed: playback multiplier (0 = authored default).
        // Fade: crossfade seconds from the current pose (0 = instant).
        {"PlayAnimation", "Play Animation", "Animation", false, FlowParamKind::Text,
         3, {"Loop", "Speed", "Fade"}, FlowParamKind::None, true, true},
        // Freezes the target's animation on its current pose.
        {"StopAnimation", "Stop Animation", "Animation", false, FlowParamKind::None,
         0, {}, FlowParamKind::None, true, true},
        // Player
        // Teleports the player (entity or FPP template player) to the target
        // object's position - e.g. respawn at a spawn point. A position link
        // overrides the object's position.
        {"TeleportPlayer", "Spawn Player At", "Player", false, FlowParamKind::ObjectName,
         0, {}, FlowParamKind::None, true, true, true, false, false},
        // Sets the player's flashlight master switch (the Player object's
        // "Enabled"). On = 1 turns it on, 0 off. The optional toggle button on
        // the player still gates the beam on/off, but only while enabled.
        {"SetFlashlight", "Set Flashlight", "Player", false, FlowParamKind::None, 1,
         {"On"}, FlowParamKind::None, false, false},
        // Scene
        {"SetSky", "Set Sky Color", "Scene", false, FlowParamKind::None, 3, {},
         FlowParamKind::Color, false, false},
        // Loads another scene (applied after the current frame's scripts):
        // runtime objects are rebuilt from the target scene's data, script
        // state resets; textures/models stay loaded (shared across scenes).
        {"SwitchScene", "Switch Scene", "Scene", false, FlowParamKind::SceneName, 0, {},
         FlowParamKind::None, false, false},
        // Applies a color grading preset (Tools > Color Grading) as the
        // frame's GS post pass; "<none>" restores the ungraded image. The
        // switch is global and persists across scene changes.
        {"SetGrading", "Set Color Grading", "Scene", false, FlowParamKind::GradingName,
         0, {}, FlowParamKind::None, false, false},
        // HUD (all HUD images at once; the USE prompt is unaffected)
        {"ShowHud", "Show HUD", "HUD", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false},
        {"HideHud", "Hide HUD", "HUD", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false},
        {"ToggleHud", "Toggle HUD", "HUD", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false},
        // Audio (music: 16-bit 22kHz stereo WAV; sounds: ADPCM one-shots)
        {"PlayMusic", "Play Music", "Audio", false, FlowParamKind::MusicTrack, 2,
         {"Volume", "Loop"}, FlowParamKind::None, false, false},
        {"StopMusic", "Stop Music", "Audio", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false},
        {"SetMusicVolume", "Set Music Volume", "Audio", false, FlowParamKind::None, 1,
         {"Volume"}, FlowParamKind::None, false, false},
        {"PlaySound", "Play Sound", "Audio", false, FlowParamKind::SoundTrack, 2,
         {"Volume", "Channel"}, FlowParamKind::None, false, false},
        // Save data: named values persisted in memory card slots (defined in
        // the Project panel, "Save data"). "Value At Least" is a pure bool
        // source (value >= threshold, evaluated fresh every frame) for logic
        // gates / On Condition. "Get Save Value" / "Get Save Text" are pure
        // text sources (wire into Log Message / Set Save Text). "Open Save
        // Menu" opens the in-game 3-slot save/load menu (the same one a Save
        // point object opens on USE).
        {"SetValue", "Set Save Value", "Save", false, FlowParamKind::SaveValue, 1,
         {"Value"}, FlowParamKind::None, false, false},
        {"AddValue", "Add To Save Value", "Save", false, FlowParamKind::SaveValue, 1,
         {"Delta"}, FlowParamKind::None, false, false},
        {"ValueAtLeast", "Value At Least", "Save", false, FlowParamKind::SaveValue, 1,
         {"Threshold"}, FlowParamKind::None, false, false, false, false, true, false,
         true},
        {"GetSaveValue", "Get Save Value", "Save", false, FlowParamKind::SaveValue, 0,
         {}, FlowParamKind::None, false, false, false, false, true, false, false,
         false, true},
        // Text save values: str = which entry, str2 = the text to store
        // (a text link into Set Save Text overrides str2).
        {"SetSaveText", "Set Save Text", "Save", false, FlowParamKind::SaveText, 0,
         {}, FlowParamKind::None, false, false, false, false, false, false, false,
         true, false},
        {"GetSaveText", "Get Save Text", "Save", false, FlowParamKind::SaveText, 0,
         {}, FlowParamKind::None, false, false, false, false, true, false, false,
         false, true},
        {"OpenSaveMenu", "Open Save Menu", "Save", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false},
        // Variables: named game-global values (one namespace per type - int,
        // bool, position), zeroed at boot, kept across scene switches, NOT
        // saved to the memory card (use Save data for persistence). Setters
        // run on exec; Get Bool / Int At Least are pure bool sources for the
        // logic gates, Get Position is a pure position source. A position
        // link into Set Position overrides its X/Y/Z params (e.g. store an
        // object's position via Get Position on it).
        {"SetVarInt", "Set Int", "Variables", false, FlowParamKind::VarName, 1,
         {"Value"}, FlowParamKind::None, false, false},
        {"SetVarBool", "Set Bool", "Variables", false, FlowParamKind::VarName, 1,
         {"Value"}, FlowParamKind::None, false, false},
        {"SetVarPos", "Set Position", "Variables", false, FlowParamKind::VarName, 3,
         {"X", "Y", "Z"}, FlowParamKind::None, false, false, true},
        {"GetVarBool", "Get Bool", "Variables", false, FlowParamKind::VarName, 0, {},
         FlowParamKind::None, false, false, false, false, true, false, true},
        {"GetVarPos", "Get Position", "Variables", false, FlowParamKind::VarName, 0, {},
         FlowParamKind::None, false, false, false, true, true},
        {"VarAtLeast", "Int At Least", "Variables", false, FlowParamKind::VarName, 1,
         {"Threshold"}, FlowParamKind::None, false, false, false, false, true, false,
         true},
        {"GetVarIntText", "Get Int As Text", "Variables", false, FlowParamKind::VarName,
         0, {}, FlowParamKind::None, false, false, false, false, true, false, false,
         false, true},
        // Menus (Project panel, "Menus"): open a baked menu from logic, and
        // react to menu entries with the "Flow event" action - On Menu Event
        // fires the frame such an entry is selected (also a bool source).
        {"OpenMenu", "Open Menu", "Menus", false, FlowParamKind::MenuName, 0, {},
         FlowParamKind::None, false, false},
        {"OnMenuEvent", "On Menu Event", "Menus", true, FlowParamKind::Text, 0, {},
         FlowParamKind::None, false, false, false, false, false, false, true},
        // Convert: pure data-to-text bridges, so variables and positions can
        // be wired into Log Message / Set Save Text.
        {"PosToText", "Position To Text", "Convert", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false, true, false, true, false, false, false,
         true},
        {"BoolToText", "Bool To Text", "Convert", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false, false, false, true, true, false, false,
         true},
        // Debug. Log Message prints its text followed by every wired text
        // input (in link order, space-separated).
        {"Log", "Log Message", "Debug", false, FlowParamKind::Text, 0, {},
         FlowParamKind::None, false, false, false, false, false, false, false,
         true, false},
        // Logic gates: pure boolean-data nodes (no exec pins). They combine the
        // bool outputs of triggers (and each other) into a new bool. The bool
        // input pin accepts several links - AND/OR/XOR fold over all of them,
        // NOT/NAND/XNOR negate the fold. "On Condition" bridges back to exec:
        // it is a trigger that fires on the rising edge of its bool input.
        {"And", "AND", "Logic", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false, false, false, true, true, true},
        {"Nand", "NAND", "Logic", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false, false, false, true, true, true},
        {"Or", "OR", "Logic", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false, false, false, true, true, true},
        {"Not", "NOT", "Logic", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false, false, false, true, true, true},
        {"Xor", "XOR", "Logic", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false, false, false, true, true, true},
        {"Xnor", "XNOR", "Logic", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false, false, false, true, true, true},
        {"OnCondition", "On Condition", "Logic", true, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false, false, false, false, true, false},
    };
    return types;
}

inline const FlowNodeType* flowNodeType(const std::string& key) {
    for (const auto& t : flowNodeTypes())
        if (key == t.key) return &t;
    return nullptr;
}

// Categories in add-menu order (derived from the registry, first-seen order).
inline std::vector<const char*> flowNodeCategories() {
    std::vector<const char*> cats;
    for (const auto& t : flowNodeTypes()) {
        bool seen = false;
        for (const char* c : cats) seen |= (std::string(c) == t.category);
        if (!seen) cats.push_back(t.category);
    }
    return cats;
}

// imnodes pin ids derived from node ids (pin % 16 encodes the pin kind,
// pin / 16 the node): object-id in / exec out / exec in / object-id out /
// position in / position out / bool in / bool out / text in / text out.
// Pin ids are never persisted, so widening from 8 to 16 slots is safe.
inline int flowIdInPin(int nodeId) { return nodeId * 16; }
inline int flowOutPin(int nodeId) { return nodeId * 16 + 1; }
inline int flowInPin(int nodeId) { return nodeId * 16 + 2; }
inline int flowIdOutPin(int nodeId) { return nodeId * 16 + 3; }
inline int flowPosInPin(int nodeId) { return nodeId * 16 + 4; }
inline int flowPosOutPin(int nodeId) { return nodeId * 16 + 5; }
inline int flowBoolInPin(int nodeId) { return nodeId * 16 + 6; }
inline int flowBoolOutPin(int nodeId) { return nodeId * 16 + 7; }
inline int flowTextInPin(int nodeId) { return nodeId * 16 + 8; }
inline int flowTextOutPin(int nodeId) { return nodeId * 16 + 9; }
