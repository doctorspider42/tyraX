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
    CreditsName,   // name of a Project::credits roll (Tools > Credits Editor)
    HudTextName,  // name of a Project::hudTexts entry (baked text sprite)
    FontName,  // name of a Project::fonts entry (Tools > Font Manager)
    InputActionName,  // name of a Project::input action (Tools > Input Map)
    KeyName,   // a keyboard key label from inputKeyNames() ("Space", "F1")
    EventName,  // name of a graph event (free text; exists by being named)
    ScreenFxName,  // key of a Project::screenFx placement (custom .screenfx)
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
    // node is a number source.
    bool numIn = false;
    bool numOut = false;
    // The number input FOLDS over every wired link (a + b + c) instead of
    // taking only the first - the n-ary Math nodes (Add, Min, Modulo...). It
    // used to be inferred from `pure && numIn && numOut`, which was true of
    // every combining node but is wrong for a UNARY one (Absolute, Sine,
    // Clamp): those are pure, read a number and produce one, and a second
    // wired link would be silently ignored. So it is declared.
    bool numFold = false;
    // The wired number is an operand of its OWN rather than a replacement for
    // num[0]. That is the shape of every node whose SUBJECT is the wire:
    // Number At Least tests the wired value against its Threshold param, Clamp
    // holds the wired value between Min and Max. Without saying so the editor
    // hides those params and claims they come "from link", while codegen keeps
    // reading what was typed - a silent disagreement between the two.
    bool numInExtra = false;
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
         .idOut = true, .posIn = true,
         .desc = "Instantly turns the target by (dX, dY, dZ) DEGREES about the "
                 "world X/Y/Z axes (dY = yaw); a linked position carries the "
                 "delta as a 3-vector. A one-shot: for something that keeps "
                 "turning use Spin Object rather than firing this every "
                 "frame."},
        {.key = "SetRotation", .title = "Set Object Rotation",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .numCount = 3, .numLabels = {"X", "Y", "Z"}, .idIn = true,
         .idOut = true, .posIn = true,
         .desc = "Sets the target's rotation to X/Y/Z degrees (absolute; Y is "
                 "the yaw), or to a linked position read as a rotation triple - "
                 "which is what makes Get Object Rotation -> With Y -> Set "
                 "Object Rotation change just the heading. Applied in the order "
                 "X, then Y, then Z - the same as the Properties panel's "
                 "Rotation."},
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
        // Scale and rotation as READABLE values, and the two writers the scale
        // family was missing. Scale and rotation ride the POSITION plane as
        // 3-vectors - it is the plane that already carries three floats, so
        // "read a rotation, change its Y, write it back" is Get Object Rotation
        // -> With Y -> Set Object Rotation with no new machinery.
        {.key = "SetScale", .title = "Set Object Scale", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .numCount = 3,
         .numLabels = {"X", "Y", "Z"}, .idIn = true, .idOut = true,
         .posIn = true,
         .desc = "Sets the target's scale to X/Y/Z (a linked position overrides "
                 "the params, so a computed size works). Scale 0 on an axis "
                 "flattens the object rather than hiding it - use Set Object "
                 "Visible for that."},
        {.key = "ScaleObjectBy", .title = "Scale Object By",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .numCount = 1, .numLabels = {"Factor"}, .idIn = true, .idOut = true,
         .numIn = true,
         .desc = "Multiplies the target's scale on all three axes by num[0] "
                 "(Factor), or by a linked number. Factor 1 changes nothing; "
                 "wire a Tween into it for a grow/shrink pop."},
        {.key = "GetScale", .title = "Get Object Scale", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .idIn = true, .idOut = true,
         .posOut = true, .pure = true,
         .desc = "Pure: the target's scale as a 3-vector on the position plane. "
                 "Read a component out of it with Get X / Get Y / Get Z."},
        {.key = "GetRotation", .title = "Get Object Rotation",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .idIn = true, .idOut = true, .posOut = true, .pure = true,
         .desc = "Pure: the target's rotation in DEGREES as a 3-vector on the "
                 "position plane (Y is the yaw). Get Object Rotation -> With Y "
                 "-> Set Object Rotation changes just the heading."},
        {.key = "LookAt", .title = "Look At", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .numCount = 1,
         .numLabels = {"Tilt too"}, .idIn = true, .idOut = true, .posIn = true,
         .desc = "Turns the target to face the linked position. num[0] Tilt too "
                 "= 0 rotates the yaw only (the normal case: a character, a "
                 "turret base, a signpost stays upright), 1 also pitches it up "
                 "or down at the point. Wire Player Position into it for an NPC "
                 "that watches you, or a Get Position for one prop aiming at "
                 "another."},
        {.key = "ObjDistance", .title = "Distance To Object",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .idIn = true, .posIn = true, .pure = true, .numOut = true,
         .desc = "Pure number: the distance from the target object to the linked "
                 "position. With Player Position wired in it is \"how far away "
                 "is the player\" as a VALUE - which Near Object (a trigger with "
                 "a fixed radius) cannot give you. Feeds the comparators, Remap "
                 "Range, a fade, an AI decision."},
        // Physics reads and writes. Velocities on RuntimeObject are per-frame
        // displacements, so codegen converts to and from units/SECOND here -
        // a graph should never have to know the frame rate.
        {.key = "SetVelocity", .title = "Set Velocity", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .numCount = 3,
         .numLabels = {"X", "Y", "Z"}, .idIn = true, .idOut = true,
         .posIn = true,
         .desc = "Sets a physics body's velocity to X/Y/Z units per SECOND (a "
                 "linked position overrides the params) and wakes it. Unlike "
                 "Apply Impulse, which ADDS to whatever the body was already "
                 "doing, this replaces it - use it to stop a body dead (0,0,0) "
                 "or to launch one at an exact speed."},
        {.key = "GetVelocity", .title = "Get Velocity", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .idIn = true, .idOut = true,
         .posOut = true, .pure = true,
         .desc = "Pure: a physics body's velocity in units per SECOND as a "
                 "3-vector. Get Y of it is the fall speed - the input for fall "
                 "damage or a landing sound."},
        {.key = "StopMotion", .title = "Stop Motion", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .idIn = true, .idOut = true,
         .desc = "Zeroes a physics body's velocity AND its tumble, and clears "
                 "any Spin Object rate - everything that was moving the object "
                 "on its own. It does not put the body to sleep, so gravity "
                 "still applies."},
        {.key = "SetUsable", .title = "Set Object Usable", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .idIn = true, .idOut = true,
         .execInCount = 2, .execInLabels = {"on", "off"},
         .desc = "Turns the target's USE prompt on or off at runtime - a door "
                 "that only becomes usable once you have the key, a lever that "
                 "stops working after it is pulled."},
        {.key = "IsActive", .title = "Is Object Active", .category = "Object",
         .strKind = FlowParamKind::ObjectName, .idIn = true, .idOut = true,
         .pure = true, .boolOut = true,
         .desc = "Pure bool: is the target in the game at all this frame? False "
                 "while its streaming layer is unloaded, or after Despawn "
                 "Object. Different from Is Visible, which is about being "
                 "DRAWN - an active object can be invisible."},
        {.key = "FindNearest", .title = "Find Nearest", .category = "Object",
         .strKind = FlowParamKind::Text, .numCount = 1,
         .numLabels = {"Max Dist"}, .idOut = true, .posIn = true,
         .posOut = true, .execOutCount = 1, .execOutLabels = {"then"},
         .desc = "On exec, finds the nearest ACTIVE scene object whose name "
                 "starts with prefix str, measured from the linked position, "
                 "and LATCHES it: the object output is that object (none if "
                 "nothing is within num[0] Max Dist, or Max Dist 0 = no limit) "
                 "and the position output is where it is. 'then' fires right "
                 "after. The runtime counterpart of naming an object in a param "
                 "- \"the closest pickup\", \"the nearest waypoint\"."},
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
        // The player as a readable thing. Until these existed a graph could
        // teleport the player but not ask where they were.
        {.key = "PlayerPos", .title = "Player Position", .category = "Player",
         .numCount = 1, .numLabels = {"Player"}, .posOut = true, .pure = true,
         .desc = "Pure position: where the player is this frame (the eye/camera "
                 "position). num[0] Player: 0 = player 1, 1 = player 2 (which "
                 "reads as player 1's position while player 2 is not in the "
                 "game, so \"nearest player\" logic works unconditionally). Wire "
                 "it into Look At, Distance To Object, Spawn Object."},
        {.key = "PlayerLook", .title = "Player Look Direction",
         .category = "Player", .posOut = true, .pure = true,
         .desc = "Pure position used as a DIRECTION: the unit vector the player "
                 "is looking along. Scale Position it and Offset Position from "
                 "Player Position to get a point out in front of the player - "
                 "where to spawn a projectile, where to drop a marker."},
        {.key = "PlayerFallSpeed", .title = "Player Fall Speed",
         .category = "Player", .pure = true, .numOut = true,
         .desc = "Pure number: the player's vertical speed in units per SECOND "
                 "- negative while falling, positive on the way up, 0 on the "
                 "ground. Number At Most a big negative value is a fall-damage "
                 "test; wire it through Absolute for a landing thump."},
        {.key = "SetPlayerInput", .title = "Set Player Input",
         .category = "Player", .execInCount = 2,
         .execInLabels = {"lock", "unlock"},
         .desc = "'lock' takes the controls away from the player and 'unlock' "
                 "gives them back - what a dialogue, a scripted moment or a "
                 "cutscene that still shows the avatar needs. Only INPUT is "
                 "taken: gravity, collision and the camera keep running, so a "
                 "locked player still falls and is still framed instead of "
                 "freezing in mid-air. A scene load always unlocks."},
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
        // ------------------------------------------------------------------
        // Camera and presentation. Everything here rides fields the Cutscene
        // Director already publishes on ScriptContext, so a graph gets the
        // cinematic vocabulary without a second camera system - and a running
        // cutscene always wins, because its player rewrites those fields every
        // frame and clears them when it ends.
        {.key = "SetCamera", .title = "Set Camera", .category = "Camera",
         .strKind = FlowParamKind::ObjectName, .idIn = true, .posIn = true,
         .desc = "Takes the camera over: the linked position becomes the EYE and "
                 "the target object is what it looks AT. Fired from On Start it "
                 "is a fixed camera for the room; fired from On Update it "
                 "tracks. Release Camera gives control back. A playing cutscene "
                 "overrides it for as long as it runs."},
        {.key = "CameraFromObject", .title = "Camera From Object",
         .category = "Camera", .strKind = FlowParamKind::ObjectName,
         .idIn = true, .idOut = true,
         .desc = "Cuts the camera to the target object: its position is the eye "
                 "and its own rotation is the aim (the +Z lens direction, the "
                 "same convention the Cutscene Director's Camera entities use - "
                 "so a Camera object placed and aimed in the viewport is exactly "
                 "what you get). Move or rotate that object and the shot follows."},
        {.key = "ReleaseCamera", .title = "Release Camera",
         .category = "Camera",
         .desc = "Hands the camera back to the player/game. Also un-tilts any "
                 "roll a shot had applied."},
        {.key = "CameraShake", .title = "Camera Shake", .category = "Camera",
         .numCount = 2, .numLabels = {"Amplitude", "Seconds"}, .numIn = true,
         .desc = "Shakes the camera by num[0] (Amplitude) world units for num[1] "
                 "(Seconds), easing out at the end. Applies to whatever camera "
                 "is in force, a cutscene's included. Amplitude 0 stops it. Both "
                 "the eye and the aim move together, so the shot wobbles instead "
                 "of swinging."},
        {.key = "SetFade", .title = "Set Screen Fade", .category = "Camera",
         .numCount = 1, .numLabels = {"Amount"}, .numIn = true,
         .desc = "Black overlay over everything (0 = clear, 1 = fully black). "
                 "Wire a Tween into it for a real fade: Tween 0 -> 1 over a "
                 "second into Set Screen Fade, then its 'finished' output "
                 "switches the scene. Survives across frames until something "
                 "changes it."},
        {.key = "SetBars", .title = "Set Letterbox Bars", .category = "Camera",
         .numCount = 2, .numLabels = {"Style", "Amount"}, .numIn = true,
         .desc = "Masks the frame with black bars. num[0] Style: 0 none, 1 "
                 "cinema 2.39:1, 2 wide 16:9, 3 pillarbox, 4 frame. num[1] "
                 "Amount 0..1 of that style's full coverage - wire a Tween into "
                 "it to slide them in. A playing cutscene's own bars win."},
        {.key = "SetPlayerVisible", .title = "Set Player Visible",
         .category = "Camera", .execInCount = 2,
         .execInLabels = {"show", "hide"},
         .desc = "Shows or hides the third-person avatar (no effect in "
                 "first-person or noclip, which have no visible body). For a "
                 "scripted camera move that should fly free without the "
                 "character in shot."},
        {.key = "OnSequenceEnd", .title = "On Sequence Finished",
         .category = "Camera", .trigger = true, .boolOut = true,
         .desc = "Fires the frame a cutscene stops - whether it ran out, was "
                 "stopped by Stop Sequence, or the player skipped it. THE way to "
                 "chain \"play the cutscene, then carry on\". Its bool output is "
                 "the live \"a cutscene is playing right now\" condition, so you "
                 "can gate gameplay logic out while one runs."},
        {.key = "PlaySequence", .title = "Play Sequence", .category = "Scene",
         .strKind = FlowParamKind::SequenceName,
         .desc = "Starts the cutscene sequence named str; retriggering "
                 "restarts it."},
        {.key = "StopSequence", .title = "Stop Sequence", .category = "Scene",
         .desc = "Stops the active cutscene."},
        // Credits (docs/credits.md). A rolling credits screen owns the whole
        // frame, so unlike a cutscene it is not something the graph keeps
        // driving: it starts here and reports back through On Credits Finished.
        {.key = "PlayCredits", .title = "Play Credits", .category = "Scene",
         .strKind = FlowParamKind::CreditsName,
         .desc = "Starts the credits roll named str (Tools > Credits Editor). "
                 "The roll takes over the screen and the pad until it ends or "
                 "the player skips it - gameplay, scripts and this graph are "
                 "frozen meanwhile - and then runs its own finish action (stay "
                 "in the game, switch scene, open a menu, fire a flow event)."},
        {.key = "StopCredits", .title = "Stop Credits", .category = "Scene",
         .desc = "Ends the rolling credits immediately, exactly as a player's "
                 "skip does - including the roll's finish action."},
        {.key = "OnCreditsEnd", .title = "On Credits Finished",
         .category = "Triggers", .trigger = true, .boolOut = true,
         .desc = "Fires the frame a credits roll stops - whether it ran out, "
                 "was skipped or was stopped by a node. Its bool output is "
                 "\"credits are rolling right now\"."},
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
        {.key = "SetSfxVolume", .title = "Set Sound Volume",
         .category = "Audio", .numCount = 1, .numLabels = {"Volume"},
         .numIn = true,
         .desc = "Master sound-effect volume num[0] 0..100 (100 = unscaled). "
                 "Rides on top of every Play Sound's own volume and every sound "
                 "emitter, so it is the one place to duck the effects under a "
                 "cutscene or a dialogue. Music has its own Set Music Volume."},
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
        {.key = "ValueAtMost", .title = "Value At Most", .category = "Save",
         .strKind = FlowParamKind::SaveValue, .numCount = 1,
         .numLabels = {"Threshold"}, .pure = true, .boolOut = true,
         .numIn = true,
         .desc = "Pure bool: save value named str <= num[0], evaluated fresh "
                 "every frame. The other half of Value At Least - together they "
                 "bound a range, and on their own they are \"out of lives\" and "
                 "\"full health\"."},
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
         .numFold = true,
         .desc = "Pure number: every wired number input summed. With exactly "
                 "ONE input wired it adds num[0] (B) to it - Get Int -> Add "
                 "(B 1) -> Set Int is the read-modify-write counter. With none "
                 "wired it is just B."},
        {.key = "NumSub", .title = "Subtract", .category = "Math",
         .numCount = 1, .numLabels = {"B"}, .pure = true, .numIn = true,
         .numOut = true, .numFold = true,
         .desc = "Pure number: the wired number inputs subtracted in LINK "
                 "order (a - b - c). With one input wired it subtracts num[0] "
                 "(B) from it; with none it is -B."},
        {.key = "NumMul", .title = "Multiply", .category = "Math",
         .numCount = 1, .numLabels = {"B"}, .pure = true, .numIn = true,
         .numOut = true, .numFold = true,
         .desc = "Pure number: every wired number input multiplied. With one "
                 "input wired it multiplies by num[0] (B); with none it is B."},
        {.key = "NumDiv", .title = "Divide", .category = "Math",
         .numCount = 1, .numLabels = {"B"}, .pure = true, .numIn = true,
         .numOut = true, .numFold = true,
         .desc = "Pure number: the wired number inputs divided in LINK order "
                 "(a / b / c). With one input wired it divides by num[0] (B). "
                 "Division by zero yields 0 instead of a NaN."},
        {.key = "NumAtLeast", .title = "Number At Least", .category = "Math",
         .numCount = 1, .numLabels = {"Threshold"}, .pure = true,
         .boolOut = true, .numIn = true, .numInExtra = true,
         .desc = "Pure bool: the wired number >= num[0] (Threshold) - the "
                 "bridge from the number plane into the logic gates and On "
                 "Condition."},
        // The rest of the number plane's arithmetic. The n-ary ones fold over
        // every wired link (.numFold), the unary ones read only the first - a
        // distinction the editor's link pruning and codegen both take from
        // that one flag.
        {.key = "NumMin", .title = "Min", .category = "Math", .numCount = 1,
         .numLabels = {"B"}, .pure = true, .numIn = true, .numOut = true,
         .numFold = true,
         .desc = "Pure number: the SMALLEST of every wired number input. With "
                 "one input wired it is min(input, num[0]) - the usual way to "
                 "cap a value; with none it is B."},
        {.key = "NumMax", .title = "Max", .category = "Math", .numCount = 1,
         .numLabels = {"B"}, .pure = true, .numIn = true, .numOut = true,
         .numFold = true,
         .desc = "Pure number: the LARGEST of every wired number input. With "
                 "one input wired it is max(input, num[0]) - a floor under a "
                 "value; with none it is B."},
        {.key = "NumMod", .title = "Modulo", .category = "Math", .numCount = 1,
         .numLabels = {"B"}, .pure = true, .numIn = true, .numOut = true,
         .numFold = true,
         .desc = "Pure number: the wired inputs taken modulo each other in LINK "
                 "order (a % b % c). With one input wired it is input % num[0] "
                 "- the wrap-around for cycling through N states or keeping an "
                 "angle inside 360. A zero divisor yields 0, not a NaN."},
        {.key = "NumPow", .title = "Power", .category = "Math", .numCount = 1,
         .numLabels = {"Exponent"}, .pure = true, .numIn = true, .numOut = true,
         .numFold = true,
         .desc = "Pure number: the wired inputs raised to each other in LINK "
                 "order. With one input wired it is input ^ num[0] (Exponent); "
                 "Exponent 2 squares, 0.5 is a square root."},
        {.key = "NumAbs", .title = "Absolute", .category = "Math", .pure = true,
         .numIn = true, .numOut = true,
         .desc = "Pure number: the wired number without its sign. The distance "
                 "of a value from zero - pair it with Number At Most for \"is "
                 "it close to X\"."},
        {.key = "NumNeg", .title = "Negate", .category = "Math", .pure = true,
         .numIn = true, .numOut = true,
         .desc = "Pure number: the wired number with its sign flipped."},
        {.key = "NumSign", .title = "Sign", .category = "Math", .pure = true,
         .numIn = true, .numOut = true,
         .desc = "Pure number: -1 if the wired number is negative, 1 if "
                 "positive, 0 if exactly zero. The direction of a value without "
                 "its size."},
        {.key = "NumFloor", .title = "Floor", .category = "Math", .pure = true,
         .numIn = true, .numOut = true,
         .desc = "Pure number: the wired number rounded DOWN to a whole "
                 "number."},
        {.key = "NumCeil", .title = "Ceiling", .category = "Math", .pure = true,
         .numIn = true, .numOut = true,
         .desc = "Pure number: the wired number rounded UP to a whole number."},
        {.key = "NumRound", .title = "Round", .category = "Math", .pure = true,
         .numIn = true, .numOut = true,
         .desc = "Pure number: the wired number rounded to the NEAREST whole "
                 "number."},
        {.key = "NumSqrt", .title = "Square Root", .category = "Math",
         .pure = true, .numIn = true, .numOut = true,
         .desc = "Pure number: the square root of the wired number (0 for a "
                 "negative input rather than a NaN)."},
        {.key = "NumClamp", .title = "Clamp", .category = "Math", .numCount = 2,
         .numLabels = {"Min", "Max"}, .pure = true, .numIn = true,
         .numOut = true, .numInExtra = true,
         .desc = "Pure number: the wired number held between num[0] (Min) and "
                 "num[1] (Max). The guard to put in front of anything that "
                 "must stay in range - a health bar, a volume, a camera "
                 "distance."},
        {.key = "NumLerp", .title = "Lerp", .category = "Math", .numCount = 2,
         .numLabels = {"From", "To"}, .pure = true, .numIn = true,
         .numOut = true, .numInExtra = true,
         .desc = "Pure number: blends from num[0] (From) to num[1] (To) by the "
                 "wired number, which is the 0..1 fraction (clamped). The "
                 "manual counterpart of Tween Value - use it when you already "
                 "have the fraction (a Timer's elapsed / its duration)."},
        {.key = "NumRemap", .title = "Remap Range", .category = "Math",
         .numCount = 4, .numLabels = {"In min", "In max", "Out min", "Out max"},
         .pure = true, .numIn = true, .numOut = true, .numInExtra = true,
         .desc = "Pure number: rescales the wired number from the range "
                 "num[0]..num[1] onto num[2]..num[3] (clamped to the output "
                 "range). The one node between \"a distance in world units\" "
                 "and \"a 0..1 amount\" that everything else wants."},
        {.key = "NumSin", .title = "Sine", .category = "Math", .pure = true,
         .numIn = true, .numOut = true,
         .desc = "Pure number: the sine of the wired number, taken as DEGREES "
                 "(so 90 = 1). With Rotate Around Y it is how you place things "
                 "on a circle."},
        {.key = "NumCos", .title = "Cosine", .category = "Math", .pure = true,
         .numIn = true, .numOut = true,
         .desc = "Pure number: the cosine of the wired number, taken as "
                 "DEGREES (so 0 = 1)."},
        // Comparators. Number At Least already covers >=; these are the rest of
        // the bridge from the value plane into the logic gates.
        {.key = "NumAtMost", .title = "Number At Most", .category = "Math",
         .numCount = 1, .numLabels = {"Threshold"}, .pure = true,
         .boolOut = true, .numIn = true, .numInExtra = true,
         .desc = "Pure bool: the wired number <= num[0] (Threshold)."},
        {.key = "NumEquals", .title = "Number Equals", .category = "Math",
         .numCount = 2, .numLabels = {"Value", "Tolerance"}, .pure = true,
         .boolOut = true, .numIn = true, .numInExtra = true,
         .desc = "Pure bool: the wired number equals num[0] (Value) within "
                 "num[1] (Tolerance). Floats almost never land on an exact "
                 "value, so the tolerance is a parameter rather than a hidden "
                 "epsilon - 0.5 tests \"rounds to Value\"."},
        {.key = "NumInRange", .title = "Number In Range", .category = "Math",
         .numCount = 2, .numLabels = {"Min", "Max"}, .pure = true,
         .boolOut = true, .numIn = true, .numInExtra = true,
         .desc = "Pure bool: the wired number is between num[0] (Min) and "
                 "num[1] (Max), both ends included."},
        // Time. Both read the graph's own clock - seconds since the scene this
        // graph belongs to was (re)loaded.
        {.key = "SceneTime", .title = "Scene Time", .category = "Math",
         .pure = true, .numOut = true,
         .desc = "Pure number: SECONDS since this scene started (this graph's "
                 "own clock, so it resets with the scene and rewinds with the "
                 "time machine). Wire it into Number To Text for a play "
                 "timer, or into Sine for anything that should pulse."},
        {.key = "FrameTime", .title = "Frame Time", .category = "Math",
         .pure = true, .numOut = true,
         .desc = "Pure number: how long the LAST frame took, in seconds. "
                 "Multiply a per-second rate by it to make a per-frame step "
                 "frame-rate independent."},
        {.key = "Oscillate", .title = "Oscillate", .category = "Math",
         .numCount = 3, .numLabels = {"Amplitude", "Hz", "Offset"},
         .pure = true, .numOut = true,
         .desc = "Pure number: a sine wave riding the scene clock - num[2] "
                 "(Offset) plus num[0] (Amplitude) times a wave repeating "
                 "num[1] (Hz) times a second. THE node for a pulsing light, a "
                 "breathing glow, a bobbing prop: it costs one sinf where doing "
                 "it by hand costs a graph running every frame."},
        // Random needs a moment, not an expression: a pure random would re-roll
        // on every read, so two consumers of \"the same\" number would disagree.
        // So it is an action that LATCHES its roll, with an exec output to
        // sequence what reads it.
        {.key = "RollRandom", .title = "Roll Random", .category = "Math",
         .numCount = 3, .numLabels = {"Min", "Max", "Whole"}, .numOut = true,
         .execOutCount = 1, .execOutLabels = {"then"},
         .desc = "On exec, rolls a random number between num[0] (Min) and "
                 "num[1] (Max) and LATCHES it on the number output; the 'then' "
                 "output fires right after, so whatever reads the roll runs "
                 "after it. num[2] Whole = 1 rounds to a whole number (a dice "
                 "roll, a random index for Switch Number). The number output "
                 "keeps the last roll until the next exec - it is a value, not "
                 "a fresh surprise per read."},
        {.key = "OpenMenu", .title = "Open Menu", .category = "Menus",
         .strKind = FlowParamKind::MenuName,
         .desc = "Opens the menu named str."},
        {.key = "OnMenuEvent", .title = "On Menu Event", .category = "Menus",
         .trigger = true, .strKind = FlowParamKind::Text, .boolOut = true,
         .desc = "Fires the frame a menu entry with Flow event name str is "
                 "selected. Also usable as a bool source."},
        // ------------------------------------------------------------------
        // The position plane's own arithmetic. A position INPUT takes exactly
        // one link (unlike the bool/number planes), so these are all unary:
        // one position in, one out, with the second operand as params or on the
        // number plane. Chained, they compose - Position -> With X -> Offset ->
        // Snap To Terrain is a computed spawn point.
        {.key = "PosConst", .title = "Position", .category = "Vector",
         .numCount = 3, .numLabels = {"X", "Y", "Z"}, .posOut = true,
         .pure = true,
         .desc = "Pure position: the constant (X, Y, Z). The literal of the "
                 "position plane - the starting point to feed the other Vector "
                 "nodes when you are not reading one off an object."},
        {.key = "PosGetX", .title = "Get X", .category = "Vector", .posIn = true,
         .pure = true, .numOut = true,
         .desc = "Pure number: the X of the linked position. With Get Position "
                 "this is how a coordinate reaches the Math nodes."},
        {.key = "PosGetY", .title = "Get Y", .category = "Vector", .posIn = true,
         .pure = true, .numOut = true,
         .desc = "Pure number: the Y (height) of the linked position."},
        {.key = "PosGetZ", .title = "Get Z", .category = "Vector", .posIn = true,
         .pure = true, .numOut = true,
         .desc = "Pure number: the Z of the linked position."},
        {.key = "PosWithX", .title = "With X", .category = "Vector",
         .numCount = 1, .numLabels = {"X"}, .posIn = true, .posOut = true,
         .pure = true, .numIn = true,
         .desc = "Pure position: the linked position with its X replaced by "
                 "num[0] (or by a wired number). Chain With X / With Y / With Z "
                 "to build a position out of computed numbers."},
        {.key = "PosWithY", .title = "With Y", .category = "Vector",
         .numCount = 1, .numLabels = {"Y"}, .posIn = true, .posOut = true,
         .pure = true, .numIn = true,
         .desc = "Pure position: the linked position with its Y (height) "
                 "replaced by num[0] or a wired number."},
        {.key = "PosWithZ", .title = "With Z", .category = "Vector",
         .numCount = 1, .numLabels = {"Z"}, .posIn = true, .posOut = true,
         .pure = true, .numIn = true,
         .desc = "Pure position: the linked position with its Z replaced by "
                 "num[0] or a wired number."},
        {.key = "PosOffset", .title = "Offset Position", .category = "Vector",
         .numCount = 3, .numLabels = {"dX", "dY", "dZ"}, .posIn = true,
         .posOut = true, .pure = true,
         .desc = "Pure position: the linked position shifted by (dX, dY, dZ). "
                 "\"Two units above that object\" is Get Position -> Offset "
                 "Position (dY 2)."},
        {.key = "PosScale", .title = "Scale Position", .category = "Vector",
         .numCount = 1, .numLabels = {"Factor"}, .posIn = true, .posOut = true,
         .pure = true, .numIn = true,
         .desc = "Pure position: the linked position multiplied by num[0] "
                 "(Factor), or by a wired number. Scaling a position treats it "
                 "as a direction from the world origin - it is the node for "
                 "lengthening an offset, not for moving a point."},
        {.key = "PosRotateY", .title = "Rotate Around Y", .category = "Vector",
         .numCount = 1, .numLabels = {"Degrees"}, .posIn = true, .posOut = true,
         .pure = true, .numIn = true,
         .desc = "Pure position: the linked position turned num[0] DEGREES "
                 "about the world Y axis (through the origin). Offset Position "
                 "-> Rotate Around Y -> Offset Position back is how you put "
                 "things on a ring; feed the angle from a For Loop's index x "
                 "(360 / count) to place a whole circle of them."},
        {.key = "PosDistance", .title = "Distance To Point",
         .category = "Vector", .numCount = 3, .numLabels = {"X", "Y", "Z"},
         .posIn = true, .pure = true, .numOut = true,
         .desc = "Pure number: the straight-line distance from the linked "
                 "position to (X, Y, Z). Wire it through Number At Most for a "
                 "proximity test the Near Object trigger cannot express (any "
                 "two points, not the player and an object)."},
        {.key = "PosTerrainY", .title = "Terrain Height At",
         .category = "Vector", .posIn = true, .pure = true, .numOut = true,
         .desc = "Pure number: the ground height under the linked position - "
                 "the same bilinear heightmap the player walks on."},
        {.key = "PosOnTerrain", .title = "Snap To Terrain",
         .category = "Vector", .numCount = 1, .numLabels = {"Lift"},
         .posIn = true, .posOut = true, .pure = true,
         .desc = "Pure position: the linked position with its Y set to the "
                 "ground height there plus num[0] (Lift). The node to put in "
                 "front of Spawn Object so a computed spawn point lands ON the "
                 "terrain instead of inside it or in mid-air."},
        // Same reasoning as Roll Random: a pure random point would be a
        // different point per read.
        {.key = "RollAreaPoint", .title = "Roll Point In Area",
         .category = "Vector", .strKind = FlowParamKind::AreaName,
         .posOut = true, .execOutCount = 1, .execOutLabels = {"then"},
         .desc = "On exec, picks a random point inside the Area object named "
                 "str and LATCHES it on the position output; 'then' fires right "
                 "after. Spawn scattering, wander targets, random patrol - the "
                 "volume is read live, so a moving area moves the scatter with "
                 "it."},
        {.key = "SetScreenFx", .title = "Set Screen Effect",
         .category = "Scene", .strKind = FlowParamKind::ScreenFxName,
         .numCount = 4, .numLabels = {"P1", "P2", "P3", "P4"}, .numIn = true,
         .execInCount = 3, .execInLabels = {"set", "on", "off"},
         .desc = "Drives one of the project's custom screen effects (Tools > UI "
                 "Editor > screen stack, authored as a .screenfx file). The "
                 "'set' pin writes its four parameters - whatever that effect "
                 "declared, shown by name on the node - and 'on'/'off' switch "
                 "the effect itself. A linked number overrides P1, so a Tween "
                 "can ramp the first parameter. Until this existed a .screenfx "
                 "effect was frozen at whatever the editor authored."},
        {.key = "RestartScene", .title = "Restart Scene", .category = "Scene",
         .desc = "Reloads the CURRENT scene from scratch: objects back to their "
                 "authored transforms, graph state reset, spawned clones freed. "
                 "Flow variables and save values survive (they are game-global) "
                 "- the death-and-retry of a scene without naming it."},
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
        {.key = "NumToTextFmt", .title = "Number To Text (formatted)",
         .category = "Convert", .numCount = 2,
         .numLabels = {"Decimals", "Min digits"}, .pure = true, .textOut = true,
         .numIn = true, .numInExtra = true,
         .desc = "Pure converter with control over the shape: num[0] Decimals "
                 "(0 = a whole number) and num[1] Min digits, zero-padded - so a "
                 "score reads 00420 and a lap time 1.25. The wired number is the "
                 "VALUE; both params stay editable."},
        {.key = "SecondsToText", .title = "Seconds To Clock",
         .category = "Convert", .numCount = 1, .numLabels = {"Show tenths"},
         .pure = true, .textOut = true, .numIn = true, .numInExtra = true,
         .desc = "Pure converter: a number of seconds -> \"M:SS\" (or "
                 "\"M:SS.t\" with num[0] Show tenths). Wire a Timer straight "
                 "into it for a countdown or a lap clock on screen; negative "
                 "input clamps to 0:00."},
        {.key = "TextJoin", .title = "Join Text", .category = "Convert",
         .strKind = FlowParamKind::Text, .pure = true, .textIn = true,
         .textOut = true,
         .desc = "Pure text: every wired text input joined in link order, with "
                 "str between them as a separator (empty = straight "
                 "concatenation, \" - \" or \", \" for a list). The way to build "
                 "one Display Text out of several values."},
        {.key = "TextEquals", .title = "Text Equals", .category = "Convert",
         .strKind = FlowParamKind::Text, .pure = true, .boolOut = true,
         .textIn = true,
         .desc = "Pure bool: the first wired text input equals str exactly (case "
                 "sensitive). Compares a save text against a known value - a "
                 "chosen name, a stored difficulty, a quest state."},
        // ------------------------------------------------------------------
        // The event bus: the ONE way one object's graph talks to another's.
        // Before it existed the only channel was a global variable polled from
        // On Update, which is both slower and impossible to read as intent.
        // Delivery is deliberately ONE FRAME later and uniform for every
        // receiver - see the desc.
        {.key = "SendEvent", .title = "Send Event", .category = "Events",
         .strKind = FlowParamKind::EventName, .numCount = 1,
         .numLabels = {"Value"}, .numIn = true,
         .desc = "Broadcasts the event named str to EVERY graph in the game, "
                 "with num[0] (Value) as an optional number payload (a linked "
                 "number overrides it). Every On Event node of that name fires "
                 "on the NEXT frame - uniformly, whichever object owns it, so "
                 "the order graphs happen to run in can never change the "
                 "outcome. That one frame (20 ms) is the price of that "
                 "guarantee; for something that must happen inside the same "
                 "frame, wire the action directly."},
        {.key = "OnEvent", .title = "On Event", .category = "Events",
         .trigger = true, .strKind = FlowParamKind::EventName, .boolOut = true,
         .numOut = true,
         .desc = "Fires the frame after any graph sends the event named str. Its "
                 "number output is the Value that came with it, and its bool "
                 "output is \"the event arrived this frame\". Events are how a "
                 "pickup tells the HUD, a switch tells three doors, or a boss "
                 "tells the music - without any of them naming the others."},
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

// A number input that FOLDS over several links (the n-ary Math nodes) rather
// than taking just the first one. Read by the editor (which prunes extra links
// on a single-value input) and by codegen (which folds), so the two cannot
// disagree.
inline bool flowNumFolds(const FlowNodeType& t) {
    return t.numFold && t.numIn && t.numOut;
}

// Label shown on exec input `pin` of `t` ("> do" when the type declares none).
inline const char* flowExecInLabel(const FlowNodeType& t, int pin) {
    if (t.execInCount <= 1 || pin < 0 || pin >= t.execInCount) return "do";
    return t.execInLabels[pin] ? t.execInLabels[pin] : "do";
}
