#include "livelogic.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>

#include "project.hpp"

namespace livelogic {
namespace {

constexpr uint32_t kMagic = 0x504C5854;  // "TXLP"
constexpr uint32_t kVersion = 1;
constexpr int kHeader = 32;
constexpr uint32_t kFooterXor = 0x5A5A5A5AU;

uint64_t fnv(uint64_t h, const void* data, size_t len) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}
uint64_t fnvStr(uint64_t h, const std::string& s) {
    return fnv(h, s.data(), s.size());
}

void put32(std::vector<unsigned char>& v, uint32_t x) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(&x);
    v.insert(v.end(), b, b + 4);
}
void put16(std::vector<unsigned char>& v, uint16_t x) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(&x);
    v.insert(v.end(), b, b + 2);
}
void putF(std::vector<unsigned char>& v, float f) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(&f);
    v.insert(v.end(), b, b + 4);
}

// Trigger node types the interpreter can enter a block on, and the actions it
// can run. These two tables ARE the feature's capability contract: the editor
// checks a graph against them and templates.cpp generates one interpreter case
// per entry, so a type reaches the running game only by being listed here.
struct TriggerMap {
    const char* type;
    BlockKind kind;
};
const std::vector<TriggerMap>& triggerMap() {
    static const std::vector<TriggerMap> v = {
        {"OnStart", BK_OnStart},
        {"EverySeconds", BK_EverySeconds},
        {"OnButton", BK_OnButton},
        {"NearObject", BK_NearObject},
        {"OnCondition", BK_OnCondition},
    };
    return v;
}
struct ActionMap {
    const char* type;
    OpCode op;
};
const std::vector<ActionMap>& actionMap() {
    static const std::vector<ActionMap> v = {
        {"SetObjectVisible", OP_SetObjectVisible},
        {"MoveObjectBy", OP_MoveObjectBy},
        {"SetObjectColor", OP_SetObjectColor},
        {"SetPosition", OP_SetPosition},
        {"MoveObjectTo", OP_MoveObjectTo},
        {"TeleportPlayer", OP_TeleportPlayer},
        {"SetSky", OP_SetSky},
        {"Delay", OP_Delay},
        {"Log", OP_Log},
        {"SetVarInt", OP_SetVarInt},
        {"SetVarBool", OP_SetVarBool},
        {"SetVarPos", OP_SetVarPos},
        {"SetValue", OP_SetValue},
        {"AddValue", OP_AddValue},
        {"SetTextVisible", OP_SetTextVisible},
        {"SetHudVisible", OP_SetHudVisible},
        {"SwitchScene", OP_SwitchScene},
        {"SetFog", OP_SetFog},
        {"SetBloom", OP_SetBloom},
        {"SetGrain", OP_SetGrain},
        {"SetParticles", OP_SetParticles},
    };
    return v;
}
// Data nodes the compiler can resolve away entirely (they never run).
const std::vector<std::string>& dataTypes() {
    static const std::vector<std::string> v = {
        "Self", "GetPosition", "GetVarPos", "IsVisible", "GetVarBool",
        "VarAtLeast", "ValueAtLeast", "And", "Or", "Not", "Nand", "Xor", "Xnor",
    };
    return v;
}

const TriggerMap* triggerFor(const std::string& type) {
    for (const TriggerMap& t : triggerMap())
        if (type == t.type) return &t;
    return nullptr;
}
const ActionMap* actionFor(const std::string& type) {
    for (const ActionMap& a : actionMap())
        if (type == a.type) return &a;
    return nullptr;
}
bool isDataType(const std::string& type) {
    for (const std::string& d : dataTypes())
        if (type == d) return true;
    return false;
}

}  // namespace

const std::vector<std::string>& padButtons() {
    // The DualShock buttons the pad exposes as getClicked() fields, in the
    // order the generated interpreter switches on.
    static const std::vector<std::string> v = {
        "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2",
        "L3", "R3", "Start", "Select", "DpadUp", "DpadDown", "DpadLeft",
        "DpadRight"};
    return v;
}

int padButtonIndex(const std::string& name) {
    const auto& b = padButtons();
    for (size_t i = 0; i < b.size(); ++i)
        if (b[i] == name) return (int)i;
    return -1;
}

const std::vector<std::string>& opNames() {
    static std::vector<std::string> v;
    if (v.empty())
        for (const ActionMap& a : actionMap()) v.push_back(std::string("OP_") + a.type);
    return v;
}

const std::vector<std::string>& blockKindNames() {
    static const std::vector<std::string> v = {"BK_OnStart",   "BK_EverySeconds",
                                               "BK_OnButton",  "BK_NearObject",
                                               "BK_OnCondition", "BK_Delay"};
    return v;
}

const std::vector<std::string>& condOpNames() {
    static const std::vector<std::string> v = {
        "CO_End",  "CO_IsVisible", "CO_VarBool", "CO_VarAtLeast",
        "CO_ValueAtLeast", "CO_And", "CO_Or", "CO_Not", "CO_Nand", "CO_Xor",
        "CO_Xnor"};
    return v;
}

const std::vector<std::string>& supportedNodeTypes() {
    static std::vector<std::string> v;
    if (v.empty()) {
        for (const TriggerMap& t : triggerMap()) v.push_back(t.type);
        for (const ActionMap& a : actionMap()) v.push_back(a.type);
        for (const std::string& d : dataTypes()) v.push_back(d);
    }
    return v;
}

uint64_t graphHash(const FlowGraph& fg) {
    // Node POSITIONS are deliberately excluded: dragging a node around the
    // canvas must not read as a logic change (it would re-patch on every
    // mouse move and, worse, mark an untouched graph as needing a rebuild).
    uint64_t h = 1469598103934665603ULL;
    for (const FlowNode& n : fg.nodes) {
        h = fnv(h, &n.id, sizeof(n.id));
        h = fnvStr(h, n.type);
        h = fnvStr(h, n.str);
        h = fnvStr(h, n.str2);
        h = fnv(h, n.num, sizeof(n.num));
    }
    for (const FlowLink& l : fg.links) {
        h = fnv(h, &l.fromNode, sizeof(l.fromNode));
        h = fnv(h, &l.toNode, sizeof(l.toNode));
        h = fnv(h, &l.kind, sizeof(l.kind));
        h = fnv(h, &l.toPin, sizeof(l.toPin));
    }
    return h;
}

// ---------------------------------------------------------------------------
// Compiler
// ---------------------------------------------------------------------------
namespace {

// Shared context for one graph: the lookups codegen does with literals, done
// here into IR fields instead.
struct Ctx {
    const Project* p = nullptr;
    const SceneData* sc = nullptr;
    const FlowGraph* fg = nullptr;
    size_t owner = 0;
    std::vector<std::string> intVars, boolVars, posVars;

    const FlowNode* node(int id) const {
        for (const FlowNode& n : fg->nodes)
            if (n.id == id) return &n;
        return nullptr;
    }
    int objectIndex(const std::string& name) const {
        for (size_t i = 0; i < sc->objects.size(); ++i)
            if (sc->objects[i].name == name) return (int)i;
        return -1;
    }
    int varIndex(const std::vector<std::string>& v, const std::string& n) const {
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] == n) return (int)i;
        return -1;
    }
    int saveValueIndex(const std::string& n) const {
        for (size_t i = 0; i < p->saveValues.size(); ++i)
            if (p->saveValues[i].name == n) return (int)i;
        return -1;
    }
    int hudTextIndex(const std::string& n) const {
        for (size_t i = 0; i < p->hudTexts.size(); ++i)
            if (p->hudTexts[i].name == n) return (int)i;
        return -1;
    }
    int sceneIndex(const std::string& n) const {
        for (size_t i = 0; i < p->scenes.size(); ++i)
            if (p->scenes[i].name == n) return (int)i;
        return -1;
    }

    // Mirror of templates.cpp resolveTarget: object link chain > explicit name
    // > self. Every node in a patchable graph resolves statically (the runtime
    // object sources - Spawn Object, Raycast, custom nodes - are not supported,
    // which is exactly why this can be a plain index).
    int resolveTarget(const FlowNode& n) const {
        const FlowNode* cur = &n;
        std::vector<int> visited;
        for (;;) {
            bool seen = false;
            for (int id : visited) seen |= (id == cur->id);
            if (seen) break;
            visited.push_back(cur->id);
            const FlowNodeType* t = flowNodeType(cur->type);
            if (!t || !t->idIn) break;
            const FlowLink* link = nullptr;
            for (const FlowLink& l : fg->links)
                if (l.kind == FlowLinkObject && l.toNode == cur->id) {
                    link = &l;
                    break;
                }
            if (!link) break;
            const FlowNode* src = node(link->fromNode);
            if (!src) break;
            cur = src;
        }
        const FlowNodeType* ct = flowNodeType(cur->type);
        if (ct && ct->strKind == FlowParamKind::ObjectName && !cur->str.empty())
            return objectIndex(cur->str);
        return (int)owner;  // self
    }
};

// Mirror of templates.cpp posExprImpl, reduced to the operands the IR has.
void resolvePos(const Ctx& c, const FlowNode& n, Instr& out,
                std::vector<int>& visited) {
    bool seen = false;
    for (int id : visited) seen |= (id == n.id);
    if (!seen) {
        visited.push_back(n.id);
        const FlowNodeType* t = flowNodeType(n.type);
        if (t && t->posIn) {
            for (const FlowLink& l : c.fg->links) {
                if (l.kind != FlowLinkPos || l.toNode != n.id) continue;
                const FlowNode* src = c.node(l.fromNode);
                if (!src) continue;
                const FlowNodeType* st = flowNodeType(src->type);
                if (st && st->posOut) {
                    resolvePos(c, *src, out, visited);
                    return;
                }
            }
        }
    }
    if (n.type == "GetVarPos") {
        out.posKind = PK_VarPos;
        out.posIndex = (int16_t)c.varIndex(c.posVars, n.str);
        if (out.posIndex < 0) {  // unknown variable: codegen emits zeros
            out.posKind = PK_Literal;
            out.posLit[0] = out.posLit[1] = out.posLit[2] = 0.0f;
        }
        return;
    }
    if (n.type == "SetPosition" || n.type == "SetVarPos" ||
        n.type == "MoveObjectTo") {
        out.posKind = PK_Literal;
        for (int a = 0; a < 3; ++a) out.posLit[a] = n.num[a];
        return;
    }
    // Self / Get Position / any object-carrying node: the target's live
    // position.
    out.posKind = PK_ObjectPos;
    out.posIndex = (int16_t)c.resolveTarget(n);
    if (out.posIndex < 0) {
        out.posKind = PK_Literal;
        out.posLit[0] = out.posLit[1] = out.posLit[2] = 0.0f;
    }
}

// Bool plane -> RPN. Mirrors sourceCondition/boolExprImpl for the supported
// sources and gates; an unresolvable source pushes a constant false (a NOT
// with nothing wired, an unknown variable name - same as codegen).
void emitCond(const Ctx& c, const FlowNode& n, std::vector<unsigned char>& out,
              std::vector<int> visited, int depth) {
    auto pushFalse = [&]() {
        // "false" is CO_VarAtLeast against an impossible index; cleaner to
        // encode it as VarBool with index -1, which the interpreter reads as 0.
        out.push_back(CO_VarBool);
        put16(out, (uint16_t)0xFFFF);
    };
    if (depth > 16) {
        pushFalse();
        return;
    }
    for (int id : visited)
        if (id == n.id) {
            pushFalse();
            return;
        }
    visited.push_back(n.id);

    const FlowNodeType* t = flowNodeType(n.type);
    if (!t) {
        pushFalse();
        return;
    }
    if (!(t->pure && t->boolIn)) {  // a bool SOURCE
        if (n.type == "IsVisible") {
            out.push_back(CO_IsVisible);
            put16(out, (uint16_t)(int16_t)c.resolveTarget(n));
            return;
        }
        if (n.type == "GetVarBool") {
            out.push_back(CO_VarBool);
            put16(out, (uint16_t)(int16_t)c.varIndex(c.boolVars, n.str));
            return;
        }
        if (n.type == "VarAtLeast") {
            out.push_back(CO_VarAtLeast);
            put16(out, (uint16_t)(int16_t)c.varIndex(c.intVars, n.str));
            putF(out, n.num[0]);
            return;
        }
        if (n.type == "ValueAtLeast") {
            out.push_back(CO_ValueAtLeast);
            put16(out, (uint16_t)(int16_t)c.saveValueIndex(n.str));
            putF(out, n.num[0]);
            return;
        }
        pushFalse();
        return;
    }

    // A gate: emit every wired input, then fold them.
    int count = 0;
    for (const FlowLink& l : c.fg->links) {
        if (l.kind != FlowLinkBool || l.toNode != n.id) continue;
        const FlowNode* src = c.node(l.fromNode);
        if (!src) continue;
        const FlowNodeType* st = flowNodeType(src->type);
        if (!st || !st->boolOut) continue;
        emitCond(c, *src, out, visited, depth + 1);
        ++count;
    }
    if (count == 0) {
        pushFalse();
        return;
    }
    CondOp fold = CO_And;
    if (n.type == "Or") fold = CO_Or;
    else if (n.type == "Not") fold = CO_Not;
    else if (n.type == "Nand") fold = CO_Nand;
    else if (n.type == "Xor") fold = CO_Xor;
    else if (n.type == "Xnor") fold = CO_Xnor;
    out.push_back(fold);
    out.push_back((unsigned char)(count > 255 ? 255 : count));
}

// OR over the bool inputs of a node (On Condition) - boolInputsOr's twin.
bool emitCondInputsOr(const Ctx& c, const FlowNode& n,
                      std::vector<unsigned char>& out) {
    int count = 0;
    for (const FlowLink& l : c.fg->links) {
        if (l.kind != FlowLinkBool || l.toNode != n.id) continue;
        const FlowNode* src = c.node(l.fromNode);
        if (!src) continue;
        const FlowNodeType* st = flowNodeType(src->type);
        if (!st || !st->boolOut) continue;
        emitCond(c, *src, out, std::vector<int>{}, 0);
        ++count;
    }
    if (count == 0) return false;
    if (count > 1) {
        out.push_back(CO_Or);
        out.push_back((unsigned char)count);
    }
    out.push_back(CO_End);
    return true;
}

}  // namespace

Capability capability(const Project& p, const SceneData& sc,
                      const FlowGraph& fg) {
    Capability cap;
    auto reject = [&](const std::string& why) {
        for (const std::string& r : cap.reasons)
            if (r == why) return;
        cap.reasons.push_back(why);
        cap.patchable = false;
    };
    for (const FlowNode& n : fg.nodes) {
        const FlowNodeType* t = flowNodeType(n.type);
        if (!t) {
            reject("an unknown node type (" + n.type + ")");
            continue;
        }
        const bool supported = triggerFor(n.type) || actionFor(n.type) ||
                               isDataType(n.type);
        if (!supported) {
            reject(std::string(t->title));
            continue;
        }
        // Get Position with its exec wired is a sampling action with a runtime
        // latch - the IR has no latches.
        if (n.type == "GetPosition")
            for (const FlowLink& l : fg.links)
                if (l.kind == FlowLinkExec &&
                    (l.toNode == n.id || l.fromNode == n.id))
                    reject("an exec-wired Get Position (sampling latch)");
        // A text link means the text plane, which the IR does not carry.
        for (const FlowLink& l : fg.links)
            if (l.kind == FlowLinkText && (l.toNode == n.id || l.fromNode == n.id))
                reject("a text link");
    }
    if ((int)fg.nodes.size() > kMaxInstrs) reject("too many nodes");
    (void)p;
    (void)sc;
    return cap;
}

bool compile(const Project& p, int sceneIndex, size_t ownerIndex, Program& out) {
    if (sceneIndex < 0 || sceneIndex >= (int)p.scenes.size()) return false;
    const SceneData& sc = p.scenes[sceneIndex];
    if (ownerIndex >= sc.objects.size()) return false;
    const FlowGraph& fg = sc.objects[ownerIndex].flowGraph;
    if (fg.empty()) return false;
    if (!capability(p, sc, fg).patchable) return false;

    Ctx c;
    c.p = &p;
    c.sc = &sc;
    c.fg = &fg;
    c.owner = ownerIndex;
    // The flow-variable name spaces are project-global and index-ordered
    // exactly like codegen's flowInt/flowBool/flowPos arrays - the interpreter
    // reads those very arrays, so the order must be the same.
    {
        auto collect = [](std::vector<std::string>& v, const std::string& name) {
            if (name.empty()) return;
            for (const std::string& e : v)
                if (e == name) return;
            v.push_back(name);
        };
        for (const SceneData& s : p.scenes)
            for (const SceneObject& o : s.objects)
                for (const FlowNode& n : o.flowGraph.nodes) {
                    if (n.type == "SetVarInt" || n.type == "VarAtLeast" ||
                        n.type == "GetVarIntText")
                        collect(c.intVars, n.str);
                    else if (n.type == "SetVarBool" || n.type == "GetVarBool")
                        collect(c.boolVars, n.str);
                    else if (n.type == "SetVarPos" || n.type == "GetVarPos")
                        collect(c.posVars, n.str);
                }
    }

    out = Program();
    out.scene = sceneIndex;
    out.ownerIdHash = project::liveLinkIdHash(sc.objects[ownerIndex]);
    uint16_t nextState = 0;
    auto stateSlot = [&]() -> uint16_t {
        return nextState < kMaxStateSlots ? nextState++ : (uint16_t)0xFFFF;
    };

    // One instruction per action reached from an exec output, in the order
    // codegen's emitExec walks them (node+pin visited set, no recursion into
    // Delay - its chain is its own block, armed at runtime).
    std::vector<std::pair<int, int>> pendingDelays;  // (delay node id, block idx)
    std::function<void(int, std::vector<int>&)> emitChain =
        [&](int fromId, std::vector<int>& visited) {
            for (const FlowLink& l : fg.links) {
                if (l.kind != FlowLinkExec || l.fromNode != fromId) continue;
                const FlowNode* m = c.node(l.toNode);
                if (!m) continue;
                const FlowNodeType* t = flowNodeType(m->type);
                if (!t || t->trigger || t->pure) continue;
                const ActionMap* am = actionFor(m->type);
                if (!am) continue;
                const int key = m->id * kFlowMaxExecIn + l.toPin;
                bool seen = false;
                for (int v : visited) seen |= (v == key);
                if (seen) continue;
                visited.push_back(key);
                if ((int)out.instrs.size() >= kMaxInstrs) return;

                Instr in;
                in.op = (uint8_t)am->op;
                in.pin = (uint8_t)l.toPin;
                in.nodeId = (uint16_t)m->id;
                for (int a = 0; a < 4; ++a) in.num[a] = m->num[a];
                const FlowNodeType* mt = t;
                if (mt->strKind == FlowParamKind::ObjectName)
                    in.obj = (int16_t)c.resolveTarget(*m);
                else
                    in.obj = (int16_t)c.resolveTarget(*m);  // self / linked
                if (mt->posIn || m->type == "TeleportPlayer") {
                    std::vector<int> pv;
                    resolvePos(c, *m, in, pv);
                }
                switch (am->op) {
                    case OP_SetVarInt:
                        in.aux = (int16_t)c.varIndex(c.intVars, m->str);
                        break;
                    case OP_SetVarBool:
                        in.aux = (int16_t)c.varIndex(c.boolVars, m->str);
                        break;
                    case OP_SetVarPos:
                        in.aux = (int16_t)c.varIndex(c.posVars, m->str);
                        break;
                    case OP_SetValue:
                    case OP_AddValue:
                        in.aux = (int16_t)c.saveValueIndex(m->str);
                        break;
                    case OP_SetTextVisible:
                        in.aux = (int16_t)c.hudTextIndex(m->str);
                        break;
                    case OP_SwitchScene:
                        in.aux = (int16_t)c.sceneIndex(m->str);
                        break;
                    case OP_MoveObjectTo:
                        in.stateSlot = stateSlot();
                        break;
                    case OP_Delay:
                        in.stateSlot = stateSlot();
                        break;
                    default: break;
                }
                // Actions whose operand did not resolve are dropped, exactly
                // like codegen emitting a comment instead of code.
                const bool needsObj =
                    mt->strKind == FlowParamKind::ObjectName ||
                    am->op == OP_SetObjectVisible || am->op == OP_MoveObjectBy ||
                    am->op == OP_SetObjectColor || am->op == OP_SetPosition ||
                    am->op == OP_MoveObjectTo || am->op == OP_TeleportPlayer;
                const bool needsAux =
                    am->op == OP_SetVarInt || am->op == OP_SetVarBool ||
                    am->op == OP_SetVarPos || am->op == OP_SetValue ||
                    am->op == OP_AddValue || am->op == OP_SetTextVisible ||
                    am->op == OP_SwitchScene;
                if ((needsObj && in.obj < 0) || (needsAux && in.aux < 0))
                    continue;

                if (am->op == OP_Log) {
                    // strOff indexes Program::strings here; encode() turns it
                    // into a byte offset into the file's pool.
                    in.strOff = (uint16_t)out.strings.size();
                    in.strLen = (uint16_t)m->str.size();
                    out.strings.push_back(m->str);
                }
                if (am->op == OP_Delay) {
                    // The chain after the delay becomes its own block, armed by
                    // this instruction; recorded now, emitted after this walk.
                    Block b;
                    b.kind = BK_Delay;
                    b.nodeId = (uint16_t)m->id;
                    b.p[0] = m->num[0];
                    b.stateSlot = in.stateSlot;
                    out.blocks.push_back(b);
                    in.blockRef = (int16_t)(out.blocks.size() - 1);
                    pendingDelays.emplace_back(m->id, in.blockRef);
                }
                out.instrs.push_back(in);
            }
        };

    // Trigger blocks first, then the delay blocks they spawned (a delay chain
    // may itself contain another delay, so the queue is drained in order).
    for (const FlowNode& n : fg.nodes) {
        const TriggerMap* tm = triggerFor(n.type);
        if (!tm) continue;
        if ((int)out.blocks.size() >= kMaxBlocks) break;
        Block b;
        b.kind = (uint8_t)tm->kind;
        b.nodeId = (uint16_t)n.id;
        b.firstInstr = (uint16_t)out.instrs.size();
        switch (tm->kind) {
            case BK_EverySeconds:
                b.p[0] = n.num[0] > 0.001f ? n.num[0] : 1.0f;
                b.stateSlot = stateSlot();
                break;
            case BK_OnButton:
                b.aux = (int16_t)padButtonIndex(n.str.empty() ? "Cross" : n.str);
                break;
            case BK_NearObject:
                b.obj = (int16_t)c.resolveTarget(n);
                b.p[0] = n.num[0] > 0.01f ? n.num[0] : 3.0f;
                b.stateSlot = stateSlot();
                break;
            case BK_OnCondition: {
                b.condOff = (uint16_t)out.cond.size();
                if (!emitCondInputsOr(c, n, out.cond)) continue;  // no input
                b.stateSlot = stateSlot();
                break;
            }
            case BK_OnStart:
                b.stateSlot = stateSlot();
                break;
            default: break;
        }
        if (b.kind == BK_OnButton && b.aux < 0) continue;  // unknown button
        if (b.kind == BK_NearObject && b.obj < 0) continue;
        std::vector<int> visited;
        emitChain(n.id, visited);
        b.instrCount = (uint16_t)(out.instrs.size() - b.firstInstr);
        if (b.instrCount == 0) continue;  // nothing wired: no block needed
        out.blocks.push_back(b);
    }
    for (size_t i = 0; i < pendingDelays.size(); ++i) {
        const int delayNode = pendingDelays[i].first;
        const int blockIdx = pendingDelays[i].second;
        out.blocks[blockIdx].firstInstr = (uint16_t)out.instrs.size();
        std::vector<int> visited;
        emitChain(delayNode, visited);
        out.blocks[blockIdx].instrCount =
            (uint16_t)(out.instrs.size() - out.blocks[blockIdx].firstInstr);
    }
    out.stateSlots = nextState;
    if (out.cond.size() > kMaxCond) return false;
    return !out.blocks.empty();
}

// ---------------------------------------------------------------- encoding ---

std::vector<unsigned char> encode(const std::vector<Program>& programs,
                                  uint32_t seq) {
    // One pool for the whole file; every program's string indices become byte
    // offsets into it.
    std::string pool;
    std::vector<std::vector<uint16_t>> offsets(programs.size());
    for (size_t i = 0; i < programs.size(); ++i)
        for (const std::string& s : programs[i].strings) {
            offsets[i].push_back((uint16_t)pool.size());
            if (pool.size() + s.size() < kMaxStrings) pool += s;
        }

    std::vector<unsigned char> body;
    put32(body, (uint32_t)programs.size());
    put32(body, (uint32_t)pool.size());
    for (size_t pi = 0; pi < programs.size(); ++pi) {
        const Program& pr = programs[pi];
        const uint64_t id = pr.ownerIdHash;
        const unsigned char* idb = reinterpret_cast<const unsigned char*>(&id);
        body.insert(body.end(), idb, idb + 8);
        put32(body, (uint32_t)(int32_t)pr.scene);
        put16(body, (uint16_t)pr.blocks.size());
        put16(body, (uint16_t)pr.instrs.size());
        put16(body, (uint16_t)pr.cond.size());
        put16(body, pr.stateSlots);
        for (const Block& b : pr.blocks) {
            body.push_back(b.kind);
            body.push_back(0);  // pad
            put16(body, b.nodeId);
            put16(body, (uint16_t)(int16_t)b.obj);
            put16(body, (uint16_t)(int16_t)b.aux);
            putF(body, b.p[0]);
            putF(body, b.p[1]);
            put16(body, b.firstInstr);
            put16(body, b.instrCount);
            put16(body, b.condOff);
            put16(body, b.stateSlot);
            put16(body, b.dbgKey);
            put16(body, 0);  // pad to 28 bytes
        }
        for (const Instr& in : pr.instrs) {
            body.push_back(in.op);
            body.push_back(in.pin);
            put16(body, in.nodeId);
            put16(body, (uint16_t)(int16_t)in.obj);
            put16(body, (uint16_t)(int16_t)in.aux);
            put16(body, (uint16_t)(int16_t)in.blockRef);
            body.push_back(in.posKind);
            body.push_back(0);  // pad
            put16(body, (uint16_t)(int16_t)in.posIndex);
            put16(body, in.stateSlot);
            put16(body, in.dbgKey);
            put16(body, in.op == OP_Log && in.strOff < offsets[pi].size()
                            ? offsets[pi][in.strOff]
                            : (uint16_t)0);
            put16(body, in.strLen);
            for (int a = 0; a < 4; ++a) putF(body, in.num[a]);
            for (int a = 0; a < 3; ++a) putF(body, in.posLit[a]);
        }
        body.insert(body.end(), pr.cond.begin(), pr.cond.end());
    }
    body.insert(body.end(), pool.begin(), pool.end());

    std::vector<unsigned char> file;
    file.reserve(kHeader + body.size() + 4);
    put32(file, kMagic);
    put32(file, kVersion);
    put32(file, seq);
    put32(file, (uint32_t)body.size());
    put32(file, 0);
    put32(file, 0);
    put32(file, 0);
    put32(file, 0);
    file.insert(file.end(), body.begin(), body.end());
    put32(file, seq ^ kFooterXor);
    return file;
}

std::string write(const std::string& path,
                  const std::vector<unsigned char>& bytes) {
    namespace fs = std::filesystem;
    const fs::path target(path);
    const fs::path tmp(path + ".tmp");
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return "cannot write " + tmp.string();
        f.write(reinterpret_cast<const char*>(bytes.data()),
                (std::streamsize)bytes.size());
        if (!f) return "write failed: " + tmp.string();
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) return "cannot replace " + target.string();
    }
    return "";
}

// -------------------------------------------------------------- built list ---

std::string builtListText(const Project& p) {
    std::ostringstream o;
    o << "# TyraX Live Logic: the flow graphs this build compiled natively.\n"
         "# One line per graph: <scene> <objectId> <graphHash>. The editor\n"
         "# hot-patches a graph only when the live one differs (see\n"
         "# docs/live-logic.md). Generated - do not edit.\n"
         "1\n";
    for (size_t si = 0; si < p.scenes.size(); ++si)
        for (const SceneObject& o2 : p.scenes[si].objects) {
            if (o2.flowGraph.empty()) continue;
            char hex[24];
            std::snprintf(hex, sizeof(hex), "%016llx",
                          (unsigned long long)graphHash(o2.flowGraph));
            o << "g " << si << " " << o2.id << " " << hex << "\n";
        }
    return o.str();
}

bool loadBuiltList(const std::string& path, BuiltList& out) {
    out = BuiltList();
    std::ifstream f(path);
    if (!f) return false;
    BuiltList list;
    std::string line;
    bool versionSeen = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (!versionSeen) {
            if (tag != "1") return false;
            versionSeen = true;
            continue;
        }
        if (tag != "g") return false;
        BuiltGraph g;
        std::string hex;
        ls >> g.scene >> g.objectId >> hex;
        if (!ls || g.objectId.empty()) return false;
        g.hash = std::strtoull(hex.c_str(), nullptr, 16);
        list.graphs.push_back(std::move(g));
    }
    if (!versionSeen) return false;
    list.loaded = true;
    out = std::move(list);
    return true;
}

}  // namespace livelogic
