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
    PrefabName,  // name of a Project::prefabs entry (Tools > Prefabs)
    EventName,  // name of a graph event (free text; exists by being named)
    FactName,   // name of a Project::facts entry (Tools > World Facts)
    FactQueryName,  // name of a Project::factQueries entry (a named condition)
    ScreenFxName,  // key of a Project::screenFx placement (custom .screenfx)
    // Which slot a Commit Checkpoint writes: "" / "fixed" = the Slot number
    // below it, "autosave" = the project's autosave slot, "next" = the next
    // free one. "" is fixed so a graph written before this existed is
    // unchanged. A closed list, not a project lookup - see saveSlotModes().
    SaveSlotMode,
};

// The SaveSlotMode choices. Order is cosmetic; the KEY is what a graph stores.
struct SaveSlotModeInfo {
    const char* key;
    const char* label;
    const char* desc;
};
inline const std::vector<SaveSlotModeInfo>& saveSlotModes() {
    static const std::vector<SaveSlotModeInfo> modes = {
        {"fixed", "This slot",
         "Always the Slot number below. What every Commit Checkpoint did "
         "before there was a choice."},
        {"autosave", "Autosave slot",
         "The slot set aside for autosaves in Tools > Save Editor. With none "
         "set this writes nothing at all, rather than guessing at a slot."},
        {"next", "Next free slot",
         "The first slot with nothing in it, so a run leaves a trail instead "
         "of one save. When they are all full it cycles through them, oldest "
         "of this session first. The autosave slot is never picked."},
    };
    return modes;
}

struct FlowNodeType {
    const char* key = "";
    const char* title = "";
    const char* category = "";  // add-menu submenu ("Triggers", "Object", ...)
    bool trigger = false;  // true = has exec output, false = has exec input (action)
    FlowParamKind strKind = FlowParamKind::None;  // meaning of FlowNode::str
    // One line saying what the STRING param means on this node. The label
    // beside the widget comes from strKind (Object / Clip / Event / ...), which
    // says what kind of thing it is and never what it does here - see the tips
    // below for why that distinction is the whole point.
    const char* strTip = "";
    // Same, for the SECOND string param (FlowNode::str2 - Set Save Text's value,
    // Display Text's static prefix). Only those two nodes have one, but a knob
    // on screen with nothing to say about it is exactly what this change is
    // about, so it gets a slot rather than an exception.
    const char* str2Tip = "";
    int numCount = 0;             // how many of num[] are used
    const char* numLabels[4] = {};
    // One line per numeric param, in the same order as numLabels. THE
    // documentation of that knob: the node body hovers it, and the node's own
    // tooltip lists them under `desc`. Mandatory in spirit for every param a
    // node declares - `desc` says what the NODE does and a tip says what the
    // PARAM does, and prose about a parameter buried in `desc` is the half of
    // the documentation the reader is actually looking at (they are hovering a
    // drag labelled "Seed" wanting to know what 0 and -1 mean). A trap about
    // one parameter belongs in its tip; a trap about the node stays in `desc`.
    const char* numTips[4] = {};
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
    // One line per exec input, same order as execInLabels. Only worth writing
    // when the node has SEVERAL - a lone "> do" has nothing to say that `desc`
    // does not - but then it is the difference between three unexplained pins
    // and a node that documents its own branches (Generate Volume's
    // generate / clear, Timer's start / stop / reset).
    const char* execInTips[kFlowMaxExecIn] = {};
    // Exec OUTPUT pins on an action - the control-flow half. 0 keeps the old
    // rules (a trigger has its "then", an execThrough action its "after",
    // everything else none); >= 1 gives the node that many LABELED outputs and
    // the branch a link leaves is FlowLink::fromPin. A node declaring these
    // emits the chain hanging off each output itself, which is what makes
    // Branch / Sequence / Gate expressible at all.
    int execOutCount = 0;
    const char* execOutLabels[kFlowMaxExecOut] = {};
    // What the NODE does and why you would reach for it - a paragraph, not a
    // parameter list. Shown in the editor (add-menu tooltips, hovering a node)
    // and fed to the AI flow-graph generator's catalog, so a node added with a
    // desc is automatically documented everywhere. Custom .flownode nodes fill
    // it from their `desc =` header key.
    const char* desc = "";
};

// The node registry. Designated initializers on purpose: omitted fields keep
// their defaults, so each entry states only what the node HAS - and `.desc`
// is required by convention (it is the node's documentation: add-menu
// tooltips, node hover, and the AI generator's catalog all read it), with a
// `.numTips` / `.strTip` line for every parameter it declares (same three
// consumers, and the only one the reader sees while looking AT the knob).
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
         .strTip = "Which pad button, by its hardware name. This is the RAW "
                   "button - a player who rebinds their controls does not "
                   "move this trigger.",
         .desc = "Fires the frame the pad button goes down. For anything the "
                 "player should be able to rebind, use On Action instead."},
        // The configurable half of the input story (docs/input-bindings.md):
        // these two follow the Input Map, so a preset switch or an in-game
        // rebind moves the trigger with the binding.
        {.key = "OnAction", .title = "On Action", .category = "Triggers",
         .trigger = true, .strKind = FlowParamKind::InputActionName,
         .strTip = "The named action from Tools > Input Map - \"jump\", "
                   "\"sprint\". Whatever button or key it is currently bound to "
                   "fires this, including a player's own in-game rebind. An "
                   "unknown name never fires.",
         .boolOut = true, .execInCount = 1,
         .desc = "The rebindable half of the input story "
                 "(docs/input-bindings.md): fires the frame the action is "
                 "pressed. Its bool output is the live \"held right now\" "
                 "condition for the logic gates, so one node covers both the "
                 "press and the hold."},
        {.key = "OnKey", .title = "On Key", .category = "Triggers",
         .trigger = true, .strKind = FlowParamKind::KeyName,
         .strTip = "A USB keyboard key by label. Needs Preferences > Build > "
                   "Keyboard & mouse; without it the trigger never fires.",
         .boolOut = true,
         .desc = "Fires the frame a keyboard key goes down. It bypasses the "
                 "Input Map entirely, so use it for a fixed debug or cheat "
                 "key - not for gameplay the player should be able to rebind. "
                 "Its bool output is \"held right now\"."},
        {.key = "NearObject", .title = "Near Object", .category = "Triggers",
         .trigger = true, .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to measure the distance FROM. Empty = the object "
                   "this graph belongs to.",
         .numCount = 1, .numLabels = {"Radius"},
         .numTips = {"How close the player has to get, in world units. Measured "
                     "in the XZ plane only - a circle around the object's "
                     "origin, not a sphere, so height does not matter and it "
                     "does not follow the object's shape."},
         .idIn = true, .idOut = true,
         .desc = "Fires the frame the player comes INSIDE the radius - a rising "
                 "edge, like In Area and On Player Seen. Walking out and back "
                 "in fires it again; standing still inside does not. Add a Do "
                 "Once for once-per-scene rather than once-per-entry, or use In "
                 "Area when the region has a shape and needs to bound Y."},
        // Volume trigger: the Area object's box instead of Near Object's
        // radius (docs/areas.md). Read live, so a moving area drags its
        // trigger along.
        {.key = "InArea", .title = "In Area", .category = "Triggers",
         .trigger = true, .strKind = FlowParamKind::AreaName,
         .strTip = "The Area object whose box is the volume. Read live, so "
                   "moving or scaling that object moves the trigger with it. "
                   "Without one the node compiles out.",
         .numCount = 1, .numLabels = {"Who"},
         .numTips = {"Who counts as entering: 0 = either player, 1 = player 1 "
                     "only, 2 = player 2 only."},
         .idOut = true, .boolOut = true,
         .desc = "Fires the frame someone ENTERS the area - a rising edge, like "
                 "On Player Seen. Its bool output is the live \"inside right "
                 "now\" condition; wire that through NOT into On Condition for "
                 "an exit trigger. Unlike Near Object the test is a real "
                 "volume: it bounds Y too, so one floor of a building can "
                 "trigger on its own."},
        // BTN_USE lives in controls.hpp.
        {.key = "OnUsed", .title = "On Used", .category = "Triggers",
         .trigger = true, .strKind = FlowParamKind::ObjectName,
         .strTip = "The object being used. It must have its 'usable' flag set "
                   "in the Properties panel, or nothing can ever be used. "
                   "Empty = this graph's own object.",
         .idIn = true,
         .idOut = true,
         .desc = "Fires when the player presses USE while looking at the "
                 "target up close - the interaction trigger for a door, a "
                 "lever, a pickup."},
        {.key = "EverySeconds", .title = "Every N Seconds", .category = "Triggers",
         .trigger = true, .numCount = 1, .numLabels = {"Seconds"},
         .numTips = {"The interval between fires. Below one frame it fires "
                     "every frame; for that, On Update says so plainly."},
         .desc = "A metronome: fires again and again for as long as the scene "
                 "runs. Use it for a patrol re-check, a spawn wave, a ticking "
                 "clock."},
        // One-shot clips: once; looping clips: every wrap.
        {.key = "OnAnimFinished", .title = "On Animation Finished",
         .category = "Triggers", .trigger = true,
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The animated model to watch. Empty = this graph's own "
                   "object.",
         .idIn = true, .idOut = true,
         .desc = "Fires when an animation clip reaches its last frame - once "
                 "for a one-shot clip, on every wrap for a looping one. "
                 "Animated .glb/.fbx objects only."},
        {.key = "Delay", .title = "Delay", .category = "Time", .numCount = 1,
         .numLabels = {"Seconds"},
         .numTips = {"How long to wait before 'after' fires. A wired number "
                     "replaces it, so the wait can be computed."},
         .numIn = true, .execThrough = true,
         .desc = "Exec input arms a timer; the 'after' exec output fires once "
                 "the wait elapses. Re-arming while it is counting RESTARTS the "
                 "timer rather than queuing a second one."},
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
         .execInCount = 2, .execInLabels = {"do", "reset"},
         .execInTips = {"The exec to filter. The first one through continues out "
                        "of 'then'; every one after it is swallowed.",
                        "Arms the gate again, so the next 'do' passes. Fire it "
                        "from whatever should make the one-shot repeatable."},
         .execOutCount = 1, .execOutLabels = {"then"},
         .desc = "Passes the FIRST exec through and then nothing: the gate for "
                 "a one-shot inside a trigger that keeps firing (Near Object, "
                 "On Update). The 'reset' pin arms it again. Resets on scene "
                 "reload."},
        {.key = "DoN", .title = "Do N Times", .category = "Flow",
         .numCount = 1, .numLabels = {"Times"},
         .numTips = {"How many execs get through before the gate shuts. 0 or "
                     "less blocks everything; a wired number replaces it."},
         .numIn = true,
         .execInCount = 2, .execInLabels = {"do", "reset"},
         .execInTips = {"The exec to count and filter. Each one through "
                        "increments the counter until Times is reached.",
                        "Sets the counter back to zero, so the next Times execs "
                        "pass again."},
         .execOutCount = 1, .execOutLabels = {"then"},
         .desc = "Passes the first few execs through and then blocks - Do "
                 "Once with a count. 'reset' starts the count over."},
        {.key = "Gate", .title = "Gate", .category = "Flow", .numCount = 1,
         .numLabels = {"Start open"},
         .numTips = {"Which state the gate is in at scene start (and after a "
                     "scene reload): on = open, so exec passes until something "
                     "closes it."},
         .execInCount = 3, .execInLabels = {"enter", "open", "close"},
         .execInTips = {"The exec being gated - continues out of 'then', but "
                        "only while the gate is open. A blocked exec is gone, "
                        "not queued.",
                        "Opens the gate. Firing it while already open changes "
                        "nothing.",
                        "Closes the gate."},
         .execOutCount = 1, .execOutLabels = {"then"},
         .desc = "A valve on the exec plane. Cleaner than a bool variable plus a "
                 "Branch when the STATE is what you are modelling - a door being "
                 "unlocked, a phase being active."},
        {.key = "FlipFlop", .title = "Flip Flop", .category = "Flow",
         .execOutCount = 2, .execOutLabels = {"A", "B"},
         .desc = "Alternates: the first exec goes out of 'A', the next out of "
                 "'B', then A again. The two-state toggle without a variable - "
                 "a light switch, an in/out door, a stance change."},
        {.key = "SwitchNumber", .title = "Switch Number", .category = "Flow",
         .numCount = 1, .numLabels = {"Value"},
         .numTips = {"The value to dispatch on when nothing is wired into the "
                     "number input. Rounded to a whole number before the "
                     "comparison."},
         .numIn = true,
         .execOutCount = 5, .execOutLabels = {"= 0", "= 1", "= 2", "= 3", "else"},
         .desc = "Routes exec by a number: continues out of the output "
                 "matching 0, 1, 2 or 3, or out of 'else' for anything else. "
                 "The dispatch for a state machine kept in an int variable."},
        {.key = "RandomBranch", .title = "Random Branch", .category = "Flow",
         .numCount = 1, .numLabels = {"Outputs"},
         .numTips = {"How many of the four outputs are in play (2..4), so a "
                     "three-way pick does not need D wired."},
         .execOutCount = 4,
         .execOutLabels = {"A", "B", "C", "D"},
         .desc = "Continues out of one output picked at random. Use it for "
                 "idle barks, wander directions, loot rolls."},
        {.key = "Cooldown", .title = "Cooldown", .category = "Flow",
         .numCount = 1, .numLabels = {"Seconds"},
         .numTips = {"The minimum gap between two execs getting through."},
         .execOutCount = 1,
         .execOutLabels = {"then"},
         .desc = "Rate-limits exec: passes one through, then swallows "
                 "everything until the gap has passed. For a trigger that "
                 "fires every frame (Near Object) or a weapon that must not "
                 "fire faster than it reloads. Unlike Delay nothing is queued "
                 "- a swallowed exec is LOST, not postponed."},
        {.key = "Counter", .title = "Counter", .category = "Flow",
         .numCount = 1, .numLabels = {"Every"},
         .numTips = {"Fire 'then' on every Nth exec: 1 = every time, 3 = "
                     "every third."},
         .numOut = true,
         .execInCount = 2, .execInLabels = {"count", "reset"},
         .execInTips = {"Adds one to the count.",
                        "Zeroes the count and the number output."},
         .execOutCount = 1, .execOutLabels = {"then"},
         .desc = "Counts execs and fires every Nth one. Its number output is "
                 "the running total, so it doubles as a graph-local tally "
                 "without touching a variable."},
        {.key = "Timer", .title = "Timer", .category = "Flow", .numCount = 1,
         .numLabels = {"Duration"},
         .numTips = {"Fire 'finished' and stop after this many seconds. 0 = "
                     "run forever, which is what you want when you only care "
                     "about the elapsed-time output."},
         .numOut = true, .execInCount = 3,
         .execInLabels = {"start", "stop", "reset"},
         .execInTips = {"Starts or resumes the clock.",
                        "Pauses it where it is, keeping the elapsed time.",
                        "Zeroes the elapsed time. Does not stop a running "
                        "clock."},
         .execOutCount = 1,
         .execOutLabels = {"finished"},
         .desc = "A stopwatch whose number output is the elapsed SECONDS, "
                 "readable while it runs - wire it into Seconds To Clock for "
                 "an on-screen timer."},
        {.key = "Tween", .title = "Tween Value", .category = "Flow",
         .numCount = 4, .numLabels = {"From", "To", "Seconds", "Ease"},
         .numTips = {"The value the number output starts at. Whatever unit the "
                     "driven parameter is in - a colour channel, a volume, a "
                     "world coordinate.",
                     "The value it ends at, and holds once 'finished' fires.",
                     "How long the trip takes. Frame-rate independent.",
                     "The shape of the motion: 0 linear, 1 ease in (starts "
                     "slow), 2 ease out (lands soft), 3 smooth in-out."},
         .numOut = true, .execInCount = 2, .execInLabels = {"start", "stop"},
         .execInTips = {"Starts the tween from From. Re-firing restarts it.",
                        "Freezes the number output where it is - the tween does "
                        "not finish, so 'finished' never fires."},
         .execOutCount = 1, .execOutLabels = {"finished"},
         .desc = "Animates a NUMBER over time and fires 'finished' when it "
                 "arrives. Its number output is the live value - wire it into "
                 "Set Object Color, Set Bloom, Set Music Volume, a position, "
                 "anything on the number plane, and that parameter animates. THE "
                 "node for a fade, a slide, a ramp."},
        {.key = "ForLoop", .title = "For Loop", .category = "Flow",
         .numCount = 1, .numLabels = {"Times"},
         .numTips = {"How many times to run the 'body' chain. Capped at 64; a "
                     "wired number replaces it."},
         .numIn = true, .numOut = true,
         .execOutCount = 2, .execOutLabels = {"body", "done"},
         .desc = "Runs its 'body' chain N times, then fires 'done' once. Its "
                 "number output is the 0-based iteration index, so the body "
                 "can spread things out - spawn eight objects in a ring. The "
                 "WHOLE loop runs inside one frame, so keep the count small; "
                 "a body that moves objects re-bakes their geometry once per "
                 "iteration."},
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
         .strTip = "The object to show or hide. Empty = this graph's own "
                   "object.",
         .idIn = true, .idOut = true, .execInCount = 3,
         .execInLabels = {"show", "hide", "toggle"},
         .execInTips = {"Makes it visible.",
                        "Makes it invisible. It is still in the game - "
                        "collision and logic keep running; use Despawn Object "
                        "to take it out.",
                        "Flips whichever it currently is."},
         .desc = "Turns an object's rendering on and off. Read the state back "
                 "with the pure bool Is Visible."},
        {.key = "SetLight", .title = "Set Light", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The Point Light object to drive. Empty = this graph's own "
                   "object.",
         .numCount = 2, .numLabels = {"On", "Intensity"},
         .numTips = {"Whether the light contributes at all.",
                     "Multiplies the light's authored brightness: 1 = as "
                     "placed, 0 = dark, above 1 = brighter than authored."},
         .idIn = true, .idOut = true,
         .desc = "Switches a dynamic point light and scales its brightness. The "
                 "target must be a Point Light with 'Dynamic (live)' enabled - "
                 "baked (static) lights are part of the geometry's shading and "
                 "cannot change at runtime."},
        {.key = "MoveObjectBy", .title = "Move Object By", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to shift. Empty = this graph's own object.",
         .numCount = 3,
         .numLabels = {"dX", "dY", "dZ"},
         .numTips = {"Units to add to the object's X.",
                     "Units to add to its Y (up).",
                     "Units to add to its Z."},
         .idIn = true, .idOut = true,
         .desc = "Teleports an object by a delta - it does not travel, it "
                 "arrives. Move Object To glides instead."},
        {.key = "MoveObjectTo", .title = "Move Object To", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to move. Empty = this graph's own object.",
         .numCount = 4,
         .numLabels = {"X", "Y", "Z", "Speed"},
         .numTips = {"Destination X. A linked position replaces X/Y/Z and is "
                     "re-read every frame, so the target can move.",
                     "Destination Y (up).",
                     "Destination Z.",
                     "How fast it travels, in units per second. It stops on "
                     "arrival."},
         .idIn = true, .idOut = true,
         .posIn = true,
         .desc = "Glides an object toward a point and stops there. Fire it "
                 "once - the motion is integrated by the game, not by the "
                 "graph."},
        {.key = "PushObject", .title = "Apply Impulse", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The rigid body to push. Empty = this graph's own object.",
         .numCount = 3,
         .numLabels = {"X", "Y", "Z"},
         .numTips = {"Velocity to ADD along X, in units per second.",
                     "Along Y - positive is up, so this is the jump/launch "
                     "component.",
                     "Along Z."},
         .idIn = true, .idOut = true,
         .desc = "Physics: adds to a rigid body's velocity and wakes it. Set "
                 "Velocity replaces the velocity instead. On a non-physics "
                 "object it only nudges the stored value - harmless, but "
                 "nothing moves."},
        {.key = "SetObjectColor", .title = "Set Object Color",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to tint. Empty = this graph's own object.",
         .numCount = 3,
         .numTips = {"The RGB tint, each channel 0..1. It MULTIPLIES the "
                     "object's material, so it can only darken a textured "
                     "surface, never brighten it past white."},
         .numKind = FlowParamKind::Color, .idIn = true,
         .idOut = true,
         .desc = "Recolours an object at runtime. Each fire re-bakes that "
                 "object's vertices, so drive it from an event rather than "
                 "from On Update."},
        // Codegen: an exec-wired Get Position latches into a posOut member
        // (templates.cpp getPosLatched); unwired ones resolve live.
        {.key = "GetPosition", .title = "Get Position", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to read. Empty = this graph's own object.",
         .idIn = true, .idOut = true,
         .posOut = true, .execThrough = true,
         .desc = "Exposes an object and its position. With its exec pins "
                 "UNWIRED it is a live data source: consumers read the "
                 "target's current position whenever they run. Wire the exec "
                 "input to SAMPLE instead - the position output freezes at "
                 "the moment the exec fires and 'after' chains on, which is "
                 "how you remember where something was when an event "
                 "happened."},
        {.key = "IsVisible", .title = "Is Visible", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to ask about. Empty = this graph's own object.",
         .idIn = true, .idOut = true,
         .pure = true, .boolOut = true,
         .desc = "Pure bool: is the target being DRAWN this frame? Is Object "
                 "Active is the different question of whether it is in the "
                 "game at all."},
        {.key = "SetPosition", .title = "Set Object Position",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to place. Empty = this graph's own object.",
         .numCount = 3, .numLabels = {"X", "Y", "Z"},
         .numTips = {"Absolute X. A linked position replaces X/Y/Z entirely.",
                     "Absolute Y (up). Nothing snaps this to the ground - put "
                     "Snap To Terrain in front of a computed position.",
                     "Absolute Z."},
         .idIn = true,
         .idOut = true, .posIn = true, .posOut = true,
         .desc = "Puts an object at a point, instantly. It ignores collision: "
                 "an object can be placed inside a wall."},
        // Rotation, the three shapes the position family already has: a delta,
        // an absolute set, and a continuous rate. Degrees, applied in the
        // engine's Euler order (X, then Y, then Z), so Y is the yaw.
        {.key = "RotateObjectBy", .title = "Rotate Object By",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to turn. Empty = this graph's own object.",
         .numCount = 3, .numLabels = {"dX", "dY", "dZ"},
         .numTips = {"Degrees to add about the world X axis (pitch).",
                     "Degrees to add about the world Y axis - the YAW, which "
                     "is what almost everything wants.",
                     "Degrees to add about the world Z axis (roll)."},
         .idIn = true,
         .idOut = true, .posIn = true,
         .desc = "Turns an object by a delta, once. A linked position carries "
                 "the delta as a 3-vector. For something that keeps turning "
                 "use Spin Object rather than firing this every frame - each "
                 "fire re-bakes the object's vertices."},
        {.key = "SetRotation", .title = "Set Object Rotation",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to aim. Empty = this graph's own object.",
         .numCount = 3, .numLabels = {"X", "Y", "Z"},
         .numTips = {"Absolute pitch about the world X axis, in degrees.",
                     "Absolute YAW about the world Y axis.",
                     "Absolute roll about the world Z axis."},
         .idIn = true,
         .idOut = true, .posIn = true,
         .desc = "Sets an object's rotation absolutely, applied in the order "
                 "X, then Y, then Z - the same as the Properties panel. A "
                 "linked position is read as a rotation triple, which is what "
                 "makes Get Object Rotation -> With Y -> Set Object Rotation "
                 "change just the heading."},
        {.key = "SpinObject", .title = "Spin Object", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to spin. Empty = this graph's own object.",
         .numCount = 3,
         .numLabels = {"X deg/s", "Y deg/s", "Z deg/s"},
         .numTips = {"Degrees per second about the world X axis.",
                     "Degrees per second about Y - the yaw, and what a coin "
                     "or a lighthouse wants.",
                     "Degrees per second about the world Z axis."},
         .idIn = true,
         .idOut = true, .execInCount = 2, .execInLabels = {"start", "stop"},
         .execInTips = {"Gives the object that angular velocity. Re-firing "
                        "REPLACES the rate rather than adding to it.",
                        "Clears the rate, leaving the object where it "
                        "stopped."},
         .desc = "Continuous rotation - THE node for something that turns all "
                 "the time. The game integrates it in its own object pass and "
                 "renders the spinner through a per-object matrix instead of "
                 "re-baking its vertices, so the whole graph is On Start -> "
                 "start and the runtime cost is one matrix refresh per frame. "
                 "Frame-rate independent. On a physics object the tumble "
                 "writes rotation too, so the two add up."},
        // Despawn on an authored object only deactivates it (layer streaming
        // can bring authored objects back).
        // Scale and rotation as READABLE values, and the two writers the scale
        // family was missing. Scale and rotation ride the POSITION plane as
        // 3-vectors - it is the plane that already carries three floats, so
        // "read a rotation, change its Y, write it back" is Get Object Rotation
        // -> With Y -> Set Object Rotation with no new machinery.
        {.key = "SetScale", .title = "Set Object Scale", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to resize. Empty = this graph's own object.",
         .numCount = 3,
         .numLabels = {"X", "Y", "Z"},
         .numTips = {"Absolute scale along X. 1 = the authored size.",
                     "Absolute scale along Y.",
                     "Absolute scale along Z."},
         .idIn = true, .idOut = true,
         .posIn = true,
         .desc = "Sets an object's scale absolutely; a linked position works "
                 "as a computed size. Scale 0 on an axis FLATTENS the object "
                 "rather than hiding it - Set Object Visible is what hides "
                 "things."},
        {.key = "ScaleObjectBy", .title = "Scale Object By",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to resize. Empty = this graph's own object.",
         .numCount = 1, .numLabels = {"Factor"},
         .numTips = {"What to multiply all three axes by. 1 changes nothing; "
                     "a wired number replaces it, so a Tween into this is a "
                     "grow/shrink pop."},
         .idIn = true, .idOut = true,
         .numIn = true,
         .desc = "Multiplies an object's current scale, so it compounds: "
                 "firing it twice with 2 makes the object four times as big."},
        {.key = "GetScale", .title = "Get Object Scale", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to read. Empty = this graph's own object.",
         .idIn = true, .idOut = true,
         .posOut = true, .pure = true,
         .desc = "Pure: an object's scale as a 3-vector on the position "
                 "plane. Pull a component out with Get X / Get Y / Get Z."},
        {.key = "GetRotation", .title = "Get Object Rotation",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to read. Empty = this graph's own object.",
         .idIn = true, .idOut = true, .posOut = true, .pure = true,
         .desc = "Pure: an object's rotation in DEGREES as a 3-vector on the "
                 "position plane (Y is the yaw). Get Object Rotation -> With "
                 "Y -> Set Object Rotation changes just the heading."},
        {.key = "LookAt", .title = "Look At", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to turn. Empty = this graph's own object.",
         .numCount = 1,
         .numLabels = {"Tilt too"},
         .numTips = {"Off = yaw only, which keeps a character, a turret base "
                     "or a signpost upright - the normal case. On = also "
                     "pitch up or down at the point."},
         .idIn = true, .idOut = true, .posIn = true,
         .desc = "Turns an object to face the linked position. Wire Player "
                 "Position in for an NPC that watches you, or a Get Position "
                 "for one prop aiming at another."},
        {.key = "ObjDistance", .title = "Distance To Object",
         .category = "Object", .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to measure from. Empty = this graph's own "
                   "object.",
         .idIn = true, .posIn = true, .pure = true, .numOut = true,
         .desc = "Pure number: how far the object is from the linked "
                 "position. With Player Position wired in it is \"how far away "
                 "is the player\" as a VALUE, which Near Object (a trigger "
                 "with a fixed radius) cannot give you. Feeds the "
                 "comparators, Remap Range, a fade, an AI decision."},
        // Physics reads and writes. Velocities on RuntimeObject are per-frame
        // displacements, so codegen converts to and from units/SECOND here -
        // a graph should never have to know the frame rate.
        {.key = "SetVelocity", .title = "Set Velocity", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The rigid body to drive. Empty = this graph's own object.",
         .numCount = 3,
         .numLabels = {"X", "Y", "Z"},
         .numTips = {"Velocity along X in units per SECOND (the graph never "
                     "sees frame rate).",
                     "Along Y - positive is up.",
                     "Along Z."},
         .idIn = true, .idOut = true,
         .posIn = true,
         .desc = "REPLACES a physics body's velocity and wakes it - use it to "
                 "stop a body dead at (0,0,0) or launch one at an exact "
                 "speed. Apply Impulse adds to whatever the body was already "
                 "doing instead."},
        {.key = "GetVelocity", .title = "Get Velocity", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The rigid body to read. Empty = this graph's own object.",
         .idIn = true, .idOut = true,
         .posOut = true, .pure = true,
         .desc = "Pure: a physics body's velocity in units per SECOND as a "
                 "3-vector. Get Y of it is the fall speed - the input for "
                 "fall damage or a landing sound."},
        {.key = "StopMotion", .title = "Stop Motion", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to stop. Empty = this graph's own object.",
         .idIn = true, .idOut = true,
         .desc = "Zeroes a physics body's velocity AND its tumble, and clears "
                 "any Spin Object rate - everything that was moving the "
                 "object on its own. It does not put the body to sleep, so "
                 "gravity still applies."},
        {.key = "SetUsable", .title = "Set Object Usable", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The object whose USE prompt to change. Empty = this "
                   "graph's own object.",
         .idIn = true, .idOut = true,
         .execInCount = 2, .execInLabels = {"on", "off"},
         .execInTips = {"The object can be used, and shows the prompt up "
                        "close.",
                        "It cannot, and shows no prompt."},
         .desc = "Turns an object's USE interaction on or off at runtime - a "
                 "door that only becomes usable once you have the key, a "
                 "lever that stops working after it is pulled."},
        {.key = "IsActive", .title = "Is Object Active", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The object to ask about. Empty = this graph's own object.",
         .idIn = true, .idOut = true,
         .pure = true, .boolOut = true,
         .desc = "Pure bool: is the target in the game at all this frame? "
                 "False while its streaming layer is unloaded, or after "
                 "Despawn Object. Different from Is Visible, which is about "
                 "being DRAWN - an active object can be invisible."},
        {.key = "FindNearest", .title = "Find Nearest", .category = "Object",
         .strKind = FlowParamKind::Text,
         .strTip = "A name PREFIX, not one object's name: every active object "
                   "whose name starts with it is a candidate. Naming "
                   "waypoints wp1, wp2, ... makes \"wp\" the whole set.",
         .numCount = 1,
         .numLabels = {"Max Dist"},
         .numTips = {"Ignore candidates farther than this from the linked "
                     "position. 0 = no limit."},
         .idOut = true, .posIn = true,
         .posOut = true, .execOutCount = 1, .execOutLabels = {"then"},
         .desc = "On exec, finds the nearest matching object and LATCHES it: "
                 "the object output is that object (none if nothing "
                 "qualified) and the position output is where it is. 'then' "
                 "fires right after. The runtime counterpart of naming an "
                 "object in a param - \"the closest pickup\", \"the nearest "
                 "waypoint\"."},
        {.key = "SpawnObject", .title = "Spawn Object", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The TEMPLATE object to clone. Empty = this graph's own "
                   "object. The template itself is not moved or hidden.",
         .numCount = 1,
         .numLabels = {"Yaw"},
         .numTips = {"The clone's heading in degrees about Y."},
         .idIn = true, .idOut = true, .posIn = true,
         .desc = "Clones an object into a runtime slot at the linked position "
                 "(or the template's own). The object OUTPUT is the CLONE, "
                 "not the template - wire it into further actions. The pool "
                 "holds 32 live clones; past that the spawn is dropped."},
        {.key = "DespawnObject", .title = "Despawn Object", .category = "Object",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "What to remove. A spawned clone frees its pool slot; an "
                   "AUTHORED object is only deactivated, because layer "
                   "streaming has to be able to bring it back. Empty = this "
                   "graph's own object.",
         .idIn = true,
         .desc = "Takes an object out of the game. Is Object Active reads "
                 "false afterwards."},
        // Prefabs (docs/prefabs.md). A prefab instance is NOT one object: its
        // static members merge into a single geometry bag (one submit for the
        // whole thing) and only the members that need an identity of their own
        // - a graph, a script, physics, a light - take a spawn-pool slot. That
        // split is what makes "stamp a room into the world at runtime" a thing
        // this machine can afford at all.
        // "Procedural" - the nodes that CREATE content while the game runs,
        // pulled out of "Object" (30 entries and the most crowded menu there
        // is). They share a mechanism as well as a menu: prefab spawns and a
        // runtime volume's output both end in TerrainGame::ProcChunk vertex
        // bags plus the clone pool, so a new node of that kind belongs here.
        {.key = "SpawnPrefab", .title = "Spawn Prefab", .category = "Procedural",
         .strKind = FlowParamKind::PrefabName,
         .strTip = "The prefab to stamp into the world (Tools > Prefabs).",
         .numCount = 2,
         .numLabels = {"Yaw", "Scale"},
         .numTips = {"The instance's heading in degrees about Y.",
                     "Uniform scale for the whole instance. 0 or 1 = the "
                     "authored size."},
         .posIn = true,
         .desc = "Builds one prefab instance at the linked position. Its "
                 "static members are merged into ONE draw call; members with "
                 "a graph, scripts, physics or an identity of their own are "
                 "spawned as real objects out of the usual clone pool. "
                 "Returns silently when the instance pool is full - check the "
                 "game log."},
        {.key = "DespawnPrefab", .title = "Despawn Prefab", .category = "Procedural",
         .strKind = FlowParamKind::PrefabName,
         .strTip = "Which prefab's instances to remove. EMPTY clears every "
                   "prefab instance in the scene, which is a scene-wide wipe "
                   "rather than a no-op.",
         .desc = "Removes every live instance of a prefab - its merged "
                 "geometry and the objects it spawned."},
        // Runtime procedural volumes (docs/procedural-runtime.md).
        {.key = "GenerateVolume", .title = "Generate Volume",
         .category = "Procedural", .strKind = FlowParamKind::ObjectName,
         .strTip = "The Procedural volume object to run. It must be in RUNTIME "
                   "mode - a baked volume's geometry already shipped, so this "
                   "node does nothing to one.",
         .numCount = 1, .numLabels = {"Seed"},
         .numTips = {"Which world to build: 0 = keep the volume's own authored "
                     "seed, -1 = roll a fresh one (a new world every time), any "
                     "other value = use it. The same value always rebuilds the "
                     "same world, so a wired number from a level counter or a "
                     "save value doubles as \"restore the map this save game "
                     "had\"."},
         .numIn = true, .execInCount = 2,
         .execInLabels = {"generate", "clear"},
         .execInTips = {"Evaluates the graph on the EE and builds its geometry. "
                        "Firing it again regenerates from scratch.",
                        "Throws the generated geometry away, leaving the volume "
                        "empty. Ignores the Seed."},
         .desc = "Grows a runtime procedural volume while the game is running - "
                 "no geometry for it ships on the disc, so the world can differ "
                 "every boot (docs/procedural-runtime.md)."},
        {.key = "Animation", .title = "Animation", .category = "Animation",
         .strKind = FlowParamKind::Text,
         .strTip = "The CLIP to play, not the object - the target comes from the "
                   "object link or defaults to self. Empty = the model's first "
                   "clip.",
         .numCount = 3, .numLabels = {"Loop", "Speed", "Fade"},
         .numTips = {"On = the clip repeats; off = it holds its last frame (and "
                     "On Animation Finished fires once).",
                     "Playback rate multiplier. 0 means \"the clip's own "
                     "speed\", not \"frozen\".",
                     "Seconds spent crossfading out of whatever pose the model "
                     "was in. 0 = snap."},
         .idIn = true, .idOut = true, .execInCount = 2,
         .execInLabels = {"play", "stop"},
         .execInTips = {"Starts the clip with the parameters below.",
                        "Freezes the model at its current pose. Ignores every "
                        "parameter."},
         .desc = "Drives an animated .glb/.fbx model's clip playback. Nothing "
                 "else can be a target: a primitive or a static .obj has no "
                 "skeleton to pose."},
        // AI (docs/navigation-ai.md). NPCs walk the nav grid baked at build
        // time (navmesh.cpp -> nav_data.gen.hpp); paths come from A* on the
        // EE (navigation.gen.cpp), agents snap to the terrain and turn to
        // face their motion. The movement actions drive ONE shared AI state
        // per object - starting a new one replaces the previous (a Chase
        // interrupts a Patrol; Stop AI returns the NPC to idle).
        {.key = "PatrolWaypoints", .title = "Patrol Waypoints", .category = "AI",
         .strKind = FlowParamKind::Text,
         .strTip = "The waypoint name PREFIX, not the NPC - the walker comes "
                   "from the object link or defaults to self. \"wp\" walks wp1, "
                   "wp2, ... in natural order; Empty objects make good "
                   "waypoints.",
         .numCount = 3,
         .numLabels = {"Speed", "Pause s", "Once"},
         .numTips = {"Walking speed in units per second.",
                     "How long to stand at each waypoint before moving on.",
                     "On = one pass and then idle; off = cycle the route "
                     "forever."},
         .idIn = true, .idOut = true,
         .desc = "Sends an NPC round a route, pathfinding over the nav grid "
                 "baked at build time. One AI state per object: starting a "
                 "Chase or a Flee replaces this."},
        {.key = "ChasePlayer", .title = "Chase Player", .category = "AI",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The pursuing NPC. Empty = this graph's own object.",
         .numCount = 3,
         .numLabels = {"Speed", "Stop Dist", "Give Up"},
         .numTips = {"Running speed in units per second.",
                     "How close it gets before it stands still and just keeps "
                     "facing the player.",
                     "Drop back to idle once the player is farther away than "
                     "this. 0 = never gives up."},
         .idIn = true,
         .idOut = true,
         .desc = "The NPC pursues the player over the nav grid, repathing as "
                 "they move. One AI state per object: this replaces a Patrol "
                 "or a Flee."},
        {.key = "FleePlayer", .title = "Flee From Player", .category = "AI",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The fleeing NPC. Empty = this graph's own object.",
         .numCount = 2,
         .numLabels = {"Speed", "Safe Dist"},
         .numTips = {"Running speed in units per second.",
                     "Stop and idle once this far from the player."},
         .idIn = true, .idOut = true,
         .desc = "The NPC runs away from the player over the nav grid until "
                 "it is far enough off. One AI state per object: this "
                 "replaces a Patrol or a Chase."},
        {.key = "StopAi", .title = "Stop AI Movement", .category = "AI",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The NPC to halt. Empty = this graph's own object.",
         .idIn = true, .idOut = true,
         .desc = "Clears the target's Patrol/Chase/Flee and returns it to "
                 "idle."},
        {.key = "OnPlayerSeen", .title = "On Player Seen", .category = "AI",
         .trigger = true, .strKind = FlowParamKind::ObjectName,
         .strTip = "The object doing the WATCHING (its facing is the cone's "
                   "axis). Empty = this graph's own object.",
         .numCount = 3, .numLabels = {"Range", "FOV deg", "LOS"},
         .numTips = {"How far the watcher can see, in world units.",
                     "The full width of the vision cone in degrees, centred on "
                     "the watcher's facing. 360 = sees in every direction.",
                     "On = terrain line-of-sight is required too, so hills hide "
                     "the player. OBJECTS never block sight either way."},
         .idIn = true, .idOut = true, .boolOut = true,
         .desc = "Fires on the rising edge of an NPC spotting the player. Its "
                 "bool output is the live \"seen right now\" condition for the "
                 "logic gates, so one node covers both the alert and the "
                 "still-alert state."},
        {.key = "TeleportPlayer", .title = "Spawn Player At",
         .category = "Player", .strKind = FlowParamKind::ObjectName,
         .strTip = "The object whose position the player lands on - a respawn "
                   "point, a door's far side. Empty = this graph's own "
                   "object.",
         .idIn = true, .idOut = true, .posIn = true,
         .desc = "Moves the player to a point instantly. A linked position "
                 "overrides the object's."},
        // The hit object is a runtime reference (-1 = none) - actions fed it
        // are guarded like Spawn Object clones.
        {.key = "Raycast", .title = "Raycast", .category = "Player",
         .numCount = 1, .numLabels = {"Max Dist"},
         .numTips = {"How far to cast, in world units. Nothing beyond this is "
                     "hit."},
         .idOut = true,
         .posOut = true, .execThrough = true,
         .desc = "On exec, casts a ray from the player's eye along the view "
                 "direction and LATCHES the results: position output = the "
                 "hit point, object output = the object hit (may be none). "
                 "'after' fires right after the cast, so whatever reads the "
                 "outputs runs with them already set."},
        // The optional toggle button on the player still gates the beam,
        // but only while enabled.
        // The player as a readable thing. Until these existed a graph could
        // teleport the player but not ask where they were.
        {.key = "PlayerPos", .title = "Player Position", .category = "Player",
         .numCount = 1, .numLabels = {"Player"},
         .numTips = {"Which player. Player 2 reads as player 1's position "
                     "while player 2 is not in the game, so \"nearest player\" "
                     "logic works without a special case."},
         .posOut = true, .pure = true,
         .desc = "Pure position: where the player is this frame (the "
                 "eye/camera position). Wire it into Look At, Distance To "
                 "Object, Spawn Object."},
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
         .execInTips = {"Stops reading the pad for the player. Gravity, "
                        "collision and the camera keep running, so the avatar "
                        "still falls and is still framed.",
                        "Hands the controls back. A scene load also unlocks, so "
                        "a graph that locks and never unlocks cannot strand the "
                        "player across a load."},
         .desc = "'lock' takes the controls away from the player and 'unlock' "
                 "gives them back - what a dialogue, a scripted moment or a "
                 "cutscene that still shows the avatar needs. Only INPUT is "
                 "taken: gravity, collision and the camera keep running, so a "
                 "locked player still falls and is still framed instead of "
                 "freezing in mid-air. A scene load always unlocks."},
        {.key = "SetFlashlight", .title = "Set Flashlight", .category = "Player",
         .numCount = 1, .numLabels = {"On"},
         .numTips = {"The flashlight's master switch. Off also hides its "
                     "cone; the player's own toggle button cannot turn it "
                     "back on while this is off."},
         .desc = "Controls the player's flashlight."},
        {.key = "SetInputPreset", .title = "Set Input Preset",
         .category = "Player", .strKind = FlowParamKind::Text,
         .strTip = "Which binding preset from Tools > Input Map to make "
                   "active. An unknown name changes nothing.",
         .desc = "Switches the whole control scheme at once. A player's own "
                 "rebinds are re-applied on top of the new preset."},
        {.key = "SetStickCurve", .title = "Set Stick Curve",
         .category = "Player", .numCount = 3,
         .numLabels = {"Stick", "Curve", "Exponent"},
         .numTips = {"Which stick the curve applies to: 0 left (move), 1 "
                     "right (camera), 2 both.",
                     "The response shape: 0 Linear, 1 Exponential, 2 S-Curve.",
                     "How pronounced the shape is (1 or more). Ignored by the "
                     "Linear curve, which has nothing to bend."},
         .desc = "Changes how far a stick has to move before the game reacts "
                 "- the accessibility/feel knob, live."},
        {.key = "VibratePad", .title = "Vibrate Pad", .category = "Player",
         .numCount = 3, .numLabels = {"Big", "Small", "Seconds"},
         .numTips = {"The heavy motor's strength, 0..1 - the low rumble.",
                     "The small motor, which has no strength on a DualShock 2: "
                     "it is on or off, a buzz.",
                     "How long to vibrate for. 0 = until the next Vibrate Pad "
                     "node says otherwise, which is what you want when the "
                     "graph decides when to stop."},
         .desc = "Rumbles the pad. Big 0 with Small off is the stop - there is "
                 "no separate node for it."},
        // Scene

        {.key = "SetSky", .title = "Set Sky Color", .category = "Scene",
         .numCount = 3,
         .numTips = {"The sky's RGB, each channel 0..1. It repaints the dome "
                     "only: the scene's lighting and fog are baked and do not "
                     "follow."},
         .numKind = FlowParamKind::Color,
         .desc = "Recolours the sky."},
        // Runtime objects rebuild from the target scene's data, script state
        // resets; textures/models stay loaded (shared across scenes).
        {.key = "SwitchScene", .title = "Switch Scene", .category = "Scene",
         .strKind = FlowParamKind::SceneName,
         .strTip = "The scene to load. An unknown name loads nothing.",
         .desc = "Loads another scene. Applied AFTER this frame's scripts "
                 "finish, so the rest of the chain still runs. Objects "
                 "rebuild from the target scene's data and graph state "
                 "resets; flow variables and save values survive."},
        {.key = "SetLayerLoaded", .title = "Set Layer Loaded",
         .category = "Scene", .strKind = FlowParamKind::LayerName,
         .strTip = "The streaming layer to load or unload (Project panel > "
                   "Layers).",
         .execInCount = 2, .execInLabels = {"load", "unload"},
         .execInTips = {"Starts pulling the layer's assets into memory and "
                        "activates its objects once they are resident - so "
                        "this is not instant. Is Layer Loaded says when it is "
                        "done.",
                        "Deactivates its objects and frees the assets no "
                        "other loaded layer still needs."},
         .desc = "Streams a chunk of the scene in or out by hand, instead of "
                 "leaving it to the layer's own zone."},
        {.key = "IsLayerLoaded", .title = "Is Layer Loaded", .category = "Scene",
         .strKind = FlowParamKind::LayerName,
         .strTip = "The layer to ask about.",
         .pure = true, .boolOut = true,
         .desc = "Pure bool: is the layer fully resident? The gate to put in "
                 "front of anything that touches objects in a layer you just "
                 "asked to load."},
        {.key = "SetGrading", .title = "Set Color Grading", .category = "Scene",
         .strKind = FlowParamKind::GradingName,
         .strTip = "The grading preset from Tools > Color Grading. EMPTY "
                   "restores the ungraded image, which is how you turn "
                   "grading off.",
         .desc = "Applies a colour grade to the whole frame. It persists "
                 "across scene changes, so a scene that should look normal "
                 "has to say so."},
        {.key = "SetFog", .title = "Set Fog", .category = "Scene",
         .numCount = 1, .numLabels = {"On"},
         .numTips = {"On = re-apply the scene's authored fog; off = no fog. "
                     "There is no amount here - the colour and distance are "
                     "baked per scene."},
         .desc = "Switches the scene's fog."},
        {.key = "SetBloom", .title = "Set Bloom", .category = "Scene",
         .numCount = 1, .numLabels = {"Amount"},
         .numTips = {"How much of the blurred image is added back: 0 off, 1 "
                     "fully re-added, up to 2 over-added for a hot glow. A "
                     "wired number replaces it, so a Tween ramps the glow."},
         .numIn = true,
         .desc = "Controls the bloom pass."},
        {.key = "SetGrain", .title = "Set Grain", .category = "Scene",
         .numCount = 1, .numLabels = {"Amount"},
         .numTips = {"Grain strength, 0 off to 1 heavy. A wired number "
                     "replaces it."},
         .numIn = true,
         .desc = "Controls the film-grain overlay."},
        {.key = "SetFlare", .title = "Set Lens Flare", .category = "Scene",
         .numCount = 1, .numLabels = {"Amount"},
         .numTips = {"Flare brightness, 0 off to 1. A wired number replaces "
                     "it."},
         .numIn = true,
         .desc = "Controls the sun lens flare. It follows the scene's "
                 "lighting direction and hides behind geometry, so there is "
                 "nothing to position."},
        {.key = "SetGodRays", .title = "Set God Rays", .category = "Scene",
         .numCount = 1, .numLabels = {"Amount"},
         .numTips = {"Shaft strength, 0 off to 1. A wired number replaces it."},
         .numIn = true,
         .desc = "Controls the god rays - radial light shafts streaking from "
                 "the sun's position on screen."},
        // Authored baseline: Tools > UI Editor > Depth of field.
        {.key = "SetDof", .title = "Set Depth Of Field", .category = "Scene",
         .numCount = 4, .numLabels = {"Focus", "Range", "Amount", "Mode"},
         .numTips = {"Distance from the camera that stays sharp. A linked "
                     "position replaces it with the live player-to-point "
                     "distance, which is how you keep a moving subject in "
                     "focus.",
                     "How far in front of and behind the focus distance the "
                     "image stays sharp.",
                     "How strong the blur gets outside that range, 0..1.",
                     "What this fire does: 0 = set the three values below, 1 "
                     "= turn depth of field off, 2 = restore the scene's "
                     "authored setting."},
         .posIn = true,
         .desc = "Drives the depth-of-field blur. The authored baseline lives "
                 "in Tools > UI Editor > Depth of field."},
        {.key = "SetParticles", .title = "Set Particles", .category = "Scene",
         .numCount = 1, .numLabels = {"On"},
         .numTips = {"The global switch for every particle emitter in the "
                     "scene. Off is a way to buy back frame time, not an "
                     "artistic choice."},
         .desc = "Turns all particle emitters on or off at once."},
        // The confirm prompt auto-reverts - a mode the TV can't show would
        // otherwise strand the player on a black screen.
        {.key = "SetDisplayMode", .title = "Set Display Mode",
         .category = "Scene", .numCount = 2,
         .numLabels = {"Mode", "Confirm s"},
         .numTips = {"The scan mode: 0 interlaced, 1 progressive 480p, 2 1080i, "
                     "3 interlaced field rendering (a fresh half-height image "
                     "every field), 4 full-height PAL 576i (always 50 Hz).",
                     "Seconds to show a keep-or-revert prompt for before rolling "
                     "back on its own. 0 = switch blind, which strands the "
                     "player on a black screen if the TV cannot show the mode."},
         .desc = "Switches the console's video mode at runtime."},
        {.key = "SetWidescreen", .title = "Set Widescreen", .category = "Scene",
         .numCount = 1, .numLabels = {"On"},
         .numTips = {"On = fit the projection for 16:9, off = 4:3. This "
                     "changes the PROJECTION, not the picture's resolution."},
         .desc = "Switches the aspect ratio the game renders for."},
        {.key = "SetAmbience", .title = "Set Ambience", .category = "Scene",
         .strKind = FlowParamKind::AmbienceName,
         .strTip = "The ambience preset from Tools > Ambience Editor.",
         .desc = "Repaints the sky from a preset. Only the sky changes live - "
                 "a preset's lighting and fog are baked per scene, so those "
                 "parts of it do nothing here."},
        // ------------------------------------------------------------------
        // Camera and presentation. Everything here rides fields the Cutscene
        // Director already publishes on ScriptContext, so a graph gets the
        // cinematic vocabulary without a second camera system - and a running
        // cutscene always wins, because its player rewrites those fields every
        // frame and clears them when it ends.
        {.key = "SetCamera", .title = "Set Camera", .category = "Camera",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "What the camera looks AT. The linked position is where it "
                   "looks FROM, so this node needs both. Empty = this graph's "
                   "own object.",
         .idIn = true, .posIn = true,
         .desc = "Takes the camera over. Fired from On Start it is a fixed "
                 "camera for the room; fired from On Update it tracks. "
                 "Release Camera gives control back, and a playing cutscene "
                 "overrides it for as long as it runs."},
        {.key = "CameraFromObject", .title = "Camera From Object",
         .category = "Camera", .strKind = FlowParamKind::ObjectName,
         .strTip = "The object that BECOMES the camera: its position is the "
                   "eye and its own rotation is the aim (the +Z lens "
                   "direction, the same convention the Cutscene Director's "
                   "Camera entities use). Move or rotate it and the shot "
                   "follows. Empty = this graph's own object.",
         .idIn = true, .idOut = true,
         .desc = "Cuts the camera to an object, so a Camera object placed and "
                 "aimed in the viewport is exactly the shot you get."},
        {.key = "ReleaseCamera", .title = "Release Camera",
         .category = "Camera",
         .desc = "Hands the camera back to the player/game. Also un-tilts any "
                 "roll a shot had applied."},
        {.key = "CameraShake", .title = "Camera Shake", .category = "Camera",
         .numCount = 2, .numLabels = {"Amplitude", "Seconds"},
         .numTips = {"How far the camera is thrown, in WORLD UNITS. 0 stops a "
                     "shake in progress. Small values are invisible - a few "
                     "centimetres is sub-pixel at 512x448 unless something is "
                     "very close to the lens.",
                     "How long it lasts. It eases out over the last of it, so "
                     "the tail is gentler than the amplitude suggests."},
         .numIn = true,
         .desc = "Knocks the camera about. Applies to whatever camera is in "
                 "force, a cutscene's included. Both the eye and the aim move "
                 "together, so the shot wobbles rather than swinging."},
        {.key = "SetFade", .title = "Set Screen Fade", .category = "Camera",
         .numCount = 1, .numLabels = {"Amount"},
         .numTips = {"How black the overlay is: 0 clear, 1 fully black. It "
                     "SURVIVES across frames until something changes it, so a "
                     "fade to 1 with nothing to bring it back leaves a black "
                     "screen."},
         .numIn = true,
         .desc = "A black overlay over everything. Wire a Tween into it for a "
                 "real fade: Tween 0 -> 1 over a second into this, then its "
                 "'finished' output switches the scene."},
        {.key = "SetBars", .title = "Set Letterbox Bars", .category = "Camera",
         .numCount = 2, .numLabels = {"Style", "Amount"},
         .numTips = {"Which mask: 0 none, 1 cinema 2.39:1, 2 wide 16:9, 3 "
                     "pillarbox, 4 frame.",
                     "How far the chosen style is deployed, 0..1 of its full "
                     "coverage - wire a Tween into it to slide the bars in."},
         .numIn = true,
         .desc = "Masks the frame with black bars. A playing cutscene's own "
                 "bars win over this."},
        {.key = "SetPlayerVisible", .title = "Set Player Visible",
         .category = "Camera", .execInCount = 2,
         .execInLabels = {"show", "hide"},
         .execInTips = {"Draws the third-person avatar.",
                        "Hides it, for a scripted camera move that should fly "
                        "free without the character in shot."},
         .desc = "Shows or hides the player's body. No effect in first-person "
                 "or noclip, which have no visible body to begin with."},
        {.key = "OnSequenceEnd", .title = "On Sequence Finished",
         .category = "Camera", .trigger = true, .boolOut = true,
         .desc = "Fires the frame a cutscene stops - whether it ran out, was "
                 "stopped by Stop Sequence, or the player skipped it. THE way to "
                 "chain \"play the cutscene, then carry on\". Its bool output is "
                 "the live \"a cutscene is playing right now\" condition, so you "
                 "can gate gameplay logic out while one runs."},
        {.key = "PlaySequence", .title = "Play Sequence", .category = "Scene",
         .strKind = FlowParamKind::SequenceName,
         .strTip = "The cutscene from Tools > Cutscene Director. Retriggering "
                   "RESTARTS it from the top.",
         .desc = "Starts a cutscene. It drives the camera (and whatever "
                 "tracks it owns) until it ends; On Sequence Finished is how "
                 "you carry on afterwards."},
        {.key = "StopSequence", .title = "Stop Sequence", .category = "Scene",
         .desc = "Stops the active cutscene."},
        // Endless scroller (Insert > World > Scroller, docs/endless-scroller.md).
        // The target is the Scroller MARKER itself, never one of its segment
        // members - the members are hidden templates and the baked clones are
        // not addressable from a graph.
        {.key = "StartScroller", .title = "Start Scroller", .category = "Scroller",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The Scroller object whose belt to run. Empty = this "
                   "graph's own object.",
         .idIn = true, .idOut = true,
         .desc = "Runs a stopped endless-scroller belt, from wherever it was "
                 "frozen. A belt with 'Run at start' ticked is already "
                 "running, so this only matters after a Stop Scroller (or for "
                 "a belt authored to start still)."},
        {.key = "StopScroller", .title = "Stop Scroller", .category = "Scroller",
         .strKind = FlowParamKind::ObjectName,
         .strTip = "The Scroller object whose belt to freeze. Empty = this "
                   "graph's own object.",
         .idIn = true, .idOut = true,
         .desc = "Freezes an endless-scroller belt in place. The tiled pieces "
                 "stay exactly where they are and cost nothing per frame; "
                 "Start Scroller resumes from the same spot."},
        {.key = "SetScrollerSpeed", .title = "Set Scroller Speed",
         .category = "Scroller", .strKind = FlowParamKind::ObjectName,
         .strTip = "The Scroller object to re-speed. Empty = this graph's own "
                   "object.",
         .numCount = 1, .numLabels = {"Speed"},
         .numTips = {"Belt units per second along the scroller's axis. "
                     "Negative reverses the belt; 0 stalls it without "
                     "stopping it (Stop Scroller is the cheaper freeze)."},
         .idIn = true, .idOut = true, .numIn = true,
         .desc = "Changes a belt's speed while the game runs - accelerating "
                 "scenery as a train pulls away, or reversing it. Takes "
                 "effect on the next frame; the belt keeps its current "
                 "position."},
        // Credits (docs/credits.md). A rolling credits screen owns the whole
        // frame, so unlike a cutscene it is not something the graph keeps
        // driving: it starts here and reports back through On Credits Finished.
        {.key = "PlayCredits", .title = "Play Credits", .category = "Scene",
         .strKind = FlowParamKind::CreditsName,
         .strTip = "The roll from Tools > Credits Editor. Its own finish "
                   "action decides what happens when it ends.",
         .desc = "Starts a credits roll. It takes over the screen and the pad "
                 "until it ends or the player skips it - gameplay, scripts "
                 "and this graph are frozen meanwhile - and then runs its "
                 "finish action (stay in the game, switch scene, open a menu, "
                 "fire a flow event)."},
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
         .execInTips = {"Draws every HUD image again.",
                        "Hides every HUD image at once. The USE prompt is not a "
                        "HUD image and stays as it was.",
                        "Flips whichever state the HUD is in - for a single "
                        "button that opens and closes it."},
         .desc = "Sets all HUD images' visibility. The USE prompt is "
                 "unaffected."},
        {.key = "SetTextVisible", .title = "Set Text Visible", .category = "HUD",
         .strKind = FlowParamKind::HudTextName,
         .strTip = "The baked on-screen text from Tools > UI Editor (Texts). "
                   "Its string is frozen at build time - for one that varies "
                   "at runtime use Display Text.",
         .numCount = 1,
         .numLabels = {"Seconds"},
         .numTips = {"On 'show', auto-hide after this many seconds. 0 = stay "
                     "until 'hide' fires. Ignored by the 'hide' pin."},
         .execInCount = 2,
         .execInLabels = {"show", "hide"},
         .execInTips = {"Draws the text, and starts the Seconds countdown when "
                        "one is set.",
                        "Hides it now, whatever the countdown was doing."},
         .desc = "Shows or hides a pre-baked text sprite. Nothing is drawn "
                 "glyph by glyph here, so it costs one textured quad."},
        // Runtime text: the string is only known while the game runs, so it
        // draws glyph by glyph from a Font Manager font's atlas instead of a
        // pre-baked sprite. The atlas only reaches VRAM once shown.
        {.key = "DisplayText", .title = "Display Text", .category = "HUD",
         .strKind = FlowParamKind::FontName,
         .strTip = "Which Font Manager font to draw with. Its colour and drop "
                   "shadow come with it - they are not parameters here. Empty = "
                   "the project's first font.",
         .str2Tip = "A fixed string printed in FRONT of the wired text value - "
                    "\"Score: \" before a Get Save Value. Leave it empty to show "
                    "the value alone.",
         .numCount = 4, .numLabels = {"X", "Y", "Size", "Seconds"},
         .numTips = {"Horizontal position, 0..1 across the screen. The text is "
                     "CENTRED on it, not left-aligned from it.",
                     "Vertical position, 0..1 down the screen.",
                     "Glyph height in pixels, at the console's 512x448.",
                     "Auto-hide after this many seconds. 0 = stays until the "
                     "'hide' pin fires."},
         .textIn = true, .execInCount = 2, .execInLabels = {"show", "hide"},
         .execInTips = {"Starts drawing, and re-reads the wired text every frame "
                        "from then on.",
                        "Stops drawing."},
         .desc = "Puts a value the game only knows at RUNTIME on screen - wire a "
                 "text source (Number To Text, Get Save Text) into it. Unlike Set "
                 "Text Visible, whose string is baked into a sprite at build "
                 "time, this draws glyph by glyph from the font's atlas."},
        // Audio (music: 16-bit 22kHz stereo WAV; sounds: ADPCM one-shots)
        {.key = "PlayMusic", .title = "Play Music", .category = "Audio",
         .strKind = FlowParamKind::MusicTrack,
         .strTip = "The track to play (imported in the Project panel > "
                   "Music). Starting a track replaces whatever was playing - "
                   "there is one music voice.",
         .numCount = 2,
         .numLabels = {"Volume", "Loop"},
         .numTips = {"How loud, 0..100. Set Music Volume changes it "
                     "afterwards.",
                     "On = the track repeats seamlessly; off = it plays once "
                     "and stops."},
         .desc = "Starts a streamed music track."},
        {.key = "StopMusic", .title = "Stop Music", .category = "Audio",
         .desc = "Stops the music."},
        {.key = "SetMusicVolume", .title = "Set Music Volume",
         .category = "Audio", .numCount = 1, .numLabels = {"Volume"},
         .numTips = {"The music level, 0..100. A wired number replaces it, so "
                     "a Tween into this fades the music out."},
         .numIn = true,
         .desc = "Sets the music volume live."},
        {.key = "SetSfxVolume", .title = "Set Sound Volume",
         .category = "Audio", .numCount = 1, .numLabels = {"Volume"},
         .numTips = {"The master effects level, 0..100 (100 = unscaled). It "
                     "rides on top of every Play Sound's own volume and every "
                     "sound emitter."},
         .numIn = true,
         .desc = "The one place to duck all the sound effects at once - under "
                 "a cutscene, under dialogue. Music has its own Set Music "
                 "Volume."},
        {.key = "PlaySound", .title = "Play Sound", .category = "Audio",
         .strKind = FlowParamKind::SoundTrack,
         .strTip = "The sound effect to play (imported in the Project panel > "
                   "Sounds). One-shot ADPCM - for looping background audio use "
                   "Play Music.",
         .numCount = 2, .numLabels = {"Volume", "Channel"},
         .numTips = {"How loud, 0..100. Set Sound Volume scales this on top.",
                     "Which of the SPU's 24 voices to use, or auto to rotate "
                     "through them. Pinning a channel is how you make a new "
                     "trigger CUT OFF the previous one instead of layering."},
         .desc = "Fires a one-shot sound effect."},
        // Save data: named values persisted in memory card slots (Project
        // panel, "Save data"); every save slot stores a snapshot.
        {.key = "SetValue", .title = "Set Save Value", .category = "Save",
         .strKind = FlowParamKind::SaveValue,
         .strTip = "The save value to write (Project panel > Save data). "
                   "These are what a memory-card save actually stores.",
         .numCount = 1,
         .numLabels = {"Value"},
         .numTips = {"What to assign. A wired number replaces it."},
         .numIn = true,
         .desc = "Writes a save value. It is not written to the card here - a "
                 "save slot stores a snapshot when the player saves."},
        {.key = "AddValue", .title = "Add To Save Value", .category = "Save",
         .strKind = FlowParamKind::SaveValue,
         .strTip = "The save value to change (Project panel > Save data).",
         .numCount = 1,
         .numLabels = {"Delta"},
         .numTips = {"How much to add. Negative subtracts; a wired number "
                     "replaces it."},
         .numIn = true,
         .desc = "Adds to a save value without having to read it back first - "
                 "a score, a coin count, a health delta."},
        {.key = "ValueAtLeast", .title = "Value At Least", .category = "Save",
         .strKind = FlowParamKind::SaveValue,
         .strTip = "The save value to test (Project panel > Save data).",
         .numCount = 1, .numLabels = {"Threshold"},
         .numTips = {"The value it has to reach or beat. A wired number replaces "
                     "it, so one save value can be compared against another."},
         .pure = true, .boolOut = true, .numIn = true,
         .desc = "Pure bool: is the save value at or above the threshold? "
                 "Evaluated fresh every frame, so wire it into On Condition or a "
                 "logic gate rather than expecting it to fire on its own."},
        {.key = "ValueAtMost", .title = "Value At Most", .category = "Save",
         .strKind = FlowParamKind::SaveValue,
         .strTip = "The save value to test (Project panel > Save data).",
         .numCount = 1,
         .numLabels = {"Threshold"},
         .numTips = {"The value it has to be at or below. A wired number "
                     "replaces it."},
         .pure = true, .boolOut = true,
         .numIn = true,
         .desc = "Pure bool: is the save value at or below the threshold? The "
                 "other half of Value At Least - together they bound a range, "
                 "and on their own they are \"out of lives\" and \"full health\"."},
        {.key = "GetSaveValue", .title = "Get Save Value", .category = "Save",
         .strKind = FlowParamKind::SaveValue,
         .strTip = "The save value to read (Project panel > Save data).",
         .pure = true, .textOut = true,
         .numOut = true,
         .desc = "Pure: a save value as both a number and text, so it can "
                 "feed the Math nodes and a Display Text at once."},
        {.key = "SetSaveText", .title = "Set Save Text", .category = "Save",
         .strKind = FlowParamKind::SaveText,
         .strTip = "The save text entry to write (Project panel > Save data).",
         .str2Tip = "The string to store. A wired text input replaces it, "
                    "which is how a computed name or a formatted value gets "
                    "saved.",
         .textIn = true,
         .desc = "Writes a save text - the string half of the save data (a "
                 "player name, a chosen difficulty)."},
        {.key = "GetSaveText", .title = "Get Save Text", .category = "Save",
         .strKind = FlowParamKind::SaveText,
         .strTip = "The save text entry to read (Project panel > Save data).",
         .pure = true, .textOut = true,
         .desc = "Pure text: a save text's current value. Wire it into Text "
                 "Equals to branch on it, or into Display Text to show it."},
        // The same 3-slot menu a Save point object opens on USE.
        {.key = "OpenSaveMenu", .title = "Open Save Menu", .category = "Save",
         .desc = "Opens the in-game 3-slot save/load menu."},
        // Checkpoints (docs/save-editor.md). Exactly ONE checkpoint exists at
        // a time, deliberately: it costs a few KB of RAM that never grows,
        // instead of a stack of snapshots nobody budgeted for.
        {.key = "SaveCheckpoint", .title = "Save Checkpoint", .category = "Save",
         .desc = "Snapshots the current save payload into a RAM buffer - "
                 "instant, and nothing touches the memory card. Use it at the "
                 "points a death should send the player back to. Overwrites "
                 "any previous checkpoint."},
        {.key = "LoadCheckpoint", .title = "Load Checkpoint", .category = "Save",
         .desc = "Restores the save values and texts from the checkpoint "
                 "buffer. Does nothing at all if no checkpoint has been taken "
                 "yet, so it is safe to wire unconditionally - guard it with "
                 "Has Checkpoint when the player should be told."},
        {.key = "CommitCheckpoint", .title = "Commit Checkpoint",
         .category = "Save", .strKind = FlowParamKind::SaveSlotMode,
         .strTip = "Which slot to write. \"This slot\" uses the number below; "
                   "the other two are decided at runtime.",
         .numCount = 1, .numLabels = {"Slot"},
         .numTips = {"Memory card slot 0-2, the same three the save menu "
                     "shows. Out-of-range values are ignored. Only read when "
                     "the mode above is \"This slot\"."},
         .numIn = true,
         .desc = "Writes the checkpoint buffer to a real memory card slot. "
                 "This is the one checkpoint node that touches the card, so "
                 "it is the one that can be slow - a chapter break, not a "
                 "death. (Save Editor > Write in the background takes the "
                 "pause out of it.)"},
        {.key = "HasCheckpoint", .title = "Has Checkpoint", .category = "Save",
         .pure = true, .boolOut = true,
         .desc = "True once a checkpoint has been taken this session. A pure "
                 "bool source: wire it into a gate or On Condition to offer "
                 "\"continue\" only when there is something to continue from."},
        // Variables: named game-global values (one namespace per type),
        // zeroed at boot, kept across scene switches, NOT saved to the
        // memory card (use Save data for persistence).
        {.key = "SetVarInt", .title = "Set Int", .category = "Variables",
         .strKind = FlowParamKind::VarName,
         .strTip = "The variable's name. It exists by being named - no "
                   "declaration anywhere. Game-global and kept across scene "
                   "switches, but NOT written to the memory card (that is Save "
                   "data).",
         .numCount = 1, .numLabels = {"Value"},
         .numTips = {"What to assign, or what to add, depending on which pin "
                     "fired. A wired number replaces it on both."},
         .numIn = true, .execInCount = 2, .execInLabels = {"set", "add"},
         .execInTips = {"Assigns Value, discarding whatever was there.",
                        "ADDS Value to what is there - so a counter is On Button "
                        "-> add with Value 1, and needs no read at all."},
         .desc = "Writes a global int variable."},
        {.key = "SetVarBool", .title = "Set Bool", .category = "Variables",
         .strKind = FlowParamKind::VarName,
         .strTip = "The variable's name. It exists by being named. "
                   "Game-global and kept across scene switches, but NOT saved "
                   "to the memory card (that is Save data).",
         .numCount = 1,
         .numLabels = {"Value"},
         .numTips = {"The value the 'set' pin assigns: anything other than 0 "
                     "is true. Ignored by 'toggle'."},
         .numIn = true, .execInCount = 2,
         .execInLabels = {"set", "toggle"},
         .execInTips = {"Assigns Value.",
                        "Flips whatever is there, without needing to read it."},
         .desc = "Writes a global bool variable."},
        {.key = "SetVarPos", .title = "Set Position", .category = "Variables",
         .strKind = FlowParamKind::VarName,
         .strTip = "The variable's name. It exists by being named. "
                   "Game-global and kept across scene switches, but NOT saved "
                   "to the memory card.",
         .numCount = 3,
         .numLabels = {"X", "Y", "Z"},
         .numTips = {"The X to store. A linked position replaces all three.",
                     "The Y to store.",
                     "The Z to store."},
         .posIn = true,
         .desc = "Writes a global position variable - a remembered spawn "
                 "point, a last-known player location."},
        {.key = "GetVarBool", .title = "Get Bool", .category = "Variables",
         .strKind = FlowParamKind::VarName,
         .strTip = "The variable to read. One never written reads false.",
         .pure = true, .boolOut = true,
         .desc = "Pure bool: a global bool variable."},
        {.key = "GetVarPos", .title = "Get Position", .category = "Variables",
         .strKind = FlowParamKind::VarName,
         .strTip = "The variable to read. One never written reads (0, 0, 0).",
         .posOut = true, .pure = true,
         .desc = "Pure position: a global position variable."},
        {.key = "VarAtLeast", .title = "Int At Least", .category = "Variables",
         .strKind = FlowParamKind::VarName,
         .strTip = "The int variable to test.",
         .numCount = 1,
         .numLabels = {"Threshold"},
         .numTips = {"The value it has to reach or beat. A wired number "
                     "replaces it, so one variable can be compared against "
                     "another."},
         .pure = true, .boolOut = true,
         .numIn = true,
         .desc = "Pure bool: is the int variable at or above the threshold? "
                 "Evaluated fresh every frame, so wire it into On Condition "
                 "or a logic gate rather than expecting it to fire."},
        {.key = "GetVarInt", .title = "Get Int", .category = "Variables",
         .strKind = FlowParamKind::VarName,
         .strTip = "The variable to read. One never written reads 0.",
         .pure = true, .numOut = true,
         .desc = "Pure number: a global int variable. Wire it into a Math "
                 "node or straight into any number input."},
        {.key = "GetVarIntText", .title = "Get Int As Text",
         .category = "Variables", .strKind = FlowParamKind::VarName,
         .strTip = "The variable to read.",
         .pure = true, .textOut = true,
         .desc = "Pure text: a global int variable, printed. Number To Text "
                 "(formatted) is the version with padding and decimals."},
        // World Facts (docs/world-facts.md): the DECLARED half of the same
        // idea as the Variables nodes above. A fact is picked from the
        // catalog rather than typed, carries a type, a default and a
        // lifetime, and is visible in the World Blackboard while the game
        // runs. The Variables nodes keep working and are a separate
        // namespace - one is a scratch value, the other is game state
        // someone wrote down.
        {.key = "SetFact", .title = "Set Fact", .category = "Facts",
         .strKind = FlowParamKind::FactName,
         .strTip = "The fact to write, from the catalog (Tools > World "
                   "Facts). A computed fact cannot be written - it is derived "
                   "from others.",
         .numCount = 1, .numLabels = {"Value"},
         .numTips = {"What to assign or add, depending on which pin fired. "
                     "For a yes/no fact anything other than 0 is true; for a "
                     "one-of-several fact this is the option's position in the "
                     "list. A wired number replaces it."},
         .numIn = true, .execInCount = 3,
         .execInLabels = {"set", "add", "toggle"},
         .execInTips = {"Assigns Value, discarding whatever was there.",
                        "ADDS Value to what is there - a counter is On Button "
                        "-> add with Value 1, and needs no read at all.",
                        "Flips a yes/no fact without reading it first. On any "
                        "other type this sets 0 when it was non-zero and 1 "
                        "when it was 0."},
         .desc = "Writes a fact in the World Facts catalog. Unlike a "
                 "variable, the write is checked against the fact's declared "
                 "type at build time and shows up in the blackboard's change "
                 "history with this node's name against it."},
        {.key = "SetFactPos", .title = "Set Fact Position",
         .category = "Facts", .strKind = FlowParamKind::FactName,
         .strTip = "The position fact to write.",
         .numCount = 3, .numLabels = {"X", "Y", "Z"},
         .numTips = {"The X to store. A linked position replaces all three.",
                     "The Y to store.", "The Z to store."},
         .posIn = true,
         .desc = "Writes a position fact - a remembered spawn point, the "
                 "place the player last died, where the boat was left."},
        {.key = "ClearFact", .title = "Clear Fact", .category = "Facts",
         .strKind = FlowParamKind::FactName,
         .strTip = "The fact to put back to the value it starts a new game "
                   "at.",
         .desc = "Resets one fact to its declared default. What 'start this "
                 "puzzle again' is made of - and the honest version of it, "
                 "because the default lives in the catalog rather than being "
                 "retyped at every reset site."},
        {.key = "GetFact", .title = "Get Fact", .category = "Facts",
         .strKind = FlowParamKind::FactName,
         .strTip = "The fact to read. A yes/no fact reads 1 or 0.",
         .pure = true, .numOut = true,
         .desc = "Pure number: a fact's value. Wire it into a Math node or "
                 "straight into any number input."},
        {.key = "GetFactBool", .title = "Fact Is True", .category = "Facts",
         .strKind = FlowParamKind::FactName,
         .strTip = "The fact to test. Anything other than 0 is true, so this "
                   "reads a count as 'at least one'.",
         .pure = true, .boolOut = true,
         .desc = "Pure bool: is this fact set? The usual way into a gate or "
                 "an On Condition."},
        {.key = "GetFactPos", .title = "Get Fact Position",
         .category = "Facts", .strKind = FlowParamKind::FactName,
         .strTip = "The position fact to read. One never written reads its "
                   "declared default.",
         .posOut = true, .pure = true,
         .desc = "Pure position: a position fact, ready to wire into a "
                 "Teleport or a Move Object To."},
        {.key = "GetFactText", .title = "Get Fact As Text",
         .category = "Facts", .strKind = FlowParamKind::FactName,
         .strTip = "The fact to print.",
         .pure = true, .textOut = true,
         .desc = "Pure text: a fact printed for the player. A one-of-several "
                 "fact prints its OPTION NAME, not its number - which is the "
                 "reason to declare one instead of using a bare int."},
        {.key = "FactAtLeast", .title = "Fact At Least", .category = "Facts",
         .strKind = FlowParamKind::FactName,
         .strTip = "The fact to test.",
         .numCount = 1, .numLabels = {"Threshold"},
         .numTips = {"The value it has to reach or beat. A wired number "
                     "replaces it, so one fact can be compared against "
                     "another."},
         .pure = true, .boolOut = true, .numIn = true,
         .desc = "Pure bool: is the fact at or above the threshold? Evaluated "
                 "fresh every frame, so wire it into On Condition or a gate "
                 "rather than expecting it to fire."},
        {.key = "FactAtMost", .title = "Fact At Most", .category = "Facts",
         .strKind = FlowParamKind::FactName,
         .strTip = "The fact to test.",
         .numCount = 1, .numLabels = {"Threshold"},
         .numTips = {"The value it must not exceed. A wired number replaces "
                     "it."},
         .pure = true, .boolOut = true, .numIn = true,
         .desc = "Pure bool: is the fact at or below the threshold?"},
        {.key = "FactIs", .title = "Fact Is", .category = "Facts",
         .strKind = FlowParamKind::FactName,
         .strTip = "The fact to test.",
         .numCount = 1, .numLabels = {"Value"},
         .numTips = {"The value it must equal. For a one-of-several fact this "
                     "is the option's position in the list - the editor shows "
                     "the names beside it. Compared with a small tolerance, "
                     "because the plane is float."},
         .pure = true, .boolOut = true, .numIn = true,
         .desc = "Pure bool: does the fact hold exactly this value? The node "
                 "for a one-of-several fact - 'power.state is Overloaded'."},
        {.key = "FactQuery", .title = "Query", .category = "Facts",
         .strKind = FlowParamKind::FactQueryName,
         .strTip = "The named condition to evaluate (Tools > World Facts > "
                   "Queries).",
         .pure = true, .boolOut = true,
         .desc = "Pure bool: a reusable named condition - CanEnterBasement, "
                 "MartaWillTalk. The same query gates a door, a dialogue line "
                 "and an NPC's behaviour, so the design changes in one place "
                 "instead of in every graph that happened to copy it."},
        {.key = "OnFactChanged", .title = "On Fact Changed",
         .category = "Facts", .trigger = true,
         .strKind = FlowParamKind::FactName,
         .strTip = "The fact to watch. Fires on the frame its value differs "
                   "from the frame before - whoever wrote it, graph or rule.",
         .desc = "Trigger: the fact changed. The reactive door into a graph - "
                 "no polling, no On Condition that has to describe the state "
                 "you are already storing. A position fact fires when any of "
                 "its three numbers moves."},

        // The number plane. Sources (Number, Get Int, Get Save Value) feed
        // these, they feed each other, and a consumer's num[0] gives way to
        // the wire. Every one of them is PURE - a number is an expression
        // evaluated where it is read, never a step that runs.
        {.key = "Number", .title = "Number", .category = "Math",
         .numCount = 1, .numLabels = {"Value"},
         .numTips = {"The constant this node outputs."},
         .pure = true, .numOut = true,
         .desc = "Pure number: a literal. What you feed a Math node or any "
                 "number input when the value is simply known."},
        {.key = "NumAdd", .title = "Add", .category = "Math", .numCount = 1,
         .numLabels = {"B"},
         .numTips = {"The second operand, used only while fewer than two "
                     "links are wired: with one input it is added to it, with "
                     "none the node IS B."},
         .pure = true, .numIn = true, .numOut = true,
         .numFold = true,
         .desc = "Pure number: every wired number input summed. Get Int -> "
                 "Add (B 1) -> Set Int is the read-modify-write counter."},
        {.key = "NumSub", .title = "Subtract", .category = "Math",
         .numCount = 1, .numLabels = {"B"},
         .numTips = {"The amount to subtract while only one input is wired. "
                     "With none the node is -B."},
         .pure = true, .numIn = true,
         .numOut = true, .numFold = true,
         .desc = "Pure number: the wired inputs subtracted in LINK order (a - "
                 "b - c), so which link you drew first matters."},
        {.key = "NumMul", .title = "Multiply", .category = "Math",
         .numCount = 1, .numLabels = {"B"},
         .numTips = {"The factor to multiply by while only one input is "
                     "wired. With none the node IS B."},
         .pure = true, .numIn = true,
         .numOut = true, .numFold = true,
         .desc = "Pure number: every wired number input multiplied together."},
        {.key = "NumDiv", .title = "Divide", .category = "Math",
         .numCount = 1, .numLabels = {"B"},
         .numTips = {"The divisor while only one input is wired. With none "
                     "the node IS B."},
         .pure = true, .numIn = true,
         .numOut = true, .numFold = true,
         .desc = "Pure number: the wired inputs divided in LINK order (a / b "
                 "/ c). Division by zero yields 0 rather than a NaN, so a bad "
                 "divisor gives a wrong answer instead of poisoning "
                 "everything downstream."},
        {.key = "NumAtLeast", .title = "Number At Least", .category = "Math",
         .numCount = 1, .numLabels = {"Threshold"},
         .numTips = {"The value the wired number has to reach or beat."},
         .pure = true,
         .boolOut = true, .numIn = true, .numInExtra = true,
         .desc = "Pure bool: is the wired number at or above the threshold? "
                 "The bridge from the number plane into the logic gates and "
                 "On Condition."},
        // The rest of the number plane's arithmetic. The n-ary ones fold over
        // every wired link (.numFold), the unary ones read only the first - a
        // distinction the editor's link pruning and codegen both take from
        // that one flag.
        {.key = "NumMin", .title = "Min", .category = "Math", .numCount = 1,
         .numLabels = {"B"},
         .numTips = {"The other candidate while only one input is wired - so "
                     "min(input, B) is the usual way to CAP a value. With "
                     "none the node IS B."},
         .pure = true, .numIn = true, .numOut = true,
         .numFold = true,
         .desc = "Pure number: the SMALLEST of every wired number input."},
        {.key = "NumMax", .title = "Max", .category = "Math", .numCount = 1,
         .numLabels = {"B"},
         .numTips = {"The other candidate while only one input is wired - so "
                     "max(input, B) puts a FLOOR under a value. With none the "
                     "node IS B."},
         .pure = true, .numIn = true, .numOut = true,
         .numFold = true,
         .desc = "Pure number: the LARGEST of every wired number input."},
        {.key = "NumMod", .title = "Modulo", .category = "Math", .numCount = 1,
         .numLabels = {"B"},
         .numTips = {"The divisor whose remainder you want, while only one "
                     "input is wired - the wrap-around for cycling through N "
                     "states or keeping an angle inside 360."},
         .pure = true, .numIn = true, .numOut = true,
         .numFold = true,
         .desc = "Pure number: the wired inputs taken modulo each other in "
                 "LINK order (a % b % c). A zero divisor yields 0, not a NaN."},
        {.key = "NumPow", .title = "Power", .category = "Math", .numCount = 1,
         .numLabels = {"Exponent"},
         .numTips = {"The power to raise the input to, while only one is "
                     "wired: 2 squares, 0.5 is a square root, -1 is a "
                     "reciprocal."},
         .pure = true, .numIn = true, .numOut = true,
         .numFold = true,
         .desc = "Pure number: the wired inputs raised to each other in LINK "
                 "order."},
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
         .numLabels = {"Min", "Max"},
         .numTips = {"The lowest value the output may take.",
                     "The highest. Below Min the two swap roles rather than "
                     "erroring, so a computed range cannot break the graph."},
         .pure = true, .numIn = true,
         .numOut = true, .numInExtra = true,
         .desc = "Pure number: the WIRED number held inside a range (both "
                 "params stay editable, since the wire is the value rather "
                 "than a replacement for one). The guard to put in front of "
                 "anything that must stay in range - a health bar, a volume, "
                 "a camera distance."},
        {.key = "NumLerp", .title = "Lerp", .category = "Math", .numCount = 2,
         .numLabels = {"From", "To"},
         .numTips = {"What the output is when the wired fraction is 0.",
                     "What it is at 1."},
         .pure = true, .numIn = true,
         .numOut = true, .numInExtra = true,
         .desc = "Pure number: blends between two values by the WIRED number, "
                 "read as a 0..1 fraction and clamped. The manual counterpart "
                 "of Tween Value - use it when you already have the fraction "
                 "(a Timer's elapsed over its duration)."},
        {.key = "NumRemap", .title = "Remap Range", .category = "Math",
         .numCount = 4, .numLabels = {"In min", "In max", "Out min", "Out max"},
         .numTips = {"The input value that should map onto Out min.",
                     "The input value that should map onto Out max.",
                     "What the output is at In min.",
                     "What the output is at In max. Putting it BELOW Out min is "
                     "how you invert the mapping (near = 1, far = 0)."},
         .pure = true, .numIn = true, .numOut = true, .numInExtra = true,
         .desc = "Pure number: rescales the WIRED number from one range onto "
                 "another, clamped to the output range. The one node between \"a "
                 "distance in world units\" and \"a 0..1 amount\" that "
                 "everything else wants."},
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
         .numCount = 1, .numLabels = {"Threshold"},
         .numTips = {"The value the wired number has to be at or below."},
         .pure = true,
         .boolOut = true, .numIn = true, .numInExtra = true,
         .desc = "Pure bool: is the wired number at or below the threshold?"},
        {.key = "NumEquals", .title = "Number Equals", .category = "Math",
         .numCount = 2, .numLabels = {"Value", "Tolerance"},
         .numTips = {"The value to compare against.",
                     "How far off still counts as equal. Floats almost never "
                     "land on an exact value, so this is a parameter rather "
                     "than a hidden epsilon - 0.5 tests \"rounds to Value\"."},
         .pure = true,
         .boolOut = true, .numIn = true, .numInExtra = true,
         .desc = "Pure bool: does the wired number equal a value, within a "
                 "tolerance?"},
        {.key = "NumInRange", .title = "Number In Range", .category = "Math",
         .numCount = 2, .numLabels = {"Min", "Max"},
         .numTips = {"The low end, included.",
                     "The high end, included."},
         .pure = true,
         .boolOut = true, .numIn = true, .numInExtra = true,
         .desc = "Pure bool: is the wired number between two bounds? One node "
                 "instead of an At Least and an At Most through an AND."},
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
         .numTips = {"How far the wave swings either side of Offset.",
                     "How many full cycles per second.",
                     "The value the wave is centred on - the resting level."},
         .pure = true, .numOut = true,
         .desc = "Pure number: a sine wave riding the scene clock. THE node "
                 "for a pulsing light, a breathing glow, a bobbing prop - it "
                 "costs one sinf where doing it by hand costs a graph running "
                 "every frame."},
        // Random needs a moment, not an expression: a pure random would re-roll
        // on every read, so two consumers of \"the same\" number would disagree.
        // So it is an action that LATCHES its roll, with an exec output to
        // sequence what reads it.
        {.key = "RollRandom", .title = "Roll Random", .category = "Math",
         .numCount = 3, .numLabels = {"Min", "Max", "Whole"},
         .numTips = {"The lowest value the roll can give.",
                     "The highest.",
                     "On = round the result to a whole number, for a dice "
                     "roll or a random index into Switch Number."},
         .numOut = true,
         .execOutCount = 1, .execOutLabels = {"then"},
         .desc = "On exec, rolls a random number and LATCHES it on the number "
                 "output; 'then' fires right after, so whatever reads the "
                 "roll runs after it. The output keeps that roll until the "
                 "next exec - it is a value, not a fresh surprise per read, "
                 "which is what stops two consumers of \"the same\" number "
                 "disagreeing."},
        {.key = "OpenMenu", .title = "Open Menu", .category = "Menus",
         .strKind = FlowParamKind::MenuName,
         .strTip = "Which menu to open (Project panel > Menus). Its own entries "
                   "decide how the player gets back out.",
         .desc = "Opens a menu, which takes the pad and pauses gameplay."},
        {.key = "OnMenuEvent", .title = "On Menu Event", .category = "Menus",
         .trigger = true, .strKind = FlowParamKind::Text,
         .strTip = "The Flow event name set on a menu entry (Project panel > "
                   "Menus). Use the same string on both sides.",
         .boolOut = true,
         .desc = "Fires the frame a menu entry carrying this event name is "
                 "chosen - the way a menu tells the game to do something. "
                 "Also usable as a bool source."},
        // ------------------------------------------------------------------
        // The position plane's own arithmetic. A position INPUT takes exactly
        // one link (unlike the bool/number planes), so these are all unary:
        // one position in, one out, with the second operand as params or on the
        // number plane. Chained, they compose - Position -> With X -> Offset ->
        // Snap To Terrain is a computed spawn point.
        {.key = "PosConst", .title = "Position", .category = "Vector",
         .numCount = 3, .numLabels = {"X", "Y", "Z"},
         .numTips = {"The X of the constant point.",
                     "Its Y (height).",
                     "Its Z."},
         .posOut = true,
         .pure = true,
         .desc = "Pure position: a literal. The starting point to feed the "
                 "other Vector nodes when you are not reading one off an "
                 "object."},
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
         .numCount = 1, .numLabels = {"X"},
         .numTips = {"The X to substitute. A wired number replaces it."},
         .posIn = true, .posOut = true,
         .pure = true, .numIn = true,
         .desc = "Pure position: the linked position with its X replaced. "
                 "Chain With X / With Y / With Z to build a position out of "
                 "computed numbers."},
        {.key = "PosWithY", .title = "With Y", .category = "Vector",
         .numCount = 1, .numLabels = {"Y"},
         .numTips = {"The Y (height) to substitute. A wired number replaces "
                     "it."},
         .posIn = true, .posOut = true,
         .pure = true, .numIn = true,
         .desc = "Pure position: the linked position with its height replaced "
                 "- the node between a ground position and a point above it."},
        {.key = "PosWithZ", .title = "With Z", .category = "Vector",
         .numCount = 1, .numLabels = {"Z"},
         .numTips = {"The Z to substitute. A wired number replaces it."},
         .posIn = true, .posOut = true,
         .pure = true, .numIn = true,
         .desc = "Pure position: the linked position with its Z replaced."},
        {.key = "PosOffset", .title = "Offset Position", .category = "Vector",
         .numCount = 3, .numLabels = {"dX", "dY", "dZ"},
         .numTips = {"Units to add along X.",
                     "Units to add along Y - \"two units above that object\" is "
                     "Get Position into this with dY 2.",
                     "Units to add along Z."},
         .posIn = true,
         .posOut = true, .pure = true,
         .desc = "Pure position: the linked position shifted by a fixed "
                 "delta."},
        {.key = "PosScale", .title = "Scale Position", .category = "Vector",
         .numCount = 1, .numLabels = {"Factor"},
         .numTips = {"What to multiply all three components by. A wired "
                     "number replaces it."},
         .posIn = true, .posOut = true,
         .pure = true, .numIn = true,
         .desc = "Pure position: the linked position multiplied. Scaling a "
                 "position treats it as a DIRECTION from the world origin, so "
                 "this is the node for lengthening an offset - not for moving "
                 "a point."},
        {.key = "PosRotateY", .title = "Rotate Around Y", .category = "Vector",
         .numCount = 1, .numLabels = {"Degrees"},
         .numTips = {"Degrees to turn about the world Y axis, THROUGH THE "
                     "ORIGIN - not about the point itself. A wired number "
                     "replaces it."},
         .posIn = true, .posOut = true,
         .pure = true, .numIn = true,
         .desc = "Pure position: the linked position swung around the world's "
                 "vertical axis. Offset Position -> Rotate Around Y -> Offset "
                 "Position back is how you put things on a ring; feed the "
                 "angle from a For Loop's index times (360 / count) to place "
                 "a whole circle of them."},
        {.key = "PosDistance", .title = "Distance To Point",
         .category = "Vector", .numCount = 3, .numLabels = {"X", "Y", "Z"},
         .numTips = {"The X of the point to measure to.",
                     "Its Y.",
                     "Its Z."},
         .posIn = true, .pure = true, .numOut = true,
         .desc = "Pure number: the straight-line distance from the linked "
                 "position to a fixed point. Through Number At Most it is a "
                 "proximity test between ANY two points, which the Near "
                 "Object trigger cannot express."},
        {.key = "PosTerrainY", .title = "Terrain Height At",
         .category = "Vector", .posIn = true, .pure = true, .numOut = true,
         .desc = "Pure number: the ground height under the linked position - "
                 "the same bilinear heightmap the player walks on."},
        {.key = "PosOnTerrain", .title = "Snap To Terrain",
         .category = "Vector", .numCount = 1, .numLabels = {"Lift"},
         .numTips = {"Units to add above the ground - half an object's height "
                     "keeps it from sinking into the floor. 0 puts the point "
                     "exactly on the surface."},
         .posIn = true, .posOut = true, .pure = true,
         .desc = "Pure position: the linked position with its Y replaced by the "
                 "ground height there. The node to put in front of Spawn Object "
                 "so a computed spawn point lands ON the terrain instead of "
                 "inside it or in mid-air."},
        // Same reasoning as Roll Random: a pure random point would be a
        // different point per read.
        {.key = "RollAreaPoint", .title = "Roll Point In Area",
         .category = "Vector", .strKind = FlowParamKind::AreaName,
         .strTip = "The Area object to pick inside (docs/areas.md). Read "
                   "live, so a moving area moves the scatter with it.",
         .posOut = true, .execOutCount = 1, .execOutLabels = {"then"},
         .desc = "On exec, picks a random point inside an area and LATCHES it "
                 "on the position output; 'then' fires right after. Spawn "
                 "scattering, wander targets, random patrol."},
        {.key = "SetScreenFx", .title = "Set Screen Effect",
         .category = "Scene", .strKind = FlowParamKind::ScreenFxName,
         .strTip = "Which PLACED custom effect to drive. Only effects placed "
                   "in the UI Editor's screen stack exist at runtime - a "
                   ".screenfx file on its own is not enough.",
         .numCount = 4, .numLabels = {"P1", "P2", "P3", "P4"},
         .numTips = {"The effect's first parameter. What it MEANS is the "
                     "effect's own business - the node shows its real name "
                     "and range once one is picked. A wired number replaces "
                     "this one.",
                     "The effect's second parameter.",
                     "Its third.",
                     "Its fourth."},
         .numIn = true,
         .execInCount = 3, .execInLabels = {"set", "on", "off"},
         .execInTips = {"Writes the four parameters below.",
                        "Switches the effect on.",
                        "Switches it off."},
         .desc = "Drives one of the project's custom screen effects "
                 "(docs/custom-screen-effects.md). Until this existed a "
                 ".screenfx effect was frozen at whatever the editor "
                 "authored."},
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
         .numLabels = {"Decimals", "Min digits"},
         .numTips = {"How many digits after the point. 0 = a whole number, no "
                     "point at all.",
                     "Pad with leading zeros out to at least this many digits "
                     "before the point - 5 makes a score read 00420."},
         .pure = true, .textOut = true, .numIn = true, .numInExtra = true,
         .desc = "Pure converter: the WIRED number as text, with control over "
                 "its shape (both params stay editable, since the wire is the "
                 "value rather than a replacement for one). Number To Text is "
                 "the same thing with no formatting."},
        {.key = "SecondsToText", .title = "Seconds To Clock",
         .category = "Convert", .numCount = 1, .numLabels = {"Show tenths"},
         .numTips = {"On = print tenths as well (\"1:05.3\" instead of \"1:05\")."},
         .pure = true, .textOut = true, .numIn = true, .numInExtra = true,
         .desc = "Pure converter: a number of seconds as \"M:SS\". Wire a Timer "
                 "straight into it for a countdown or a lap clock on screen; "
                 "a negative input clamps to 0:00 rather than printing a "
                 "minus."},
        {.key = "TextJoin", .title = "Join Text", .category = "Convert",
         .strKind = FlowParamKind::Text,
         .strTip = "What to put BETWEEN the joined values: empty for straight "
                   "concatenation, \" - \" or \", \" for a list.",
         .pure = true, .textIn = true,
         .textOut = true,
         .desc = "Pure text: every wired text input joined in link order. The "
                 "way to build one Display Text out of several values."},
        {.key = "TextEquals", .title = "Text Equals", .category = "Convert",
         .strKind = FlowParamKind::Text,
         .strTip = "The string to compare against. CASE SENSITIVE, and "
                   "compared whole - there is no prefix or contains test "
                   "here.",
         .pure = true, .boolOut = true,
         .textIn = true,
         .desc = "Pure bool: does the first wired text input equal this "
                 "string? Compares a save text against a known value - a "
                 "chosen name, a stored difficulty, a quest state."},
        // ------------------------------------------------------------------
        // The event bus: the ONE way one object's graph talks to another's.
        // Before it existed the only channel was a global variable polled from
        // On Update, which is both slower and impossible to read as intent.
        // Delivery is deliberately ONE FRAME later and uniform for every
        // receiver - see the desc.
        {.key = "SendEvent", .title = "Send Event", .category = "Events",
         .strKind = FlowParamKind::EventName,
         .strTip = "The event's name - free text that exists by being used. Use "
                   "the SAME string on the On Event nodes that should hear it; "
                   "an unnamed event compiles out.",
         .numCount = 1, .numLabels = {"Value"},
         .numTips = {"An optional number payload, readable off the receiving On "
                     "Event's number output. A wired number replaces it."},
         .numIn = true,
         .desc = "Broadcasts to EVERY graph in the game - the one way one "
                 "object's graph talks to another's without either naming the "
                 "other. Receivers fire on the NEXT frame, uniformly, whichever "
                 "object owns them, so the order graphs happen to run in can "
                 "never change the outcome. That one frame (20 ms) is the price "
                 "of the guarantee; for something that must land inside the same "
                 "frame, wire the action directly."},
        {.key = "OnEvent", .title = "On Event", .category = "Events",
         .trigger = true, .strKind = FlowParamKind::EventName,
         .strTip = "The event name to listen for. It must match the Send "
                   "Event's string exactly; an unnamed event compiles out.",
         .boolOut = true,
         .numOut = true,
         .desc = "Fires the frame AFTER any graph sends this event. Its "
                 "number output is the Value that came with it and its bool "
                 "output is \"the event arrived this frame\". Events are how a "
                 "pickup tells the HUD, a switch tells three doors, or a boss "
                 "tells the music - without any of them naming the others."},
        {.key = "Log", .title = "Log Message", .category = "Debug",
         .strKind = FlowParamKind::Text,
         .strTip = "A fixed label printed first, before every wired text input - "
                   "so \"health:\" plus a Number To Text reads as a labelled "
                   "value.",
         .textIn = true,
         .desc = "Prints a line to the game's bin/log.txt in DEBUG builds (a "
                 "release build emits nothing). The printf of the graph."},
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
    // `tip0..tip3` / `tip_string` header keys -> FlowNodeType::numTips /
    // strTip. A custom node's parameters are the ones a reader has the least
    // chance of guessing (they are one project's idea), so the format offers
    // the same per-parameter line the built-ins carry.
    std::string numTipStore[4];
    std::string strTipStore;
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

// Hover help for exec input `pin` of `t` ("" when it has none). Bounds-checked
// the same way as the label, so a caller can walk execInCount blind.
inline const char* flowExecInTip(const FlowNodeType& t, int pin) {
    if (pin < 0 || pin >= kFlowMaxExecIn) return "";
    return t.execInTips[pin] ? t.execInTips[pin] : "";
}

// The name of the STRING param on `t`, as the node body labels its widget.
// One answer for the node body and the node's tooltip: a tooltip that lists a
// parameter under a name no widget carries is worse than no tooltip, and the
// two drifted the moment either was edited alone. Keyed by node first because
// three nodes repurpose the param (a clip name, a name PREFIX, a preset).
inline const char* flowStrLabel(const FlowNodeType& t) {
    const std::string key = t.key ? t.key : "";
    if (key == "Animation") return "Clip";
    if (key == "PatrolWaypoints" || key == "FindNearest") return "Prefix";
    if (key == "SetInputPreset") return "Preset";
    switch (t.strKind) {
        case FlowParamKind::ObjectName: return "Object";
        case FlowParamKind::Button: return "Button";
        case FlowParamKind::InputActionName: return "Action";
        case FlowParamKind::KeyName: return "Key";
        case FlowParamKind::Text: return "Text";
        case FlowParamKind::MusicTrack: return "Track";
        case FlowParamKind::SoundTrack: return "Sound";
        case FlowParamKind::SceneName: return "Scene";
        case FlowParamKind::LayerName: return "Layer";
        case FlowParamKind::AreaName: return "Area";
        case FlowParamKind::SaveValue: return "Value";
        case FlowParamKind::SaveText: return "Value";
        case FlowParamKind::GradingName: return "Preset";
        case FlowParamKind::AmbienceName: return "Preset";
        case FlowParamKind::SequenceName: return "Sequence";
        case FlowParamKind::CreditsName: return "Credits";
        case FlowParamKind::MenuName: return "Menu";
        case FlowParamKind::HudTextName: return "Text";
        case FlowParamKind::FontName: return "Font";
        case FlowParamKind::ScreenFxName: return "Effect";
        case FlowParamKind::PrefabName: return "Prefab";
        case FlowParamKind::VarName: return "Variable";
        case FlowParamKind::EventName: return "Event";
        case FlowParamKind::FactName: return "Fact";
        case FlowParamKind::FactQueryName: return "Query";
        default: return "";
    }
}

// The name of the SECOND string param, as the node body labels it ("" for the
// nodes that have none). Same single-answer reasoning as flowStrLabel.
inline const char* flowStr2Label(const FlowNodeType& t) {
    const std::string key = t.key ? t.key : "";
    if (key == "SetSaveText") return "Text";
    if (key == "DisplayText") return "Prefix";
    return "";
}

// Hover help for numeric param `i` of `t` ("" when it has none).
inline const char* flowNumTip(const FlowNodeType& t, int i) {
    if (i < 0 || i >= 4) return "";
    return t.numTips[i] ? t.numTips[i] : "";
}

// True when `t` documents at least one of its parameters - the switch that
// decides whether the node's tooltip prints a parameter section at all.
inline bool flowHasParamTips(const FlowNodeType& t) {
    if (t.strTip && *t.strTip) return true;
    if (t.str2Tip && *t.str2Tip) return true;
    for (int i = 0; i < 4 && i < t.numCount; ++i)
        if (t.numTips[i] && *t.numTips[i]) return true;
    for (int i = 0; i < kFlowMaxExecIn && i < t.execInCount; ++i)
        if (t.execInTips[i] && *t.execInTips[i]) return true;
    return false;
}
