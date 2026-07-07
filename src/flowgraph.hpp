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

enum class FlowParamKind { None, Text, ObjectName, Button, Color, MusicTrack, SoundTrack };

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
};

inline const std::vector<FlowNodeType>& flowNodeTypes() {
    static const std::vector<FlowNodeType> types = {
        // Triggers (id out = the watched object, or self)
        {"OnStart", "On Start", "Triggers", true, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, true},
        {"OnButton", "On Button", "Triggers", true, FlowParamKind::Button, 0, {},
         FlowParamKind::None, false, true},
        {"NearObject", "Near Object", "Triggers", true, FlowParamKind::ObjectName, 1,
         {"Radius"}, FlowParamKind::None, true, true},
        {"EverySeconds", "Every N Seconds", "Triggers", true, FlowParamKind::None, 1,
         {"Seconds"}, FlowParamKind::None, false, true},
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
        // Audio (music: 16-bit 22kHz stereo WAV; sounds: ADPCM one-shots)
        {"PlayMusic", "Play Music", "Audio", false, FlowParamKind::MusicTrack, 2,
         {"Volume", "Loop"}, FlowParamKind::None, false, false},
        {"StopMusic", "Stop Music", "Audio", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false},
        {"SetMusicVolume", "Set Music Volume", "Audio", false, FlowParamKind::None, 1,
         {"Volume"}, FlowParamKind::None, false, false},
        {"PlaySound", "Play Sound", "Audio", false, FlowParamKind::SoundTrack, 2,
         {"Volume", "Channel"}, FlowParamKind::None, false, false},
        // Debug
        {"Log", "Log Message", "Debug", false, FlowParamKind::Text, 0, {},
         FlowParamKind::None, false, false},
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
// position in / position out.
inline int flowIdInPin(int nodeId) { return nodeId * 8; }
inline int flowOutPin(int nodeId) { return nodeId * 8 + 1; }
inline int flowInPin(int nodeId) { return nodeId * 8 + 2; }
inline int flowIdOutPin(int nodeId) { return nodeId * 8 + 3; }
inline int flowPosInPin(int nodeId) { return nodeId * 8 + 4; }
inline int flowPosOutPin(int nodeId) { return nodeId * 8 + 5; }
