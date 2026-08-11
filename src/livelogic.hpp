// Live Logic - hot-patching flow-graph LOGIC into the running game
// (docs/live-logic.md). Live Link streams object state, the Live Debugger
// streams what ran; this streams the PROGRAM itself: edit a graph in the
// editor and the game's behavior changes without a Docker rebuild.
//
// A flow graph normally compiles to C++ (flow_graph.gen.cpp), which is why
// editing one has always meant rebuilding. Here the EDITOR compiles the graph
// instead - into a tiny pre-resolved instruction list (every name already
// resolved to the index the game uses) - and writes it to bin/livelogic.bin.
// A debug build carries a small interpreter (src/gen/live_logic.gen.cpp) that
// runs those instructions and makes the natively compiled script for that
// graph stand down. Release builds still compile the graph to native C++ and
// carry no interpreter at all.
//
// This module is the host half: the opcode set (the SINGLE source of truth,
// read by templates.cpp when it generates the interpreter), the compiler with
// its capability check, and the wire format. No GL, no ImGui.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "flowgraph.hpp"

struct Project;
struct SceneData;

namespace livelogic {

// --------------------------------------------------------------- opcodes ---

// What a block is entered by. Anything not listed here cannot be hot-patched.
enum BlockKind : uint8_t {
    BK_OnStart = 0,      // once per scene load
    BK_EverySeconds,     // p[0] = period seconds
    BK_OnButton,         // aux = pad button index (padButtonIndex)
    BK_NearObject,       // obj = target, p[0] = radius; edge-triggered
    BK_OnCondition,      // cond program; rising edge
    BK_Delay,            // armed by OP_Delay; p[0] = seconds
    BK_OnUpdate,         // every frame, no condition
    BK_Count
};

// One action. Operands are pre-resolved by the compiler: `obj` is a runtime
// object index, `aux` an index into whatever table the op reads (variables,
// save values, HUD texts, scenes), `num` the literal parameters, `pos` a
// position operand, `str` an offset into the patch's string pool.
enum OpCode : uint8_t {
    OP_SetObjectVisible = 0,  // pin 0 show / 1 hide / 2 toggle
    OP_MoveObjectBy,
    OP_SetObjectColor,
    OP_SetPosition,
    OP_MoveObjectTo,      // state slot: glides at num[3] units/s
    OP_TeleportPlayer,
    OP_SetSky,
    OP_Delay,             // arms block `blockRef`
    OP_Log,
    OP_SetVarInt,
    OP_SetVarBool,
    OP_SetVarPos,
    OP_SetValue,          // save value = num[0]
    OP_AddValue,          // save value += num[0]
    OP_SetTextVisible,    // pin 0 show (num[0] s) / 1 hide
    OP_SetHudVisible,     // pin 0 show / 1 hide / 2 toggle
    OP_SwitchScene,
    OP_SetFog,
    OP_SetBloom,
    OP_SetGrain,
    OP_SetParticles,
    OP_RotateObjectBy,    // degrees, additive
    OP_SetRotation,       // degrees, absolute
    OP_SpinObject,        // pin 0 start (num[0..2] deg/s) / 1 stop
    OP_SetFootIk,         // pin 0 enable / 1 disable / 2 toggle
    OP_Count
};

// Bool-condition program, evaluated on a tiny stack (RPN). Sources push,
// gates fold the top `n` values.
enum CondOp : uint8_t {
    CO_End = 0,
    CO_IsVisible,     // push: object `a` visible
    CO_VarBool,       // push: flowBool[a]
    CO_VarAtLeast,    // push: flowInt[a] >= imm
    CO_ValueAtLeast,  // push: saveValues[a] >= imm
    CO_And,           // fold n
    CO_Or,
    CO_Not,           // fold n with OR, then negate
    CO_Nand,
    CO_Xor,
    CO_Xnor,
    CO_Count
};

// Where a position operand comes from.
enum PosKind : uint8_t {
    PK_Literal = 0,  // num[0..2]
    PK_VarPos,       // flowPos[posIndex]
    PK_ObjectPos     // objects[posIndex].data.position
};

// ------------------------------------------------------------- program ---

struct Instr {
    uint8_t op = 0;
    uint8_t pin = 0;
    uint16_t nodeId = 0;   // the FlowNode it came from (diagnostics, debug keys)
    int16_t obj = -1;      // runtime object index (-1 = none)
    int16_t aux = -1;      // table index (variable / save value / text / scene)
    int16_t blockRef = -1;  // OP_Delay: the block it arms
    uint8_t posKind = PK_Literal;
    int16_t posIndex = -1;
    uint16_t stateSlot = 0xFFFF;  // per-node runtime state (MoveObjectTo)
    uint16_t dbgKey = 0xFFFF;     // Live Debugger node key (0xFFFF = none)
    uint16_t strOff = 0;          // string pool offset
    uint16_t strLen = 0;
    float num[4] = {0, 0, 0, 0};
    float posLit[3] = {0, 0, 0};  // PK_Literal operand (a linked position can
                                  // come from another node's params)
};

struct Block {
    uint8_t kind = 0;
    uint16_t nodeId = 0;
    int16_t obj = -1;
    int16_t aux = -1;
    float p[2] = {0, 0};
    uint16_t firstInstr = 0;
    uint16_t instrCount = 0;
    uint16_t condOff = 0xFFFF;  // BK_OnCondition: offset into the cond stream
    uint16_t stateSlot = 0xFFFF;
    uint16_t dbgKey = 0xFFFF;
};

/** One graph's compiled program. */
struct Program {
    uint64_t ownerIdHash = 0;  // project::liveLinkIdHash of the owning object
    int scene = 0;
    std::vector<Block> blocks;
    std::vector<Instr> instrs;
    std::vector<unsigned char> cond;  // RPN streams, referenced by condOff
    std::vector<std::string> strings;  // Log texts; Instr::strOff indexes this
    uint16_t stateSlots = 0;
};

/** Why a graph cannot be hot-patched (empty = it can). Human-readable, shown
 * in the editor: "Play Sound", "an object link from Spawn Object", ... */
struct Capability {
    bool patchable = true;
    std::vector<std::string> reasons;  // deduped, in graph order
};

/** Can this graph run on the interpreter at all? Checks every node type
 * against the supported set and rejects the constructs the IR cannot carry
 * (runtime object references, unsupported data planes). */
Capability capability(const Project& p, const SceneData& sc,
                      const FlowGraph& fg);

/** Compiles one object's graph. Returns false when `capability` says no. */
bool compile(const Project& p, int sceneIndex, size_t ownerIndex,
             Program& out);

/** Identity of a graph's compiled behavior: two equal hashes mean the game is
 * running exactly this program (used to decide what needs patching, and to
 * skip rewrites). Covers the node/link content the compiler reads. */
uint64_t graphHash(const FlowGraph& fg);

// ----------------------------------------------------------- wire format ---

/** bin/livelogic.bin: header + programs + one shared string pool (built from
 * the programs' own `strings`, whose indices in Instr::strOff become byte
 * offsets here). `seq` must change whenever the payload does; the game applies
 * a new seq only after the size + footer checks pass. */
std::vector<unsigned char> encode(const std::vector<Program>& programs,
                                  uint32_t seq);

/** Atomic write (tmp + rename). Returns "" or an error string. */
std::string write(const std::string& path,
                  const std::vector<unsigned char>& bytes);

// ------------------------------------------------------- built-graph list ---

/** src/gen/livelogic.built - what the ELF was built with, emitted by codegen:
 * one line per object graph with its graphHash. The editor patches a graph
 * only when the live one differs, so untouched graphs keep running their
 * native C++. */
struct BuiltGraph {
    int scene = 0;
    std::string objectId;
    uint64_t hash = 0;
};
struct BuiltList {
    bool loaded = false;
    std::vector<BuiltGraph> graphs;
};
bool loadBuiltList(const std::string& path, BuiltList& out);
std::string builtListText(const Project& p);

// The pad buttons OP/BK_OnButton can name, in the order the generated
// interpreter switches on (keep the two in sync - the generator reads this).
const std::vector<std::string>& padButtons();
int padButtonIndex(const std::string& name);

// Node types the interpreter implements, derived from the tables above - the
// editor's capability check and the generated interpreter both read this, so
// there is one list, not two.
const std::vector<std::string>& supportedNodeTypes();

// The enum names in enum order, for the generated interpreter: templates.cpp
// emits its enums and its dispatch switches from these, so the game's opcode
// numbering can only come from this header.
const std::vector<std::string>& opNames();
const std::vector<std::string>& blockKindNames();
const std::vector<std::string>& condOpNames();

// Shared caps (the generated interpreter mirrors them).
constexpr int kMaxPrograms = 64;
constexpr int kMaxBlocks = 256;
constexpr int kMaxInstrs = 1024;
constexpr int kMaxCond = 2048;
constexpr int kMaxStrings = 4096;
constexpr int kMaxStateSlots = 256;
constexpr int kCondStack = 16;

}  // namespace livelogic
