#pragma once

#include <memory>
#include <string>
#include <vector>

// Visual logic graph (CryEngine-FlowGraph-like). Every scene object can carry
// its own graph; the graph is stored inside the object in the .tyra file and
// compiled into src/gen/flow_graph.gen.cpp on every build (one script
// class per object graph).
//
// Link kinds:
//  - exec links (trigger "then" -> action "do"): execution flow
//  - object links (square pins): pass an object reference between nodes
//  - position links (triangle pins): pass XYZ coordinates between nodes
//  - bool links (circle pins): per-frame conditions for the logic gates
//  - text links (circle pins): strings for Log Message / Set Save Text
//  - number links (circle pins): a computed float driving a node's num[0].
// A number link is the value plane: Get Int / Get Save Value / Number are its
// sources, the Math nodes combine them, and a consumer's own num[0] param
// steps aside for a wired one exactly the way X/Y/Z do for a position link.
// So "increase a variable by 1" is a graph, not a hardcoded literal.
// An action may expose SEVERAL exec inputs (Set Object Visible: show / hide /
// toggle); FlowLink::toPin says which one a link targets, so one node replaces
// what used to be a pair of Show*/Hide* nodes.
// Symmetrically, an action may expose several exec OUTPUTS (Branch: true /
// false; Sequence: 1..4) - FlowLink::fromPin says which one a link leaves.
// That is what makes the exec plane a real control-flow language rather than a
// list of things a trigger does: a "Flow" node reads a condition or its own
// state and decides WHICH of its outputs continues.
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
    FlowLinkNum = 5,     // number value out -> number value in (overrides num[0])
};

struct FlowLink {
    int id = 0;
    int fromNode = 0;  // output side
    int toNode = 0;    // input side
    int kind = FlowLinkExec;
    // Which exec input of toNode this link fires (exec links only; 0 = the
    // node's first pin, "> do" on a single-input action). Index into the
    // target type's execInLabels.
    int toPin = 0;
    // Which exec OUTPUT of fromNode this link leaves (exec links only; 0 = a
    // trigger's "then" / an action's "after", the only output most nodes have).
    // Index into the source type's execOutLabels.
    int fromPin = 0;
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
           a.kind == b.kind && a.toPin == b.toPin && a.fromPin == b.fromPin;
}

inline bool operator==(const FlowGraph& a, const FlowGraph& b) {
    return a.nextId == b.nextId && a.nodes == b.nodes && a.links == b.links;
}

// ---------------------------------------------------------------------------

// Exec input pins per node. The pin-id space (see the flow*Pin helpers at the
// bottom) reserves slot 2 plus slots 10..15 for them, so seven is the ceiling
// - well past what any node needs (the widest is show/hide/toggle).
constexpr int kFlowMaxExecIn = 7;

// Exec OUTPUT pins per node. Output 0 keeps the original slot 1 (a trigger's
// "then", an action's "after"), outputs 1..7 take the spare slots 18..24 - so
// eight, again well past what any node needs (the widest is Switch Number's
// four cases plus "else").
constexpr int kFlowMaxExecOut = 8;

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
    AmbienceName,  // name of a Project::ambiencePresets entry ("" = none)
    LayerName,  // name of a SceneData::layers entry (streaming layer)
    AreaName,   // name of a PrimitiveType::Area object in the scene
    SequenceName,  // name of a Project::sequences entry (Cutscene Director)
    HudTextName,  // name of a Project::hudTexts entry (baked text sprite)
    FontName,  // name of a Project::fonts entry (Tools > Font Manager)
    InputActionName,  // name of a Project::input action (Tools > Input Map)
    KeyName,   // a keyboard key label from inputKeyNames() ("Space", "F1")
};

struct FlowNodeType {
    const char* key = "";
    const char* title = "";
    const char* category = "";  // add-menu submenu ("Triggers", "Object", ...)
    bool trigger = false;  // true = has exec output, false = has exec input (action)
    FlowParamKind strKind = FlowParamKind::None;  // meaning of FlowNode::str
    int numCount = 0;             // how many of num[] are used
    const char* numLabels[4] = {};
    FlowParamKind numKind = FlowParamKind::None;  // Color = picker for num[0..2]
    bool idIn = false;    // accepts an object id from a data link (object-param nodes)
    bool idOut = false;   // exposes its resolved object as an id output
    bool posIn = false;   // accepts XYZ coordinates from a position link
    bool posOut = false;  // exposes XYZ coordinates as a position output
    bool pure = false;    // data-only node: no exec pins, never "runs"
    bool boolIn = false;  // accepts boolean value(s) from bool link(s)
    bool boolOut = false; // exposes a per-frame boolean condition as a bool output
    bool textIn = false;  // accepts text value(s) from text link(s)
    bool textOut = false; // exposes a text value as a text output
    // Number plane. numIn = a wired number REPLACES this node's num[0] param
    // (the one convention, so every consumer behaves the same); numOut = the
    // node is a number source. A node with both AND pure is a Math node: its
    // input pin folds over several links instead of taking just the first.
    bool numIn = false;
    bool numOut = false;
    // action that ALSO has an exec output fired later (Delay's "after >")
    bool execThrough = false;
    // Exec input pins on an action. 1 = the plain "> do" pin. More than one
    // gives the node a labeled pin per branch (Set Object Visible: show /
    // hide / toggle) - the branch a link fires is FlowLink::toPin, and the
    // node's codegen switches on it. Ignored by triggers and pure nodes.
    int execInCount = 1;
    const char* execInLabels[kFlowMaxExecIn] = {};
    // Exec OUTPUT pins on an action - the control-flow half. 0 keeps the old
    // rules (a trigger has its "then", an execThrough action its "after",
    // everything else none); >= 1 gives the node that many LABELED outputs and
    // the branch a link leaves is FlowLink::fromPin. A node declaring these
    // emits the chain hanging off each output itself, which is what makes
    // Branch / Sequence / Gate expressible at all.
    int execOutCount = 0;
    const char* execOutLabels[kFlowMaxExecOut] = {};
    // One-paragraph behavior description - THE documentation of the node.
    // Shown in the editor (add-menu tooltips, hovering a node) and fed to
    // the AI flow-graph generator's catalog, so a node added with a desc is
    // automatically documented everywhere. Custom .flownode nodes fill it
    // from their `desc =` header key.
    const char* desc = "";
};

// The node registry. Designated initializers on purpose: omitted fields keep
// their defaults, so each entry states only what the node HAS - and `.desc`
// is required by convention (it is the node's documentation: add-menu
// tooltips, node hover, and the AI generator's catalog all read it).
inline const std::vector<FlowNodeType>& flowNodeTypes() {
    static const std::vector<FlowNodeType> types = {
        // Triggers. Some expose their watched object as an object output
        // (Near/Used/Anim Finished); the per-frame conditions for the logic
        // gates come from the pure bool sources (Value At Least, Get Bool,
        // Is Visible, ...) instead.
        {.key = "OnStart", .title = "On Start", .category = "Triggers",
         .trigger = true,
         .desc = "Fires once when the scene starts."},
        // The explicit spelling of what "Every N Seconds" with Seconds 0 has
        // always done (everyFrames(0) clamps to 1). Continuous MOTION belongs
        // in a node the game integrates itself (Spin Object, Move Object To) -
        // a graph re-entered 50 times a second to nudge a transform pays a
        // world-space vertex re-bake per frame per object.
        {.key = "OnUpdate", .title = "On Update", .category = "Triggers",
         .trigger = true,
         .desc = "Fires EVERY frame while the scene runs. For logic that must "
                 "be re-evaluated continuously. For continuous MOVEMENT use "
                 "Spin Object / Move Object To instead - those are integrated "
                 "by the game itself and cost a fraction of a graph running "
                 "every frame."},
        {.key = "OnButton", .title = "On Button", .category = "Triggers",
         .trigger = true, .strKind = FlowParamKind::Button,
         .desc = "Fires the frame the pad button (str) is pressed. This is the "
                 "RAW button - for something the player can rebind use On "
                 "Action instead."},
        // The configurable half of the input story (docs/input-bindings.md):
        // these two follow the Input Map, so a preset switch or an in-game
        // rebind moves the trigger with the binding.
        {.key = "OnAction", .title = "On Action", .category = "Triggers",
         .trigger = true, .strKind = FlowParamKind::InputActionName,
         .boolOut = true, .execInCount = 1,
         .desc = "Fires the frame the input action named str is pressed - "
                 "\"jump\", \"sprint\" or any action from Tools > Input Map, "
                 "whatever button/key it is currently bound to (including a "
                 "player's own rebind). Its bool output is the live \"held "
                 "right now\" condition for the logic gates."},
        {.key = "OnKey", .title = "On Key", .category = "Triggers",
         .trigger = true, .strKind = FlowParamKind::KeyName, .boolOut = true,
         .desc = "Fires the frame the USB keyboard key named str goes down "
                 "(needs Preferences > Build > Keyboard & mouse). Bypasses the "
                 "Input Map: use it for a fixed debug/cheat key, not for "
                 "gameplay the player should be able to rebind. Its bool "
                 "output is \"held right now\"."},
        {.key = "NearObject", .title = "Near Object", .category = "Triggers",
         .trigger = true, .strKind = FlowParamKind::ObjectName, .numCount = 1,
         .numLabels = {"Radius"}, .idIn = true, .idOut = true,
         .desc = "Fires every frame the player is within num[0] (Radius) "
                 "units of the target object."},
        // Volume trigger: the Area object's box instead of Near Object's
        // radius (docs/areas.md). Read live, so a moving area drags its
        // trigger along.
        {.key = "InArea", .title = "In Area", .category = "Triggers",
         .trigger = true, .strKind = FlowParamKind::AreaName, .numCount = 1,
         .numLabels = {"Who"}, .idOut = true, .boolOut = true,
         .desc = "Fires the frame someone ENTERS the Area object named str (a "
                 "rising edge, like On Player Seen). num[0] Who: 0 = either "
                 "player, 1 = player 1 only, 2 = player 2 only. Its bool "
                 "output is the live 'inside right now' condition - wire it "
                 "through NOT into On Condition for an exit trigger. Unlike "
                 "Near Object the test is a real volume: it bounds Y too, so "
                 "one floor of a building can trigger on its own."},
        // BTN_USE lives in controls.hpp.
        {.key = "OnUsed", .title = "On Used", .category = "Triggers",
         .trigger = true, .strKind = FlowParamKind::ObjectName, .idIn = true,
         .idOut = true,
         .desc = "Fires when the player presses the USE button while looking "
                 "at the target up close. The target object must have its "
                 "'usable' flag set."},
        {.key = "EverySeconds", .title = "Every N Seconds", .category = "Triggers",
         .trigger = true, .numCount = 1, .numLabels = {"Seconds"},
         .desc = "Fires every num[0] seconds."},
        // One-shot clips: once; looping clips: every wrap.
        {.key = "OnAnimFinished", .title = "On Animation Finished",
         .category = "Triggers", .trigger = true,
         .strKind = FlowParamKind::ObjectName, .idIn = true, .idOut = true,
         .desc = "Fires when the target's animation clip reaches its last "
                 "frame (animated .glb model objects only)."},
        {.key = "Delay", .title = "Delay", .category = "Time", .numCount = 1,
         .numLabels = {"Seconds"}, .numIn = true, .execThrough = true,
         .desc = "Exec input arms a timer; the 'after' exec output fires once "
                 "num[0] seconds elapse. Re-arming while counting restarts "
                 "the timer. A linked number overrides num[0], so the wait "
                 "can be computed."},
        // ------------------------------------------------------------------
        // Flow control. These are the nodes that make the exec plane a
        // language: each one decides WHICH of its own exec outputs continues
        // (FlowLink::fromPin), instead of just doing a thing. Every chain is
        // emitted inline by codegen, so a Branch costs one C++ `if`.
        {.key = "Branch", .title = "Branch (If)", .category = "Flow",
         .boolIn = true, .execOutCount = 2, .execOutLabels = {"true", "false"},
         .desc = "The if/else of the graph: on exec it evaluates its bool "
                 "input ONCE and continues out of 'true' or out of 'false'. "
                 "Unlike On Condition (which fires on a rising edge whenever "
                 "the condition becomes true) this runs only when something "
                 "execs it, so it answers \"is it true right now?\" at a "
                 "moment you choose. With no bool wired it always takes "
                 "'false'."},
        {.key = "Sequence", .title = "Sequence", .category = "Flow",
         .execOutCount = 4, .execOutLabels = {"1", "2", "3", "4"},
         .desc = "Fires its outputs 1, 2, 3, 4 in that order, each one's whole "
                 "chain completing before the next starts. Use it when the "
                 "ORDER matters - several links out of one trigger pin run in "
                 "link order, which is invisible in the editor. Unused outputs "
                 "cost nothing."},
        {.key = "DoOnce", .title = "Do Once", .category = "Flow",
         .execInCount = 2, .execInLabels = {"do", "reset"}, .execOutCount = 1,
         .execOutLabels = {"then"},
         .desc = "Passes the FIRST exec through and then nothing: the gate for "
                 "a one-shot inside a trigger that keeps firing (Near Object, "
                 "On Update). The 'reset' pin arms it again. Resets on scene "
                 "reload."},
        {.key = "DoN", .title = "Do N Times", .category = "Flow",
         .numCount = 1, .numLabels = {"Times"}, .numIn = true,
         .execInCount = 2, .execInLabels = {"do", "reset"}, .execOutCount = 1,
         .execOutLabels = {"then"},
         .desc = "Passes the first num[0] (Times) execs through, then blocks. "
                 "'reset' starts the count over. Times <= 0 blocks everything; "
                 "a linked number overrides num[0]."},
        {.key = "Gate", .title = "Gate", .category = "Flow", .numCount = 1,
         .numLabels = {"Start open"}, .execInCount = 3,
         .execInLabels = {"enter", "open", "close"}, .execOutCount = 1,
         .execOutLabels = {"then"},
         .desc = "A valve on the exec plane: exec into 'enter' continues out of "
                 "'then' only while the gate is open. 'open' and 'close' flip "
                 "it, num[0] Start open says which state it boots in. Cleaner "
                 "than a bool variable + Branch when the STATE is what you are "
                 "modelling (a door being unlocked, a phase being active)."},
        {.key = "FlipFlop", .title = "Flip Flop", .category = "Flow",
         .execOutCount = 2, .execOutLabels = {"A", "B"},
         .desc = "Alternates: the first exec goes out of 'A', the next out of "
                 "'B', then A again. The two-state toggle without a variable - "
                 "a light switch, an in/out door, a stance change."},
        {.key = "SwitchNumber", .title = "Switch Number", .category = "Flow",
         .numCount = 1, .numLabels = {"Value"}, .numIn = true,
         .execOutCount = 5, .execOutLabels = {"= 0", "= 1", "= 2", "= 3", "else"},
         .desc = "Routes exec by a number: rounds its input (a wired number, "
                 "else num[0]) and continues out of the matching output, or out "
                 "of 'else' when it is not 0..3. The dispatch for a state "
                 "machine kept in an int variable."},
        {.key = "RandomBranch", .title = "Random Branch", .category = "Flow",
         .numCount = 1, .numLabels = {"Outputs"}, .execOutCount = 4,
         .execOutLabels = {"A", "B", "C", "D"},
         .desc = "Continues out of one output picked at random. num[0] Outputs "
                 "(2..4) says how many are in play, so a 3-way pick does not "
                 "need D wired. Use it for idle barks, wander directions, loot "
                 "rolls."},
        {.key = "Cooldown", .title = "Cooldown", .category = "Flow",
         .numCount = 1, .numLabels = {"Seconds"}, .execOutCount = 1,
         .execOutLabels = {"then"},
         .desc = "Passes exec through at most once every num[0] seconds and "
                 "silently swallows the rest - the rate limiter for a trigger "
                 "that fires every frame (Near Object) or a weapon that must "
                 "not fire faster than it reloads. Unlike Delay nothing is "
                 "queued: a swallowed exec is gone, not postponed."},
        {.key = "Counter", .title = "Counter", .category = "Flow",
         .numCount = 1, .numLabels = {"Every"}, .numOut = true,
         .execInCount = 2, .execInLabels = {"count", "reset"},
         .execOutCount = 1, .execOutLabels = {"then"},
         .desc = "Counts execs into 'count' and fires 'then' every num[0] "
                 "(Every) of them (Every 1 = every time, 3 = every third). Its "
                 "number output is the running total, so it doubles as a "
                 "graph-local tally without touching a variable. 'reset' zeroes "
                 "both."},
        {.key = "Timer", .title = "Timer", .category = "Flow", .numCount = 1,
         .numLabels = {"Duration"}, .numOut = true, .execInCount = 3,
         .execInLabels = {"start", "stop", "reset"}, .execOutCount = 1,
         .execOutLabels = {"finished"},
         .desc = "A stopwatch: 'start' runs it, 'stop' pauses it, 'reset' zeroes "
                 "it. Its number output is the elapsed SECONDS, readable while "
                 "running - wire it into Number To Text for an on-screen clock. "
                 "With num[0] Duration > 0 the 'finished' output fires the frame "
                 "it reaches that many seconds and the timer stops itself; "
                 "Duration 0 = runs forever."},
        {.key = "Tween", .title = "Tween Value", .category = "Flow",
         .numCount = 4, .numLabels = {"From", "To", "Seconds", "Ease"},
         .numOut = true, .execInCount = 2, .execInLabels = {"start", "stop"},
         .execOutCount = 1, .execOutLabels = {"finished"},
         .desc = "Drives a NUMBER from num[0] (From) to num[1] (To) over num[2] "
                 "(Seconds) and fires 'finished' when it arrives. num[3] Ease: "
                 "0 linear, 1 ease in, 2 ease out, 3 smooth (in-out). Its "
                 "number output is the live value - wire it into Set Object "
                 "Color, Set Bloom, Set Music Volume, a position, anything on "
                 "the number plane, and that parameter animates. Frame-rate "
                 "independent. 'stop' freezes it where it is."},
        {.key = "ForLoop", .title = "For Loop", .category = "Flow",
         .numCount = 1, .numLabels = {"Times"}, .numIn = true, .numOut = true,
         .execOutCount = 2, .execOutLabels = {"body", "done"},
         .desc = "Runs its 'body' chain num[0] (Times) times, then fires 'done' "
                 "once. Its number output is the 0-based iteration index, so "
                 "the body can spread things out (spawn 8 objects in a ring). "
                 "The whole loop runs inside ONE frame, so keep Times small - "
                 "it is capped at 64, and a body that moves objects re-bakes "
                 "their geometry per iteration."},
        // Object params already default to self when empty; Self makes the
        // reference explicit and wireable into any pin.
        {.key = "Self", .title = "Self", .category = "Object", .idOut = true,
         .posOut = true, .pure = true,
         .desc = "Pure data node exposing the graph's owner object and its "
                 "live position."},
        // Object actions (id in = target, id out = the same target).
        // Visibility folds show/hide/toggle onto one node's exec pins; read
        // back with the pure bool Is Visible.
        {.key = "SetObjectVisible", .title = "Set Object Visible",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .idIn = true, .idOut = true, .execInCount = 3,
         .execInLabels = {"show", "hide", "toggle"},
         .desc = "Sets the target's visibility. Three exec pins: 'show' makes "
                 "it visible, 'hide' invisible, 'toggle' flips it."},
        {.key = "SetLight", .title = "Set Light", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .numCount = 2,
         .numLabels = {"On", "Intensity"}, .idIn = true, .idOut = true,
         .desc = "Switches a dynamic point light on or off (num[0]) and "
                 "scales its brightness (num[1], 1 = authored value). The "
                 "target must be a Point Light object with 'Dynamic (live)' "
                 "enabled - baked (static) lights cannot change at runtime."},
        {.key = "MoveObjectBy", .title = "Move Object By", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .numCount = 3,
         .numLabels = {"dX", "dY", "dZ"}, .idIn = true, .idOut = true,
         .desc = "Instantly shifts the target by (dX, dY, dZ)."},
        {.key = "MoveObjectTo", .title = "Move Object To", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .numCount = 4,
         .numLabels = {"X", "Y", "Z", "Speed"}, .idIn = true, .idOut = true,
         .posIn = true,
         .desc = "Glides the target toward X/Y/Z (or a linked position, "
                 "re-read every frame) at num[3] (Speed) units/s until it "
                 "arrives."},
        {.key = "PushObject", .title = "Apply Impulse", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .numCount = 3,
         .numLabels = {"X", "Y", "Z"}, .idIn = true, .idOut = true,
         .desc = "Physics: adds velocity (units/s) to a rigid body and wakes "
                 "it. num[0..2] = X/Y/Z impulse. On a non-physics object it "
                 "only nudges the stored velocity - harmless."},
        {.key = "SetObjectColor", .title = "Set Object Color",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .numCount = 3, .numKind = FlowParamKind::Color, .idIn = true,
         .idOut = true,
         .desc = "Tints the target; num[0..2] = RGB, each 0..1."},
        // Codegen: an exec-wired Get Position latches into a posOut member
        // (templates.cpp getPosLatched); unwired ones resolve live.
        {.key = "GetPosition", .title = "Get Position", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .idIn = true, .idOut = true,
         .posOut = true, .execThrough = true,
         .desc = "Exposes the target object and its position. With its exec "
                 "pins unwired it is a live data source: consumers read the "
                 "target's CURRENT position whenever they run. Wire its exec "
                 "input to SAMPLE instead: the position output freezes at "
                 "the moment the exec fires and the 'after' exec chains on - "
                 "use this to remember where something was when an event "
                 "happened."},
        {.key = "IsVisible", .title = "Is Visible", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .idIn = true, .idOut = true,
         .pure = true, .boolOut = true,
         .desc = "Pure bool: is the target visible this frame?"},
        {.key = "SetPosition", .title = "Set Object Position",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .numCount = 3, .numLabels = {"X", "Y", "Z"}, .idIn = true,
         .idOut = true, .posIn = true, .posOut = true,
         .desc = "Sets the target's position to X/Y/Z (a linked position "
                 "overrides the params)."},
        // Rotation, the three shapes the position family already has: a delta,
        // an absolute set, and a continuous rate. Degrees, applied in the
        // engine's Euler order (X, then Y, then Z), so Y is the yaw.
        {.key = "RotateObjectBy", .title = "Rotate Object By",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .numCount = 3, .numLabels = {"dX", "dY", "dZ"}, .idIn = true,
         .idOut = true,
         .desc = "Instantly turns the target by (dX, dY, dZ) DEGREES about the "
                 "world X/Y/Z axes (dY = yaw). A one-shot: for something that "
                 "keeps turning use Spin Object rather than firing this every "
                 "frame."},
        {.key = "SetRotation", .title = "Set Object Rotation",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .numCount = 3, .numLabels = {"X", "Y", "Z"}, .idIn = true,
         .idOut = true,
         .desc = "Sets the target's rotation to X/Y/Z degrees (absolute; Y is "
                 "the yaw). Applied in the order X, then Y, then Z - the same "
                 "as the Properties panel's Rotation."},
        {.key = "SpinObject", .title = "Spin Object", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .numCount = 3,
         .numLabels = {"X deg/s", "Y deg/s", "Z deg/s"}, .idIn = true,
         .idOut = true, .execInCount = 2, .execInLabels = {"start", "stop"},
         .desc = "Continuous rotation - THE node for something that turns all "
                 "the time (a coin, a fan, a lighthouse). 'start' gives the "
                 "target an angular velocity of num[0..2] degrees per SECOND "
                 "(Y = yaw), 'stop' clears it. The game integrates it in its "
                 "own object pass, and a spinner renders through a per-object "
                 "matrix instead of re-baking its vertices every frame, so the "
                 "whole graph is On Start -> start and the runtime cost is one "
                 "matrix refresh per frame. Frame-rate independent. Re-firing "
                 "'start' replaces the rate. On a physics object the tumble "
                 "writes rotation too, so the two add up."},
        // Despawn on an authored object only deactivates it (layer streaming
        // can bring authored objects back).
        {.key = "SpawnObject", .title = "Spawn Object", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .numCount = 1,
         .numLabels = {"Yaw"}, .idIn = true, .idOut = true, .posIn = true,
         .desc = "Clones the target object into a runtime slot at the linked "
                 "position (or the template's own), yaw = num[0] degrees. "
                 "The object OUTPUT is the clone, not the template - wire it "
                 "into further actions. Pool of 32 live clones."},
        {.key = "DespawnObject", .title = "Despawn Object", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .idIn = true,
         .desc = "Removes a spawned clone (frees its slot); deactivates an "
                 "authored object."},
        {.key = "Animation", .title = "Animation", .category = "Animation",
         .strKind = FlowParamKind::Text, .numCount = 3,
         .numLabels = {"Loop", "Speed", "Fade"}, .idIn = true, .idOut = true,
         .execInCount = 2, .execInLabels = {"play", "stop"},
         .desc = "Controls a target's animation (animated .glb objects). "
                 "'play' starts clip named str (str \"\" = the model's first "
                 "clip); num[0] Loop (1 = loop), num[1] Speed multiplier (0 = "
                 "default), num[2] crossfade seconds. 'stop' freezes the "
                 "current pose and ignores the params. NOTE: str holds the "
                 "CLIP name; the target comes from an object link or self."},
        // AI (docs/navigation-ai.md). NPCs walk the nav grid baked at build
        // time (navmesh.cpp -> nav_data.gen.hpp); paths come from A* on the
        // EE (navigation.gen.cpp), agents snap to the terrain and turn to
        // face their motion. The movement actions drive ONE shared AI state
        // per object - starting a new one replaces the previous (a Chase
        // interrupts a Patrol; Stop AI returns the NPC to idle).
        {.key = "PatrolWaypoints", .title = "Patrol Waypoints", .category = "AI",
         .strKind = FlowParamKind::Text, .numCount = 3,
         .numLabels = {"Speed", "Pause s", "Once"}, .idIn = true, .idOut = true,
         .desc = "The target walks the scene objects whose names start with "
                 "prefix str, in natural order (str \"wp\" -> wp1, wp2, ...; "
                 "use Empty objects as waypoints), pathfinding over the baked "
                 "nav grid. num[0] Speed units/s, num[1] pause seconds at "
                 "each waypoint, num[2] Once (1 = one pass then idle, 0 = "
                 "cycle forever). NOTE: str holds the waypoint PREFIX; the "
                 "target comes from an object link or defaults to self."},
        {.key = "ChasePlayer", .title = "Chase Player", .category = "AI",
         .strKind = FlowParamKind::ObjectName, .numCount = 3,
         .numLabels = {"Speed", "Stop Dist", "Give Up"}, .idIn = true,
         .idOut = true,
         .desc = "The target pursues the player over the nav grid, repathing "
                 "as they move. Within num[1] (Stop Dist) it stands and keeps "
                 "facing the player; num[2] (Give Up) > 0 drops to idle once "
                 "the player escapes farther than that (0 = never gives up)."},
        {.key = "FleePlayer", .title = "Flee From Player", .category = "AI",
         .strKind = FlowParamKind::ObjectName, .numCount = 2,
         .numLabels = {"Speed", "Safe Dist"}, .idIn = true, .idOut = true,
         .desc = "The target runs away from the player over the nav grid "
                 "until num[1] (Safe Dist) away, then idles."},
        {.key = "StopAi", .title = "Stop AI Movement", .category = "AI",
         .strKind = FlowParamKind::ObjectName, .idIn = true, .idOut = true,
         .desc = "Stops the target's Patrol/Chase/Flee and returns it to "
                 "idle."},
        {.key = "OnPlayerSeen", .title = "On Player Seen", .category = "AI",
         .trigger = true, .strKind = FlowParamKind::ObjectName, .numCount = 3,
         .numLabels = {"Range", "FOV deg", "LOS"}, .idIn = true, .idOut = true,
         .boolOut = true,
         .desc = "Fires (rising edge) when the watched object sees the "
                 "player: within num[0] (Range), inside a vision cone of "
                 "num[1] (FOV) degrees around the object's facing, and with "
                 "num[2] (LOS) = 1 also terrain line-of-sight (hills hide "
                 "the player; objects do not block). Its bool output is the "
                 "live 'seen right now' condition for the logic gates."},
        {.key = "TeleportPlayer", .title = "Spawn Player At",
         .category = "Player", .strKind = FlowParamKind::ObjectName,
         .idIn = true, .idOut = true, .posIn = true,
         .desc = "Teleports the player to the target object's position (or a "
                 "linked position) - e.g. respawn points."},
        // The hit object is a runtime reference (-1 = none) - actions fed it
        // are guarded like Spawn Object clones.
        {.key = "Raycast", .title = "Raycast", .category = "Player",
         .numCount = 1, .numLabels = {"Max Dist"}, .idOut = true,
         .posOut = true, .execThrough = true,
         .desc = "On exec, casts a ray from the player's eye along the view "
                 "direction up to num[0] (Max Dist) and latches the results: "
                 "position output = hit point, object output = hit object "
                 "(may be none). The 'after' exec fires right after the "
                 "cast."},
        // The optional toggle button on the player still gates the beam,
        // but only while enabled.
        {.key = "SetFlashlight", .title = "Set Flashlight", .category = "Player",
         .numCount = 1, .numLabels = {"On"},
         .desc = "num[0] = 1 turns the player's flashlight master switch on, "
                 "0 off."},
        {.key = "SetInputPreset", .title = "Set Input Preset",
         .category = "Player", .strKind = FlowParamKind::Text,
         .desc = "Switches the active binding preset (Tools > Input Map) to the "
                 "one named str. The player's own rebinds are re-applied on "
                 "top. Unknown name = no change."},
        {.key = "SetStickCurve", .title = "Set Stick Curve",
         .category = "Player", .numCount = 3,
         .numLabels = {"Stick", "Curve", "Exponent"},
         .desc = "Changes the analog stick response curve live. num[0] "
                 "Stick: 0 left, 1 right, 2 both. num[1] Curve: 0 Linear, 1 "
                 "Exponential, 2 S-Curve. num[2] Exponent >= 1."},
        {.key = "VibratePad", .title = "Vibrate Pad", .category = "Player",
         .numCount = 3, .numLabels = {"Big", "Small", "Seconds"},
         .desc = "Vibrates the DualShock pad. num[0] Big: heavy-motor "
                 "strength 0..1. num[1] Small: the on/off buzz motor (!= 0 = "
                 "on). num[2] Seconds > 0 auto-stops after that long, 0 = "
                 "vibrate until the next Vibrate Pad. Big 0 + Small off stops."},
        // Scene
        {.key = "SetSky", .title = "Set Sky Color", .category = "Scene",
         .numCount = 3, .numKind = FlowParamKind::Color,
         .desc = "Sets the sky color; num[0..2] = RGB, each 0..1."},
        // Runtime objects rebuild from the target scene's data, script state
        // resets; textures/models stay loaded (shared across scenes).
        {.key = "SwitchScene", .title = "Switch Scene", .category = "Scene",
         .strKind = FlowParamKind::SceneName,
         .desc = "Loads the scene named str (applied after this frame's "
                 "scripts)."},
        {.key = "SetLayerLoaded", .title = "Set Layer Loaded",
         .category = "Scene", .strKind = FlowParamKind::LayerName,
         .execInCount = 2, .execInLabels = {"load", "unload"},
         .desc = "Streams a layer in or out. 'load' starts pulling its assets "
                 "into memory and activates its objects when resident; "
                 "'unload' deactivates them and frees assets no other loaded "
                 "layer uses."},
        {.key = "IsLayerLoaded", .title = "Is Layer Loaded", .category = "Scene",
         .strKind = FlowParamKind::LayerName, .pure = true, .boolOut = true,
         .desc = "Pure bool: is the layer fully loaded?"},
        {.key = "SetGrading", .title = "Set Color Grading", .category = "Scene",
         .strKind = FlowParamKind::GradingName,
         .desc = "Applies the color grading preset named str; \"\" restores "
                 "the ungraded image. Persists across scene changes."},
        {.key = "SetFog", .title = "Set Fog", .category = "Scene",
         .numCount = 1, .numLabels = {"On"},
         .desc = "num[0] = 1 re-applies the scene's fog, 0 disables it."},
        {.key = "SetBloom", .title = "Set Bloom", .category = "Scene",
         .numCount = 1, .numLabels = {"Amount"}, .numIn = true,
         .desc = "Bloom amount num[0] 0..2 (0 = off, 1 = the blur fully "
                 "re-added, above that over-added for a hot glow). A linked "
                 "number overrides num[0] - wire a Tween into it to ramp the "
                 "glow up."},
        {.key = "SetGrain", .title = "Set Grain", .category = "Scene",
         .numCount = 1, .numLabels = {"Amount"}, .numIn = true,
         .desc = "Film grain amount num[0] 0..1 (0 = off). A linked number "
                 "overrides num[0]."},
        {.key = "SetFlare", .title = "Set Lens Flare", .category = "Scene",
         .numCount = 1, .numLabels = {"Amount"}, .numIn = true,
         .desc = "Sun lens flare brightness num[0] 0..1 (0 = off). The flare "
                 "follows the scene's sun (lighting direction) and hides "
                 "behind geometry. A linked number overrides num[0]."},
        {.key = "SetGodRays", .title = "Set God Rays", .category = "Scene",
         .numCount = 1, .numLabels = {"Amount"}, .numIn = true,
         .desc = "God rays (sun light shafts) strength num[0] 0..1 (0 = "
                 "off). Radial streaks from the sun's screen position. A "
                 "linked number overrides num[0]."},
        // Authored baseline: Tools > UI Editor > Depth of field.
        {.key = "SetDof", .title = "Set Depth Of Field", .category = "Scene",
         .numCount = 4, .numLabels = {"Focus", "Range", "Amount", "Mode"},
         .posIn = true,
         .desc = "Depth of field. num[3] Mode: 0 = set custom num[0] Focus / "
                 "num[1] Range / num[2] Amount 0..1, 1 = off, 2 = restore "
                 "the scene's authored setting. A linked position replaces "
                 "Focus with the live player-to-point distance."},
        {.key = "SetParticles", .title = "Set Particles", .category = "Scene",
         .numCount = 1, .numLabels = {"On"},
         .desc = "num[0] = 1/0: global switch for all particle emitters."},
        // The confirm prompt auto-reverts - a mode the TV can't show would
        // otherwise strand the player on a black screen.
        {.key = "SetDisplayMode", .title = "Set Display Mode",
         .category = "Scene", .numCount = 2,
         .numLabels = {"Mode", "Confirm s"},
         .desc = "Switches the scan mode. num[0] Mode: 0 interlaced, 1 "
                 "progressive 480p, 2 1080i, 3 interlaced field rendering "
                 "(a fresh half-height image every field), 4 full-height "
                 "PAL 576i (always 50 Hz). num[1] Confirm "
                 "seconds > 0 shows a keep-or-revert prompt with automatic "
                 "rollback."},
        {.key = "SetWidescreen", .title = "Set Widescreen", .category = "Scene",
         .numCount = 1, .numLabels = {"On"},
         .desc = "num[0] = 1 re-fits the projection for 16:9, 0 for 4:3."},
        {.key = "SetAmbience", .title = "Set Ambience", .category = "Scene",
         .strKind = FlowParamKind::AmbienceName,
         .desc = "Repaints the sky from the ambience preset named str "
                 "(lighting/fog are baked per scene; only the sky changes "
                 "live)."},
        {.key = "PlaySequence", .title = "Play Sequence", .category = "Scene",
         .strKind = FlowParamKind::SequenceName,
         .desc = "Starts the cutscene sequence named str; retriggering "
                 "restarts it."},
        {.key = "StopSequence", .title = "Stop Sequence", .category = "Scene",
         .desc = "Stops the active cutscene."},
        // HUD (all HUD images at once; the USE prompt is unaffected)
        {.key = "SetHudVisible", .title = "Set HUD Visible", .category = "HUD",
         .execInCount = 3, .execInLabels = {"show", "hide", "toggle"},
         .desc = "Sets all HUD images' visibility. Exec pins: 'show', 'hide', "
                 "'toggle' (the USE prompt is unaffected)."},
        {.key = "SetTextVisible", .title = "Set Text Visible", .category = "HUD",
         .strKind = FlowParamKind::HudTextName, .numCount = 1,
         .numLabels = {"Seconds"}, .execInCount = 2,
         .execInLabels = {"show", "hide"},
         .desc = "Shows or hides the baked on-screen text named str. On "
                 "'show', num[0] Seconds > 0 auto-hides after that long; 0 = "
                 "stays until 'hide'. The string is frozen at build - for a "
                 "runtime-varying one use Display Text."},
        // Runtime text: the string is only known while the game runs, so it
        // draws glyph by glyph from a Font Manager font's atlas instead of a
        // pre-baked sprite. The atlas only reaches VRAM once shown.
        {.key = "DisplayText", .title = "Display Text", .category = "HUD",
         .strKind = FlowParamKind::FontName, .numCount = 4,
         .numLabels = {"X", "Y", "Size", "Seconds"}, .textIn = true,
         .execInCount = 2, .execInLabels = {"show", "hide"},
         .desc = "Draws a live text value (wire a text source into it) with "
                 "the Font Manager font named str. str2 = a static prefix in "
                 "front of the wired value. num[0] X / num[1] Y = normalized "
                 "screen position (center anchor), num[2] Size px, num[3] "
                 "Seconds > 0 auto-hides after 'show'. Re-read every frame "
                 "while shown; color and shadow come from the font entry."},
        // Audio (music: 16-bit 22kHz stereo WAV; sounds: ADPCM one-shots)
        {.key = "PlayMusic", .title = "Play Music", .category = "Audio",
         .strKind = FlowParamKind::MusicTrack, .numCount = 2,
         .numLabels = {"Volume", "Loop"},
         .desc = "Plays the music track str (use the exact track path from "
                 "the project context). num[0] Volume 0..100, num[1] Loop (1 "
                 "= loop)."},
        {.key = "StopMusic", .title = "Stop Music", .category = "Audio",
         .desc = "Stops the music."},
        {.key = "SetMusicVolume", .title = "Set Music Volume",
         .category = "Audio", .numCount = 1, .numLabels = {"Volume"},
         .numIn = true,
         .desc = "num[0] Volume 0..100. A linked number overrides num[0], so "
                 "a Tween can fade the music out."},
        {.key = "PlaySound", .title = "Play Sound", .category = "Audio",
         .strKind = FlowParamKind::SoundTrack, .numCount = 2,
         .numLabels = {"Volume", "Channel"},
         .desc = "Plays the sound effect str (exact path from the project "
                 "context). num[0] Volume 0..100, num[1] Channel 0..23 or -1 "
                 "= auto-rotate."},
        // Save data: named values persisted in memory card slots (Project
        // panel, "Save data"); every save slot stores a snapshot.
        {.key = "SetValue", .title = "Set Save Value", .category = "Save",
         .strKind = FlowParamKind::SaveValue, .numCount = 1,
         .numLabels = {"Value"}, .numIn = true,
         .desc = "Sets the save value named str to num[0] (a linked number "
                 "overrides num[0])."},
        {.key = "AddValue", .title = "Add To Save Value", .category = "Save",
         .strKind = FlowParamKind::SaveValue, .numCount = 1,
         .numLabels = {"Delta"}, .numIn = true,
         .desc = "Adds num[0] to the save value named str (a linked number "
                 "overrides num[0])."},
        {.key = "ValueAtLeast", .title = "Value At Least", .category = "Save",
         .strKind = FlowParamKind::SaveValue, .numCount = 1,
         .numLabels = {"Threshold"}, .pure = true, .boolOut = true,
         .numIn = true,
         .desc = "Pure bool: save value named str >= num[0], evaluated fresh "
                 "every frame. A linked number overrides num[0], so the "
                 "threshold can itself be computed."},
        {.key = "GetSaveValue", .title = "Get Save Value", .category = "Save",
         .strKind = FlowParamKind::SaveValue, .pure = true, .textOut = true,
         .numOut = true,
         .desc = "Pure output of the save value named str, as a number and as "
                 "text."},
        {.key = "SetSaveText", .title = "Set Save Text", .category = "Save",
         .strKind = FlowParamKind::SaveText, .textIn = true,
         .desc = "Sets the save text named str to str2 (a linked text "
                 "overrides str2)."},
        {.key = "GetSaveText", .title = "Get Save Text", .category = "Save",
         .strKind = FlowParamKind::SaveText, .pure = true, .textOut = true,
         .desc = "Pure text output of the save text named str."},
        // The same 3-slot menu a Save point object opens on USE.
        {.key = "OpenSaveMenu", .title = "Open Save Menu", .category = "Save",
         .desc = "Opens the in-game 3-slot save/load menu."},
        // Variables: named game-global values (one namespace per type),
        // zeroed at boot, kept across scene switches, NOT saved to the
        // memory card (use Save data for persistence).
        {.key = "SetVarInt", .title = "Set Int", .category = "Variables",
         .strKind = FlowParamKind::VarName, .numCount = 1,
         .numLabels = {"Value"}, .numIn = true, .execInCount = 2,
         .execInLabels = {"set", "add"},
         .desc = "Writes the global int variable named str. The 'set' pin "
                 "assigns num[0], the 'add' pin ADDS it - so a counter is On "
                 "Button -> add with Value 1, no read needed. A linked number "
                 "overrides num[0] on both pins."},
        {.key = "SetVarBool", .title = "Set Bool", .category = "Variables",
         .strKind = FlowParamKind::VarName, .numCount = 1,
         .numLabels = {"Value"}, .numIn = true, .execInCount = 2,
         .execInLabels = {"set", "toggle"},
         .desc = "Writes the global bool variable named str: the 'set' pin "
                 "assigns num[0] != 0 (a linked number overrides num[0]), the "
                 "'toggle' pin flips it."},
        {.key = "SetVarPos", .title = "Set Position", .category = "Variables",
         .strKind = FlowParamKind::VarName, .numCount = 3,
         .numLabels = {"X", "Y", "Z"}, .posIn = true,
         .desc = "Sets the global position variable named str to X/Y/Z (a "
                 "linked position overrides the params)."},
        {.key = "GetVarBool", .title = "Get Bool", .category = "Variables",
         .strKind = FlowParamKind::VarName, .pure = true, .boolOut = true,
         .desc = "Pure bool: the global bool variable named str."},
        {.key = "GetVarPos", .title = "Get Position", .category = "Variables",
         .strKind = FlowParamKind::VarName, .posOut = true, .pure = true,
         .desc = "Pure position: the global position variable named str."},
        {.key = "VarAtLeast", .title = "Int At Least", .category = "Variables",
         .strKind = FlowParamKind::VarName, .numCount = 1,
         .numLabels = {"Threshold"}, .pure = true, .boolOut = true,
         .numIn = true,
         .desc = "Pure bool: global int variable named str >= num[0]. A linked "
                 "number overrides num[0], so one variable can be compared "
                 "against another."},
        {.key = "GetVarInt", .title = "Get Int", .category = "Variables",
         .strKind = FlowParamKind::VarName, .pure = true, .numOut = true,
         .desc = "Pure number: the global int variable named str. Wire it into "
                 "a Math node or straight into any number input."},
        {.key = "GetVarIntText", .title = "Get Int As Text",
         .category = "Variables", .strKind = FlowParamKind::VarName,
         .pure = true, .textOut = true,
         .desc = "Pure text of the global int variable named str."},
        // The number plane. Sources (Number, Get Int, Get Save Value) feed
        // these, they feed each other, and a consumer's num[0] gives way to
        // the wire. Every one of them is PURE - a number is an expression
        // evaluated where it is read, never a step that runs.
        {.key = "Number", .title = "Number", .category = "Math",
         .numCount = 1, .numLabels = {"Value"}, .pure = true, .numOut = true,
         .desc = "Pure number: the constant num[0]. The literal to feed a Math "
                 "node or any number input."},
        {.key = "NumAdd", .title = "Add", .category = "Math", .numCount = 1,
         .numLabels = {"B"}, .pure = true, .numIn = true, .numOut = true,
         .desc = "Pure number: every wired number input summed. With exactly "
                 "ONE input wired it adds num[0] (B) to it - Get Int -> Add "
                 "(B 1) -> Set Int is the read-modify-write counter. With none "
                 "wired it is just B."},
        {.key = "NumSub", .title = "Subtract", .category = "Math",
         .numCount = 1, .numLabels = {"B"}, .pure = true, .numIn = true,
         .numOut = true,
         .desc = "Pure number: the wired number inputs subtracted in LINK "
                 "order (a - b - c). With one input wired it subtracts num[0] "
                 "(B) from it; with none it is -B."},
        {.key = "NumMul", .title = "Multiply", .category = "Math",
         .numCount = 1, .numLabels = {"B"}, .pure = true, .numIn = true,
         .numOut = true,
         .desc = "Pure number: every wired number input multiplied. With one "
                 "input wired it multiplies by num[0] (B); with none it is B."},
        {.key = "NumDiv", .title = "Divide", .category = "Math",
         .numCount = 1, .numLabels = {"B"}, .pure = true, .numIn = true,
         .numOut = true,
         .desc = "Pure number: the wired number inputs divided in LINK order "
                 "(a / b / c). With one input wired it divides by num[0] (B). "
                 "Division by zero yields 0 instead of a NaN."},
        {.key = "NumAtLeast", .title = "Number At Least", .category = "Math",
         .numCount = 1, .numLabels = {"Threshold"}, .pure = true,
         .boolOut = true, .numIn = true,
         .desc = "Pure bool: the wired number >= num[0] (Threshold) - the "
                 "bridge from the number plane into the logic gates and On "
                 "Condition."},
        {.key = "OpenMenu", .title = "Open Menu", .category = "Menus",
         .strKind = FlowParamKind::MenuName,
         .desc = "Opens the menu named str."},
        {.key = "OnMenuEvent", .title = "On Menu Event", .category = "Menus",
         .trigger = true, .strKind = FlowParamKind::Text, .boolOut = true,
         .desc = "Fires the frame a menu entry with Flow event name str is "
                 "selected. Also usable as a bool source."},
        {.key = "PosToText", .title = "Position To Text", .category = "Convert",
         .posIn = true, .pure = true, .textOut = true,
         .desc = "Pure converter: linked position -> text."},
        {.key = "BoolToText", .title = "Bool To Text", .category = "Convert",
         .pure = true, .boolIn = true, .textOut = true,
         .desc = "Pure converter: linked bool -> text."},
        {.key = "NumToText", .title = "Number To Text", .category = "Convert",
         .pure = true, .textOut = true, .numIn = true,
         .desc = "Pure converter: linked number -> text. Wire it into Display "
                 "Text to put a computed value on screen. Whole numbers print "
                 "without a decimal point."},
        {.key = "Log", .title = "Log Message", .category = "Debug",
         .strKind = FlowParamKind::Text, .textIn = true,
         .desc = "Prints str followed by every wired text input to the game "
                 "log (debug builds)."},
        // Logic gates: the bool input pin accepts several links - the gates
        // fold over all of them.
        {.key = "And", .title = "AND", .category = "Logic", .pure = true,
         .boolIn = true, .boolOut = true,
         .desc = "Pure bool gate: AND over all wired bool inputs."},
        {.key = "Nand", .title = "NAND", .category = "Logic", .pure = true,
         .boolIn = true, .boolOut = true,
         .desc = "Pure bool gate: NOT AND over all wired bool inputs."},
        {.key = "Or", .title = "OR", .category = "Logic", .pure = true,
         .boolIn = true, .boolOut = true,
         .desc = "Pure bool gate: OR over all wired bool inputs."},
        {.key = "Not", .title = "NOT", .category = "Logic", .pure = true,
         .boolIn = true, .boolOut = true,
         .desc = "Pure bool gate: NOT of the folded bool inputs."},
        {.key = "Xor", .title = "XOR", .category = "Logic", .pure = true,
         .boolIn = true, .boolOut = true,
         .desc = "Pure bool gate: XOR over all wired bool inputs."},
        {.key = "Xnor", .title = "XNOR", .category = "Logic", .pure = true,
         .boolIn = true, .boolOut = true,
         .desc = "Pure bool gate: NOT XOR over all wired bool inputs."},
        {.key = "OnCondition", .title = "On Condition", .category = "Logic",
         .trigger = true, .boolIn = true,
         .desc = "Trigger that fires on the RISING EDGE of its bool input - "
                 "the bridge from logic gates back to exec flow."},
    };
    return types;
}

// ---------------------------------------------------------------------------
// Node types retired by the exec-pin merge: each Show*/Hide*/Toggle* family
// collapsed into one node carrying a labeled exec pin per branch. A pre-merge
// graph would otherwise lose those nodes outright (readFlowGraph drops unknown
// types), so project::readFlowGraph rewrites the type and retargets every exec
// link landing on the node to `pin`.
struct FlowLegacyNode {
    const char* from;  // the retired FlowNode::type
    const char* to;    // its replacement
    int pin;           // exec input the old node's behavior now lives on
};

inline const std::vector<FlowLegacyNode>& flowLegacyNodes() {
    static const std::vector<FlowLegacyNode> v = {
        {"ShowObject", "SetObjectVisible", 0},
        {"HideObject", "SetObjectVisible", 1},
        {"ToggleObject", "SetObjectVisible", 2},
        {"ShowHud", "SetHudVisible", 0},
        {"HideHud", "SetHudVisible", 1},
        {"ToggleHud", "SetHudVisible", 2},
        {"ShowText", "SetTextVisible", 0},
        {"HideText", "SetTextVisible", 1},
        {"LoadLayer", "SetLayerLoaded", 0},
        {"UnloadLayer", "SetLayerLoaded", 1},
        {"PlayAnimation", "Animation", 0},
        {"StopAnimation", "Animation", 1},
    };
    return v;
}

inline const FlowLegacyNode* flowLegacyNode(const std::string& type) {
    for (const FlowLegacyNode& m : flowLegacyNodes())
        if (type == m.from) return &m;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Project-defined custom nodes (see flownode.cpp).
//
// A custom node is a user-authored *action* node loaded from a .flownode text
// file in <project>/flow-nodes/. It carries its own FlowNodeType plus the C++
// snippet emitted into flow_graph.gen.cpp when the node runs, so a project can
// add its own logic node without editing the editor's C++. Dropping the same
// .flownode file into another project's flow-nodes/ folder makes the node
// available there too (its filename is its identity - see `key`).
//
// Lifetime note: FlowNodeType stores raw `const char*` into the std::strings
// below, so a CustomFlowNode must never move once its type is handed out. The
// registry keeps them behind unique_ptr for stable addresses.
struct CustomFlowNode {
    std::string key;         // "custom:<file-stem>" - the serialized FlowNode::type
    std::string title;
    std::string category;
    std::string desc;        // `desc =` header key -> FlowNodeType::desc
    // Behavior: either an inline C++ snippet (`code`, emitted verbatim with
    // {placeholders} substituted) OR a call into a user function (`callFn`,
    // written in inc/scripts/flow_nodes.hpp, receiving a FlowNodeIO). `callFn`
    // wins when both are set; only `callFn` nodes can drive output pins.
    std::string code;
    std::string callFn;
    std::string numLabelStore[4];
    std::string sourceFile;  // absolute path of the .flownode file (diagnostics)
    FlowNodeType type{};     // char* fields point into the strings above
};

// The global custom-node registry, rebuilt on every project load
// (flownode::loadForProject). unique_ptr keeps each entry's address stable
// while its FlowNodeType hands out pointers into its strings.
inline std::vector<std::unique_ptr<CustomFlowNode>>& customFlowNodes() {
    static std::vector<std::unique_ptr<CustomFlowNode>> reg;
    return reg;
}

inline const CustomFlowNode* flowCustomNode(const std::string& key) {
    for (const auto& c : customFlowNodes())
        if (key == c->key) return c.get();
    return nullptr;
}

inline const FlowNodeType* flowNodeType(const std::string& key) {
    for (const auto& t : flowNodeTypes())
        if (key == t.key) return &t;
    if (const CustomFlowNode* c = flowCustomNode(key)) return &c->type;
    return nullptr;
}

// Built-in node types first, then the project's custom nodes - the order the
// add-menu and category derivation walk.
inline std::vector<const FlowNodeType*> flowAllNodeTypes() {
    std::vector<const FlowNodeType*> v;
    for (const auto& t : flowNodeTypes()) v.push_back(&t);
    for (const auto& c : customFlowNodes()) v.push_back(&c->type);
    return v;
}

// Categories in add-menu order (derived from the registry, first-seen order).
// Custom-node categories appear after the built-in ones.
inline std::vector<const char*> flowNodeCategories() {
    std::vector<const char*> cats;
    for (const FlowNodeType* t : flowAllNodeTypes()) {
        bool seen = false;
        for (const char* c : cats) seen |= (std::string(c) == t->category);
        if (!seen) cats.push_back(t->category);
    }
    return cats;
}

namespace flownode {

// Absolute path of a project's custom-node folder (<projectDir>/flow-nodes).
std::string dirForProject(const std::string& projectDir);

// Rebuilds the global customFlowNodes() registry from every *.flownode file in
// dirForProject(projectDir). Called by project::load BEFORE the graphs are
// parsed (readFlowGraph drops nodes whose type is unknown), and again by the
// editor's "Reload Custom Nodes" action. Returns a short human-readable
// summary / error list for the status bar.
std::string loadForProject(const std::string& projectDir);

// Writes a commented starter template into flow-nodes/example.flownode (never
// overwriting an existing file) so users have something to copy. Returns the
// file path, or an error string prefixed with "error:".
std::string writeExample(const std::string& projectDir);

}  // namespace flownode

// imnodes pin ids derived from node ids: `pin % kFlowPinSlots` encodes the pin
// KIND, `pin / kFlowPinSlots` the node. Slots 0..9 are the fixed pins (object-id
// in / exec out / exec in / object-id out / position in / position out / bool in
// / bool out / text in / text out), 10..15 a node's 2nd..7th exec input,
// 16..17 the number plane and 18..24 a node's 2nd..8th exec OUTPUT. Pin ids are
// never persisted, so widening the stride is safe - it went 8 -> 16 -> 32
// without touching a single project file.
constexpr int kFlowPinSlots = 32;

inline int flowIdInPin(int nodeId) { return nodeId * kFlowPinSlots; }
inline int flowOutPin(int nodeId) { return nodeId * kFlowPinSlots + 1; }
inline int flowInPin(int nodeId) { return nodeId * kFlowPinSlots + 2; }
inline int flowIdOutPin(int nodeId) { return nodeId * kFlowPinSlots + 3; }
inline int flowPosInPin(int nodeId) { return nodeId * kFlowPinSlots + 4; }
inline int flowPosOutPin(int nodeId) { return nodeId * kFlowPinSlots + 5; }
inline int flowBoolInPin(int nodeId) { return nodeId * kFlowPinSlots + 6; }
inline int flowBoolOutPin(int nodeId) { return nodeId * kFlowPinSlots + 7; }
inline int flowTextInPin(int nodeId) { return nodeId * kFlowPinSlots + 8; }
inline int flowTextOutPin(int nodeId) { return nodeId * kFlowPinSlots + 9; }
inline int flowNumInPin(int nodeId) { return nodeId * kFlowPinSlots + 16; }
inline int flowNumOutPin(int nodeId) { return nodeId * kFlowPinSlots + 17; }

// The two halves of a pin id. Everything that inspects a pin goes through
// these, so the stride lives in exactly one place.
inline int flowPinNode(int pin) { return pin / kFlowPinSlots; }
inline int flowPinKind(int pin) { return pin % kFlowPinSlots; }

// Exec input `pin` of a node: the first keeps the original slot 2, the rest
// take the spare slots 10..15 (hence kFlowMaxExecIn == 7).
inline int flowExecInPin(int nodeId, int pin) {
    return pin <= 0 ? nodeId * kFlowPinSlots + 2
                    : nodeId * kFlowPinSlots + 9 + pin;
}

// Inverse of flowExecInPin for a pin slot (flowPinKind): the exec input index,
// or -1 when the slot is not an exec input.
inline int flowExecInIndex(int slot) {
    if (slot == 2) return 0;
    if (slot >= 10 && slot < 10 + kFlowMaxExecIn - 1) return slot - 9;
    return -1;
}

// Exec output `pin` of a node: the first keeps the original slot 1 (so every
// link ever saved still resolves), the rest take the spare slots 18..24.
inline int flowExecOutPin(int nodeId, int pin) {
    return pin <= 0 ? nodeId * kFlowPinSlots + 1
                    : nodeId * kFlowPinSlots + 17 + pin;
}

// Inverse of flowExecOutPin for a pin slot: the exec output index, or -1 when
// the slot is not an exec output.
inline int flowExecOutIndex(int slot) {
    if (slot == 1) return 0;
    if (slot >= 18 && slot < 18 + kFlowMaxExecOut - 1) return slot - 17;
    return -1;
}

// How many exec outputs a node type actually has. The ONE answer read by the
// editor (which pins to submit), the link pruning and codegen - a node type
// declaring execOutCount and one relying on trigger/execThrough must not be
// two different questions.
inline int flowExecOutCount(const FlowNodeType& t) {
    if (t.pure) return 0;
    if (t.trigger) return 1;
    if (t.execOutCount > 0)
        return t.execOutCount > kFlowMaxExecOut ? kFlowMaxExecOut : t.execOutCount;
    return t.execThrough ? 1 : 0;
}

// Label shown on exec output `pin` of `t`. A trigger's lone output is "then",
// an execThrough action's is "after"; a multi-output node labels its own.
inline const char* flowExecOutLabel(const FlowNodeType& t, int pin) {
    if (t.execOutCount > 0 && pin >= 0 && pin < t.execOutCount &&
        t.execOutLabels[pin])
        return t.execOutLabels[pin];
    return t.trigger ? "then" : "after";
}

// A number input that FOLDS over several links (the Math nodes) rather than
// taking just the first one. Read by the editor (which prunes extra links on a
// single-value input) and by codegen (which folds), so the two cannot disagree.
inline bool flowNumFolds(const FlowNodeType& t) {
    return t.pure && t.numIn && t.numOut;
}

// Label shown on exec input `pin` of `t` ("> do" when the type declares none).
inline const char* flowExecInLabel(const FlowNodeType& t, int pin) {
    if (t.execInCount <= 1 || pin < 0 || pin >= t.execInCount) return "do";
    return t.execInLabels[pin] ? t.execInLabels[pin] : "do";
}
