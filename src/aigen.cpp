#include "aigen.hpp"

#include "platform.hpp"

#include <algorithm>
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
    if (backend == "openai") return "OpenAI API";
    return "Claude CLI";
}

std::vector<const char*> backendIds() { return {"claude", "copilot", "openai"}; }

std::vector<const char*> modelPresets(const std::string& backend) {
    // Presets are a convenience, not a whitelist - the preferences dialog also
    // takes free text, so new models work the day they ship.
    if (backend == "copilot")
        return {"", "claude-sonnet-4.5", "gpt-5.1"};
    if (backend == "openai")
        return {"gpt-5.1", "gpt-5", "gpt-5-mini", "gpt-4.1"};
    return {"", "opus", "sonnet", "haiku"};
}

// ---------------------------------------------------------------------------
// System prompt
// ---------------------------------------------------------------------------

static std::string jsonEsc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

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
        case FlowParamKind::HudTextName:
            return "on-screen text name (see context)";
        case FlowParamKind::InputActionName:
            return "input action name (see context; Tools > Input Map)";
        case FlowParamKind::KeyName:
            return "keyboard key label: A-Z, 0-9, Enter, Esc, Backspace, Tab, "
                   "Space, F1-F12, Up, Down, Left, Right, Left Shift, "
                   "Left Ctrl, Left Alt";
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
            for (int e = 0; e < t.execInCount && e < kFlowMaxExecIn; ++e)
                s += std::string(e ? ", " : " ") + std::to_string(e) + "=" +
                     flowExecInLabel(t, e);
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
        } else if (eo == 1 && t.execThrough) {
            out.push_back("exec (fires later - see doc)");
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
    // params
    const std::string sd = strKindDesc(t.strKind);
    if (!sd.empty()) o << " str = " << sd << ".";
    if (t.numKind == FlowParamKind::Color) {
        o << " num[0..2] = RGB color, each 0..1.";
    } else if (t.numCount > 0) {
        o << " num params:";
        for (int i = 0; i < t.numCount && i < 4; ++i)
            o << " num[" << i << "]="
              << (t.numLabels[i] && *t.numLabels[i] ? t.numLabels[i] : "value");
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

// A graph in the REPLY schema ("kind" strings), for the edit-mode prompt -
// the model sees its input in exactly the shape it must answer in. (The
// project-file serializer uses bool flags instead; parseGraph reads both.)
static std::string graphPromptJson(const FlowGraph& fg) {
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
          << graphPromptJson(*editing) << "\n";
    }
    return o.str();
}

// ---------------------------------------------------------------------------
// Reply parsing
// ---------------------------------------------------------------------------

// The first balanced top-level {...} in `text` (string-aware), or "".
static std::string extractJsonObject(const std::string& text) {
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

std::string parseGraph(const std::string& reply, FlowGraph& out,
                       std::string* warnings) {
    const std::string doc = extractJsonObject(reply);
    if (doc.empty())
        return "The reply contains no JSON object. Reply head: " +
               reply.substr(0, 200);
    json::Value root;
    if (!json::parse(doc, root))
        return "The reply's JSON is malformed. Head: " + doc.substr(0, 200);

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

void Generator::finish(State s, const std::string& reply, const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reply_ = reply;
        error_ = error;
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
            if (cfg.backend == "copilot") {
                cmdline = "copilot -p \"Follow the instructions piped on "
                          "stdin. Reply with only the JSON they describe.\"";
                if (!cfg.model.empty())
                    cmdline += " --model " + platform::shellArg(cfg.model);
                cmdline += " < " + platform::shellArg(promptFile);
            } else {  // claude
                cmdline.clear();
                // Extended thinking in the claude CLI is budget-driven.
                if (cfg.thinking)
                    cmdline += platform::envPrefix("MAX_THINKING_TOKENS", "16000");
                cmdline += "claude -p --output-format text --max-turns 1";
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
            finish(State::Success, content ? content->stringOr("") : "", "");
            return;
        }
        finish(State::Success, output, "");
    });
}

}  // namespace aigen
