#include "aigen.hpp"

#include "platform.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "json.hpp"
#include "project.hpp"

namespace fs = std::filesystem;

namespace aigen {

// ---------------------------------------------------------------------------
// Backend / model catalogs (UI + command building)
// ---------------------------------------------------------------------------

const char* backendLabel(const std::string& backend) {
    if (backend == "copilot") return "GitHub Copilot CLI";
    if (backend == "codex") return "OpenAI Codex CLI";
    if (backend == "openai") return "OpenAI API";
    return "Claude CLI";
}

std::vector<const char*> backendIds() {
    return {"claude", "codex", "copilot", "openai"};
}

std::vector<const char*> modelPresets(const std::string& backend) {
    // Presets are a convenience, not a whitelist - the preferences dialog also
    // takes free text, so new models work the day they ship.
    if (backend == "copilot")
        return {"", "claude-sonnet-4.5", "gpt-5.1"};
    if (backend == "codex")
        return {"", "gpt-5.1-codex", "gpt-5.1"};
    if (backend == "openai")
        return {"gpt-5.1", "gpt-5", "gpt-5-mini", "gpt-4.1"};
    return {"", "opus", "sonnet", "haiku"};
}

// ---------------------------------------------------------------------------
// System prompt
// ---------------------------------------------------------------------------

static std::string jsonEsc(const std::string& s) { return json::escape(s); }

// What a FlowParamKind's str slot means, plus where its legal values come from.
static std::string strKindDesc(FlowParamKind k) {
    switch (k) {
        case FlowParamKind::Text: return "free text";
        case FlowParamKind::ObjectName:
            return "target object name (\"\" = self / the graph owner)";
        case FlowParamKind::Button:
            return "pad button: Cross, Circle, Square, Triangle, DpadUp, "
                   "DpadDown, DpadLeft, DpadRight, L1, L2, L3, R1, R2, R3, "
                   "Start, Select";
        case FlowParamKind::Color: return "";
        case FlowParamKind::MusicTrack: return "music track path (see context)";
        case FlowParamKind::SoundTrack: return "sound effect path (see context)";
        case FlowParamKind::SceneName: return "scene name (see context)";
        case FlowParamKind::SaveValue: return "save value name (see context)";
        case FlowParamKind::MenuName: return "menu name (see context)";
        case FlowParamKind::VarName:
            return "variable name (free text; created on first use)";
        case FlowParamKind::SaveText: return "save text name (see context)";
        case FlowParamKind::GradingName:
            return "color grading preset name (see context; \"\" = off)";
        case FlowParamKind::AmbienceName:
            return "ambience preset name (see context)";
        case FlowParamKind::LayerName: return "streaming layer name (see context)";
        case FlowParamKind::AreaName:
            return "Area object name (an invisible volume; see context)";
        case FlowParamKind::SequenceName: return "sequence name (see context)";
        case FlowParamKind::CreditsName: return "credits roll name (see context)";
        case FlowParamKind::HudTextName:
            return "on-screen text name (see context)";
        // Display Text's font. It was the one strKind with no entry here, so
        // its str slot reached the model as "no string param" while the node's
        // prose talked about "the font named str".
        case FlowParamKind::FontName:
            return "font name (see context; Tools > Font Manager; \"\" = the "
                   "project's first font)";
        case FlowParamKind::InputActionName:
            return "input action name (see context; Tools > Input Map)";
        case FlowParamKind::KeyName:
            return "keyboard key label: A-Z, 0-9, Enter, Esc, Backspace, Tab, "
                   "Space, F1-F12, Up, Down, Left, Right, Left Shift, "
                   "Left Ctrl, Left Alt";
        case FlowParamKind::PrefabName:
            return "prefab name (see context; Tools > Prefabs)";
        case FlowParamKind::EventName:
            return "event name (free text - an event exists by being named; use "
                   "the SAME string on the Send Event and the On Event)";
        case FlowParamKind::ScreenFxName:
            return "custom screen-effect key, \"custom:<file-stem>\" (only "
                   "effects PLACED in the screen stack can be driven)";
        default: return "";
    }
}

// One catalog line per node type, derived from the live registry so custom
// .flownode nodes and future built-ins document themselves.
static std::string nodeCatalogLine(const FlowNodeType& t) {
    std::ostringstream o;
    o << "- " << t.key << " (\"" << t.title << "\", " << t.category << "). ";
    // pins
    std::vector<std::string> in, out;
    if (t.trigger) {
        out.push_back("exec");
    } else if (!t.pure) {
        if (t.execInCount > 1) {
            // A merged node's branches are only reachable through a link's
            // "pin", so the catalog has to name them - otherwise every graph the
            // model writes lands on pin 0 (show / set) whatever it meant.
            std::string s = "exec pins";
            for (int e = 0; e < t.execInCount && e < kFlowMaxExecIn; ++e) {
                s += std::string(e ? ", " : " ") + std::to_string(e) + "=" +
                     flowExecInLabel(t, e);
                // What the branch DOES lives in the pin's tip now (it used to be
                // inline in `desc`), so the catalog has to carry it or the model
                // sees three pin names and no idea which one it wants.
                if (const char* tip = flowExecInTip(t, e); *tip) {
                    std::string g(tip);
                    while (!g.empty() && (g.back() == '.' || g.back() == ' '))
                        g.pop_back();
                    s += " (" + g + ")";
                }
            }
            in.push_back(s);
        } else {
            in.push_back("exec");
        }
        // Exec OUTPUTS: a flow-control node's branches are only reachable
        // through a link's "fpin", so the catalog has to name them for the same
        // reason it names the input pins - otherwise every graph the model
        // writes leaves pin 0 ("true", "1", "A") whatever it meant.
        const int eo = flowExecOutCount(t);
        if (eo > 1) {
            std::string s = "exec out pins";
            for (int e = 0; e < eo; ++e)
                s += std::string(e ? ", " : " ") + std::to_string(e) + "=" +
                     flowExecOutLabel(t, e);
            out.push_back(s);
        } else if (eo == 1) {
            // A single output still has to be NAMED, or a node whose whole
            // point is "and then this runs" (Tween's finished, Do Once's then)
            // reads in the catalog as having no exec output at all.
            out.push_back(std::string("exec \"") + flowExecOutLabel(t, 0) +
                          "\" (fires later - see doc)");
        }
    }
    if (t.idIn) in.push_back("object");
    if (t.idOut) out.push_back("object");
    if (t.posIn) in.push_back("position");
    if (t.posOut) out.push_back("position");
    if (t.boolIn) in.push_back("bool (accepts several links)");
    if (t.boolOut) out.push_back("bool");
    if (t.textIn) in.push_back("text (accepts several links)");
    if (t.textOut) out.push_back("text");
    if (t.numIn)
        in.push_back(flowNumFolds(t)     ? "number (accepts several links)"
                     : t.numCount > 0 ? "number (overrides num[0])"
                                      : "number");
    if (t.numOut) out.push_back("number");
    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + v[i];
        return s;
    };
    o << "Inputs: " << (in.empty() ? "none" : join(in))
      << ". Outputs: " << (out.empty() ? "none" : join(out)) << ".";
    // Params. The per-parameter MEANING lives in FlowNodeType::numTips /
    // strTip / str2Tip (it used to be spelled out inline in `desc`, which made
    // hovering a node in the editor a wall of prose - see flowgraph.hpp). The
    // registry is still the single source, so the catalog reads the tips from
    // exactly where the editor's tooltips read them: drop them here and the
    // generator loses everything it knew about what a parameter is FOR.
    // A tip is written as prose ending in a full stop; inside a parenthesised
    // gloss that reads as ".)." - so drop the terminal one here rather than
    // asking every registry entry to be punctuated for this one consumer.
    auto gloss = [](const char* tip) {
        std::string s = tip ? tip : "";
        while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
        return s.empty() ? s : " (" + s + ")";
    };
    const std::string sd = strKindDesc(t.strKind);
    // The tip is emitted even when the KIND has no standard description - a node
    // whose str is free text still says what that text is for.
    if (!sd.empty() || (t.strTip && *t.strTip))
        o << " str = " << (sd.empty() ? "free text" : sd) << gloss(t.strTip)
          << ".";
    if (t.str2Tip && *t.str2Tip)
        o << " str2 = " << flowStr2Label(t) << gloss(t.str2Tip) << ".";
    if (t.numKind == FlowParamKind::Color) {
        o << " num[0..2] = RGB color, each 0..1" << gloss(t.numTips[0]) << ".";
    } else if (t.numCount > 0) {
        o << " num params:";
        for (int i = 0; i < t.numCount && i < 4; ++i) {
            o << (i ? "; " : " ") << "num[" << i << "]="
              << (t.numLabels[i] && *t.numLabels[i] ? t.numLabels[i] : "value")
              << gloss(flowNumTip(t, i));
        }
        o << ".";
    }
    if (t.desc && *t.desc) o << " " << t.desc;
    return o.str();
}

// Comma-joined name list, or "" when empty (caller skips the line).
template <typename T, typename F>
static std::string nameList(const std::vector<T>& v, F name) {
    std::string s;
    for (const T& e : v) {
        const std::string n = name(e);
        if (n.empty()) continue;
        if (!s.empty()) s += ", ";
        s += "\"" + n + "\"";
    }
    return s;
}

std::string nodeCatalog(const std::string& category) {
    std::string s;
    for (const FlowNodeType* t : flowAllNodeTypes()) {
        if (!category.empty() && category != t->category) continue;
        s += nodeCatalogLine(*t) + "\n";
    }
    return s;
}

// A graph in the REPLY schema ("kind" strings), for the edit-mode prompt -
// the model sees its input in exactly the shape it must answer in. (The
// project-file serializer uses bool flags instead; parseGraph reads both.)
std::string graphJson(const FlowGraph& fg) {
    std::ostringstream o;
    o << "{ \"nodes\": [";
    for (size_t i = 0; i < fg.nodes.size(); ++i) {
        const FlowNode& n = fg.nodes[i];
        o << (i ? ", " : "") << "{ \"id\": " << n.id << ", \"type\": \"" << n.type
          << "\", \"pos\": [" << (int)n.pos[0] << ", " << (int)n.pos[1] << "]";
        if (!n.str.empty()) o << ", \"str\": \"" << jsonEsc(n.str) << "\"";
        if (!n.str2.empty()) o << ", \"str2\": \"" << jsonEsc(n.str2) << "\"";
        o << ", \"num\": [" << n.num[0] << ", " << n.num[1] << ", " << n.num[2]
          << ", " << n.num[3] << "] }";
    }
    o << "], \"links\": [";
    for (size_t i = 0; i < fg.links.size(); ++i) {
        const FlowLink& l = fg.links[i];
        const char* kind = l.kind == FlowLinkObject ? "object"
                           : l.kind == FlowLinkPos  ? "pos"
                           : l.kind == FlowLinkBool ? "bool"
                           : l.kind == FlowLinkText ? "text"
                           : l.kind == FlowLinkNum  ? "number"
                                                    : "exec";
        o << (i ? ", " : "") << "{ \"from\": " << l.fromNode
          << ", \"to\": " << l.toNode << ", \"kind\": \"" << kind << "\"";
        // Which branch of a merged node the link fires. Dropping it here used
        // to silently rewrite every hide/toggle/add link to pin 0 when the
        // model edited an existing graph.
        if (l.toPin) o << ", \"pin\": " << l.toPin;
        // ...and which branch of a flow-control node it LEAVES.
        if (l.fromPin) o << ", \"fpin\": " << l.fromPin;
        o << " }";
    }
    o << "] }";
    return o.str();
}

std::string systemPrompt(const Project& p, int ownerIndex,
                         const FlowGraph* editing) {
    const SceneData& sc = p.active();
    std::ostringstream o;
    o << "You are a flow-graph author for TyraX, an editor that generates "
         "PlayStation 2 games. A flow graph is a visual logic script owned by "
         "one scene object. You will receive a request describing desired "
         "gameplay logic; respond with a flow graph implementing it.\n"
         "\n"
         "OUTPUT FORMAT - reply with EXACTLY ONE JSON object and nothing "
         "else. No markdown fences, no commentary. Schema:\n"
         "{\n"
         "  \"nodes\": [ { \"id\": 1, \"type\": \"OnStart\", \"pos\": [x, y],"
         " \"str\": \"\", \"str2\": \"\", \"num\": [0, 0, 0, 0] } ],\n"
         "  \"links\": [ { \"from\": 1, \"to\": 2, \"kind\": \"exec\" } ]\n"
         "}\n"
         "Node ids are positive integers, unique within the graph. \"pos\" is "
         "the node's editor canvas position in pixels: lay the graph out "
         "left-to-right (triggers in the left column, actions to the right; "
         "about 280 px between columns, 140 px between rows, no overlaps). "
         "Omit \"str\"/\"str2\"/\"num\" when unused.\n"
         "\n"
         "LINK KINDS (\"kind\"):\n"
         "- \"exec\": execution flow, from a trigger's exec output (or a "
         "'fires later' exec output like Delay's) to an action's exec input. "
         "Pure nodes have no exec pins.\n"
         "- \"object\": passes an object reference from an object output to "
         "an object input.\n"
         "- \"pos\": passes XYZ coordinates from a position output to a "
         "position input.\n"
         "- \"bool\": per-frame boolean from a bool output to a bool input "
         "(logic gates fold over ALL their wired bool inputs).\n"
         "- \"text\": a text value from a text output to a text input.\n"
         "- \"number\": a computed number from a number output to a number "
         "input. A number link REPLACES the target's num[0] param; the Math "
         "nodes (Add/Subtract/Multiply/Divide) fold over ALL their wired "
         "number inputs.\n"
         "An exec link may carry \"pin\": N to fire a specific labeled exec "
         "input of a merged node (the catalog lists them, e.g. Set Int has "
         "0=set, 1=add). Omit it for the node's first pin.\n"
         "It may also carry \"fpin\": N to LEAVE a specific labeled exec output "
         "of a flow-control node (the catalog lists those too, e.g. Branch has "
         "0=true, 1=false; Sequence 0..3). Omit it for the node's first "
         "output.\n"
         "\n"
         "SEMANTICS:\n"
         "- Triggers fire their exec output; every action wired from it runs "
         "that frame. Most actions have NO exec output - to run several "
         "actions, wire EACH of them directly from the trigger (they run in "
         "link order). The exceptions are the 'fires later' outputs (Delay's "
         "after-timeout, Raycast's after-cast) and the Flow category, whose "
         "nodes decide which of their own outputs continues.\n"
         "- Use the Flow nodes instead of inventing patterns for control flow: "
         "Branch for if/else on a bool, Sequence when the ORDER of several "
         "actions matters, Do Once to make a repeating trigger fire a one-shot, "
         "Cooldown to rate-limit one, Gate for an on/off valve, Switch Number "
         "to dispatch a state machine, Timer/Tween for anything that plays out "
         "over time. Tween's number output animates any number input.\n"
         "- Nodes with an object input resolve their target in this order: "
         "incoming object link, then their \"str\" object name, then the "
         "graph's owner object (self). An empty str on an object param means "
         "self.\n"
         "- Pure nodes (logic gates, getters, converters) have no exec pins; "
         "they are evaluated on demand every frame.\n"
         "- To run actions when a condition BECOMES true, wire bool sources "
         "through logic gates into On Condition (fires on the rising edge) "
         "and chain actions from it.\n"
         "- A position link into a node with X/Y/Z params overrides those "
         "params; a number link overrides num[0] the same way.\n"
         "- To change a variable by an amount, prefer Set Int's \"add\" pin "
         "(pin 1) with num[0] as the delta over reading it back through Get "
         "Int + Add.\n"
         "- Keep graphs minimal: no unused nodes, no redundant links.\n"
         "\n"
         "NODE CATALOG (the ONLY valid \"type\" values):\n";
    for (const FlowNodeType* t : flowAllNodeTypes()) o << nodeCatalogLine(*t) << "\n";

    o << "\nPROJECT CONTEXT (reference these exact names):\n";
    if (ownerIndex >= 0 && ownerIndex < (int)sc.objects.size())
        o << "- This graph's OWNER object (self): \""
          << sc.objects[ownerIndex].name << "\"\n";
    o << "- Active scene: \"" << sc.name << "\"\n";
    o << "- Objects in this scene:\n";
    for (const SceneObject& obj : sc.objects) {
        o << "  - \"" << obj.name << "\" (" << primitiveTypeName(obj.type);
        if (obj.usable) o << ", usable";
        if (!obj.modelPath.empty() &&
            obj.modelPath.size() > 4 &&
            obj.modelPath.substr(obj.modelPath.size() - 4) == ".glb")
            o << ", animated model";
        o << ", at " << obj.position[0] << "," << obj.position[1] << ","
          << obj.position[2] << ")\n";
    }
    auto ctxLine = [&o](const char* label, const std::string& list) {
        if (!list.empty()) o << "- " << label << ": " << list << "\n";
    };
    auto id = [](const std::string& s) { return s; };
    ctxLine("Scenes", nameList(p.scenes, [](const SceneData& s) { return s.name; }));
    ctxLine("Streaming layers (this scene)",
            nameList(sc.layers, [](const SceneLayer& l) { return l.name; }));
    {
        // Areas are objects, so they are listed above too - called out
        // separately because In Area's str only accepts these.
        std::vector<std::string> areas;
        for (const SceneObject& obj : sc.objects)
            if (obj.type == PrimitiveType::Area) areas.push_back(obj.name);
        ctxLine("Areas (invisible volumes, for In Area)", nameList(areas, id));
    }
    ctxLine("Music tracks", nameList(p.music, id));
    ctxLine("Sound effects", nameList(p.sounds, id));
    ctxLine("Save values",
            nameList(p.saveValues, [](const SaveValue& v) { return v.name; }));
    ctxLine("Save texts",
            nameList(p.saveTexts, [](const SaveTextValue& v) { return v.name; }));
    ctxLine("Menus", nameList(p.menus, [](const GameMenu& m) { return m.name; }));
    ctxLine("Input actions (On Action)",
            nameList(p.input.actions,
                     [](const InputAction& a) { return a.name; }));
    ctxLine("Input presets (Set Input Preset)",
            nameList(p.input.presets,
                     [](const InputPreset& v) { return v.name; }));
    {
        // Flow event names reachable from menu entries (On Menu Event's str).
        std::vector<std::string> events;
        for (const GameMenu& m : p.menus)
            for (const MenuEntry& e : m.entries)
                if (e.action == MenuEntry::FlowEvent && !e.param.empty())
                    events.push_back(e.param);
        ctxLine("Menu flow events", nameList(events, id));
    }
    ctxLine("On-screen texts",
            nameList(p.hudTexts, [](const HudText& t) { return t.name; }));
    ctxLine("Color grading presets",
            nameList(p.gradings, [](const ColorGradingPreset& g) { return g.name; }));
    ctxLine("Ambience presets",
            nameList(p.ambiencePresets,
                     [](const AmbiencePreset& a) { return a.name; }));
    ctxLine("Sequences (cutscenes)",
            nameList(p.sequences, [](const Sequence& s) { return s.name; }));
    ctxLine("Prefabs (Spawn Prefab / Despawn Prefab)",
            nameList(p.prefabs, [](const Prefab& pf) { return pf.name; }));
    ctxLine("Credits rolls",
            nameList(p.credits, [](const CreditsRoll& r) { return r.name; }));

    o << "\nIf the request references something that does not exist in the "
         "project context, prefer the closest existing name; only invent "
         "names for variables (Set Int / Set Bool / Set Position), which are "
         "created on first use.\n";

    if (editing && !editing->empty()) {
        o << "\nCURRENT GRAPH: this object ALREADY HAS the flow graph below "
             "(same JSON schema as your reply). Judge from the request what "
             "is wanted: change or extend this graph (the usual case), or "
             "build something fresh if the request clearly asks to start "
             "over. Either way reply with the COMPLETE graph that should "
             "exist afterwards - every node and link, not a diff; anything "
             "you leave out is DELETED. Keep the ids, positions and "
             "parameter values of nodes you are not changing exactly as they "
             "are; give new nodes fresh unused ids and place them near the "
             "nodes they relate to without overlapping.\n"
          << graphJson(*editing) << "\n";
    }
    return o.str();
}

// ---------------------------------------------------------------------------
// Reply parsing
// ---------------------------------------------------------------------------

// The first balanced top-level {...} in `text` (string-aware), or "".
std::string extractJsonObject(const std::string& text) {
    const size_t start = text.find('{');
    if (start == std::string::npos) return "";
    int depth = 0;
    bool inStr = false, esc = false;
    for (size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (inStr) {
            if (esc)
                esc = false;
            else if (c == '\\')
                esc = true;
            else if (c == '"')
                inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{') ++depth;
        else if (c == '}') {
            if (--depth == 0) return text.substr(start, i - start + 1);
        }
    }
    return "";
}

// A model writing prose inside a JSON string leaves quotes in it, and a model
// writing a Windows path leaves lone backslashes; both make the whole document
// unparseable and neither is worth a round trip to fix. Repairing them is only
// ever applied to text a MODEL wrote - never to a project file, where an
// unparseable byte is a real problem that must be reported, not guessed at.
std::string repairJson(const std::string& doc) {
    std::string out;
    out.reserve(doc.size() + 16);
    bool inStr = false, changed = false;
    for (size_t i = 0; i < doc.size(); ++i) {
        const char c = doc[i];
        if (!inStr) {
            out += c;
            if (c == '"') inStr = true;
            continue;
        }
        if (c == '\\') {
            const char n = i + 1 < doc.size() ? doc[i + 1] : '\0';
            bool valid = n == '"' || n == '\\' || n == '/' || n == 'b' ||
                         n == 'f' || n == 'n' || n == 'r' || n == 't';
            if (n == 'u') {
                valid = i + 5 < doc.size();
                for (size_t k = i + 2; valid && k <= i + 5; ++k)
                    valid = std::isxdigit((unsigned char)doc[k]) != 0;
            }
            if (valid) {
                out += c;
                out += n;
                ++i;
            } else {
                out += "\\\\";  // a lone backslash the model meant literally
                changed = true;
            }
            continue;
        }
        if (c == '"') {
            // The disambiguation this whole function rests on: a real string
            // terminator is ALWAYS followed by , } ] or : - a quote the model
            // left mid-sentence practically never is.
            size_t j = i + 1;
            while (j < doc.size() && (doc[j] == ' ' || doc[j] == '\t' ||
                                      doc[j] == '\r' || doc[j] == '\n'))
                ++j;
            const char n = j < doc.size() ? doc[j] : '\0';
            if (n == ',' || n == '}' || n == ']' || n == ':' || n == '\0') {
                out += c;
                inStr = false;
            } else {
                out += "\\\"";
                changed = true;
            }
            continue;
        }
        out += c;
    }
    return changed ? out : doc;
}

std::string parseGraph(const std::string& reply, FlowGraph& out,
                       std::string* warnings) {
    const std::string doc = extractJsonObject(reply);
    json::Value root;
    if (!doc.empty() && json::parse(doc, root))
        return parseGraphJson(root, out, warnings);
    // Second try on a repaired reply. The repair runs on the WHOLE text rather
    // than on `doc`, because a stray quote unbalances extractJsonObject's own
    // string tracking - so re-extracting from the repaired text is part of it.
    if (const std::string fixed = repairJson(reply); fixed != reply)
        if (const std::string doc2 = extractJsonObject(fixed); !doc2.empty())
            if (json::parse(doc2, root))
                return parseGraphJson(root, out, warnings);
    if (doc.empty())
        return "The reply contains no JSON object. Reply head: " +
               reply.substr(0, 200);
    return "The reply's JSON is malformed. Head: " + doc.substr(0, 200);
}

std::string parseGraphJson(const json::Value& root, FlowGraph& out,
                           std::string* warnings) {
    const json::Value* nodes = root.find("nodes");
    if (!nodes || nodes->type != json::Value::Type::Array || nodes->arr.empty())
        return "The reply has no \"nodes\" array.";

    FlowGraph fg;
    std::string unknownTypes;
    int maxId = 0;
    for (const json::Value& jn : nodes->arr) {
        FlowNode n;
        if (const auto* v = jn.find("id")) n.id = (int)v->numberOr(0);
        if (const auto* v = jn.find("type")) n.type = v->stringOr("");
        if (const auto* v = jn.find("pos");
            v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
            n.pos[0] = (float)v->arr[0].numberOr(0);
            n.pos[1] = (float)v->arr[1].numberOr(0);
        }
        if (const auto* v = jn.find("str")) n.str = v->stringOr("");
        if (const auto* v = jn.find("str2")) n.str2 = v->stringOr("");
        if (const auto* v = jn.find("num"); v && v->type == json::Value::Type::Array)
            for (size_t i = 0; i < 4 && i < v->arr.size(); ++i)
                n.num[i] = (float)v->arr[i].numberOr(0);
        if (!flowNodeType(n.type)) {
            if (!unknownTypes.empty()) unknownTypes += ", ";
            unknownTypes += "\"" + n.type + "\"";
            continue;
        }
        if (n.id <= 0) n.id = maxId + 1;
        for (const FlowNode& seen : fg.nodes)
            if (seen.id == n.id)
                return "Duplicate node id " + std::to_string(n.id) + ".";
        maxId = std::max(maxId, n.id);
        fg.nodes.push_back(n);
    }
    if (!unknownTypes.empty())
        return "Unknown node type(s): " + unknownTypes +
               ". Only types from the catalog are valid.";
    if (fg.nodes.empty()) return "No valid nodes in the reply.";

    auto typeOf = [&fg](int nodeId) -> const FlowNodeType* {
        for (const FlowNode& n : fg.nodes)
            if (n.id == nodeId) return flowNodeType(n.type);
        return nullptr;
    };

    int dropped = 0;
    if (const json::Value* links = root.find("links");
        links && links->type == json::Value::Type::Array) {
        for (const json::Value& jl : links->arr) {
            FlowLink l;
            if (const auto* v = jl.find("from")) l.fromNode = (int)v->numberOr(0);
            if (const auto* v = jl.find("to")) l.toNode = (int)v->numberOr(0);
            // Prompt schema: "kind" string. Project-file schema: bool flags.
            if (const auto* v = jl.find("kind")) {
                const std::string k = v->stringOr("exec");
                l.kind = k == "object" ? FlowLinkObject
                         : k == "pos"  ? FlowLinkPos
                         : k == "bool" ? FlowLinkBool
                         : k == "text" ? FlowLinkText
                         : k == "number" ? FlowLinkNum
                                       : FlowLinkExec;
            } else {
                auto flag = [&jl](const char* key) {
                    const auto* v = jl.find(key);
                    return v && v->type == json::Value::Type::Bool && v->boolean;
                };
                l.kind = flag("data")  ? FlowLinkObject
                         : flag("pos") ? FlowLinkPos
                         : flag("bool") ? FlowLinkBool
                         : flag("text") ? FlowLinkText
                         : flag("number") ? FlowLinkNum
                                        : FlowLinkExec;
            }
            if (const auto* v = jl.find("pin")) l.toPin = (int)v->numberOr(0);
            if (const auto* v = jl.find("fpin")) l.fromPin = (int)v->numberOr(0);
            // Same validity rules the graph editor prunes by.
            const FlowNodeType* from = typeOf(l.fromNode);
            const FlowNodeType* to = typeOf(l.toNode);
            bool ok = from && to;
            if (ok) {
                switch (l.kind) {
                    case FlowLinkExec:
                        // A pin either end does not have would fire nothing at
                        // all, which reads as "the node was ignored".
                        ok = !to->trigger && !to->pure && l.toPin >= 0 &&
                             l.toPin < (to->execInCount < 1 ? 1 : to->execInCount) &&
                             l.fromPin >= 0 &&
                             l.fromPin < flowExecOutCount(*from);
                        break;
                    case FlowLinkObject: ok = from->idOut && to->idIn; break;
                    case FlowLinkPos: ok = from->posOut && to->posIn; break;
                    case FlowLinkBool: ok = from->boolOut && to->boolIn; break;
                    case FlowLinkText: ok = from->textOut && to->textIn; break;
                    case FlowLinkNum: ok = from->numOut && to->numIn; break;
                    default: ok = false; break;
                }
            }
            if (!ok) {
                ++dropped;
                continue;
            }
            l.id = ++maxId;
            fg.links.push_back(l);
        }
    }
    if (dropped && warnings)
        *warnings += "Dropped " + std::to_string(dropped) +
                     " invalid link(s) (bad endpoints or pin kinds). ";

    // Auto-layout when the model skipped positions: more than one node parked
    // at the origin means the layout is unusable, not authored.
    int atOrigin = 0;
    for (const FlowNode& n : fg.nodes)
        if (n.pos[0] == 0.0f && n.pos[1] == 0.0f) ++atOrigin;
    if (atOrigin > 1) {
        // Column = longest link path from any source (over every link kind,
        // data links flow left-to-right too); row = order within the column.
        std::map<int, int> depth;
        for (const FlowNode& n : fg.nodes) depth[n.id] = 0;
        for (size_t pass = 0; pass < fg.nodes.size(); ++pass) {
            bool changed = false;
            for (const FlowLink& l : fg.links) {
                if (depth[l.toNode] < depth[l.fromNode] + 1) {
                    depth[l.toNode] = depth[l.fromNode] + 1;
                    changed = true;
                }
            }
            if (!changed) break;  // cycles: passes are capped by node count
        }
        std::map<int, int> rowInCol;
        for (FlowNode& n : fg.nodes) {
            const int col = depth[n.id];
            n.pos[0] = 40.0f + 280.0f * (float)col;
            // Param-heavy nodes run ~180 px tall - 200 keeps rows clear.
            n.pos[1] = 40.0f + 200.0f * (float)rowInCol[col]++;
        }
        if (warnings) *warnings += "Applied automatic layout. ";
    }

    fg.nextId = maxId + 1;
    out = std::move(fg);
    return "";
}

void appendGraph(FlowGraph& dst, FlowGraph add) {
    // Shift ids past everything dst already uses (node AND link ids share the
    // nextId counter), and drop the new nodes below the existing layout.
    const int idShift = dst.nextId;
    float lowest = 0.0f;
    for (const FlowNode& n : dst.nodes) lowest = std::max(lowest, n.pos[1]);
    const float yShift = dst.nodes.empty() ? 0.0f : lowest + 200.0f;
    int maxId = dst.nextId - 1;
    for (FlowNode n : add.nodes) {
        n.id += idShift;
        n.pos[1] += yShift;
        maxId = std::max(maxId, n.id);
        dst.nodes.push_back(n);
    }
    for (FlowLink l : add.links) {
        l.id += idShift;
        l.fromNode += idShift;
        l.toNode += idShift;
        maxId = std::max(maxId, l.id);
        dst.links.push_back(l);
    }
    dst.nextId = maxId + 1;
}

// ---------------------------------------------------------------------------
// Generator (backend execution)
// ---------------------------------------------------------------------------

Generator::~Generator() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

std::string Generator::reply() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reply_;
}

std::string Generator::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

Usage Generator::usage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return usage_;
}

void Generator::finish(State s, const std::string& reply, const std::string& error,
                       const Usage& usage) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reply_ = reply;
        error_ = error;
        usage_ = usage;
    }
    state_ = s;
}

void Generator::cancel() {
    if (!busy()) return;
    cancelRequested_ = true;
    std::lock_guard<std::mutex> lock(procMutex_);
    // Kills the whole tree - the shell wrapper AND the node/curl children.
    // Killing the wrapper alone would orphan a token-burning backend.
    if (proc_) proc_->kill();
}

// Writes `content` to a fresh file in the system temp dir; returns its path
// ("" on failure). Prompts never travel on the command line: they hold
// newlines and can exceed the 32k limit.
static std::string writeTempFile(const char* tag, const std::string& content) {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec);
    if (ec) return "";
    static std::atomic<int> counter{0};
    const fs::path path = dir / ("tyrax-ai-" + std::to_string(platform::processId()) +
                                 "-" + std::to_string(++counter) + "-" + tag);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return "";
    f << content;
    return path.string();
}

void Generator::start(const Config& cfg, const std::string& systemPrompt,
                      const std::string& userPrompt) {
    if (busy()) return;
    if (thread_.joinable()) thread_.join();
    cancelRequested_ = false;
    state_ = State::Running;

    thread_ = std::thread([this, cfg, systemPrompt, userPrompt] {
        std::vector<std::string> tempFiles;
        auto cleanup = [&tempFiles] {
            std::error_code ec;
            for (const std::string& f : tempFiles) fs::remove(f, ec);
        };

        std::string cmdline;
        std::string lastMsgFile;  // codex: where its final message is written
        const bool openai = cfg.backend == "openai";
        if (openai) {
            const char* key = getenv("OPENAI_API_KEY");
            if (!key || !*key) {
                finish(State::Failed, "",
                       "OPENAI_API_KEY is not set. Set the environment "
                       "variable and restart the editor.");
                return;
            }
            const std::string model = cfg.model.empty() ? "gpt-5.1" : cfg.model;
            std::string body = "{ \"model\": \"" + jsonEsc(model) +
                               "\", \"messages\": [ { \"role\": \"system\", "
                               "\"content\": \"" +
                               jsonEsc(systemPrompt) +
                               "\" }, { \"role\": \"user\", \"content\": \"" +
                               jsonEsc(userPrompt) + "\" } ]";
            // reasoning_effort is only sent when Thinking is on - reasoning
            // models default sensibly and non-reasoning models reject it.
            if (cfg.thinking) body += ", \"reasoning_effort\": \"high\"";
            body += " }";
            const std::string bodyFile = writeTempFile("body.json", body);
            // The API key goes into a curl config file, not the command line
            // (process command lines are world-readable on Windows).
            const std::string cfgFile = writeTempFile(
                "curl.cfg",
                "url = \"https://api.openai.com/v1/chat/completions\"\n"
                "silent\nshow-error\n"
                "header = \"Content-Type: application/json\"\n"
                "header = \"Authorization: Bearer " + std::string(key) + "\"\n"
                "data = \"@" + bodyFile + "\"\n");
            if (bodyFile.empty() || cfgFile.empty()) {
                finish(State::Failed, "", "Could not write temp files.");
                cleanup();
                return;
            }
            tempFiles = {bodyFile, cfgFile};
            cmdline = "curl --config " + platform::shellArg(cfgFile);
        } else {
            // CLI backends read the whole prompt (instructions + request)
            // from stdin via a redirect.
            const std::string prompt =
                systemPrompt + "\n\nREQUEST:\n" + userPrompt + "\n";
            const std::string promptFile = writeTempFile("prompt.txt", prompt);
            if (promptFile.empty()) {
                finish(State::Failed, "", "Could not write the prompt file.");
                return;
            }
            tempFiles = {promptFile};
            if (cfg.backend == "codex") {
                // `codex exec -` takes the prompt on stdin (its own documented
                // spelling), and the flags are the same intent as the other
                // CLIs': one non-interactive run that reads nothing and writes
                // nothing.
                //   --ephemeral          no session files for a one-shot
                //   --skip-git-repo-check  we run from a temp dir, not a repo
                //   -s read-only         the sandbox for anything it does try
                //   -o <file>            the FINAL message, alone, in a file -
                //                        stdout carries progress chatter, and
                //                        picking the reply out of that would be
                //                        guesswork
                //   --json               JSONL events, read for the token usage
                lastMsgFile = writeTempFile("codex-last.txt", "");
                if (lastMsgFile.empty()) {
                    finish(State::Failed, "", "Could not write the reply file.");
                    return;
                }
                tempFiles.push_back(lastMsgFile);
                cmdline = "codex exec --ephemeral --skip-git-repo-check "
                          "-s read-only --json -o " +
                          platform::shellArg(lastMsgFile);
                if (!cfg.model.empty())
                    cmdline += " -m " + platform::shellArg(cfg.model);
                // Codex takes its reasoning effort from config, so Thinking is
                // an override of that key rather than a flag of its own.
                if (cfg.thinking) cmdline += " -c model_reasoning_effort=\"high\"";
                cmdline += " - < " + platform::shellArg(promptFile);
            } else if (cfg.backend == "copilot") {
                // --no-custom-instructions is the Copilot twin of running from a
                // neutral directory (below): the instructions in THIS prompt are
                // the whole contract, and a project's own
                // .github/copilot-instructions.md - which the editor may well
                // have installed itself - is a second, contradictory one.
                cmdline = "copilot --no-custom-instructions -p \"Follow the "
                          "instructions piped on stdin. Reply with only the JSON "
                          "they describe.\"";
                if (!cfg.model.empty())
                    cmdline += " --model " + platform::shellArg(cfg.model);
                cmdline += " < " + platform::shellArg(promptFile);
            } else {  // claude
                cmdline.clear();
                // Extended thinking in the claude CLI is budget-driven.
                if (cfg.thinking)
                    cmdline += platform::envPrefix("MAX_THINKING_TOKENS", "16000");
                // `--tools ""` disables the built-in tool set, which is what
                // makes this ONE completion instead of an agent run: we want the
                // model to answer from the prompt, not to go reading files. It
                // replaced `--max-turns 1`, which was approximating the same
                // thing and is no longer in the CLI's --help (still parsed by
                // 2.1.x, but a hidden flag is not something to depend on).
                // The JSON envelope instead of bare text: it carries the
                // model's OWN token counts and the run's cost, which is worth
                // more than any estimate we could make of the same request (the
                // chat window shows them). Parsed below, with the raw output as
                // the fallback - a CLI whose envelope changes shape must not
                // take the feature down with it.
                cmdline += "claude -p --output-format json --tools \"\"";
                if (!cfg.model.empty())
                    cmdline += " --model " + platform::shellArg(cfg.model);
                cmdline += " < " + platform::shellArg(promptFile);
            }
        }

        // --- spawn through the platform shell (on Windows the CLIs are .cmd
        // shims; everywhere the prompt arrives via a stdin redirect), stdout
        // piped, stderr to a temp file so interleaving can't corrupt the reply.
        const std::string errFile = writeTempFile("stderr.txt", "");
        tempFiles.push_back(errFile);

        platform::Process::Options opts;
        opts.capture = true;
        opts.stderrFile = errFile;
        // Run the backend from a NEUTRAL directory. Both CLIs auto-load the
        // instruction files of whatever project they start in (CLAUDE.md /
        // AGENTS.md / .github/copilot-instructions.md, skills), and the editor's
        // CWD is usually a game project - which, with AI support installed,
        // carries guidance written for an assistant driving the editor's CLI.
        // That is exactly the wrong context for these calls: the instructions
        // here are the whole contract, and a second set of them is at best noise
        // and at worst tells the model it can run commands it cannot.
        {
            std::error_code ec;
            const fs::path neutral = fs::temp_directory_path(ec);
            if (!ec) opts.cwd = neutral.string();
        }
        std::shared_ptr<platform::Process> proc =
            platform::Process::start(cmdline, opts);
        if (!proc) {
            finish(State::Failed, "", "Could not start the backend command: " + cmdline);
            cleanup();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(procMutex_);
            proc_ = proc;
        }

        const std::string output = proc->readAll();
        const int code = proc->wait();
        {
            std::lock_guard<std::mutex> lock(procMutex_);
            proc_.reset();
        }

        std::string errText;
        {
            std::ifstream ef(errFile, std::ios::binary);
            std::ostringstream ss;
            ss << ef.rdbuf();
            errText = ss.str();
        }
        // Anything a backend wrote to a FILE has to be read before cleanup, which
        // deletes every temp path this request made (the Codex CLI's reply is one
        // - it goes to -o rather than to stdout).
        std::string lastMsgText;
        if (!lastMsgFile.empty()) {
            std::ifstream f(lastMsgFile, std::ios::binary);
            std::ostringstream ss;
            ss << f.rdbuf();
            lastMsgText = ss.str();
        }
        cleanup();

        if (cancelRequested_) {
            finish(State::Failed, "", "Cancelled.");
            return;
        }
        if (code != 0) {
            std::string msg = "Backend exited with code " + std::to_string(code) + ".";
            if (!errText.empty()) msg += "\n" + errText.substr(0, 1000);
            else if (!output.empty()) msg += "\n" + output.substr(0, 1000);
            finish(State::Failed, "", msg);
            return;
        }

        // Usage as the backend reports it, per backend envelope.
        auto usageOf = [](const json::Value& u, double cost) {
            Usage out;
            auto num = [&u](const char* k) -> long long {
                const json::Value* v = u.find(k);
                return v ? (long long)v->numberOr(0) : 0;
            };
            // Everything the model READ: fresh input plus whatever was served
            // from or written to the prompt cache. Reporting only input_tokens
            // would show 2 for a 20 KB prompt the moment caching kicks in.
            out.inputTokens = num("input_tokens") + num("cache_creation_input_tokens") +
                              num("cache_read_input_tokens") + num("prompt_tokens");
            out.outputTokens = num("output_tokens") + num("completion_tokens");
            out.costUsd = cost;
            out.real = out.inputTokens > 0 || out.outputTokens > 0;
            return out;
        };

        if (cfg.backend == "codex") {
            // The reply is the file; stdout is the event stream and is read only
            // for what it says about tokens. A run that wrote no file failed in
            // a way its own output explains better than we could.
            const std::string& text = lastMsgText;
            Usage u;
            // JSONL, one event per line, shape not contractual - so this looks
            // for token counts wherever they sit and keeps the LAST it sees
            // (the totals event comes at the end), rather than insisting on a
            // schema that a future version may rename.
            {
                std::istringstream lines(output);
                std::string line;
                while (std::getline(lines, line)) {
                    if (line.find("token") == std::string::npos) continue;
                    json::Value ev;
                    if (!json::parse(line, ev)) continue;
                    // Walk one level of nesting: the counts live under an
                    // "info"/"usage"/"token_usage" object depending on version.
                    const json::Value* holders[] = {&ev, ev.find("info"),
                                                    ev.find("usage"),
                                                    ev.find("token_usage"),
                                                    ev.find("msg")};
                    for (const json::Value* h : holders) {
                        if (!h) continue;
                        const json::Value* nested = h->find("total_token_usage");
                        const json::Value* src = nested ? nested : h;
                        const json::Value* in = src->find("input_tokens");
                        const json::Value* out = src->find("output_tokens");
                        if (!in && !out) continue;
                        // NOT added to input_tokens, unlike Anthropic's
                        // cache fields: OpenAI reports cached_input_tokens as a
                        // SUBSET of input_tokens (measured - a 40-character
                        // prompt reported 14039 input with 9984 of them cached,
                        // which only makes sense as a subset), while Anthropic
                        // reports its cache_creation/cache_read separately from
                        // input_tokens and they have to be summed. Adding here
                        // would inflate every Codex turn by its cache hit.
                        u.inputTokens = (long long)(in ? in->numberOr(0) : 0);
                        u.outputTokens = (long long)(out ? out->numberOr(0) : 0);
                        u.real = u.inputTokens > 0 || u.outputTokens > 0;
                    }
                }
            }
            if (text.empty()) {
                finish(State::Failed, "",
                       "The Codex CLI produced no final message.\n" +
                           (errText.empty() ? output.substr(0, 1000)
                                            : errText.substr(0, 1000)));
                return;
            }
            finish(State::Success, text, "", u);
            return;
        }

        if (!openai && cfg.backend != "copilot") {
            // claude --output-format json: { result, usage, total_cost_usd, ... }
            json::Value root;
            const json::Value* result = nullptr;
            if (json::parse(output, root)) result = root.find("result");
            if (!result) {
                // Not the envelope we expected (an older CLI, a changed shape):
                // the output is still the reply, we just learn nothing about
                // what it cost.
                finish(State::Success, output, "");
                return;
            }
            const json::Value* err = root.find("is_error");
            const std::string text = result->stringOr("");
            if (err && err->boolOr(false)) {
                finish(State::Failed, "",
                       "Claude CLI reported an error: " +
                           (text.empty() ? output.substr(0, 500) : text));
                return;
            }
            const json::Value* u = root.find("usage");
            const json::Value* cost = root.find("total_cost_usd");
            finish(State::Success, text, "",
                   u ? usageOf(*u, cost ? cost->numberOr(0.0) : 0.0) : Usage{});
            return;
        }

        if (openai) {
            // Chat Completions envelope -> the assistant message text.
            json::Value root;
            if (!json::parse(output, root)) {
                finish(State::Failed, "",
                       "OpenAI reply is not JSON: " + output.substr(0, 500));
                return;
            }
            if (const json::Value* err = root.find("error")) {
                const json::Value* m = err->find("message");
                finish(State::Failed, "",
                       "OpenAI error: " + (m ? m->stringOr("") : output.substr(0, 500)));
                return;
            }
            const json::Value* choices = root.find("choices");
            if (!choices || choices->type != json::Value::Type::Array ||
                choices->arr.empty()) {
                finish(State::Failed, "",
                       "OpenAI reply has no choices: " + output.substr(0, 500));
                return;
            }
            const json::Value* msg = choices->arr[0].find("message");
            const json::Value* content = msg ? msg->find("content") : nullptr;
            const json::Value* u = root.find("usage");
            finish(State::Success, content ? content->stringOr("") : "", "",
                   u ? usageOf(*u, 0.0) : Usage{});
            return;
        }
        finish(State::Success, output, "");  // copilot: plain text, no usage
    });
}

}  // namespace aigen
