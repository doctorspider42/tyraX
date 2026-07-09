#pragma once

#include <string>
#include <vector>

// Visual logic graph (CryEngine-FlowGraph-like). Every scene object can carry
// its own graph; the graph is stored inside the object in project.json and
// compiled into src/scripts/flow_graph.gen.cpp on every build (one script
// class per object graph).
//
// Three link kinds:
//  - exec links (trigger "then" -> action "do"): execution flow
//  - object links (square pins): pass an object reference between nodes
//  - position links (triangle pins): pass XYZ coordinates between nodes.
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
    float num[3] = {0, 0, 0};  // numeric params (radius, color, volume...)
};

enum FlowLinkKind {
    FlowLinkExec = 0,    // trigger "then" -> action "do"
    FlowLinkObject = 1,  // object id out -> object id in
    FlowLinkPos = 2,     // position out -> position in
    FlowLinkBool = 3,    // boolean value out -> boolean value in (logic gates)
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
           a.pos[1] == b.pos[1] && a.str == b.str && a.num[0] == b.num[0] &&
           a.num[1] == b.num[1] && a.num[2] == b.num[2];
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
};

struct FlowNodeType {
    const char* key;
    const char* title;
    const char* category;  // add-menu submenu ("Triggers", "Object", ...)
    bool trigger;  // true = has exec output, false = has exec input (action)
    FlowParamKind strKind;   // meaning of FlowNode::str
    int numCount;            // how many of num[] are used
    const char* numLabels[3];
    FlowParamKind numKind;   // Color = show a color picker for num[0..2]
    bool idIn;   // accepts an object id from a data link (object-param nodes)
    bool idOut;  // exposes its resolved object as an id output
    bool posIn = false;   // accepts XYZ coordinates from a position link
    bool posOut = false;  // exposes XYZ coordinates as a position output
    bool pure = false;    // data-only node: no exec pins, never "runs"
    bool boolIn = false;  // accepts boolean value(s) from bool link(s)
    bool boolOut = false; // exposes a per-frame boolean condition as a bool output
};

inline const std::vector<FlowNodeType>& flowNodeTypes() {
    static const std::vector<FlowNodeType> types = {
        // Triggers (id out = the watched object, or self). Each also exposes a
        // bool output = "does this condition hold this frame?" for logic gates.
        {"OnStart", "On Start", "Triggers", true, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, true, false, false, false, false, true},
        {"OnButton", "On Button", "Triggers", true, FlowParamKind::Button, 0, {},
         FlowParamKind::None, false, true, false, false, false, false, true},
        {"NearObject", "Near Object", "Triggers", true, FlowParamKind::ObjectName, 1,
         {"Radius"}, FlowParamKind::None, true, true, false, false, false, false, true},
        // Fires when the player presses BTN_USE (controls.hpp) while looking
        // at the target object up close. The object must be marked "usable".
        {"OnUsed", "On Used", "Triggers", true, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None, true, true, false, false, false, false, true},
        {"EverySeconds", "Every N Seconds", "Triggers", true, FlowParamKind::None, 1,
         {"Seconds"}, FlowParamKind::None, false, true, false, false, false, false, true},
        // Object actions (id in = target, id out = the same target)
        {"ShowObject", "Show Object", "Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None, true, true},
        {"HideObject", "Hide Object", "Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None, true, true},
        {"ToggleObject", "Toggle Object", "Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None, true, true},
        {"MoveObjectBy", "Move Object By", "Object", false, FlowParamKind::ObjectName, 3,
         {"dX", "dY", "dZ"}, FlowParamKind::None, true, true},
        {"SetObjectColor", "Set Object Color", "Object", false, FlowParamKind::ObjectName,
         3, {}, FlowParamKind::Color, true, true},
        // Position plumbing: Get Position is a pure data node (no exec pins);
        // Set Object Position uses its X/Y/Z params unless a position link
        // feeds it. Both pass the object through and expose the position.
        {"GetPosition", "Get Position", "Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None, true, true, false, true, true},
        {"SetPosition", "Set Object Position", "Object", false, FlowParamKind::ObjectName,
         3, {"X", "Y", "Z"}, FlowParamKind::None, true, true, true, true, false},
        // Player
        // Teleports the player (entity or FPP template player) to the target
        // object's position - e.g. respawn at a spawn point. A position link
        // overrides the object's position.
        {"TeleportPlayer", "Spawn Player At", "Player", false, FlowParamKind::ObjectName,
         0, {}, FlowParamKind::None, true, true, true, false, false},
        // Scene
        {"SetSky", "Set Sky Color", "Scene", false, FlowParamKind::None, 3, {},
         FlowParamKind::Color, false, false},
        // Loads another scene (applied after the current frame's scripts):
        // runtime objects are rebuilt from the target scene's data, script
        // state resets; textures/models stay loaded (shared across scenes).
        {"SwitchScene", "Switch Scene", "Scene", false, FlowParamKind::SceneName, 0, {},
         FlowParamKind::None, false, false},
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
        // gates / On Condition. "Open Save Menu" opens the in-game 3-slot
        // save/load menu (the same one a Save point object opens on USE).
        {"SetValue", "Set Save Value", "Save", false, FlowParamKind::SaveValue, 1,
         {"Value"}, FlowParamKind::None, false, false},
        {"AddValue", "Add To Save Value", "Save", false, FlowParamKind::SaveValue, 1,
         {"Delta"}, FlowParamKind::None, false, false},
        {"ValueAtLeast", "Value At Least", "Save", false, FlowParamKind::SaveValue, 1,
         {"Threshold"}, FlowParamKind::None, false, false, false, false, true, false,
         true},
        {"OpenSaveMenu", "Open Save Menu", "Save", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false},
        // Menus (Project panel, "Menus"): open a baked menu from logic, and
        // react to menu entries with the "Flow event" action - On Menu Event
        // fires the frame such an entry is selected (also a bool source).
        {"OpenMenu", "Open Menu", "Menus", false, FlowParamKind::MenuName, 0, {},
         FlowParamKind::None, false, false},
        {"OnMenuEvent", "On Menu Event", "Menus", true, FlowParamKind::Text, 0, {},
         FlowParamKind::None, false, false, false, false, false, false, true},
        // Debug
        {"Log", "Log Message", "Debug", false, FlowParamKind::Text, 0, {},
         FlowParamKind::None, false, false},
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

// imnodes pin ids derived from node ids (pin % 8 encodes the pin kind,
// pin / 8 the node): object-id in / exec out / exec in / object-id out /
// position in / position out / bool in / bool out.
inline int flowIdInPin(int nodeId) { return nodeId * 8; }
inline int flowOutPin(int nodeId) { return nodeId * 8 + 1; }
inline int flowInPin(int nodeId) { return nodeId * 8 + 2; }
inline int flowIdOutPin(int nodeId) { return nodeId * 8 + 3; }
inline int flowPosInPin(int nodeId) { return nodeId * 8 + 4; }
inline int flowPosOutPin(int nodeId) { return nodeId * 8 + 5; }
inline int flowBoolInPin(int nodeId) { return nodeId * 8 + 6; }
inline int flowBoolOutPin(int nodeId) { return nodeId * 8 + 7; }
