#pragma once

#include <memory>
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
    AmbienceName,  // name of a Project::ambiencePresets entry ("" = none)
    LayerName,  // name of a SceneData::layers entry (streaming layer)
    SequenceName,  // name of a Project::sequences entry (Cutscene Director)
    HudTextName,  // name of a Project::hudTexts entry (baked text sprite)
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
        // Dynamic spawning: Spawn Object clones its target object (link >
        // name > self) into a free runtime slot - the clone starts at the
        // linked position (or the template's own) with the given yaw, and the
        // node's object output is the CLONE, not the template (wire it into
        // Despawn Object / Set Position / Play Animation / ...). The pool
        // holds 32 live clones; spawning past that fails silently (-1).
        // Despawn Object removes a clone immediately (frees its slot); on an
        // authored object it only deactivates it (layer streaming can bring
        // authored objects back).
        {"SpawnObject", "Spawn Object", "Object", false, FlowParamKind::ObjectName, 1,
         {"Yaw"}, FlowParamKind::None, true, true, true, false, false},
        {"DespawnObject", "Despawn Object", "Object", false, FlowParamKind::ObjectName,
         0, {}, FlowParamKind::None, true, false},
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
        // Casts a ray from the player's eye along the view direction when its
        // exec fires, and latches the results: the position output is the hit
        // point (an object's bounding sphere or the terrain; the ray's end at
        // Max Dist when nothing was hit) and the object output is the hit
        // object (a runtime reference, -1 = none - actions fed it are guarded
        // like Spawn Object clones). The "after" exec fires right after the
        // cast, so downstream actions read fresh results.
        {"Raycast", "Raycast", "Player", false, FlowParamKind::None, 1,
         {"Max Dist"}, FlowParamKind::None, false, true, false, true, false,
         false, false, false, false, true},
        // Sets the player's flashlight master switch (the Player object's
        // "Enabled"). On = 1 turns it on, 0 off. The optional toggle button on
        // the player still gates the beam on/off, but only while enabled.
        {"SetFlashlight", "Set Flashlight", "Player", false, FlowParamKind::None, 1,
         {"On"}, FlowParamKind::None, false, false},
        // Changes the analog stick response curve live (Preferences > Input
        // sets the defaults). Stick: 0 = left/movement, 1 = right/camera,
        // 2 = both. Curve: 0 = Linear, 1 = Exponential, 2 = S-Curve. Exponent
        // (>=1) shapes curves 1/2 - e.g. a sniper mode dropping to a gentle
        // low-sensitivity curve, or an options menu offering response presets.
        {"SetStickCurve", "Set Stick Curve", "Player", false, FlowParamKind::None, 3,
         {"Stick", "Curve", "Exponent"}, FlowParamKind::None, false, false},
        // Scene
        {"SetSky", "Set Sky Color", "Scene", false, FlowParamKind::None, 3, {},
         FlowParamKind::Color, false, false},
        // Loads another scene (applied after the current frame's scripts):
        // runtime objects are rebuilt from the target scene's data, script
        // state resets; textures/models stay loaded (shared across scenes).
        {"SwitchScene", "Switch Scene", "Scene", false, FlowParamKind::SceneName, 0, {},
         FlowParamKind::None, false, false},
        // Streaming layers (GTA3-style): Load Layer starts pulling the layer's
        // assets into memory (spread over frames, no hitch) and activates its
        // objects when everything is resident; Unload Layer deactivates the
        // objects immediately and frees assets no other loaded layer uses.
        // Is Layer Loaded is a pure bool: true once the layer is fully in.
        {"LoadLayer", "Load Layer", "Scene", false, FlowParamKind::LayerName, 0, {},
         FlowParamKind::None, false, false},
        {"UnloadLayer", "Unload Layer", "Scene", false, FlowParamKind::LayerName, 0, {},
         FlowParamKind::None, false, false},
        {"IsLayerLoaded", "Is Layer Loaded", "Scene", false, FlowParamKind::LayerName,
         0, {}, FlowParamKind::None, false, false, false, false, true, false, true},
        // Applies a color grading preset (Tools > Color Grading) as the
        // frame's GS post pass; "<none>" restores the ungraded image. The
        // switch is global and persists across scene changes.
        {"SetGrading", "Set Color Grading", "Scene", false, FlowParamKind::GradingName,
         0, {}, FlowParamKind::None, false, false},
        // Runtime graphics switches (options menus / perf tuning). Set Fog
        // On=1 re-applies the scene's own fog, 0 disables it. Set Bloom / Set
        // Grain take a 0..1 amount (0 = off). Set Particles is a global switch
        // for every emitter's draw. Handy wired to On Menu Event entries so the
        // player can toggle expensive effects on real hardware.
        {"SetFog", "Set Fog", "Scene", false, FlowParamKind::None, 1, {"On"},
         FlowParamKind::None, false, false},
        {"SetBloom", "Set Bloom", "Scene", false, FlowParamKind::None, 1, {"Amount"},
         FlowParamKind::None, false, false},
        {"SetGrain", "Set Grain", "Scene", false, FlowParamKind::None, 1, {"Amount"},
         FlowParamKind::None, false, false},
        // Depth of field: the image blurs progressively past Focus (world
        // units from the camera), reaching full blur at Focus + Range; Amount
        // 0..1 scales the far blur. The authored baseline lives in Tools >
        // UI Editor > Depth of field (per-scene overridable); this node's
        // Mode switches it at runtime: 0 = set the custom Focus/Range/Amount
        // params, 1 = off, 2 = restore the scene's authored setting. A
        // position link replaces Focus with the distance from the player to
        // that point (e.g. keep an object in focus via Get Position).
        {"SetDof", "Set Depth Of Field", "Scene", false, FlowParamKind::None, 4,
         {"Focus", "Range", "Amount", "Mode"}, FlowParamKind::None, false,
         false, true, false, false},
        {"SetParticles", "Set Particles", "Scene", false, FlowParamKind::None, 1, {"On"},
         FlowParamKind::None, false, false},
        // Runtime video output (options menus). Set Display Mode switches the
        // scan mode (Mode: 0 = interlaced 480i/576i, 1 = progressive 480p,
        // 2 = 1080i - shown as a combo in the node UI); with Confirm s > 0
        // the game shows a keep-or-revert prompt and AUTOMATICALLY reverts
        // to the previous mode unless the player confirms with X in time
        // (a mode the TV can't show would otherwise strand them on a black
        // screen). Set Widescreen re-fits the projection for a 16:9 display.
        {"SetDisplayMode", "Set Display Mode", "Scene", false, FlowParamKind::None, 2,
         {"Mode", "Confirm s"}, FlowParamKind::None, false, false},
        {"SetWidescreen", "Set Widescreen", "Scene", false, FlowParamKind::None, 1,
         {"On"}, FlowParamKind::None, false, false},
        // Repaints the sky from an Ambience Editor preset at runtime. Lighting
        // and fog are baked per scene at build, so only the sky changes live
        // (assign presets per scene, or switch scenes, for the full mood).
        {"SetAmbience", "Set Ambience", "Scene", false, FlowParamKind::AmbienceName,
         0, {}, FlowParamKind::None, false, false},
        // Cutscene Director (Tools > Cutscene Director). Play Sequence starts a
        // named keyframe timeline: it poses the referenced objects (and, if the
        // sequence has a camera track, drives the camera) until it ends or is
        // stopped. Retriggering restarts it from t=0. Stop Sequence ends the
        // active cutscene and hands the camera back to the game.
        {"PlaySequence", "Play Sequence", "Scene", false, FlowParamKind::SequenceName,
         0, {}, FlowParamKind::None, false, false},
        {"StopSequence", "Stop Sequence", "Scene", false, FlowParamKind::None,
         0, {}, FlowParamKind::None, false, false},
        // Endless scroller (Insert > World > Scroller). Start/Stop run or freeze
        // the belt of the target scroller object (defaults to self); Set
        // Scroller Speed changes its belt speed live (units/s, negative =
        // reverse). The target is the Scroller object itself, not its members.
        {"StartScroller", "Start Scroller", "Scroller", false,
         FlowParamKind::ObjectName, 0, {}, FlowParamKind::None, true, true},
        {"StopScroller", "Stop Scroller", "Scroller", false,
         FlowParamKind::ObjectName, 0, {}, FlowParamKind::None, true, true},
        {"SetScrollerSpeed", "Set Scroller Speed", "Scroller", false,
         FlowParamKind::ObjectName, 1, {"Speed"}, FlowParamKind::None, true, true},
        // HUD (all HUD images at once; the USE prompt is unaffected)
        {"ShowHud", "Show HUD", "HUD", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false},
        {"HideHud", "Hide HUD", "HUD", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false},
        {"ToggleHud", "Toggle HUD", "HUD", false, FlowParamKind::None, 0, {},
         FlowParamKind::None, false, false},
        // On-screen texts (Tools > UI Editor > Texts; baked to sprites at
        // build). Show Text: Seconds > 0 auto-hides after that long, 0 =
        // stays until a Hide Text (subtitles, tutorial hints, pickup toasts).
        {"ShowText", "Show Text", "HUD", false, FlowParamKind::HudTextName, 1,
         {"Seconds"}, FlowParamKind::None, false, false},
        {"HideText", "Hide Text", "HUD", false, FlowParamKind::HudTextName, 0, {},
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
