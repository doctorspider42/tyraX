#pragma once

#include <string>
#include <vector>

// Visual logic graph (CryEngine-FlowGraph-like, minimal v1).
// Triggers fire, execution flows through links into action nodes.
// The graph is stored in project.json and compiled into
// src/scripts/flow_graph.gen.cpp on every build.

struct FlowNode {
    int id = 0;
    std::string type;      // key into flowNodeTypes()
    float pos[2] = {0, 0};  // node editor position
    std::string str;       // string param (object name / text / button)
    float num[3] = {0, 0, 0};  // numeric params (radius, color, delta...)
};

struct FlowLink {
    int id = 0;
    int fromNode = 0;  // trigger side
    int toNode = 0;    // action side
};

struct FlowGraph {
    std::vector<FlowNode> nodes;
    std::vector<FlowLink> links;
    int nextId = 1;
};

// ---------------------------------------------------------------------------

enum class FlowParamKind { None, Text, ObjectName, Button, Color, MusicTrack };

struct FlowNodeType {
    const char* key;
    const char* title;
    bool trigger;  // true = has exec output, false = has exec input (action)
    FlowParamKind strKind;   // meaning of FlowNode::str
    int numCount;            // how many of num[] are used
    const char* numLabels[3];
    FlowParamKind numKind;   // Color = show a color picker for num[0..2]
};

inline const std::vector<FlowNodeType>& flowNodeTypes() {
    static const std::vector<FlowNodeType> types = {
        // Triggers
        {"OnStart", "On Start", true, FlowParamKind::None, 0, {}, FlowParamKind::None},
        {"OnButton", "On Button", true, FlowParamKind::Button, 0, {}, FlowParamKind::None},
        {"NearObject", "Near Object", true, FlowParamKind::ObjectName, 1,
         {"Radius"}, FlowParamKind::None},
        {"EverySeconds", "Every N Seconds", true, FlowParamKind::None, 1,
         {"Seconds"}, FlowParamKind::None},
        // Actions
        {"SetSky", "Set Sky Color", false, FlowParamKind::None, 3, {}, FlowParamKind::Color},
        {"ShowObject", "Show Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None},
        {"HideObject", "Hide Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None},
        {"ToggleObject", "Toggle Object", false, FlowParamKind::ObjectName, 0, {},
         FlowParamKind::None},
        {"MoveObjectBy", "Move Object By", false, FlowParamKind::ObjectName, 3,
         {"dX", "dY", "dZ"}, FlowParamKind::None},
        {"SetObjectColor", "Set Object Color", false, FlowParamKind::ObjectName, 3, {},
         FlowParamKind::Color},
        {"Log", "Log Message", false, FlowParamKind::Text, 0, {}, FlowParamKind::None},
        // Music (background song: 16-bit 22kHz stereo WAV, one at a time)
        {"PlayMusic", "Play Music", false, FlowParamKind::MusicTrack, 2,
         {"Volume", "Loop"}, FlowParamKind::None},
        {"StopMusic", "Stop Music", false, FlowParamKind::None, 0, {}, FlowParamKind::None},
        {"SetMusicVolume", "Set Music Volume", false, FlowParamKind::None, 1,
         {"Volume"}, FlowParamKind::None},
    };
    return types;
}

inline const FlowNodeType* flowNodeType(const std::string& key) {
    for (const auto& t : flowNodeTypes())
        if (key == t.key) return &t;
    return nullptr;
}

// imnodes pin ids derived from node ids
inline int flowOutPin(int nodeId) { return nodeId * 4 + 1; }
inline int flowInPin(int nodeId) { return nodeId * 4 + 2; }
