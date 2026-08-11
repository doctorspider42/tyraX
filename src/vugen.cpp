#include "vugen.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "vusim.hpp"

namespace vugen {

using vuir::Instr;
using vuir::kAcc;
using vuir::kI;
using vuir::kNoBc;
using vuir::kQ;
using vuir::MALL;
using vuir::MW;
using vuir::MX;
using vuir::MXYZ;
using vuir::MY;
using vuir::MZ;
using vuir::Op;
using vuir::Program;
using vuir::Src;

namespace {

// The VU1 data-memory map, mirroring stapip_vu1_shared_defines.h. The generator
// owns BOTH sides (the microprogram and the EE upload), so these are the single
// place the layout is stated - which is the point: the maxVertCount arithmetic,
// the NLOOP patch offset and the tag-block size all derive from here instead of
// being restated per file.
constexpr int kMvpMatrixAddr = 0;
constexpr int kLightsMatrixAddr = 4;
constexpr int kSingleColorAddr = 7;
constexpr int kOptionsAddr = 8;
constexpr int kLodAddr = 9;
constexpr int kZTestsAddr = 10;
constexpr int kClutAddr = 11;
constexpr int kLightsDirsAddr = 12;
constexpr int kLightsColorsAddr = 15;
constexpr int kSetGifTagAddr = 19;
constexpr int kAlphaAddr = 21;
// The CLIP family's own memory (stapip_vu1_shared_defines.h). The constants sit
// low with everything else; the plane table and the two scratch polygons live
// ABOVE the double buffer, which is why VU1_STAPIP_DBUFFER_END stops where it
// does.
constexpr int kClipConstsAddr = 20;
constexpr int kClipPlanesAddr = 944;
// The two scratch polygons the clipper ping-pongs between. Fifteen quadwords
// each - five vertices at the textured stride of three - which is what a
// triangle cut by six planes can reach.
constexpr int kClipPolyAAddr = 956;
constexpr int kClipPolyBAddr = 986;
constexpr float kClipGuard = 4096.0f;
// The env (matcap) camera basis reuses the lights-matrix area: an env bag
// never carries lighting, so the two can share (stapip_vu1_shared_defines.h).
constexpr int kEnvBasisAddr = kLightsMatrixAddr;
constexpr int kVertDataAddr = 2;  // relative to the double-buffer base
// TyraX addition: the two quadwords a project's own program reads. They sit in
// the DIRECTIONAL LIGHTS area, which is free in exactly the programs that can
// carry a custom stage list - the colour ones - and is why the engine only
// uploads them for a bag with no lighting. Putting them anywhere else would
// have meant growing the per-mesh constant block for every project.
constexpr int kCustomParamsAddr = 15;  // the four per-mesh parameters
constexpr int kCustomTimeAddr = 16;    // (time, sin time, cos time, 1.0)

}  // namespace

// ---------------------------------------------------------------------------
// Vu - the builder
// ---------------------------------------------------------------------------

Vu::Vu(Program& program) : p_(&program) {
    // See Vu::id - successive build() calls reuse the same stack address, so a
    // pointer cannot answer "is this still the same program".
    static int counter = 0;
    id_ = ++counter;
}

void Vu::emit(Instr in) { p_->code.push_back(in); }

Val Vu::named(const std::string& name) { return Val{p_->vf(name), kNoBc}; }
IVal Vu::inamed(const std::string& name) { return IVal{p_->vi(name)}; }

Val Vu::tmp(const char* hint) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%d", hint, tmpSeq_++);
    return named(buf);
}

Val Vu::lq(IVal base, int offset, uint8_t mask, const char* hint) {
    const Val d = tmp(hint);
    Instr in;
    in.op = Op::Lq;
    in.dst = d.reg;
    in.base = base.reg;
    in.imm = offset;
    in.mask = mask;
    emit(in);
    return d;
}

Val Vu::lqConst(int address, uint8_t mask, const char* hint) {
    return lq(izero(), address, mask, hint);
}

void Vu::sq(Val value, IVal base, int offset, uint8_t mask) {
    Instr in;
    in.op = Op::Sq;
    in.s1 = value.reg;
    in.base = base.reg;
    in.imm = offset;
    in.mask = mask;
    emit(in);
}

IVal Vu::ilw(IVal base, int offset, int field, const char* hint) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%d", hint, tmpSeq_++);
    const IVal d = inamed(buf);
    Instr in;
    in.op = Op::Ilw;
    in.dst = d.reg;
    in.base = base.reg;
    in.imm = offset;
    in.mask = (uint8_t)(1 << field);
    emit(in);
    return d;
}

void Vu::isw(IVal value, IVal base, int offset, int field) {
    Instr in;
    in.op = Op::Isw;
    in.s1 = value.reg;
    in.base = base.reg;
    in.imm = offset;
    in.mask = (uint8_t)(1 << field);
    emit(in);
}

namespace {

Instr floatOp(Op op, int16_t dst, Val a, Val b, uint8_t mask) {
    Instr in;
    in.op = op;
    in.dst = dst;
    in.s1 = a.reg;
    in.bc1 = a.bc;
    in.s2 = b.reg;
    in.bc2 = b.bc;
    in.mask = mask;
    return in;
}

}  // namespace

Val Vu::add(Val a, Val b, uint8_t mask) {
    const Val d = tmp();
    emit(floatOp(Op::Add, d.reg, a, b, mask));
    return d;
}
Val Vu::sub(Val a, Val b, uint8_t mask) {
    const Val d = tmp();
    emit(floatOp(Op::Sub, d.reg, a, b, mask));
    return d;
}
Val Vu::mul(Val a, Val b, uint8_t mask) {
    const Val d = tmp();
    emit(floatOp(Op::Mul, d.reg, a, b, mask));
    return d;
}
Val Vu::minimum(Val a, Val b, uint8_t mask) {
    const Val d = tmp();
    emit(floatOp(Op::Mini, d.reg, a, b, mask));
    return d;
}
Val Vu::maximum(Val a, Val b, uint8_t mask) {
    const Val d = tmp();
    emit(floatOp(Op::Max, d.reg, a, b, mask));
    return d;
}
Val Vu::move(Val a, uint8_t mask) {
    const Val d = tmp();
    Instr in;
    in.op = Op::Move;
    in.dst = d.reg;
    in.s2 = a.reg;
    in.bc2 = a.bc;
    in.mask = mask;
    emit(in);
    return d;
}

void Vu::mulAcc(Val a, Val b, uint8_t mask) {
    emit(floatOp(Op::Mula, kAcc, a, b, mask));
}
void Vu::maddAcc(Val a, Val b, uint8_t mask) {
    emit(floatOp(Op::Madda, kAcc, a, b, mask));
}
void Vu::maddInto(Val dst, Val a, Val b, uint8_t mask) {
    emit(floatOp(Op::Madd, dst.reg, a, b, mask));
}
void Vu::msubInto(Val dst, Val a, Val b, uint8_t mask) {
    emit(floatOp(Op::Msub, dst.reg, a, b, mask));
}
void Vu::mulInto(Val dst, Val a, Val b, uint8_t mask) {
    emit(floatOp(Op::Mul, dst.reg, a, b, mask));
}
void Vu::addInto(Val dst, Val a, Val b, uint8_t mask) {
    emit(floatOp(Op::Add, dst.reg, a, b, mask));
}
void Vu::subInto(Val dst, Val a, Val b, uint8_t mask) {
    emit(floatOp(Op::Sub, dst.reg, a, b, mask));
}
void Vu::maximumInto(Val dst, Val a, Val b, uint8_t mask) {
    emit(floatOp(Op::Max, dst.reg, a, b, mask));
}
void Vu::minimumInto(Val dst, Val a, Val b, uint8_t mask) {
    emit(floatOp(Op::Mini, dst.reg, a, b, mask));
}

void Vu::loadI(float value) {
    Instr in;
    in.op = Op::Loi;
    in.fimm = value;
    emit(in);
}

Val Vu::minimumI(Val a, uint8_t mask) {
    const Val d = tmp();
    minimumIInto(d, a, mask);
    return d;
}

void Vu::minimumIInto(Val dst, Val a, uint8_t mask) {
    Instr in = floatOp(Op::Mini, dst.reg, a, Val{kI, kNoBc}, mask);
    in.s2kind = Src::I;
    emit(in);
}

Val Vu::addI(Val a, uint8_t mask) {
    const Val d = tmp();
    Instr in = floatOp(Op::Add, d.reg, a, Val{kI, kNoBc}, mask);
    in.s2kind = Src::I;
    emit(in);
    return d;
}

void Vu::divQ(Val a, int fa, Val b, int fb) {
    Instr in;
    in.op = Op::Div;
    in.dst = kQ;
    in.s1 = a.reg;
    in.bc1 = (uint8_t)fa;
    in.s2 = b.reg;
    in.bc2 = (uint8_t)fb;
    emit(in);
}

void Vu::rsqrtQ(Val a, int fa, Val b, int fb) {
    Instr in;
    in.op = Op::Rsqrt;
    in.dst = kQ;
    in.s1 = a.reg;
    in.bc1 = (uint8_t)fa;
    in.s2 = b.reg;
    in.bc2 = (uint8_t)fb;
    emit(in);
}

Val Vu::mulQ(Val a, uint8_t mask) {
    const Val d = tmp();
    mulQInto(d, a, mask);
    return d;
}

void Vu::mulQInto(Val dst, Val a, uint8_t mask) {
    Instr in = floatOp(Op::Mul, dst.reg, a, Val{kQ, kNoBc}, mask);
    in.s2kind = Src::Q;
    emit(in);
}

Val Vu::ftoi4(Val a, uint8_t mask) {
    const Val d = tmp();
    ftoi4Into(d, a, mask);
    return d;
}
Val Vu::ftoi0(Val a, uint8_t mask) {
    const Val d = tmp();
    ftoi0Into(d, a, mask);
    return d;
}
void Vu::ftoi4Into(Val dst, Val a, uint8_t mask) {
    Instr in;
    in.op = Op::Ftoi4;
    in.dst = dst.reg;
    in.s2 = a.reg;
    in.bc2 = a.bc;
    in.mask = mask;
    emit(in);
}
void Vu::ftoi0Into(Val dst, Val a, uint8_t mask) {
    Instr in;
    in.op = Op::Ftoi0;
    in.dst = dst.reg;
    in.s2 = a.reg;
    in.bc2 = a.bc;
    in.mask = mask;
    emit(in);
}

IVal Vu::iadd(IVal a, IVal b, const char* hint) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%d", hint, tmpSeq_++);
    const IVal d = inamed(buf);
    iaddInto(d, a, b);
    return d;
}
void Vu::iaddInto(IVal dst, IVal a, IVal b) {
    Instr in;
    in.op = Op::Iadd;
    in.dst = dst.reg;
    in.s1 = a.reg;
    in.s2 = b.reg;
    emit(in);
}
IVal Vu::iaddiu(IVal a, int imm, const char* hint) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%d", hint, tmpSeq_++);
    const IVal d = inamed(buf);
    iaddiuInto(d, a, imm);
    return d;
}
void Vu::iaddiuInto(IVal dst, IVal a, int imm) {
    Instr in;
    // The engine's loop counter decrements with the signed 5-bit iaddi; a
    // negative immediate must not become a 15-bit unsigned iaddiu.
    in.op = imm < 0 ? Op::Iaddi : Op::Iaddiu;
    in.dst = dst.reg;
    in.s1 = a.reg;
    in.imm = imm;
    emit(in);
}
IVal Vu::ior(IVal a, IVal b, const char* hint) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%d", hint, tmpSeq_++);
    const IVal d = inamed(buf);
    Instr in;
    in.op = Op::Ior;
    in.dst = d.reg;
    in.s1 = a.reg;
    in.s2 = b.reg;
    emit(in);
    return d;
}
IVal Vu::iand(IVal a, IVal b, const char* hint) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%d", hint, tmpSeq_++);
    const IVal d = inamed(buf);
    Instr in;
    in.op = Op::Iand;
    in.dst = d.reg;
    in.s1 = a.reg;
    in.s2 = b.reg;
    emit(in);
    return d;
}
IVal Vu::mtir(Val a, int field, const char* hint) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%d", hint, tmpSeq_++);
    const IVal d = inamed(buf);
    Instr in;
    in.op = Op::Mtir;
    in.dst = d.reg;
    in.s1 = a.reg;
    in.bc1 = (uint8_t)field;
    emit(in);
    return d;
}

int Vu::mark() const { return (int)p_->code.size(); }

int Vu::hoist(int from, int to) {
    if (from < 0 || to < 0 || from >= (int)p_->code.size() || to > from) return 0;
    const int n = (int)p_->code.size() - from;
    std::vector<vuir::Instr> moved(p_->code.begin() + from, p_->code.end());
    p_->code.erase(p_->code.begin() + from, p_->code.end());
    p_->code.insert(p_->code.begin() + to, moved.begin(), moved.end());
    // EVERYTHING THAT REMEMBERS A CODE INDEX MOVES WITH IT. Two things do:
    // labels already bound, and branches still waiting for `finish` to resolve
    // them. The as_is and cull families never noticed - their script hook sits
    // in a straight run with no label between the hoist target and the code
    // being hoisted - but the clip family's hooks are inside three nested
    // loops, so one constant hoisted out of the vertex emitter steps over every
    // label and every unresolved branch of the clipper and the program jumps
    // into the middle of its own polygon loop.
    auto shift = [&](int& at) {
        if (at >= from)
            at -= (from - to);
        else if (at >= to)
            at += n;
    };
    for (int& at : labelIndex_)
        if (at >= 0) shift(at);
    for (auto& f : fixups_)
        if (f.first >= 0) shift(f.first);
    return n;
}

Lbl Vu::label(const char* name) {
    labelIndex_.push_back(-1);
    labelNames_.push_back(name);
    return Lbl{(int)labelIndex_.size() - 1};
}

void Vu::bind(Lbl l) {
    Instr in;
    in.op = Op::Label;
    in.comment = l.id >= 0 && (size_t)l.id < labelNames_.size()
                     ? labelNames_[l.id]
                     : ("L" + std::to_string(l.id));
    emit(in);
    if (l.id >= 0 && (size_t)l.id < labelIndex_.size())
        labelIndex_[l.id] = (int)p_->code.size() - 1;
}

void Vu::branch(Lbl l) {
    Instr in;
    in.op = Op::B;
    emit(in);
    fixups_.push_back({(int)p_->code.size() - 1, l.id});
}
void Vu::branchIfLez(IVal a, Lbl l) {
    Instr in;
    in.op = Op::Iblez;
    in.s1 = a.reg;
    emit(in);
    fixups_.push_back({(int)p_->code.size() - 1, l.id});
}
void Vu::branchIfNotEq(IVal a, IVal b, Lbl l) {
    Instr in;
    in.op = Op::Ibne;
    in.s1 = a.reg;
    in.s2 = b.reg;
    emit(in);
    fixups_.push_back({(int)p_->code.size() - 1, l.id});
}

void Vu::xtop(IVal dst) {
    Instr in;
    in.op = Op::Xtop;
    in.dst = dst.reg;
    emit(in);
}
void Vu::branchIfEq(IVal a, IVal b, Lbl l) {
    Instr in;
    in.op = Op::Ibeq;
    in.s1 = a.reg;
    in.s2 = b.reg;
    emit(in);
    fixups_.push_back({(int)p_->code.size() - 1, l.id});
}
void Vu::branchIfGtz(IVal a, Lbl l) {
    Instr in;
    in.op = Op::Ibgtz;
    in.s1 = a.reg;
    emit(in);
    fixups_.push_back({(int)p_->code.size() - 1, l.id});
}
void Vu::branchIfLtz(IVal a, Lbl l) {
    Instr in;
    in.op = Op::Ibltz;
    in.s1 = a.reg;
    emit(in);
    fixups_.push_back({(int)p_->code.size() - 1, l.id});
}
void Vu::moveInto(Val dst, Val a, uint8_t mask) {
    Instr in;
    in.op = Op::Move;
    in.mask = mask;
    in.dst = dst.reg;
    // The SECOND source slot, like every other one-operand op in the IR (see
    // Vu::move). Writing s1 instead reads vf00 and copies zeros - which is a
    // silent no-op, not a compile error, and cost an afternoon: the clipper's
    // `move ppos, cpos` cleared the previous vertex and every cut edge came out
    // interpolated against the origin.
    in.s2 = a.reg;
    in.bc2 = a.bc;
    emit(in);
}
void Vu::clipwInto(Val a, uint8_t mask) {
    Instr in;
    in.op = Op::Clipw;
    in.mask = mask;
    // Both operands are the same register: clipw judges xyz against the w of
    // its SECOND source, and every caller wants the vertex judged against its
    // own w. The handwritten programs spell it the same way.
    in.s1 = a.reg;
    in.s2 = a.reg;
    emit(in);
}
void Vu::fcandInto(IVal dst, int mask) {
    Instr in;
    in.op = Op::Fcand;
    in.dst = dst.reg;
    in.imm = mask;
    emit(in);
}
void Vu::iorInto(IVal dst, IVal a, IVal b) {
    Instr in;
    in.op = Op::Ior;
    in.dst = dst.reg;
    in.s1 = a.reg;
    in.s2 = b.reg;
    emit(in);
}
void Vu::iandInto(IVal dst, IVal a, IVal b) {
    Instr in;
    in.op = Op::Iand;
    in.dst = dst.reg;
    in.s1 = a.reg;
    in.s2 = b.reg;
    emit(in);
}
void Vu::isubInto(IVal dst, IVal a, IVal b) {
    Instr in;
    in.op = Op::Isub;
    in.dst = dst.reg;
    in.s1 = a.reg;
    in.s2 = b.reg;
    emit(in);
}
void Vu::xgkick(IVal address) {
    Instr in;
    in.op = Op::Xgkick;
    in.s1 = address.reg;
    emit(in);
}
void Vu::barrier() {
    Instr in;
    in.op = Op::Barrier;
    emit(in);
}
void Vu::cont() {
    Instr in;
    in.op = Op::Cont;
    emit(in);
}

void Vu::finish() {
    for (const auto& f : fixups_) {
        const int target =
            f.second >= 0 && (size_t)f.second < labelIndex_.size()
                ? labelIndex_[f.second]
                : -1;
        if (f.first >= 0 && (size_t)f.first < p_->code.size())
            p_->code[f.first].target = target;
    }
    fixups_.clear();
}

// --- the method library -----------------------------------------------------

void Vu::scaleToGsFormat(Val vertex, Val scale) {
    p_->code.reserve(p_->code.size() + 3);
    mulAcc(scale, zero().broadcast(3), MXYZ);
    p_->code.back().comment = "ScaleVertexToGSFormat";
    maddInto(vertex, vertex, scale, MXYZ);
    ftoi4Into(vertex, vertex, MXYZ);
}

void Vu::fixColor(Val color) {
    loadI(255.0f);
    p_->code.back().comment = "FixColor: clamp to 0..255";
    minimumIInto(color, color, MXYZ);
    maximumInto(color, color, zero().broadcast(0), MXYZ);
    ftoi0Into(color, color, MALL);
}

void Vu::fogCoefficient(IVal dst, Val vertex, Val fogParams, Val scratch) {
    // F = clamp(w * fogScale + fogOffset, 0, 255), then ftoi4 so the value is
    // already shifted into the F field of a packed XYZF2 (bits 4..11).
    addInto(scratch, zero(), vertex.broadcast(3), MX);
    p_->code.back().comment = "CalculateTyraFog";
    mulInto(scratch, scratch, fogParams.broadcast(2), MX);
    addInto(scratch, scratch, fogParams.broadcast(3), MX);
    loadI(255.0f);
    minimumIInto(scratch, scratch, MX);
    maximumInto(scratch, scratch, zero().broadcast(0), MX);
    ftoi4Into(scratch, scratch, MX);
    Instr in;
    in.op = Op::Mtir;
    in.dst = dst.reg;
    in.s1 = scratch.reg;
    in.bc1 = 0;
    emit(in);
}

void Vu::envStq(Val stq, Val envBasisX, Val envBasisY, Val envBasisZ,
                const Val scratch[2]) {
    const Val len = scratch[0], nrm = scratch[1];
    mulInto(len, stq, stq, MXYZ);
    p_->code.back().comment = "CalculateTyraEnvStq: renormalize (uses Q)";
    addInto(len, len, len.broadcast(1), MX);
    addInto(len, len, len.broadcast(2), MX);
    rsqrtQ(zero(), 3, len, 0);
    mulQInto(nrm, stq, MXYZ);
    mulAcc(envBasisX, nrm.broadcast(0), (uint8_t)(MX | MY));
    maddAcc(envBasisY, nrm.broadcast(1), (uint8_t)(MX | MY));
    maddInto(stq, envBasisZ, nrm.broadcast(2), (uint8_t)(MX | MY));
    addInto(stq, stq, envBasisZ.broadcast(3), (uint8_t)(MX | MY));
    addInto(stq, zero(), envBasisZ.broadcast(2), MZ);
}

void Vu::resetClipFlags() {
    Instr in;
    in.op = Op::Fcset;
    in.imm = 0;
    in.comment = "ResetClipFlags";
    emit(in);
}

void Vu::makeAdcMask(IVal dst) {
    iaddiuInto(dst, izero(), 0x4000);
    p_->code.back().comment = "MakeTyraAdcMask: 0x8000 in two 15-bit adds";
    Instr in;
    in.op = Op::Iadd;
    in.dst = dst.reg;
    in.s1 = dst.reg;
    in.s2 = dst.reg;
    emit(in);
}

void Vu::persCorrect(Val dst, Val v) {
    divQ(zero(), 3, v, 3);
    p_->code.back().comment = "VertexPersCorr";
    mulQInto(dst, v, MXYZ);
}

void Vu::fogClipCheck(Val vertex, IVal destAddress, int offset, IVal fogInt,
                      IVal adcMask, IVal adcBit) {
    Instr clip;
    clip.op = Op::Clipw;
    clip.s1 = vertex.reg;
    clip.s2 = vertex.reg;
    clip.mask = MXYZ;
    clip.comment = "PerformTyraFogClipCheck";
    emit(clip);
    Instr fc;
    fc.op = Op::Fcand;
    fc.dst = p_->vi("VI01");
    fc.imm = 0x3FFFF;  // three vertices' worth of clip flags
    emit(fc);
    iaddiuInto(adcBit, IVal{p_->vi("VI01")}, 0x7FFF);
    Instr andi;
    andi.op = Op::Iand;
    andi.dst = adcBit.reg;
    andi.s1 = adcBit.reg;
    andi.s2 = adcMask.reg;
    emit(andi);
    Instr ori;
    ori.op = Op::Ior;
    ori.dst = adcBit.reg;
    ori.s1 = adcBit.reg;
    ori.s2 = fogInt.reg;
    emit(ori);
    isw(adcBit, destAddress, offset, 3);
}

void Vu::spotLight(Val color, Val vertex, Val spotPos, Val spotDir, Val spotCol,
                   const Val scratch[7]) {
    const Val d = scratch[0], sq2 = scratch[1], dist = scratch[2],
              tm = scratch[3], t = scratch[4], c = scratch[5], a = scratch[6];
    emit(floatOp(Op::Sub, d.reg, vertex, spotPos, MXYZ));
    p_->code.back().comment = "CalculateTyraSpotLight";
    mulInto(sq2, d, d, MXYZ);
    addInto(dist, sq2, sq2.broadcast(1), MX);
    addInto(dist, dist, sq2.broadcast(2), MX);
    mulInto(tm, d, spotDir, MXYZ);
    addInto(t, tm, tm.broadcast(1), MX);
    addInto(t, t, tm.broadcast(2), MX);
    maximumInto(t, t, zero().broadcast(0), MX);
    mulInto(t, t, t, MX);
    mulInto(c, dist, spotDir.broadcast(3), MX);
    emit(floatOp(Op::Sub, c.reg, t, c, MX));
    mulInto(c, c, spotCol.broadcast(3), MX);
    minimumInto(c, c, zero().broadcast(3), MX);
    maximumInto(c, c, zero().broadcast(0), MX);
    emit(floatOp(Op::Add, kAcc, zero(), zero().broadcast(3), MX));
    msubInto(a, dist, spotPos.broadcast(3), MX);
    minimumInto(a, a, zero().broadcast(3), MX);
    maximumInto(a, a, zero().broadcast(0), MX);
    mulInto(c, c, a, MX);
    mulInto(d, spotCol, c.broadcast(0), MXYZ);  // reuse d as the addend
    addInto(color, color, d, MXYZ);
}

void Vu::constants(Val dst, float x, float y, float z, float w) {
    const float v[4] = {x, y, z, w};
    for (int f = 0; f < 4; ++f) {
        loadI(v[f]);
        if (f == 0) p_->code.back().comment = "constants";
        // vf00 is (0, 0, 0, 1). Adding the literal to it therefore gives the
        // literal in x/y/z and the literal PLUS ONE in w - which is silent, and
        // was wrong for a week: the sine's 0.225 correction coefficient sits in
        // w, so every generated sine was applying 1.225 instead. It survived
        // the host checks because the simulator models vf00 correctly too, so
        // both sides were consistently wrong; the console's own numbers not
        // matching a hand calculation is what exposed it. Multiply for w
        // instead - 1 * literal - and it is exact in all four fields.
        Instr in = floatOp(f == 3 ? Op::Mul : Op::Add, dst.reg, zero(),
                           Val{kI, kNoBc}, (uint8_t)(1 << f));
        in.s2kind = Src::I;
        emit(in);
    }
}

void Vu::truncate(Val dst, Val a, uint8_t mask) {
    ftoi0Into(dst, a, mask);
    Instr in;
    in.op = Op::Itof0;
    in.dst = dst.reg;
    in.s2 = dst.reg;
    in.mask = mask;
    emit(in);
}

void Vu::absInto(Val dst, Val a, uint8_t mask) {
    Instr in;
    in.op = Op::Abs;
    in.dst = dst.reg;
    in.s2 = a.reg;
    in.bc2 = a.bc;
    in.mask = mask;
    emit(in);
}

void Vu::onesInto(Val dst) {
    // vf00 is (0,0,0,1), so xyz come from an add against its w and the w lane
    // from a MULTIPLY by it - adding would make it 2.
    addInto(dst, zero(), zero().broadcast(3), MXYZ);
    p_->code.back().comment = "1.0 in every field";
    mulInto(dst, zero(), zero().broadcast(3), MW);
}

void Vu::sineApprox(Val dst, Val angle, uint8_t mask, Val kc, Val one,
                    const Val scratch[3], Val phase) {
    const Val t = scratch[0], f = scratch[1], y = scratch[2];
    mulInto(t, angle, kc.broadcast(0), mask);
    p_->code.back().comment = "sin(): angle -> [-1,1) turns";
    addInto(t, t, phase, mask);
    // floor(t) with the 2^23 trick: the VU truncates toward zero, and adding a
    // number big enough to push every fractional bit off the mantissa makes
    // that truncation a floor. Valid for |t| well under 2^23, which is why the
    // engine wraps the clock before it ever reaches the microprogram.
    addInto(f, t, kc.broadcast(2), mask);
    subInto(f, f, kc.broadcast(2), mask);
    subInto(t, t, f, mask);              // frac in [0,1)
    addInto(t, t, t, mask);              // 2*frac
    subInto(t, t, one, mask);            // x in [-1,1), angle = pi*x
    absInto(f, t, mask);
    subInto(f, one, f, mask);            // 1 - |x|
    mulInto(y, t, f, mask);
    addInto(y, y, y, mask);
    addInto(y, y, y, mask);              // 4x(1-|x|) ~ sin(pi x), 5.6% error
    absInto(f, y, mask);
    mulInto(f, y, f, mask);              // y|y|
    subInto(f, f, y, mask);
    mulInto(f, f, kc.broadcast(3), mask);  // * 0.225
    addInto(dst, y, f, mask);              // 0.2% error
}

void Vu::dirLightShade(Val color, Val normal, const Val lightMatrix[3],
                       const Val lightDirs[3], const Val lightColors[3],
                       Val ambient) {
    // CalculateTyraDirectionalLights, instruction for instruction. `normal` is
    // OVERWRITTEN with its world-space self on the way - the handwritten macro
    // does that too, and an extra move would break bit-identity.
    mulAcc(lightMatrix[0], normal.broadcast(0), MXYZ);
    p_->code.back().comment = "CalculateTyraDirectionalLights";
    maddAcc(lightMatrix[1], normal.broadcast(1), MXYZ);
    maddInto(normal, lightMatrix[2], normal.broadcast(2), MXYZ);
    mulAcc(lightDirs[0], normal.broadcast(0), MXYZ);
    maddAcc(lightDirs[1], normal.broadcast(1), MXYZ);
    maddInto(color, lightDirs[2], normal.broadcast(2), MXYZ);
    minimumInto(color, color, zero().broadcast(3), MXYZ);
    maximumInto(color, color, zero().broadcast(0), MXYZ);
    mulAcc(lightColors[0], color.broadcast(0), MXYZ);
    maddAcc(lightColors[1], color.broadcast(1), MXYZ);
    maddAcc(lightColors[2], color.broadcast(2), MXYZ);
    maddInto(color, ambient, zero().broadcast(3), MXYZ);
    // Alpha 128 = the GS "1.0" - a lit mesh carries no colour stream to take
    // one from.
    loadI(128.0f);
    Instr in;
    in.op = Op::Add;
    in.dst = color.reg;
    in.s1 = zero().reg;
    in.s2 = kI;
    in.s2kind = Src::I;
    in.mask = vuir::MW;
    emit(in);
}

void Vu::transform(Val dst, const Val m[4], Val v) {
    mulAcc(m[0], v.broadcast(0), MALL);
    p_->code.back().comment = "MatrixMultiplyVertex";
    maddAcc(m[1], v.broadcast(1), MALL);
    maddAcc(m[2], v.broadcast(2), MALL);
    maddInto(dst, m[3], v.broadcast(3), MALL);
}

// ---------------------------------------------------------------------------
// Stages - the authoring layer (docs/vu-authoring.md)
// ---------------------------------------------------------------------------

bool stagesMoveGeometry(const std::vector<Stage>& stages) {
    for (const Stage& s : stages) {
        if (stageIsNoOp(s)) continue;
        const StageDef* d = stageDef(s.key);
        if (d && d->movesGeometry) return true;
    }
    return false;
}

const char* slotName(Slot s) {
    switch (s) {
        case Slot::ObjectSpace: return "Object space";
        case Slot::ClipSpace: return "Clip space";
        case Slot::Ndc: return "Screen (NDC)";
        case Slot::Color: return "Colour";
        case Slot::Texture: return "Texture (ST)";
        case Slot::Kernel: return "Kernel element";
    }
    return "?";
}

const std::vector<StageDef>& stageDefs() {
    // THE catalogue. Two conventions every entry keeps, and both are load
    // bearing rather than stylistic:
    //
    // 1. A stage is the IDENTITY when its strength parameters are zero. That is
    //    what makes "one program per material class" workable at all - the
    //    program runs on every mesh of that class, and a mesh that wants
    //    nothing gets nothing by leaving its parameters at zero. It also lets
    //    the generator DROP a stage whose strengths are all literal zero, so an
    //    experiment left in the list costs no micro memory.
    // 2. Every parameter carries a tip, and the tip says what THAT knob does -
    //    the `.desc` says what the stage is for. Same split as the flow-graph
    //    node registry, and for the same reason: a paragraph on the stage is
    //    not an answer to "what does Frequency mean here".
    static const std::vector<StageDef> defs = {
        {"wobble", "Wobble", Slot::ObjectSpace,
         "A travelling sine wave along Y, phased by the vertex's own X+Z - "
         "water, cloth, a heat shimmer. The cheapest stage that reads as "
         "animation, and the one to try first.",
         3,
         {{"Amplitude", "How far a vertex moves, in the model's own units. 0 "
                        "switches the stage off entirely.", 0.0f, 0.0f, 8.0f,
           false, true},
          {"Frequency", "Waves per unit across the mesh. Big meshes want small "
                        "numbers; 0 makes the whole mesh move as one.", 0.5f,
           0.0f, 8.0f},
          {"Speed", "Radians per second. 0 freezes the wave in place, which is "
                    "how you author the shape before animating it.", 2.0f,
           0.0f, 20.0f}},
         true, false, 23, true, true, 3},

        {"twist", "Twist", Slot::ObjectSpace,
         "Rotates the mesh about its own Y axis by an angle that grows with "
         "height - a wrung cloth, a tornado, a spiral column. The most "
         "expensive stage in the catalogue: it needs a sine AND a cosine.",
         2,
         {{"Strength", "Radians of twist per unit of height. 0 is the "
                       "identity.", 0.0f, -3.0f, 3.0f, false, true},
          {"Speed", "Radians per second added to the whole twist, so the mesh "
                    "turns as it wrings.", 0.0f, -10.0f, 10.0f}},
         true, false, 47, true, true, 6},

        {"inflate", "Inflate", Slot::ObjectSpace,
         "Scales the mesh about its object origin, optionally breathing. Note "
         "it pushes along the direction FROM THE ORIGIN, not along the normal - "
         "the colour programs carry no normals, so a true shell is not "
         "available here.",
         3,
         {{"Amount", "Constant swell, as a fraction of the model's size. 0.1 = "
                     "ten per cent bigger.", 0.0f, -0.9f, 2.0f, false, true},
          {"Pulse", "How much the swell breathes around Amount. Left at a "
                    "literal 0 the sine is not generated at all.", 0.0f, 0.0f,
           2.0f, false, true},
          {"Speed", "Breaths per second, in radians.", 2.0f, 0.0f, 20.0f}},
         true, false, 26, true, true, 3},

        {"squash", "Squash / stretch", Slot::ObjectSpace,
         "Per-axis scale about the object origin. Constant, so it costs four "
         "instructions - the cheap way to make a batch of identical props stop "
         "looking identical, one parameter slot each.",
         3,
         {{"X", "Extra scale along X, as a fraction. 0 is unchanged.", 0.0f,
           -0.9f, 3.0f, false, true},
          {"Y", "Extra scale along Y.", 0.0f, -0.9f, 3.0f, false, true},
          {"Z", "Extra scale along Z.", 0.0f, -0.9f, 3.0f, false, true}},
         false, false, 4, true, true, 1},

        {"heightShade", "Height shade", Slot::ObjectSpace,
         "Darkens or brightens the vertex by its object-space height - a free "
         "gradient down a wall, dirt at the base of a prop. Runs in object "
         "space because that is the last point where the height means "
         "anything.",
         3,
         {{"Amount", "How far the shade swings, 1.0 = full black to double "
                     "brightness. 0 is the identity.", 0.0f, 0.0f, 1.0f, false,
           true},
          {"Scale", "Height units per unit of shade. Negative flips it.", 0.1f,
           -4.0f, 4.0f},
          {"Bias", "Shifts where the gradient sits. -1 puts the dark end at "
                   "the model's origin.", 0.0f, -4.0f, 4.0f}},
         false, false, 7, false, false, 1},

        {"zbias", "Depth bias", Slot::ClipSpace,
         "Pulls the vertex toward or away from the camera in clip space, "
         "proportionally to its distance - so it is a CONSTANT bias in screen "
         "depth rather than a world offset. What a decal or a coplanar overlay "
         "needs to stop z-fighting.",
         1,
         {{"Bias", "Fraction of the NDC depth range. 0.001 is usually enough; "
                   "0 is the identity.", 0.0f, -0.05f, 0.05f, false, true}},
         false, false, 2, false, true, 1},

        {"snap", "Vertex snap", Slot::Ndc,
         "Quantises the screen position to a grid - the PlayStation 1 wobble, "
         "on purpose. Runs after the perspective divide, so the grid is in "
         "SCREEN space and a distant vertex jitters more, which is exactly the "
         "artefact being imitated.",
         2,
         {{"Steps", "Grid divisions across the screen. 160 is roughly a PS1 "
                    "look; 640 is subtle. Baked into the microprogram, so it "
                    "cannot be bound to a mesh.", 160.0f, 8.0f, 1024.0f, true},
          {"Strength", "Blend toward the snapped position. 1 is full snap, 0 "
                       "the identity.", 0.0f, 0.0f, 1.0f, false, true}},
         false, false, 8, false, true, 1},

        {"pulse", "Pulse colour", Slot::Color,
         "Brightens and dims the vertex colour on a sine - a beacon, a "
         "heartbeat, a damage flash. Multiplies, so a black vertex stays "
         "black.",
         2,
         {{"Amount", "Peak swing as a fraction of the colour. 0.5 = half as "
                     "dark to half as bright. 0 is the identity.", 0.0f, 0.0f,
           2.0f, false, true},
          {"Speed", "Radians per second.", 4.0f, 0.0f, 30.0f}},
         true, false, 22, false, false, 2},

        {"posterize", "Posterize", Slot::Color,
         "Rounds the colour down to N levels per channel. Cheap stylisation, "
         "and it composes: put it after Pulse and the pulse steps instead of "
         "sliding.",
         2,
         {{"Levels", "Steps per channel. 4 is a poster, 16 is subtle. Baked "
                     "into the microprogram, so it cannot be bound to a mesh.",
           4.0f, 2.0f, 64.0f, true},
          {"Strength", "Blend toward the posterized colour. 0 is the "
                       "identity.", 0.0f, 0.0f, 1.0f, false, true}},
         false, false, 7, false, false, 1},

        {"desaturate", "Desaturate", Slot::Color,
         "Drains the vertex colour toward its luminance. Bound to a per-mesh "
         "slot this is how an object goes grey when it is disabled, dead or "
         "out of power - with no second material and no texture swap.",
         1,
         {{"Amount", "1 is fully grey, 0 the identity.", 0.0f, 0.0f, 1.0f,
           false, true}},
         false, false, 6, false, false, 2},

        {"scrollUv", "Scroll UV", Slot::Texture,
         "Slides the texture across the surface. A conveyor, a waterfall, a "
         "starfield - the effect that would otherwise need a new texture per "
         "frame.",
         2,
         {{"Speed U", "Texture widths per second along U. 0 is the identity.",
           0.0f, -4.0f, 4.0f, false, true},
          {"Speed V", "Texture heights per second along V.", 0.0f, -4.0f, 4.0f,
           false, true}},
         true, true, 4, false, false, 2},
    };
    return defs;
}

const StageDef* stageDef(const std::string& key) {
    for (const StageDef& d : stageDefs())
        if (key == d.key) return &d;
    return nullptr;
}

Stage makeStage(const std::string& key) {
    Stage s;
    s.key = key;
    const StageDef* d = stageDef(key);
    if (d)
        for (int i = 0; i < d->paramCount; ++i) s.params[i].value = d->params[i].def;
    return s;
}

bool stageIsNoOp(const Stage& s) {
    if (!s.enabled) return true;
    const StageDef* d = stageDef(s.key);
    if (!d) return true;
    // "Every strength is a literal zero" is the whole of the folding rule. A
    // parameter bound to a mesh slot is NOT known here, so it never folds - the
    // game may write anything into it.
    bool sawStrength = false;
    for (int i = 0; i < d->paramCount; ++i) {
        if (!d->params[i].strength) continue;
        sawStrength = true;
        if (s.params[i].meshSlot >= 0 || s.params[i].value != 0.0f) return false;
    }
    return sawStrength;
}

// ---------------------------------------------------------------------------
// Program descriptions
// ---------------------------------------------------------------------------

Desc descAsIsColor() {
    Desc d;
    d.vclName = "StaPipVU1AsIsC";
    d.asmName = "StaPipVU1As_Is_C";
    d.fileStem = "stapip_as_is_c_vu1";
    d.className = "StaPipAsIsCVU1Program";
    d.programEnum = "StaPipAsIsColor";
    d.title = "StaPip - As is - C";
    return d;
}

Desc descAsIsTextureColor() {
    Desc d;
    d.vclName = "StaPipVU1AsIsTC";
    d.asmName = "StaPipVU1As_Is_TC";
    d.fileStem = "stapip_as_is_tc_vu1";
    d.className = "StaPipAsIsTCVU1Program";
    d.programEnum = "StaPipAsIsTextureColor";
    d.title = "StaPip - As is - TC";
    d.texture = true;
    return d;
}

Desc descAsIsDirLights() {
    Desc d;
    d.vclName = "StaPipVU1AsIsD";
    d.asmName = "StaPipVU1As_Is_D";
    d.fileStem = "stapip_as_is_d_vu1";
    d.className = "StaPipAsIsDVU1Program";
    d.programEnum = "StaPipAsIsDirLights";
    d.title = "StaPip - As is - D";
    d.dirLights = true;
    return d;
}

Desc descAsIsTextureDirLights() {
    Desc d;
    d.vclName = "StaPipVU1AsIsTD";
    d.asmName = "StaPipVU1As_Is_TD";
    d.fileStem = "stapip_as_is_td_vu1";
    d.className = "StaPipAsIsTDVU1Program";
    d.programEnum = "StaPipAsIsTextureDirLights";
    d.title = "StaPip - As is - TD";
    d.texture = true;
    d.dirLights = true;
    return d;
}

Desc descAsIsTextureEnv() {
    Desc d = descAsIsTextureColor();
    d.vclName = "StaPipVU1AsIsTCE";
    d.asmName = "StaPipVU1AsIs_TCE";
    d.fileStem = "stapip_as_is_tce_vu1";
    d.className = "StaPipAsIsTCEVU1Program";
    d.programEnum = "StaPipAsIsTextureEnv";
    d.title = "StaPip - As is - TCE";
    d.env = true;
    return d;
}

/** The flat-colour CLIP program, and the four variants that share its body.
 *
 * Same relationship the cull family has with as_is: one skeleton, five material
 * classes, and the only differences are which attribute streams a vertex
 * carries and where its colour comes from. */
Desc descClipColor() {
    Desc d;
    d.vclName = "StaPipVU1ClipC";
    d.asmName = "StaPipVU1Clip_C";
    d.fileStem = "stapip_clip_c_vu1";
    d.className = "StaPipClipCVU1Program";
    d.programEnum = "StaPipClipColor";
    d.title = "StaPip - Clip - C";
    d.clip = true;
    d.dir = "clip";
    return d;
}

Desc descClipTextureColor() {
    Desc d = descClipColor();
    d.vclName = "StaPipVU1ClipTC";
    d.asmName = "StaPipVU1Clip_TC";
    d.fileStem = "stapip_clip_tc_vu1";
    d.className = "StaPipClipTCVU1Program";
    d.programEnum = "StaPipClipTextureColor";
    d.title = "StaPip - Clip - TC";
    d.texture = true;
    return d;
}

Desc descClipDirLights() {
    Desc d = descClipColor();
    d.vclName = "StaPipVU1ClipD";
    d.asmName = "StaPipVU1Clip_D";
    d.fileStem = "stapip_clip_d_vu1";
    d.className = "StaPipClipDVU1Program";
    d.programEnum = "StaPipClipDirLights";
    d.title = "StaPip - Clip - D";
    d.dirLights = true;
    return d;
}

Desc descClipTextureDirLights() {
    Desc d = descClipDirLights();
    d.vclName = "StaPipVU1ClipTD";
    d.asmName = "StaPipVU1Clip_TD";
    d.fileStem = "stapip_clip_td_vu1";
    d.className = "StaPipClipTDVU1Program";
    d.programEnum = "StaPipClipTextureDirLights";
    d.title = "StaPip - Clip - TD";
    d.texture = true;
    return d;
}

Desc descClipTextureEnv() {
    Desc d = descClipTextureColor();
    d.vclName = "StaPipVU1ClipTCE";
    d.asmName = "StaPipVU1Clip_TCE";
    d.fileStem = "stapip_clip_tce_vu1";
    d.className = "StaPipClipTCEVU1Program";
    d.programEnum = "StaPipClipTextureEnv";
    d.title = "StaPip - Clip - TCE";
    d.env = true;
    return d;
}

Desc descCullColor() {
    Desc d;
    d.vclName = "StaPipVU1CullC";
    d.asmName = "StaPipVU1Cull_C";
    d.fileStem = "stapip_cull_c_vu1";
    d.className = "StaPipCullCVU1Program";
    d.programEnum = "StaPipCullColor";
    d.title = "StaPip - Cull - C";
    d.cull = true;
    d.dir = "cull";
    return d;
}

Desc descCullTextureColor() {
    Desc d = descCullColor();
    d.vclName = "StaPipVU1CullTC";
    d.asmName = "StaPipVU1Cull_TC";
    d.fileStem = "stapip_cull_tc_vu1";
    d.className = "StaPipCullTCVU1Program";
    d.programEnum = "StaPipCullTextureColor";
    d.title = "StaPip - Cull - TC";
    d.texture = true;
    return d;
}

Desc descCullDirLights() {
    Desc d = descCullColor();
    d.vclName = "StaPipVU1CullD";
    d.asmName = "StaPipVU1Cull_D";
    d.fileStem = "stapip_cull_d_vu1";
    d.className = "StaPipCullDVU1Program";
    d.programEnum = "StaPipCullDirLights";
    d.title = "StaPip - Cull - D";
    d.dirLights = true;
    return d;
}

Desc descCullTextureDirLights() {
    Desc d = descCullDirLights();
    d.vclName = "StaPipVU1CullTD";
    d.asmName = "StaPipVU1Cull_TD";
    d.fileStem = "stapip_cull_td_vu1";
    d.className = "StaPipCullTDVU1Program";
    d.programEnum = "StaPipCullTextureDirLights";
    d.title = "StaPip - Cull - TD";
    d.texture = true;
    return d;
}

Desc descCullTextureEnv() {
    Desc d = descCullTextureColor();
    d.vclName = "StaPipVU1CullTCE";
    d.asmName = "StaPipVU1Cull_TCE";
    d.fileStem = "stapip_cull_tce_vu1";
    d.className = "StaPipCullTCEVU1Program";
    d.programEnum = "StaPipCullTextureEnv";
    d.title = "StaPip - Cull - TCE";
    d.env = true;
    return d;
}

const std::vector<unsigned>& customClasses() {
    static const std::vector<unsigned> c = {1u << 0, 1u << 1, 1u << 2,
                                            1u << 3, 1u << 4};
    return c;
}

const char* classTitle(unsigned classBit) {
    switch (classBit) {
        case 1u << 0: return "Untextured (vertex colour)";
        case 1u << 1: return "Directional lights";
        case 1u << 2: return "Textured + lights";
        case 1u << 3: return "Textured";
        case 1u << 4: return "Reflective (matcap)";
        default: return "no geometry";
    }
}

const char* halfSuffix(Half h) {
    return h == Half::AsIs ? "_ai" : h == Half::Clip ? "_cl" : "";
}
const char* halfSymbol(Half h) {
    return h == Half::AsIs ? "AI" : h == Half::Clip ? "CL" : "";
}

Desc descForClass(unsigned classBit, int lookIndex, Half half) {
    Desc d;
    const char* tag = "c";    // file stem, lowercase like every generated file
    const char* upper = "C";  // symbol stem, matching the engine's own spelling
    // Which of the three programs of this class is being described. `Cull`
    // draws a package wholly inside the frustum; the other two draw one that
    // crosses, and which of THEM is resident depends on the clipping mode.
    auto pick = [&](Desc cull, Desc asIs, Desc clip) {
        return half == Half::AsIs ? asIs : half == Half::Clip ? clip : cull;
    };
    switch (classBit) {
        case 1u << 1:
            d = pick(descCullDirLights(), descAsIsDirLights(),
                     descClipDirLights());
            tag = "d";
            upper = "D";
            break;
        case 1u << 2:
            d = pick(descCullTextureDirLights(), descAsIsTextureDirLights(),
                     descClipTextureDirLights());
            tag = "td";
            upper = "TD";
            break;
        case 1u << 3:
            d = pick(descCullTextureColor(), descAsIsTextureColor(),
                     descClipTextureColor());
            tag = "tc";
            upper = "TC";
            break;
        case 1u << 4:
            d = pick(descCullTextureEnv(), descAsIsTextureEnv(),
                     descClipTextureEnv());
            tag = "tce";
            upper = "TCE";
            break;
        default:
            d = pick(descCullColor(), descAsIsColor(), descClipColor());
            break;
    }
    d.custom = true;
    d.dir = "gen";
    // The look INDEX is in the name because several looks may target the same
    // class - only one is installed at a time, but they all have to exist in
    // the ELF for a run-time swap to be possible.
    // A material class is TWO resident programs, not one: the cull program
    // draws a package that is wholly inside the frustum, and its twin draws
    // one that crosses a plane. Overriding only the cull half is what makes a
    // look look broken - the props at the edge of the screen keep the engine's
    // shading - so a look emits both.
    const std::string ix = std::to_string(lookIndex);
    d.fileStem = std::string("vu_look") + ix + "_" + tag + halfSuffix(half);
    d.vclName = std::string("TyraXLook") + ix + upper + halfSymbol(half);
    d.asmName = d.vclName;
    d.className = d.vclName + "VU1Program";
    d.title = std::string("TyraX look - ") + classTitle(classBit);
    return d;
}

std::vector<Desc> allDescs() {
    return {descAsIsColor(),            descAsIsTextureColor(),
            descAsIsDirLights(),        descAsIsTextureDirLights(),
            descAsIsTextureEnv(),       descCullColor(),
            descCullTextureColor(),     descCullDirLights(),
            descCullTextureDirLights(), descCullTextureEnv(),
            descClipColor(),            descClipTextureColor(),
            descClipDirLights(),        descClipTextureDirLights(),
            descClipTextureEnv()};
}

// ---------------------------------------------------------------------------
// The as_is skeleton
// ---------------------------------------------------------------------------

namespace {

/** GS registers a vertex occupies, i.e. the store stride in quadwords. */
int regsPerVertex(const Desc& d) { return d.texture ? 3 : 2; }

/** The GIF tag block the program emits ahead of the vertices. Texture programs
 * carry two more (set, A+D) pairs - LOD/CLUT - than plain ones. */
int tagQuads(const Desc& d) { return d.texture ? 9 : 7; }

/** Per-vertex input arrays the EE unpacks: positions, then ST when textured,
 * then normals (lit) or colors. */
/** One per-vertex input array the EE unpacks into VU1 memory, in the order the
 * blocks sit in memory. */
struct AttrBlock {
    const char* comment;   // what the emitted EE code calls it
    const char* member;    // the StaPipQBuffer member holding it
    bool singleColorOpt;   // skipped when the mesh draws in one flat colour
};

/** THE memory layout of the per-vertex input, and the only place it is written
 * down. Three things have to agree on it and used to each spell it out
 * separately: the microprogram's pointer arithmetic in `buildAsIsBody`
 * (`stqData = vertexData + vertexCount`, and so on), the equivalence harness in
 * `stageInput`, and the EE-side `addProgramQBufferDataToPacket` this file emits.
 * They drifted: the emitted `c` put colours one block too far and the emitted
 * `td` put normals ON TOP of the ST block, neither of which `--vu-check` can see
 * because it stages memory itself and only diffs the microprogram's GS output.
 * Derive from this list, never restate it. */
std::vector<AttrBlock> attrBlocks(const Desc& d) {
    std::vector<AttrBlock> b;
    b.push_back({"Vertices", "vertices", false});
    if (d.texture)
        b.push_back({d.env ? "Normals - they ride in the ST slot (StaPipTextureBag)"
                           : "ST",
                     "sts", false});
    // The last block is normals for the lit variants and colours otherwise -
    // never both. The lit programs stage their GIF packet directly after the
    // normals (`kickAddress = normalData + vertexCount`), so they never read a
    // colour stream and unpacking one only burns DMA bandwidth.
    if (d.dirLights)
        b.push_back({"Normals", "normals", false});
    else
        b.push_back({"Colors - absent when the mesh draws in a single colour",
                     "colors", true});
    return b;
}

int attrStreams(const Desc& d) { return (int)attrBlocks(d).size(); }

// ---------------------------------------------------------------------------
// Weaving a stage list into the skeleton
// ---------------------------------------------------------------------------

/** Literals a stage list needs, packed four to a register.
 *
 * A `loi` costs an instruction per USE, which for a value read three times a
 * vertex (once per corner) is nine instructions of pure constant loading. Four
 * literals in one register cost eight instructions ONCE, in the preamble, and
 * every read afterwards is a free broadcast field. */
struct LitPool {
    std::vector<float> values;
    std::vector<Val> regs;
    int index(float v) {
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i] == v) return (int)i;
        values.push_back(v);
        return (int)values.size() - 1;
    }
    /** Emits the build sequence. Call once, in the preamble, AFTER every
     * literal has been registered. */
    void emit(Vu& b) {
        const int groups = ((int)values.size() + 3) / 4;
        for (int g = 0; g < groups; ++g) {
            char name[32];
            std::snprintf(name, sizeof name, "stageK%d", g);
            const Val r = b.named(name);
            regs.push_back(r);
            float v[4] = {0, 0, 0, 0};
            for (int f = 0; f < 4; ++f)
                if ((size_t)(g * 4 + f) < values.size()) v[f] = values[g * 4 + f];
            b.constants(r, v[0], v[1], v[2], v[3]);
        }
    }
    Val read(int idx) const {
        return regs[idx / 4].broadcast(idx % 4);
    }
};

/** Everything a stage emitter is allowed to touch. Registers are the CALLER's
 * throughout - a stage that mints its own would inflate the VF pressure VCL
 * has to allocate against, and that pressure is invisible on the host (see the
 * fogCoefficient note). */
struct StageCtx {
    Vu* b = nullptr;
    Program* p = nullptr;
    Val vertex{};    // the position, in whatever space the slot says
    Val color{};
    Val st{};
    Val params{};    // the per-mesh quadword
    Val timeV{};     // (time, sin time, cos time, 1.0)
    Val kc{};        // (1/2pi, 0.5, 2^23, 0.225) - sineApprox's constants
    Val one{};       // 1.0 in every field
    Val lumaW{};     // (0.299, 0.587, 0.114, 0) - only built when needed
    Val normal{};    // the lit classes' object-space normal, for a script
    Val s[6];        // stage scratch
    int scriptPreambleAt = 0;  // where a script hoists its constants
    bool scriptWroteQ = false;  // a script must not - see runScript
    Val sinS[3];     // sineApprox scratch, never aliased with s[]
    const LitPool* lits = nullptr;
    bool hasColor = false;
    bool hasSt = false;
};

/** One stage with its parameters already turned into readable operands. */
struct Resolved {
    const StageDef* def = nullptr;
    Stage stage;
    Val param[4];
    float literal[4] = {0, 0, 0, 0};
    bool isLiteral[4] = {false, false, false, false};
    int litIndex[4] = {-1, -1, -1, -1};   // into LitPool, when isLiteral
    int extraLit[4] = {-1, -1, -1, -1};   // stage-private derived literals
};

/** True when the parameter is a literal ZERO, which several stages use to drop
 * their expensive half (a Pulse of 0 needs no sine at all). */
bool literalZero(const Resolved& r, int i) {
    return r.isLiteral[i] && r.literal[i] == 0.0f;
}

void emitStage(StageCtx& c, const Resolved& r) {
    Vu& b = *c.b;
    const std::string key = r.stage.key;
    const Val* s = c.s;
    const Val v = c.vertex;
    auto P = [&](int i) { return r.param[i]; };

    if (key == "wobble") {
        // angle = (v.x + v.z) * frequency + time * speed
        b.addInto(s[0], v, v.broadcast(2), MX);
        c.p->code.back().comment = "Wobble";
        b.mulInto(s[0], s[0], P(1), MX);
        b.mulInto(s[1], c.timeV, P(2), MX);
        b.addInto(s[0], s[0], s[1], MX);
        b.sineApprox(s[2], s[0], MX, c.kc, c.one, c.sinS, c.kc.broadcast(1));
        b.mulInto(s[2], s[2], P(0), MX);
        b.addInto(v, v, s[2].broadcast(0), MY);
        return;
    }
    if (key == "twist") {
        // angle = v.y * strength + time * speed, then rotate X/Z by it.
        b.mulInto(s[0], v, P(0), MY);
        c.p->code.back().comment = "Twist";
        b.mulInto(s[1], c.timeV, P(1), MX);
        b.addInto(s[0], s[1], s[0].broadcast(1), MX);
        b.sineApprox(s[1], s[0], MX, c.kc, c.one, c.sinS, c.kc.broadcast(1));
        // The cosine is the same series a QUARTER TURN further on, phased
        // inside the reduction rather than by adding pi/2 to the angle - see
        // sineApprox. That is what keeps a twist of zero the exact identity.
        b.sineApprox(s[2], s[0], MX, c.kc, c.one, c.sinS,
                     c.lits->read(r.extraLit[0]));
        b.mulInto(s[3], v, s[2].broadcast(0), MX);   // x*cos
        b.mulInto(s[3], v, s[1].broadcast(0), MZ);   // z*sin
        b.subInto(s[4], s[3], s[3].broadcast(2), MX);  // x' = x cos - z sin
        b.mulInto(s[5], v, s[1].broadcast(0), MX);   // x*sin
        b.mulInto(s[5], v, s[2].broadcast(0), MZ);   // z*cos
        b.addInto(s[4], s[5], s[5].broadcast(0), MZ);  // z' = z cos + x sin
        b.addInto(v, c.b->zero(), s[4].broadcast(0), MX);
        b.addInto(v, c.b->zero(), s[4].broadcast(2), MZ);
        return;
    }
    if (key == "inflate") {
        if (literalZero(r, 1)) {
            // No breathing: the sine is not generated at all.
            b.mulInto(s[2], v, P(0), MXYZ);
            c.p->code.back().comment = "Inflate (constant)";
            b.addInto(v, v, s[2], MXYZ);
            return;
        }
        b.mulInto(s[0], c.timeV, P(2), MX);
        c.p->code.back().comment = "Inflate";
        b.sineApprox(s[1], s[0], MX, c.kc, c.one, c.sinS, c.kc.broadcast(1));
        b.mulInto(s[1], s[1], P(1), MX);
        b.addInto(s[1], s[1], P(0), MX);
        b.mulInto(s[2], v, s[1].broadcast(0), MXYZ);
        b.addInto(v, v, s[2], MXYZ);
        return;
    }
    if (key == "squash") {
        b.addInto(s[0], c.one, P(0), MX);
        c.p->code.back().comment = "Squash / stretch";
        b.addInto(s[0], c.one, P(1), MY);
        b.addInto(s[0], c.one, P(2), MZ);
        b.mulInto(v, v, s[0], MXYZ);
        return;
    }
    if (key == "heightShade") {
        b.mulInto(s[0], v, P(1), MY);
        c.p->code.back().comment = "Height shade";
        b.addInto(s[0], s[0], P(2), MY);
        b.minimumInto(s[0], s[0], c.one, MY);
        b.maximumInto(s[0], s[0], c.lits->read(r.extraLit[0]), MY);  // -1
        b.mulInto(s[0], s[0], P(0), MY);
        b.addInto(s[0], c.one, s[0].broadcast(1), MX);
        b.mulInto(c.color, c.color, s[0].broadcast(0), MXYZ);
        return;
    }
    if (key == "zbias") {
        b.mulInto(s[0], v, P(0), MW);
        c.p->code.back().comment = "Depth bias";
        b.addInto(v, v, s[0].broadcast(3), MZ);
        return;
    }
    if (key == "snap") {
        const uint8_t xy = (uint8_t)(MX | MY);
        b.mulInto(s[0], v, c.lits->read(r.extraLit[0]), xy);  // * steps
        c.p->code.back().comment = "Vertex snap";
        b.truncate(s[0], s[0], xy);
        b.mulInto(s[0], s[0], c.lits->read(r.extraLit[1]), xy);  // * 1/steps
        b.subInto(s[0], s[0], v, xy);
        b.mulInto(s[0], s[0], P(1), xy);
        b.addInto(v, v, s[0], xy);
        return;
    }
    if (key == "pulse") {
        b.mulInto(s[0], c.timeV, P(1), MX);
        c.p->code.back().comment = "Pulse colour";
        b.sineApprox(s[1], s[0], MX, c.kc, c.one, c.sinS, c.kc.broadcast(1));
        b.mulInto(s[1], s[1], P(0), MX);
        b.addInto(s[1], c.one, s[1].broadcast(0), MX);
        b.mulInto(c.color, c.color, s[1].broadcast(0), MXYZ);
        return;
    }
    if (key == "posterize") {
        b.mulInto(s[0], c.color, c.lits->read(r.extraLit[0]), MXYZ);
        c.p->code.back().comment = "Posterize";
        b.truncate(s[0], s[0], MXYZ);
        b.mulInto(s[0], s[0], c.lits->read(r.extraLit[1]), MXYZ);
        b.subInto(s[0], s[0], c.color, MXYZ);
        b.mulInto(s[0], s[0], P(1), MXYZ);
        b.addInto(c.color, c.color, s[0], MXYZ);
        return;
    }
    if (key == "desaturate") {
        b.mulInto(s[0], c.color, c.lumaW, MXYZ);
        c.p->code.back().comment = "Desaturate";
        b.addInto(s[0], s[0], s[0].broadcast(1), MX);
        b.addInto(s[0], s[0], s[0].broadcast(2), MX);  // s0.x = luminance
        b.subInto(s[1], c.color, s[0].broadcast(0), MXYZ);
        b.mulInto(s[1], s[1], P(0), MXYZ);
        b.subInto(c.color, c.color, s[1], MXYZ);
        return;
    }
    if (key == "scrollUv") {
        // The clock has to be BROADCAST into a register first: a VU broadcast
        // is only legal on the second operand, and timeV.y is sin(t), not t.
        b.addInto(s[0], b.zero(), c.timeV.broadcast(0), MXYZ);
        c.p->code.back().comment = "Scroll UV";
        b.mulInto(s[1], s[0], P(0), MX);
        b.mulInto(s[1], s[0], P(1), MY);
        b.addInto(c.st, c.st, s[1], (uint8_t)(MX | MY));
        return;
    }
}

/** Turns an authored stage list into emitter-ready form, registering every
 * literal it needs (including the DERIVED ones - a reciprocal folded on the
 * host, a level count turned into two scale factors) in the pool. */
struct StagePlan {
    std::vector<Resolved> stages;
    /** Set when a project's script emitted a Q write - see runScript. */
    bool scriptWroteQ = false;
    LitPool lits;
    bool needsTime = false;
    bool needsParams = false;
    bool needsLuma = false;
    bool needsSine = false;
    /** The largest `StageCtx::s[]` any planned stage touches. Allocating the
     * catalogue's maximum instead would cost three spare VF registers on every
     * program, and VCL only has 31 - which is a real difference, not a tidiness
     * one: a four-stage program that overshoots gets `no opt table` from vcl
     * inside Docker with no line number. */
    int scratch = 0;
    std::vector<std::string> errors;
    std::vector<std::string> dropped;  // key + why, for the panel and notes
};

StagePlan planStages(const std::vector<Stage>& stages, const Desc& d) {
    StagePlan plan;
    for (const Stage& s : stages) {
        const StageDef* def = stageDef(s.key);
        if (!def) {
            // Unknown key: dropped, never guessed at. Same rule as a retired
            // flow-graph node - a project written by a newer editor must not
            // silently get a different program.
            plan.dropped.push_back(s.key + " (no such stage)");
            continue;
        }
        if (!s.enabled) {
            plan.dropped.push_back(std::string(def->title) + " (disabled)");
            continue;
        }
        if (stageIsNoOp(s)) {
            plan.dropped.push_back(std::string(def->title) +
                                   " (every strength is a literal zero)");
            continue;
        }
        // A LOOK spans several material classes, and not every stage can run
        // on every one of them - a UV scroll has no UV on untextured geometry.
        // That is a DROP with a stated reason, not an error: refusing the whole
        // look would mean a scene-wide treatment could never include a stage
        // that only some classes can carry, which is most of them.
        if (def->needsTexture && !d.texture) {
            plan.dropped.push_back(std::string(def->title) +
                                   " (no ST stream on this class)");
            continue;
        }
        if (def->slot == Slot::Texture && d.env) {
            plan.dropped.push_back(
                std::string(def->title) +
                " (the ST slot carries an object-space normal on a reflective "
                "class, not a texture coordinate)");
            continue;
        }
        Resolved r;
        r.def = def;
        r.stage = s;
        for (int i = 0; i < def->paramCount; ++i) {
            const StageParam& sp = s.params[i];
            const bool literal = sp.meshSlot < 0 || def->params[i].literalOnly;
            if (sp.meshSlot >= 0 && def->params[i].literalOnly)
                plan.errors.push_back(std::string(def->title) + ": " +
                                      def->params[i].name +
                                      " is baked into the microprogram and "
                                      "cannot be bound to a mesh slot");
            r.isLiteral[i] = literal;
            r.literal[i] = sp.value;
            if (literal)
                r.litIndex[i] = plan.lits.index(sp.value);
            else
                plan.needsParams = true;
        }
        // Stage-private derived literals.
        // 0.75 turns = the cosine phase (see Vu::sineApprox).
        if (s.key == "twist") r.extraLit[0] = plan.lits.index(0.75f);
        if (s.key == "heightShade") r.extraLit[0] = plan.lits.index(-1.0f);
        if (s.key == "snap") {
            float steps = s.params[0].value;
            if (!(steps >= 1.0f)) steps = 1.0f;
            r.extraLit[0] = plan.lits.index(steps);
            r.extraLit[1] = plan.lits.index(1.0f / steps);
        }
        if (s.key == "posterize") {
            float lv = s.params[0].value;
            if (!(lv >= 2.0f)) lv = 2.0f;
            r.extraLit[0] = plan.lits.index(lv / 255.0f);
            r.extraLit[1] = plan.lits.index(255.0f / lv);
        }
        if (s.key == "desaturate") plan.needsLuma = true;
        if (def->needsTime) plan.needsTime = true;
        if (def->perVertex >= 20) plan.needsSine = true;
        if (def->scratch > plan.scratch) plan.scratch = def->scratch;
        plan.stages.push_back(r);
    }
    return plan;
}

/** Second half of the resolve, and it cannot happen earlier: the literal pool's
 * registers only exist after `LitPool::emit`, and the per-mesh parameter
 * register only after the preamble has loaded it. */
void bindOperands(StagePlan& plan, Val params) {
    for (Resolved& r : plan.stages)
        for (int i = 0; i < r.def->paramCount; ++i)
            r.param[i] = r.isLiteral[i]
                             ? plan.lits.read(r.litIndex[i])
                             : params.broadcast(r.stage.params[i].meshSlot);
}

/** Hand the project's own script the registers the built-in stages get. The
 * ScriptCtx is a NARROW copy on purpose: StageCtx carries the sine scratch and
 * the literal pool, which are bookkeeping for the catalogue and would only be
 * traps in a script.
 *
 * Takes the FUNCTION rather than the Desc because a VU0 kernel body is woven in
 * by exactly the same rules and has no Desc at all (see buildKernelBody). */
void runScriptBody(StageCtx& c, ScriptFn fn) {
    ScriptCtx sc;
    sc.b = c.b;
    sc.preambleAt = c.scriptPreambleAt;
    sc.position = c.vertex;
    sc.color = c.color;
    sc.st = c.st;
    sc.params = c.params;
    sc.time = c.timeV;
    sc.one = c.one;
    for (int i = 0; i < 8 && i < 6; ++i) sc.scratch[i] = c.s[i];
    sc.normal = c.normal;
    sc.hasColor = c.hasColor;
    sc.hasSt = c.hasSt;
    sc.hasNormal = c.normal.reg != 0;
    const int before = c.b->mark();
    fn(sc);
    // A SCRIPT MUST NOT WRITE Q.
    //
    // Q carries the perspective divide - persCorrect puts 1/w there and the
    // position and the ST both ride on it - and it has a latency the assembler
    // schedules around. A div or an rsqrt emitted from a script body is
    // independent in vcl's dataflow view, so it gets moved into that window and
    // the vertex comes out at the wrong place. On the console that reads as
    // grey stippled patches and shadows fighting for z, on a program that looks
    // arithmetically perfect - and the host simulator cannot show it, because it
    // runs in order and models no latency at all. Measured: removing exactly
    // this divide from examples/vu-lab's script cleaned the frame.
    for (int i = before; i < (int)c.p->code.size(); ++i) {
        const Op op = c.p->code[i].op;
        if (op == Op::Div || op == Op::Rsqrt || op == Op::Sqrt) {
            c.scriptWroteQ = true;
            break;
        }
    }
    // Constants the script hoisted moved everything after them along, so the
    // next vertex has to start from where this one left off.
    c.scriptPreambleAt = sc.preambleAt;
}

void runScript(StageCtx& c, const Desc& d) { runScriptBody(c, d.script); }

void applyStages(StageCtx& c, const StagePlan& plan, Slot slot) {
    for (const Resolved& r : plan.stages)
        if (r.def->slot == slot) emitStage(c, r);
}

// ---------------------------------------------------------------------------
// The preamble, shared by every family
// ---------------------------------------------------------------------------
//
// Three skeletons now load the same per-mesh constants and stage the same GIF
// tag block: as_is, cull and clip. Copying that block a third time is how the
// twenty handwritten programs drifted in the first place - hardware fog, the
// in-band ALPHA tag and the spot light each had to be threaded through every
// one of them by hand - so it is written ONCE here and the families differ only
// where they genuinely differ.

/** `lq dst, offset(base)` into a NAMED register. Vu::lq mints a fresh
 * temporary, which is right for an expression and wrong for a per-mesh
 * constant: the emitted source is read on hardware, so those want the names the
 * handwritten programs use. */
void loadInto(Program& prog, Val dst, IVal base, int offset,
              uint8_t mask = MALL, const char* comment = nullptr) {
    Instr in;
    in.op = Op::Lq;
    in.dst = dst.reg;
    in.base = base.reg;
    in.imm = offset;
    in.mask = mask;
    if (comment) in.comment = comment;
    prog.code.push_back(in);
}

/** The same off vi00 - a per-mesh constant at a fixed data address. */
void loadK(Program& prog, Val dst, int address, uint8_t mask = MALL,
           const char* comment = nullptr) {
    loadInto(prog, dst, IVal{vuir::kVi00}, address, mask, comment);
}

/** `ilw.<field> dst, offset(base)` into a NAMED integer register. */
void ilwInto(Program& prog, IVal dst, IVal base, int offset, int field,
             const char* comment = nullptr) {
    Instr in;
    in.op = Op::Ilw;
    in.dst = dst.reg;
    in.base = base.reg;
    in.imm = offset;
    in.mask = (uint8_t)(1 << field);
    if (comment) in.comment = comment;
    prog.code.push_back(in);
}

/** Every per-mesh constant a StaPip program can load. */
struct Constants {
    Val gifSetTag{}, lodGifTag{}, testsTag{}, clutTag{}, alphaTag{}, fogParams{};
    Val singleColor{}, mvp[4]{};
    Val lightMatrix[3]{}, lightDirs[3]{}, lightColors[3]{}, ambient{};
    Val spotPos{}, spotDirV{}, spotColV{};
    Val envBasisX{}, envBasisY{}, envBasisZ{};
    IVal singleColorEnabled{}, adcMask{};
};

/** The flashlight runs where there is a per-vertex COLOUR to add it to and the
 * object-space position is still in hand: the cull and clip families, on the
 * unlit non-env classes. A lit program computes its colour from normals further
 * down and an env bag carries no lighting at all. */
bool usesSpotLight(const Desc& d) {
    return (d.cull || d.clip) && !d.dirLights && !d.env;
}

/** LoadTyraDirectionalLights: the rotation part of the world matrix, three
 * light directions, three light colours and the ambient term.
 *
 * A function and not just preamble code because the CLIP family loads these
 * INSIDE its triangle loop. Ten registers held live across the
 * Sutherland-Hodgman block is more than VU1 has to spare, so clip_d and clip_td
 * pay the reload once per triangle and get the clipper's registers back. */
void loadDirLights(Vu& b, Program& prog, Constants& k) {
    static const char* mn[3] = {"lightMatrix[0]", "lightMatrix[1]",
                                "lightMatrix[2]"};
    static const char* dn[3] = {"lightDirections[0]", "lightDirections[1]",
                                "lightDirections[2]"};
    static const char* cn[3] = {"lightColors[0]", "lightColors[1]",
                                "lightColors[2]"};
    for (int i = 0; i < 3; ++i) {
        k.lightMatrix[i] = b.named(mn[i]);
        loadInto(prog, k.lightMatrix[i], b.izero(), kLightsMatrixAddr + i, MXYZ,
                 i == 0 ? "LoadTyraDirectionalLights" : nullptr);
    }
    for (int i = 0; i < 3; ++i) {
        k.lightDirs[i] = b.named(dn[i]);
        loadInto(prog, k.lightDirs[i], b.izero(), kLightsDirsAddr + i, MXYZ);
    }
    for (int i = 0; i < 3; ++i) {
        k.lightColors[i] = b.named(cn[i]);
        loadInto(prog, k.lightColors[i], b.izero(), kLightsColorsAddr + i, MXYZ);
    }
    k.ambient = b.named("ambientColor");
    loadInto(prog, k.ambient, b.izero(), kLightsColorsAddr + 3, MXYZ);
}

/** Everything that runs once per BUFFER before the first vertex is touched. */
void emitPreamble(Vu& b, Program& prog, const Desc& d, Constants& k) {
    const bool colorStream = !d.dirLights;
    // The families that judge a vertex against the frustum on VU1 start from a
    // cleared clip-flag shift register.
    if (d.cull || d.clip) b.resetClipFlags();
    k.gifSetTag = b.named("gifSetTag");
    loadK(prog, k.gifSetTag, kSetGifTagAddr, MALL, "VU1_SET_GIFTAG_ADDR");

    // as_is is fed NDC by the EE clipper; cull and clip transform on VU1.
    if (d.cull || d.clip) {
        static const char* mn[4] = {"mvp[0]", "mvp[1]", "mvp[2]", "mvp[3]"};
        for (int i = 0; i < 4; ++i) {
            k.mvp[i] = b.named(mn[i]);
            loadK(prog, k.mvp[i], kMvpMatrixAddr + i, MALL,
                  i == 0 ? "MatrixLoad - VU1_MVP_MATRIX_ADDR" : nullptr);
        }
    }

    if (colorStream) {
        k.singleColor = b.named("singleColor");
        loadK(prog, k.singleColor, kSingleColorAddr, MALL,
              "VU1_SINGLE_COLOR_ADDR");
        // The CLIP family deliberately does NOT keep the flag in an integer
        // register: its inner loops need every one of the sixteen, so it pays
        // an ilw per triangle instead (the handwritten programs say so too).
        if (!d.clip) {
            k.singleColorEnabled = b.inamed("singleColorEnabled");
            ilwInto(prog, k.singleColorEnabled, b.izero(), kOptionsAddr, 0,
                    "VU1_OPTIONS_ADDR.x = single colour flag");
        }
    } else if (!d.clip) {
        // Same story: clip_d and clip_td reload these per triangle.
        loadDirLights(b, prog, k);
    }

    k.lodGifTag = b.named("lodGifTag");
    loadK(prog, k.lodGifTag, kLodAddr, MALL, "VU1_LOD_ADDR");
    k.testsTag = b.named("testsTag");
    loadK(prog, k.testsTag, kZTestsAddr, MALL, "VU1_Z_TESTS_ADDR");
    if (d.texture) {
        k.clutTag = b.named("texBufferClutGifTag");
        loadK(prog, k.clutTag, kClutAddr, MALL, "VU1_CLUT_ADDR");
    }
    k.alphaTag = b.named("alphaGifTag");
    loadK(prog, k.alphaTag, kAlphaAddr, MALL,
          "VU1_ALPHA_ADDR - per-mesh GS blend equation, in-band");
    k.fogParams = b.named("fogParams");
    loadK(prog, k.fogParams, kOptionsAddr, MALL,
          "VU1_OPTIONS_ADDR.zw = GS hardware fog");

    // The ADC mask is the cull family's alone: a clip program never derives an
    // ADC bit from clip flags (the historically fragile path) - it cuts the
    // triangle instead and every emitted vertex stores ADC = 0.
    k.adcMask = b.inamed("adcMask");
    if (d.cull) b.makeAdcMask(k.adcMask);

    if (usesSpotLight(d)) {
        static const char* sn[3] = {"spotPos", "spotDirV", "spotColV"};
        Val* slot[3] = {&k.spotPos, &k.spotDirV, &k.spotColV};
        for (int i = 0; i < 3; ++i) {
            *slot[i] = b.named(sn[i]);
            loadK(prog, *slot[i], kLightsDirsAddr + i, MALL,
                  i == 0 ? "LoadTyraSpotLight - reuses the dir-lights slots, "
                           "free in the colour programs"
                         : nullptr);
        }
    }

    if (d.env) {
        static const char* en[3] = {"envBasisX", "envBasisY", "envBasisZ"};
        Val* slot[3] = {&k.envBasisX, &k.envBasisY, &k.envBasisZ};
        for (int i = 0; i < 3; ++i) {
            *slot[i] = b.named(en[i]);
            loadK(prog, *slot[i], kEnvBasisAddr + i, MALL,
                  i == 0 ? "LoadTyraEnvBasis - VU1_ENV_BASIS_ADDR (the lights "
                           "matrix area; env bags carry no lighting)"
                         : nullptr);
        }
    }
}

/** The registers a stage list or a script reads. */
struct StageConstants {
    Val params{}, timeV{}, kc{}, one{}, luma{};
};

/** A stage list's own constants - still preamble, so they cost a handful of
 * slots once per buffer and are read as broadcasts for the rest of the
 * program. */
void emitStageConstants(Vu& b, Program& prog, StagePlan& plan, bool hasStages,
                        StageConstants& s) {
    if (!hasStages) return;
    if (plan.needsParams) {
        s.params = b.named("vuParams");
        loadK(prog, s.params, kCustomParamsAddr, MALL,
              "VU1_CUSTOM_PARAMS_ADDR - the mesh's four numbers (the "
              "lights-colour area; a bag carrying these has no lighting)");
    }
    if (plan.needsTime) {
        s.timeV = b.named("vuTime");
        loadK(prog, s.timeV, kCustomTimeAddr, MALL,
              "VU1_CUSTOM_TIME_ADDR - (time, sin time, cos time, 1.0)");
    }
    s.one = b.named("vuOne");
    b.onesInto(s.one);
    if (plan.needsSine) {
        s.kc = b.named("vuSinK");
        b.constants(s.kc, 0.15915494f, 0.5f, 8388608.0f, 0.225f);
        prog.code[prog.code.size() - 8].comment =
            "sineApprox constants: 1/2pi, 0.5, 2^23 (the floor trick), 0.225";
    }
    if (plan.needsLuma) {
        s.luma = b.named("vuLuma");
        b.constants(s.luma, 0.299f, 0.587f, 0.114f, 0.0f);
        prog.code[prog.code.size() - 8].comment = "luminance weights";
    }
    plan.lits.emit(b);
    bindOperands(plan, s.params);
}

/** What every family reads out of the double buffer it was just handed. */
struct BufferHeader {
    IVal buffer{}, vertexData{}, vertexCount{};
    Val scale{}, primTag{};
};

BufferHeader emitBufferHeader(Vu& b, Program& prog) {
    BufferHeader h;
    h.buffer = b.inamed("buffer");
    b.xtop(h.buffer);
    h.scale = b.named("scale");
    loadInto(prog, h.scale, h.buffer, 0, MXYZ, "LoadTyraBufferTags");
    h.primTag = b.named("primTag");
    loadInto(prog, h.primTag, h.buffer, 1);
    h.vertexData = b.inamed("vertexData");
    b.iaddiuInto(h.vertexData, h.buffer, kVertDataAddr);
    prog.code.back().comment = "VU1_STAPIP_VERT_DATA_ADDR";
    h.vertexCount = b.inamed("vertexCount");
    ilwInto(prog, h.vertexCount, h.buffer, 0, 3);
    return h;
}

/** StoreTyraGifTagsAlpha (or its texture form): the GIF tag block, with
 * `destAddress` advanced past it. */
void emitTagBlock(Vu& b, Program& prog, const Desc& d, const Constants& k,
                  Val primTag, IVal destAddress) {
    int q = 0;
    b.sq(k.gifSetTag, destAddress, q++);
    prog.code.back().comment = "GIF tag block";
    b.sq(k.testsTag, destAddress, q++);
    b.sq(k.gifSetTag, destAddress, q++);
    b.sq(k.lodGifTag, destAddress, q++);
    if (d.texture) {
        b.sq(k.gifSetTag, destAddress, q++);
        b.sq(k.clutTag, destAddress, q++);
    }
    b.sq(k.gifSetTag, destAddress, q++);
    b.sq(k.alphaTag, destAddress, q++);
    b.sq(primTag, destAddress, q++);
    b.iaddiuInto(destAddress, destAddress, q);
}

void buildAsIsBody(const Desc& d, Program& prog, StagePlan* planOut = nullptr) {
    Vu b(prog);
    const int stride = regsPerVertex(d);
    const int stq = 0, rgba = d.texture ? 1 : 0, xyz = d.texture ? 2 : 1;

    // A project's own stage list. Planned BEFORE anything is emitted, because
    // the preamble has to know which constant registers to build.
    StagePlan plan = planStages(d.stages, d);
    // A project's own C++ script rides the SAME preamble as the stage list: it
    // reads the per-mesh quadword and the clock from the same registers, so
    // asking for them here is what makes a script and a stage interchangeable
    // from the emitter's point of view.
    if (d.script) {
        plan.needsParams = true;
        plan.needsTime = true;
        if (plan.scratch < d.scriptScratch) plan.scratch = d.scriptScratch;
    }
    const bool hasStages = !plan.stages.empty() || d.script != nullptr;

    // --- preamble: the per-mesh constants ---------------------------------
    //
    // Shared with the cull and clip families - see emitPreamble. A third copy
    // of this block is how the handwritten programs drifted.
    const bool colorStream = !d.dirLights;
    const bool spot = usesSpotLight(d);
    Constants k;
    emitPreamble(b, prog, d, k);

    // --- the stage list's own constants -------------------------------------
    //
    // Still preamble: it runs once per BUFFER, not once per vertex, so a stage
    // list pays for its constants at the cost of a handful of slots and then
    // reads them as broadcasts for the rest of the program.
    StageConstants sk;
    emitStageConstants(b, prog, plan, hasStages, sk);

    // --- per-buffer ---------------------------------------------------------
    const Lbl begin = b.label("begin");
    b.bind(begin);
    const BufferHeader hdr = emitBufferHeader(b, prog);
    const IVal buffer = hdr.buffer;
    const IVal vertexData = hdr.vertexData;
    const IVal vertexCount = hdr.vertexCount;
    const Val scale = hdr.scale;

    IVal stqData{}, colorData{}, normalData{};
    const IVal kickAddress = b.inamed("kickAddress");
    const IVal destAddress = b.inamed("destAddress");
    if (d.texture) {
        stqData = b.inamed("stqData");
        b.iaddInto(stqData, vertexData, vertexCount);
    }
    if (colorStream) {
        colorData = b.inamed("colorData");
        b.iaddInto(colorData, d.texture ? stqData : vertexData, vertexCount);
        // Where the output starts depends on whether the colour array is there
        // at all: with a single colour the EE simply does not upload it.
        const Lbl multi = b.label("setDestAddrMultiColor");
        const Lbl done = b.label("setDestAddr");
        b.branchIfLez(k.singleColorEnabled, multi);
        b.iaddInto(kickAddress, d.texture ? stqData : vertexData, vertexCount);
        b.branch(done);
        b.bind(multi);
        b.iaddInto(kickAddress, colorData, vertexCount);
        b.bind(done);
        b.iaddiuInto(destAddress, kickAddress, 0);
    } else {
        normalData = b.inamed("normalData");
        b.iaddInto(normalData, d.texture ? stqData : vertexData, vertexCount);
        b.iaddInto(kickAddress, normalData, vertexCount);
        b.iaddInto(destAddress, normalData, vertexCount);
    }

    // --- the GIF tag block --------------------------------------------------
    emitTagBlock(b, prog, d, k, hdr.primTag, destAddress);

    // --- the vertex loop ----------------------------------------------------
    const IVal vertexCounter = b.inamed("vertexCounter");
    b.iaddInto(vertexCounter, buffer, vertexCount);
    const Lbl vertexLoop = b.label("vertexLoop");
    b.bind(vertexLoop);

    Val color[3], vertex[3], st[3], outStq[3], normal[3];
    static const char* colorNames[3] = {"color1", "color2", "color3"};
    static const char* litNames[3] = {"outputColor1", "outputColor2",
                                      "outputColor3"};
    static const char* vertexNames[3] = {"vertex1", "vertex2", "vertex3"};
    static const char* stNames[3] = {"stq1", "stq2", "stq3"};
    static const char* outStqNames[3] = {"outputStq1", "outputStq2", "outputStq3"};
    static const char* normalNames[3] = {"normal1", "normal2", "normal3"};

    for (int i = 0; i < 3; ++i) {
        color[i] = b.named(colorStream ? colorNames[i] : litNames[i]);
        vertex[i] = b.named(vertexNames[i]);
        if (d.texture) {
            st[i] = b.named(stNames[i]);
            outStq[i] = b.named(outStqNames[i]);
        }
        if (!colorStream) normal[i] = b.named(normalNames[i]);
    }

    if (colorStream) {
        const Lbl multiColor = b.label("multiColor");
        const Lbl processing = b.label("processing");
        b.branchIfLez(k.singleColorEnabled, multiColor);
        for (int i = 0; i < 3; ++i) b.addInto(color[i], b.zero(), k.singleColor);
        b.branch(processing);
        b.bind(multiColor);
        for (int i = 0; i < 3; ++i) {
            Instr in;
            in.op = Op::Lq;
            in.dst = color[i].reg;
            in.base = colorData.reg;
            in.imm = i;
            prog.code.push_back(in);
        }
        b.bind(processing);
    }

    for (int i = 0; i < 3; ++i) {
        prog.code.push_back([&] {
            Instr in;
            in.op = Op::Lq;
            in.dst = vertex[i].reg;
            in.base = vertexData.reg;
            in.imm = i;
            if (i == 0) in.comment = "W carries the clip-space distance (fog)";
            return in;
        }());
        if (d.texture) {
            Instr in;
            in.op = Op::Lq;
            in.dst = st[i].reg;
            in.base = stqData.reg;
            in.imm = i;
            prog.code.push_back(in);
        }
        if (!colorStream) {
            Instr in;
            in.op = Op::Lq;
            in.dst = normal[i].reg;
            in.base = normalData.reg;
            in.imm = i;
            in.mask = MXYZ;
            prog.code.push_back(in);
        }
    }

    Val envScratch[2];
    if (d.env) {
        static const char* sn[2] = {"tyraEnvLen", "tyraEnvNrm"};
        for (int i = 0; i < 2; ++i) envScratch[i] = b.named(sn[i]);
    }

    // The cull family does its whole per-vertex chain inline, including the fog
    // word - its clip check and the fog coefficient share one store, so there is
    // no separate fog pass afterwards the way as_is has.
    Val spotScratch[7];
    const IVal cullFogInt = b.inamed("fogInt");
    const IVal adcBit = b.inamed("adcBit");
    const Val cullFogAccum = b.named("fogAccum");
    if (spot) {
        static const char* sn[7] = {"spotD",  "spotSq", "spotDist", "spotTm",
                                    "spotT",  "spotC",  "spotA"};
        for (int i = 0; i < 7; ++i) spotScratch[i] = b.named(sn[i]);
    }
    // Stage scratch, allocated ONCE and shared by every stage and all three
    // vertices - a stage that minted its own would multiply VF pressure by
    // three invisibly (fogCoefficient's trap, in a form that scales).
    StageCtx sc;
    sc.b = &b;
    sc.p = &prog;
    sc.params = sk.params;
    sc.timeV = sk.timeV;
    sc.kc = sk.kc;
    sc.one = sk.one;
    sc.lumaW = sk.luma;
    sc.lits = &plan.lits;
    sc.hasColor = true;
    sc.hasSt = d.texture;
    // Where a script's constants go. Everything before this point is the
    // preamble proper - the per-mesh quadwords, the stage constants, the GIF
    // tag - and everything after it runs once per vertex.
    sc.scriptPreambleAt = b.mark();
    // The script's one-time half, before a single per-vertex instruction is
    // emitted. Without it a script's constants are built once per VERTEX -
    // three copies of the same register, on a program that has none to spare.
    if (d.scriptPrepare) {
        ScriptCtx ps;
        ps.b = &b;
        ps.params = sc.params;
        ps.time = sc.timeV;
        ps.one = sc.one;
        ps.preambleAt = sc.scriptPreambleAt;
        d.scriptPrepare(ps);
        sc.scriptPreambleAt = ps.preambleAt;
    }
    if (hasStages) {
        static const char* sn[6] = {"vuS0", "vuS1", "vuS2",
                                    "vuS3", "vuS4", "vuS5"};
        static const char* qn[3] = {"vuSinA", "vuSinB", "vuSinC"};
        for (int i = 0; i < plan.scratch && i < 6; ++i) sc.s[i] = b.named(sn[i]);
        if (plan.needsSine)
            for (int i = 0; i < 3; ++i) sc.sinS[i] = b.named(qn[i]);
    }
    for (int i = 0; i < 3 && d.cull; ++i) {
        // The env ST goes FIRST for the same reason it does in as_is: its rsqrt
        // WRITES Q, and the position's perspective divide below has to be the
        // last Q write before the texture mulq reads it.
        if (d.env)
            b.envStq(st[i], k.envBasisX, k.envBasisY, k.envBasisZ, envScratch);
        sc.vertex = vertex[i];
        sc.color = color[i];
        sc.st = d.texture ? st[i] : Val{};
        if (hasStages) {
            // The ST is touched here, before the perspective correction: after
            // it the value is divided by w and an author-space offset would be
            // scaled by the distance to the camera.
            if (d.texture) applyStages(sc, plan, Slot::Texture);
            // Object space - the vertex is still in the model's own units and
            // the colour is still the mesh's own.
            applyStages(sc, plan, Slot::ObjectSpace);
        }
        if (d.script && d.scriptSlot == Slot::ObjectSpace) runScript(sc, d);
        if (spot)
            b.spotLight(color[i], vertex[i], k.spotPos, k.spotDirV, k.spotColV,
                        spotScratch);
        b.transform(vertex[i], k.mvp, vertex[i]);
        // Clip space: xyzw, w is the view distance. This is BEFORE the fog
        // coefficient and the frustum test on purpose - a stage that moves a
        // vertex in depth must move its fog and its clip verdict with it.
        if (hasStages) applyStages(sc, plan, Slot::ClipSpace);
        if (d.script && d.scriptSlot == Slot::ClipSpace) runScript(sc, d);
        b.fogCoefficient(cullFogInt, vertex[i], k.fogParams, cullFogAccum);
        b.fogClipCheck(vertex[i], destAddress, xyz + i * stride, cullFogInt,
                       k.adcMask, adcBit);
        b.persCorrect(vertex[i], vertex[i]);
        // NDC, and the last chance before the 12.4 conversion makes the
        // position an integer. Nothing here may write Q: the texture
        // correction below is still holding persCorrect's.
        if (hasStages) applyStages(sc, plan, Slot::Ndc);
        if (d.script && d.scriptSlot == Slot::Ndc) runScript(sc, d);
        b.scaleToGsFormat(vertex[i], scale);
        if (d.texture) {
            // No div of its own: persCorrect above already put 1/w in Q, and
            // that is exactly the factor the ST needs.
            b.mulQInto(outStq[i], st[i], MALL);
            prog.code.back().comment = "PerformTexturePerspectiveCorrection";
        }
        if (!colorStream) {
            // dirLightShade OVERWRITES the normal with its WORLD-space self, so
            // a script reading it at the colour slot gets the world normal -
            // which is the one a rim term wants anyway.
            b.dirLightShade(color[i], normal[i], k.lightMatrix, k.lightDirs,
                            k.lightColors, k.ambient);
            sc.normal = normal[i];
        }
        // Colour, before FixColor clamps it - so a stage may overshoot and the
        // clamp catches it, exactly like the lighting above.
        if (hasStages) applyStages(sc, plan, Slot::Color);
        // The script runs LAST of everything at its slot: the stage list is a
        // shortcut layered on the framework, the script IS the framework, and
        // an author who writes both means "and then this".
        if (d.script && d.scriptSlot == Slot::Color) runScript(sc, d);
        b.fixColor(color[i]);
    }

    for (int i = 0; i < 3 && !d.cull; ++i) {
        // The env ST goes FIRST: its rsqrt writes Q, so the position divide
        // below has to be the last Q write before the perspective mulq.
        if (d.env)
            b.envStq(st[i], k.envBasisX, k.envBasisY, k.envBasisZ, envScratch);
        // NDC, and this is the MIRROR of the cull half's Ndc slot - not an
        // approximation of it. This program never divides the position: the EE
        // clipper handed it over already projected, and w survives only for
        // the texture correction below. So right here, one instruction before
        // the 12.4 conversion, the vertex is in exactly the space the cull
        // half's Ndc stage works in, and the same grid means the same thing in
        // both. No scale compensation, no second set of constants.
        //
        // Leaving it out is what made a screen-space effect switch itself OFF
        // on any mesh touching the edge of the frame - the package went to
        // this twin, the twin ran stock, and a snapped scene turned smooth in
        // the corners. Geometry slots in OBJECT or CLIP space genuinely cannot
        // run here (those spaces are gone by now, which is what
        // movesGeometry() compensates for); Ndc always could.
        if (d.script && d.scriptSlot == Slot::Ndc) {
            sc.vertex = vertex[i];
            sc.color = color[i];
            sc.st = d.texture ? st[i] : Val{};
            runScript(sc, d);
        }
        b.scaleToGsFormat(vertex[i], scale);
        if (d.texture) {
            b.divQ(b.zero(), 3, vertex[i], 3);
            b.mulQInto(outStq[i], st[i], MALL);
            prog.code.back().comment = "PerformTexturePerspectiveCorrection";
        }
        if (!colorStream) {
            // dirLightShade OVERWRITES the normal with its WORLD-space self, so
            // a script reading it at the colour slot gets the world normal -
            // which is the one a rim term wants anyway.
            b.dirLightShade(color[i], normal[i], k.lightMatrix, k.lightDirs,
                            k.lightColors, k.ambient);
            sc.normal = normal[i];
        }
        // THE SAME BODY THE CULL HALF RUNS. Leaving it out is not a missing
        // optimisation - it is the effect switching itself off the moment a
        // mesh touches the edge of the screen: that package goes to this twin,
        // this twin had stock shading, and a ball that was cel-shaded in the
        // middle of the frame turned ordinary at the corner. Terrain showed it
        // worst, neighbouring chunks landing on different halves and the ground
        // breaking into patches.
        //
        // Geometry stages are filtered out of this program upstream (its
        // vertices arrive already transformed), so what runs here is the colour
        // and texture work - which is exactly the part that has to match.
        if (d.script && (d.scriptSlot == Slot::Color ||
                         d.scriptSlot == Slot::Texture)) {
            sc.vertex = vertex[i];
            sc.color = color[i];
            sc.st = d.texture ? st[i] : Val{};
            runScript(sc, d);
        }
        b.fixColor(color[i]);
    }

    // One scratch pair for all three vertices - see Vu::fogCoefficient on why
    // this must not be a fresh register per call.
    const Val fogScratch = b.named("fogAccum");
    const IVal fogInt = b.inamed("fogInt");
    for (int i = 0; i < 3 && !d.cull; ++i) {
        b.fogCoefficient(fogInt, vertex[i], k.fogParams, fogScratch);
        b.isw(fogInt, destAddress, xyz + i * stride, 3);
    }

    for (int i = 0; i < 3; ++i) {
        if (d.texture) b.sq(outStq[i], destAddress, stq + i * stride);
        b.sq(color[i], destAddress, rgba + i * stride);
        b.sq(vertex[i], destAddress, xyz + i * stride, MXYZ);
    }

    b.iaddiuInto(vertexData, vertexData, 3);
    if (d.texture) b.iaddiuInto(stqData, stqData, 3);
    if (colorStream)
        b.iaddiuInto(colorData, colorData, 3);
    else
        b.iaddiuInto(normalData, normalData, 3);
    b.iaddiuInto(destAddress, destAddress, 3 * stride);
    b.iaddiuInto(vertexCounter, vertexCounter, -3);
    prog.code.back().comment = "decrement the loop counter";
    b.branchIfNotEq(vertexCounter, buffer, vertexLoop);

    b.xgkick(kickAddress);
    prog.code.back().comment = "dispatch to the GS rasterizer";
    b.barrier();
    b.cont();
    b.branch(begin);
    b.finish();
    if (planOut) {
        plan.scriptWroteQ = sc.scriptWroteQ;
        *planOut = plan;
    }
}

// ---------------------------------------------------------------------------
// The clip skeleton
// ---------------------------------------------------------------------------

/** The CLIP family: the frustum clipper that used to run on the EE, on VU1.
 *
 * Structurally it is the cull skeleton with the per-vertex chain cut in two.
 * Everything up to the crossing judgement runs on the three INPUT corners -
 * the lighting, the flashlight, the MVP multiply - and everything after it runs
 * on whatever Sutherland-Hodgman leaves behind, which is between zero and nine
 * vertices and is not the same set at all. So the fog word, the perspective
 * divide, the GS scale and the colour clamp live in ONE shared emitter that the
 * fan loop calls three times per output triangle, and the prim tag's NLOOP is
 * patched at the very end with a count nobody knew in advance.
 *
 * Why the family is worth describing at all, beyond replacing the EE clipper:
 * it still holds the OBJECT-SPACE position when it runs. The as_is family does
 * not - the EE clipper hands it NDC - which is why a displacing script used to
 * stop at the edge of the screen and the caller had to submit its props whole
 * to compensate. Terrain cannot be submitted whole: its chunks straddle the
 * near plane and an unclipped one wraps the GS raster window. A clip program
 * with a script woven in is what reaches that geometry.
 *
 * The emitted program has to behave IDENTICALLY to the handwritten one, which
 * `--vu-check` establishes by running both over randomised triangles and
 * diffing the GIF stream - so the transcription below is free to read the way
 * the builder reads best rather than line by line. */
void buildClipBody(const Desc& d, Program& prog, StagePlan* planOut = nullptr) {
    Vu b(prog);
    const bool colorStream = !d.dirLights;
    const bool spot = usesSpotLight(d);
    // One scratch-polygon vertex: [pos, colour], or [pos, stq, colour].
    const int stride = d.texture ? 3 : 2;
    const int polyStq = 1, polyCol = stride - 1;
    // The GS store stride and where each register sits inside it.
    const int outStride = regsPerVertex(d);
    const int stqOut = 0, rgbaOut = d.texture ? 1 : 0, xyzOut = outStride - 1;
    // The prim tag is the LAST quadword of the tag block, and its NLOOP is what
    // gets patched once the emitted count is known.
    const int primQuad = tagQuads(d) - 1;

    StagePlan plan = planStages(d.stages, d);
    if (d.script) {
        plan.needsParams = true;
        plan.needsTime = true;
        if (plan.scratch < d.scriptScratch) plan.scratch = d.scriptScratch;
    }
    const bool hasStages = !plan.stages.empty() || d.script != nullptr;

    // --- preamble -----------------------------------------------------------
    Constants k;
    emitPreamble(b, prog, d, k);
    // x = nearZ - GUARD, y = -farZ - GUARD, z = 0, w = GUARD. The exact near
    // and far planes are folded into a second clipw judgement through these
    // biased constants, which is why they look nothing like a plane equation.
    const Val cvec = b.named("cvec");
    loadK(prog, cvec, kClipConstsAddr, MALL, "VU1_CLIP_CONSTS_ADDR");
    // The judge vertex: x, y and w are rewritten per use, and z comes from here
    // and stays at cvec's zero - so the z pair of clip flags never fires and
    // every judgement below reads only the x and y bits.
    const Val sjudge = b.named("sjudge");
    b.moveInto(sjudge, cvec);
    StageConstants sk;
    emitStageConstants(b, prog, plan, hasStages, sk);

    // --- per-buffer ---------------------------------------------------------
    const Lbl begin = b.label("begin");
    b.bind(begin);
    const BufferHeader hdr = emitBufferHeader(b, prog);
    const IVal buffer = hdr.buffer;
    const IVal vertexData = hdr.vertexData;
    const IVal vertexCount = hdr.vertexCount;
    const Val scale = hdr.scale;

    IVal stqData{}, colorData{}, normalData{};
    const IVal destAddress = b.inamed("destAddress");
    // Reloaded wherever it is needed rather than parked in an integer register:
    // the clipper's three nested loops want every one of VU1's sixteen.
    const IVal sceFlag = b.inamed("sceFlag");
    if (d.texture) {
        stqData = b.inamed("stqData");
        b.iaddInto(stqData, vertexData, vertexCount);
    }
    const IVal lastBlock = d.texture ? stqData : vertexData;
    if (colorStream) {
        colorData = b.inamed("colorData");
        b.iaddInto(colorData, lastBlock, vertexCount);
        const Lbl multi = b.label("setDestAddrMultiColor");
        const Lbl done = b.label("setDestAddr");
        ilwInto(prog, sceFlag, b.izero(), kOptionsAddr, 0,
                "VU1_OPTIONS_ADDR.x = single colour flag");
        b.branchIfLez(sceFlag, multi);
        b.iaddInto(destAddress, lastBlock, vertexCount);
        b.branch(done);
        b.bind(multi);
        b.iaddInto(destAddress, colorData, vertexCount);
        b.bind(done);
    } else {
        normalData = b.inamed("normalData");
        b.iaddInto(normalData, lastBlock, vertexCount);
        b.iaddInto(destAddress, normalData, vertexCount);
    }

    emitTagBlock(b, prog, d, k, hdr.primTag, destAddress);

    // What the NLOOP patch needs. A cut triangle fans out into as many as seven
    // and a fully clipped one into none, so the count is accumulated rather
    // than derived from vertexCount the way every other family derives it.
    const IVal outCount = b.inamed("outCount");
    b.iaddiuInto(outCount, b.izero(), 0);
    const IVal vertexCounter = b.inamed("vertexCounter");
    b.iaddInto(vertexCounter, b.izero(), vertexCount);

    const Lbl triLoop = b.label("triLoop");
    const Lbl triNext = b.label("triNext");
    const Lbl fanStart = b.label("fanStart");
    b.bind(triLoop);

    // --- the three input corners --------------------------------------------
    Val color[3], vertex[3], st[3], normal[3];
    static const char* colorNames[3] = {"color1", "color2", "color3"};
    static const char* vertexNames[3] = {"vertex1", "vertex2", "vertex3"};
    static const char* stNames[3] = {"stq1", "stq2", "stq3"};
    static const char* normalNames[3] = {"normal1", "normal2", "normal3"};
    for (int i = 0; i < 3; ++i) {
        color[i] = b.named(colorNames[i]);
        vertex[i] = b.named(vertexNames[i]);
        if (d.texture) st[i] = b.named(stNames[i]);
        if (!colorStream) normal[i] = b.named(normalNames[i]);
    }

    if (colorStream) {
        const Lbl multiColor = b.label("multiColor");
        const Lbl processing = b.label("processing");
        ilwInto(prog, sceFlag, b.izero(), kOptionsAddr, 0);
        b.branchIfLez(sceFlag, multiColor);
        for (int i = 0; i < 3; ++i) b.addInto(color[i], b.zero(), k.singleColor);
        b.branch(processing);
        b.bind(multiColor);
        for (int i = 0; i < 3; ++i) loadInto(prog, color[i], colorData, i);
        b.bind(processing);
    }
    for (int i = 0; i < 3; ++i)
        loadInto(prog, vertex[i], vertexData, i, MALL,
                 i == 0 ? "object space - the MVP multiply is below" : nullptr);
    if (d.texture)
        for (int i = 0; i < 3; ++i) loadInto(prog, st[i], stqData, i);
    if (!colorStream) {
        for (int i = 0; i < 3; ++i)
            loadInto(prog, normal[i], normalData, i, MXYZ);
        // Lit on the ORIGINAL three corners; the colours are then interpolated
        // through the cuts, which is what Gouraud shading does across an uncut
        // triangle anyway. The light registers are loaded here rather than in
        // the preamble so they are not live across the clipper.
        loadDirLights(b, prog, k);
        for (int i = 0; i < 3; ++i)
            b.dirLightShade(color[i], normal[i], k.lightMatrix, k.lightDirs,
                            k.lightColors, k.ambient);
    }

    Val envScratch[2];
    if (d.env) {
        static const char* sn[2] = {"tyraEnvLen", "tyraEnvNrm"};
        for (int i = 0; i < 2; ++i) envScratch[i] = b.named(sn[i]);
        // The matcap ST, from the object-space normals in the ST slot, BEFORE
        // the transform and the judgement: the macro's rsqrt writes Q and every
        // later Q write - the edge lerps, the emitter's divide - comes after.
        for (int i = 0; i < 3; ++i)
            b.envStq(st[i], k.envBasisX, k.envBasisY, k.envBasisZ, envScratch);
    }

    Val spotScratch[7];
    if (spot) {
        static const char* sn[7] = {"spotD", "spotSq", "spotDist", "spotTm",
                                    "spotT", "spotC",  "spotA"};
        for (int i = 0; i < 7; ++i) spotScratch[i] = b.named(sn[i]);
    }

    StageCtx sc;
    sc.b = &b;
    sc.p = &prog;
    sc.params = sk.params;
    sc.timeV = sk.timeV;
    sc.kc = sk.kc;
    sc.one = sk.one;
    sc.lumaW = sk.luma;
    sc.lits = &plan.lits;
    sc.hasColor = true;
    sc.hasSt = d.texture;
    if (hasStages) {
        static const char* sn[6] = {"vuS0", "vuS1", "vuS2",
                                    "vuS3", "vuS4", "vuS5"};
        static const char* qn[3] = {"vuSinA", "vuSinB", "vuSinC"};
        for (int i = 0; i < plan.scratch && i < 6; ++i) sc.s[i] = b.named(sn[i]);
        if (plan.needsSine)
            for (int i = 0; i < 3; ++i) sc.sinS[i] = b.named(qn[i]);
    }
    // A script's constants are hoisted HERE - after every label the triangle
    // loop has bound so far, which is what Vu::hoist's index fixups make safe.
    // Once per triangle rather than once per buffer, the same compromise the
    // cull skeleton makes: the alternative is holding them live across a
    // clipper that has no registers to lend.
    sc.scriptPreambleAt = b.mark();
    if (d.scriptPrepare) {
        ScriptCtx ps;
        ps.b = &b;
        ps.params = sc.params;
        ps.time = sc.timeV;
        ps.one = sc.one;
        ps.preambleAt = sc.scriptPreambleAt;
        d.scriptPrepare(ps);
        sc.scriptPreambleAt = ps.preambleAt;
    }

    for (int i = 0; i < 3; ++i) {
        sc.vertex = vertex[i];
        sc.color = color[i];
        sc.st = d.texture ? st[i] : Val{};
        sc.normal = colorStream ? Val{} : normal[i];
        if (hasStages) {
            if (d.texture) applyStages(sc, plan, Slot::Texture);
            applyStages(sc, plan, Slot::ObjectSpace);
        }
        // THE POINT OF THE WHOLE EXERCISE. The position is in the mesh's own
        // units here, exactly as it is in the cull half - so an object-space
        // displacement means the same thing in both, and a package that
        // crosses a plane no longer has to choose between being clipped and
        // being displaced.
        if (d.script && d.scriptSlot == Slot::ObjectSpace) runScript(sc, d);
    }
    if (spot)
        for (int i = 0; i < 3; ++i)
            b.spotLight(color[i], vertex[i], k.spotPos, k.spotDirV, k.spotColV,
                        spotScratch);
    if (spot)
        for (int i = 0; i < 3; ++i) {
            // CEILING BEFORE THE CLIPPER INTERPOLATES. The emitter clamps too,
            // but that is after the lerp: a saturated corner left at its raw
            // 400 drags a cut edge's midpoint to 250 where the cull path gets
            // 177, and a lit surface visibly BRIGHTENED as it touched the edge
            // of the screen. Upper bound only - the spot light strictly adds to
            // non-negative colours, so the emitter's max-with-zero stays the
            // only floor, and micro memory has no room for a second one.
            b.loadI(255.0f);
            b.minimumIInto(color[i], color[i], MXYZ);
        }
    for (int i = 0; i < 3; ++i) b.transform(vertex[i], k.mvp, vertex[i]);
    if (d.script && d.scriptSlot == Slot::ClipSpace)
        for (int i = 0; i < 3; ++i) {
            // Clip space, and BEFORE the judgement on purpose: a script that
            // moves a vertex in depth moves the verdict on it too.
            sc.vertex = vertex[i];
            sc.color = color[i];
            sc.st = d.texture ? st[i] : Val{};
            runScript(sc, d);
        }
    if (hasStages) {
        for (int i = 0; i < 3; ++i) {
            sc.vertex = vertex[i];
            sc.color = color[i];
            sc.st = d.texture ? st[i] : Val{};
            applyStages(sc, plan, Slot::ClipSpace);
        }
    }
    // The colour slot runs on the corners, like the flashlight above, and the
    // result is interpolated through the cuts. The emitter's own clamp is the
    // FixColor the cull half applies, so a stage may still overshoot here.
    if (d.script && d.scriptSlot == Slot::Color)
        for (int i = 0; i < 3; ++i) {
            sc.vertex = vertex[i];
            sc.color = color[i];
            sc.st = d.texture ? st[i] : Val{};
            runScript(sc, d);
        }
    if (hasStages)
        for (int i = 0; i < 3; ++i) {
            sc.vertex = vertex[i];
            sc.color = color[i];
            sc.st = d.texture ? st[i] : Val{};
            applyStages(sc, plan, Slot::Color);
        }

    // --- does this triangle cross anything? ---------------------------------
    //
    // Two clipw judgements per corner: the x/y guard band against the vertex's
    // own w (the GS scissor trims the overhang pixel-exactly), then the exact
    // near/far planes through the biased constants. The CLIP register is a
    // 24-bit window of four six-bit judgements, newest at bits 0..5. Push four
    // tests before the first read and the remaining two before the second, so
    // the six tests pay for two positional flag reads instead of six. fcand
    // yields 0 or 1 ("any masked bit set"), not the masked bit pattern.
    const IVal vi01 = b.inamed("VI01");
    const IVal triOr = b.inamed("triOr");
    for (int i = 0; i < 2; ++i) {
        b.clipwInto(vertex[i]);
        b.subInto(sjudge, cvec, vertex[i].broadcast(2), MX);
        b.addInto(sjudge, cvec, vertex[i].broadcast(2), MY);
        b.clipwInto(sjudge);
    }
    b.fcandInto(vi01, 0x3CA3CA);
    b.iaddInto(triOr, vi01, b.izero());

    b.clipwInto(vertex[2]);
    b.subInto(sjudge, cvec, vertex[2].broadcast(2), MX);
    b.addInto(sjudge, cvec, vertex[2].broadcast(2), MY);
    b.clipwInto(sjudge);
    b.fcandInto(vi01, 0x3CA);
    b.iorInto(triOr, triOr, vi01);

    // --- the clip-space triangle into scratch polygon A ---------------------
    for (int i = 0; i < 3; ++i) {
        b.sq(vertex[i], b.izero(), kClipPolyAAddr + i * stride);
        if (i == 0) prog.code.back().comment = "VU1_CLIP_POLY_A_ADDR";
        if (d.texture)
            b.sq(st[i], b.izero(), kClipPolyAAddr + i * stride + polyStq);
        b.sq(color[i], b.izero(), kClipPolyAAddr + i * stride + polyCol);
    }
    const IVal srcBase = b.inamed("srcBase");
    const IVal srcEnd = b.inamed("srcEnd");
    b.iaddiuInto(srcBase, b.izero(), kClipPolyAAddr);
    b.iaddiuInto(srcEnd, srcBase, 3 * stride);
    // Fully inside: no clipping, straight to the emitter. Every variant shares
    // this single emitter rather than carrying an inlined fast path - VU1 micro
    // memory cannot fit five clippers AND five fast paths.
    b.branchIfEq(triOr, b.izero(), fanStart);

    // --- Sutherland-Hodgman against the six planes --------------------------
    //
    // NEAR IS FIRST in the table and that ordering is load-bearing: everything
    // which survives it has w > 0, which is what makes the w-relative side
    // planes safe to apply at all.
    const IVal planePtr = b.inamed("planePtr");
    const IVal dstBase = b.inamed("dstBase");
    const IVal curPtr = b.inamed("curPtr");
    const IVal dstPtr = b.inamed("dstPtr");
    const IVal prevPtr = b.inamed("prevPtr");
    const IVal vertMask = b.inamed("vertMask");
    b.iaddiuInto(planePtr, b.izero(), kClipPlanesAddr);
    prog.code.back().comment = "VU1_CLIP_PLANES_ADDR - near plane first";
    b.iaddiuInto(dstBase, b.izero(), kClipPolyBAddr);

    const Lbl planeLoop = b.label("planeLoop");
    b.bind(planeLoop);
    const Val planeV = b.named("planeV");
    const Val planeE = b.named("planeE");
    loadInto(prog, planeV, planePtr, 0, MALL, "inside = dot4(v, ABCD) + E >= 0");
    loadInto(prog, planeE, planePtr, 1);

    const Val ppos = b.named("ppos");
    const Val pstq = b.named("pstq");
    const Val pcol = b.named("pcol");
    const Val dtmp = b.named("dtmp");
    const Val pd = b.named("pd");
    const Val cd = b.named("cd");
    /** dot4(v, planeV) + planeE.x, into `out`.x. */
    auto planeDistance = [&](Val out, Val pos) {
        b.mulInto(dtmp, pos, planeV);
        b.addInto(dtmp, dtmp, dtmp.broadcast(1), MX);
        b.addInto(dtmp, dtmp, dtmp.broadcast(2), MX);
        b.addInto(dtmp, dtmp, dtmp.broadcast(3), MX);
        b.addInto(out, dtmp, planeE.broadcast(0), MX);
    };
    // prev = the LAST vertex of the polygon, so the walk below closes the loop.
    b.iaddiuInto(prevPtr, srcEnd, -stride);
    loadInto(prog, ppos, prevPtr, 0);
    if (d.texture) loadInto(prog, pstq, prevPtr, polyStq);
    loadInto(prog, pcol, prevPtr, polyCol);
    planeDistance(pd, ppos);
    b.iaddInto(curPtr, srcBase, b.izero());
    b.iaddInto(dstPtr, dstBase, b.izero());

    const Lbl edgeLoop = b.label("edgeLoop");
    const Lbl edgeCross = b.label("edgeCross");
    const Lbl edgeEmitCur = b.label("edgeEmitCur");
    const Lbl edgeAdvance = b.label("edgeAdvance");
    b.bind(edgeLoop);
    const Val cpos = b.named("cpos");
    const Val cstq = b.named("cstq");
    const Val ccol = b.named("ccol");
    loadInto(prog, cpos, curPtr, 0);
    if (d.texture) loadInto(prog, cstq, curPtr, polyStq);
    loadInto(prog, ccol, curPtr, polyCol);
    planeDistance(cd, cpos);

    // Both signs out of ONE clipw: sjudge = (pd - GUARD, cd - GUARD, 0, GUARD),
    // so the -x bit means prev is outside and the -y bit means cur is.
    b.addInto(sjudge, b.zero(), pd.broadcast(0), MX);
    b.addInto(sjudge, b.zero(), cd.broadcast(0), MY);
    b.subInto(sjudge, sjudge, cvec.broadcast(3), (uint8_t)(MX | MY));
    b.clipwInto(sjudge);
    b.fcandInto(vi01, 0x2);
    prog.code.back().comment = "prev outside?";
    b.iaddInto(vertMask, vi01, b.izero());
    b.fcandInto(vi01, 0x8);
    prog.code.back().comment = "cur outside?";
    b.branchIfNotEq(vertMask, vi01, edgeCross);
    b.branchIfNotEq(vi01, b.izero(), edgeAdvance);
    b.branch(edgeEmitCur);

    b.bind(edgeCross);
    // t = pd / (pd - cd); out = prev + (cur - prev) * t.
    const Val lerpP = b.named("lerpP");
    const Val lerpT = b.named("lerpT");
    const Val lerpC = b.named("lerpC");
    b.subInto(dtmp, pd, cd.broadcast(0), MX);
    b.divQ(pd, 0, dtmp, 0);
    auto lerp = [&](Val out, Val cur, Val prev) {
        b.subInto(out, cur, prev);
        b.mulQInto(out, out);
        b.addInto(out, out, prev);
    };
    lerp(lerpP, cpos, ppos);
    if (d.texture) lerp(lerpT, cstq, pstq);
    lerp(lerpC, ccol, pcol);
    b.sq(lerpP, dstPtr, 0);
    if (d.texture) b.sq(lerpT, dstPtr, polyStq);
    b.sq(lerpC, dstPtr, polyCol);
    b.iaddiuInto(dstPtr, dstPtr, stride);
    // When cur is inside as well it is emitted after the crossing point.
    b.branchIfNotEq(vi01, b.izero(), edgeAdvance);

    b.bind(edgeEmitCur);
    b.sq(cpos, dstPtr, 0);
    if (d.texture) b.sq(cstq, dstPtr, polyStq);
    b.sq(ccol, dstPtr, polyCol);
    b.iaddiuInto(dstPtr, dstPtr, stride);

    b.bind(edgeAdvance);
    b.moveInto(ppos, cpos);
    if (d.texture) b.moveInto(pstq, cstq);
    b.moveInto(pcol, ccol);
    b.addInto(pd, b.zero(), cd.broadcast(0), MX);
    b.iaddiuInto(curPtr, curPtr, stride);
    b.branchIfNotEq(curPtr, srcEnd, edgeLoop);

    // Nothing left of the polygon - the whole triangle was outside this plane.
    b.branchIfEq(dstPtr, dstBase, triNext);
    // Ping-pong: what this plane produced is what the next one reads.
    b.iaddInto(srcEnd, dstPtr, b.izero());
    b.iaddInto(vertMask, srcBase, b.izero());
    b.iaddInto(srcBase, dstBase, b.izero());
    b.iaddInto(dstBase, vertMask, b.izero());
    b.iaddiuInto(planePtr, planePtr, 2);
    b.iaddiuInto(vertMask, b.izero(), kClipPlanesAddr + 12);
    b.branchIfNotEq(planePtr, vertMask, planeLoop);

    // --- fan-triangulate and emit -------------------------------------------
    b.bind(fanStart);
    // Fewer than three vertices left means there is nothing to draw.
    b.iaddiuInto(vertMask, srcBase, 3 * stride);
    b.isubInto(vertMask, srcEnd, vertMask);
    b.branchIfLtz(vertMask, triNext);

    const IVal fanPtr = b.inamed("fanPtr");
    const IVal fanNext = b.inamed("fanNext");
    const IVal emitPtr = b.inamed("emitPtr");
    const IVal emitNxt = b.inamed("emitNxt");
    const IVal emitCnt = b.inamed("emitCnt");
    b.iaddiuInto(fanPtr, srcBase, stride);
    prog.code.back().comment = "(P0, Pj, Pj+1) for j = 1 .. n-2";
    const Lbl fanLoop = b.label("fanLoop");
    b.bind(fanLoop);
    b.iaddiuInto(fanNext, fanPtr, stride);
    b.branchIfEq(fanNext, srcEnd, triNext);

    // ONE emitter instance, with the corner pointer rotating srcBase -> fanPtr
    // -> fanNext across three iterations. Three inlined copies would cost about
    // three times the code, and the micro memory that saves is what lets five
    // clip programs be considered for residency at all.
    b.iaddInto(emitPtr, srcBase, b.izero());
    b.iaddInto(emitNxt, fanPtr, b.izero());
    b.iaddiuInto(emitCnt, b.izero(), 3);
    const Lbl fanEmitLoop = b.label("fanEmitLoop");
    b.bind(fanEmitLoop);
    {
        // EmitClipVertex: one scratch-polygon vertex to one GS vertex.
        const Val ecPos = b.named("ecPos");
        const Val ecStq = b.named("ecStq");
        const Val ecCol = b.named("ecCol");
        const Val ecFogF = b.named("ecFogF");
        const IVal ecFog = b.inamed("ecFog");
        loadInto(prog, ecPos, emitPtr, 0);
        if (d.texture) loadInto(prog, ecStq, emitPtr, polyStq);
        loadInto(prog, ecCol, emitPtr, polyCol);
        // The fog coefficient rides in the W word beside an ADC bit that is
        // always zero: a clip program never derives ADC from clip flags, it
        // cut the triangle instead.
        b.fogCoefficient(ecFog, ecPos, k.fogParams, ecFogF);
        b.isw(ecFog, destAddress, xyzOut, 3);
        b.persCorrect(ecPos, ecPos);
        // No divide of its own for the ST: persCorrect just put 1/w in Q.
        if (d.texture) b.mulQInto(ecStq, ecStq, MALL);
        // NDC, and the mirror of the as_is family's Ndc slot - the same grid
        // means the same thing in both, so a screen-space effect does not
        // change scale when a package starts crossing a plane. This is the
        // only place the clip family HAS an NDC position: it exists per
        // EMITTED vertex, which is not the set the corners above belong to.
        if (d.script && d.scriptSlot == Slot::Ndc) {
            sc.vertex = ecPos;
            sc.color = ecCol;
            sc.st = d.texture ? ecStq : Val{};
            sc.normal = Val{};
            runScript(sc, d);
        }
        if (hasStages) {
            sc.vertex = ecPos;
            sc.color = ecCol;
            sc.st = d.texture ? ecStq : Val{};
            applyStages(sc, plan, Slot::Ndc);
        }
        b.scaleToGsFormat(ecPos, scale);
        b.fixColor(ecCol);
        if (d.texture) b.sq(ecStq, destAddress, stqOut);
        b.sq(ecCol, destAddress, rgbaOut);
        b.sq(ecPos, destAddress, xyzOut, MXYZ);
        b.iaddiuInto(destAddress, destAddress, outStride);
    }
    b.iaddInto(emitPtr, emitNxt, b.izero());
    b.iaddInto(emitNxt, fanNext, b.izero());
    b.iaddiuInto(emitCnt, emitCnt, -1);
    b.branchIfGtz(emitCnt, fanEmitLoop);
    b.iaddiuInto(outCount, outCount, 3);
    b.iaddInto(fanPtr, fanNext, b.izero());
    b.branch(fanLoop);

    b.bind(triNext);
    b.iaddiuInto(vertexData, vertexData, 3);
    if (d.texture) b.iaddiuInto(stqData, stqData, 3);
    if (colorStream)
        b.iaddiuInto(colorData, colorData, 3);
    else
        b.iaddiuInto(normalData, normalData, 3);
    b.iaddiuInto(vertexCounter, vertexCounter, -3);
    prog.code.back().comment = "decrement the loop counter";
    b.branchIfGtz(vertexCounter, triLoop);

    // --- the kick -----------------------------------------------------------
    //
    // destAddress walked away with the emitter, so the address to kick is
    // rebuilt from scratch. xtop is stable within one activation, which is what
    // makes asking a second time legal.
    b.xtop(buffer);
    b.iaddiuInto(vertexData, buffer, kVertDataAddr);
    ilwInto(prog, vertexCount, buffer, 0, 3);
    b.iaddInto(vertexData, vertexData, vertexCount);
    if (d.texture) b.iaddInto(vertexData, vertexData, vertexCount);
    if (!colorStream) {
        b.iaddInto(vertexData, vertexData, vertexCount);
    } else {
        const Lbl kickMulti = b.label("kickMulti");
        const Lbl kickPatch = b.label("kickPatch");
        ilwInto(prog, sceFlag, b.izero(), kOptionsAddr, 0);
        b.branchIfLez(sceFlag, kickMulti);
        b.branch(kickPatch);
        b.bind(kickMulti);
        b.iaddInto(vertexData, vertexData, vertexCount);
        b.bind(kickPatch);
    }
    // The prim giftag's word 0 is NLOOP | EOP, and 0x8000 does not fit in one
    // 15-bit immediate.
    b.iaddiuInto(vertMask, outCount, 0x4000);
    b.iaddiuInto(vertMask, vertMask, 0x4000);
    b.isw(vertMask, vertexData, primQuad, 0);

    b.xgkick(vertexData);
    prog.code.back().comment = "dispatch to the GS rasterizer";
    b.barrier();
    b.cont();
    b.branch(begin);
    b.finish();
    if (planOut) {
        plan.scriptWroteQ = sc.scriptWroteQ;
        *planOut = plan;
    }
}

// ---------------------------------------------------------------------------
// The .vclpp emitter
// ---------------------------------------------------------------------------

std::string emitBodyText(const Program& p);

std::string emitVclpp(const Desc& d, const Program& p) {
    std::string out;
    auto line = [&](const std::string& s) {
        out += s;
        out += "\n";
    };

    if (d.custom) {
        line("; Generated by TyraX. Do not edit - regenerated on every build");
        line("; from the stage list in Tools > VU Programs (docs/vu-authoring.md).");
        line(";");
        line("; " + d.title);
        line("; Installed over the engine's " + d.programEnum + " slot, so it");
        line("; draws EVERY mesh of that material class. A mesh whose four");
        line("; parameters are zero is rendered bit-identically to the engine's");
        line("; own program - that is the contract --vu-check enforces.");
    } else {
        line("; Generated by TyraX (src/vugen.cpp). Do not edit - regenerate from");
        line("; the C++ description with: tyrax-editor --vu-emit <dir>");
        line(";");
        line("; " + d.title);
        if (d.clip) {
            line("; Clip = replaces the EE clipper for frustum-crossing packages:");
            line("; object-space vertices in, the MVP multiply and a full");
            line("; Sutherland-Hodgman cut against six planes on VU1. The vertex");
            line("; count is VARIABLE, so the prim giftag's NLOOP is patched at the");
            line("; end with what the fan triangulation actually produced.");
        } else if (d.cull) {
            line("; Cull = the standard PS2 path: object-space vertices in, the MVP");
            line("; multiply and the ADC frustum test on VU1.");
        } else {
            line("; AsIs = NO TRANSFORM: the EE clipper already produced NDC positions,");
            line("; with the clip-space W kept for fog and the texture perspective divide.");
        }
    }
    line("");
    line(".syntax new");
    line(".name " + d.asmName);
    line(".vu");
    line(".init_vf_all");
    line(".init_vi_all");
    line("");
    line("--enter");
    line("--endenter");
    line("");
    line("#vuprog " + d.vclName);
    line("");
    out += emitBodyText(p);
    line("");
    line("#endvuprog");
    line("");
    line("--exit");
    line("--endexit");
    return out;
}

// ---------------------------------------------------------------------------
// The EE-side emitter
// ---------------------------------------------------------------------------

/** The instruction listing itself - shared by the StaPip and kernel emitters,
 * because the branch-label rewriting below is exactly the sort of thing that
 * gets fixed in one copy and not the other. */
std::string emitBodyText(const Program& p) {
    std::string out;
    auto line = [&](const std::string& s) {
        out += s;
        out += "\n";
    };
    // Which instructions are branch targets, so labels can be emitted.
    std::vector<int> isTarget(p.code.size(), 0);
    for (const Instr& in : p.code)
        if (in.target >= 0 && (size_t)in.target < isTarget.size())
            isTarget[in.target] = 1;

    for (size_t i = 0; i < p.code.size(); ++i) {
        const Instr& in = p.code[i];
        if (in.op == Op::Label) {
            if (isTarget[i]) line(in.comment + ":");
            continue;
        }
        if (in.op == Op::Barrier) {
            line("");
            line("--barrier");
            continue;
        }
        if (in.op == Op::Cont) {
            line("--cont");
            line("");
            continue;
        }
        std::string text = vuir::disassemble(p, in);
        if (text.empty()) continue;
        // Branches print their target as "L<index>"; rewrite to the label name.
        if (in.target >= 0 && (size_t)in.target < p.code.size()) {
            const std::string want = "L" + std::to_string(in.target);
            const size_t at = text.rfind(want);
            if (at != std::string::npos)
                text = text.substr(0, at) + p.code[in.target].comment;
        }
        line("    " + text + (in.comment.empty() ? "" : "  ; " + in.comment));
    }
    return out;
}

std::string emitEeHeader(const Desc& d) {
    std::string s;
    s += "// Generated by TyraX (src/vugen.cpp). Do not edit - regenerate from\n";
    s += "// the C++ description with: tyrax-editor --vu-emit <dir>\n";
    s += "#pragma once\n\n";
    s += "#include \"renderer/3d/pipeline/static/core/stapip_vu1_program.hpp\"\n\n";
    s += "namespace Tyra {\n\n";
    s += "class " + d.className + " : public StaPipVU1Program {\n";
    s += " public:\n";
    s += "  " + d.className + "();\n";
    s += "  ~" + d.className + "();\n\n";
    s += "  std::string getStringName() const;\n";
    s += "  void addProgramQBufferDataToPacket(packet2_t* packet,\n";
    s += "                                     StaPipQBuffer* qbuffer) const;\n";
    s += "};\n\n";
    s += "}  // namespace Tyra\n";
    return s;
}

std::string emitEeSource(const Desc& d) {
    const int regs = regsPerVertex(d);
    std::string gifRegs;
    if (d.texture)
        gifRegs =
            "((u64)GIF_REG_ST) << 0 | ((u64)GIF_REG_RGBAQ) << 4 |\n"
            "                           ((u64)GIF_REG_XYZF2) << 8";
    else
        gifRegs = "((u64)GIF_REG_RGBAQ) << 0 | ((u64)GIF_REG_XYZF2) << 4";

    std::string s;
    s += "// Generated by TyraX (src/vugen.cpp). Do not edit - regenerate from\n";
    s += "// the C++ description with: tyrax-editor --vu-emit <dir>\n";
    s += "//\n";
    s += "// The unpack layout below and the microprogram's addressing come from\n";
    s += "// ONE description, so they cannot drift apart - which is the whole\n";
    s += "// reason this file is generated (docs/vu-framework.md).\n\n";
    s += "#include \"debug/debug.hpp\"\n";
    // A project's own program lives in the GAME tree, next to this file - a
    // quoted include searches the includer's own directory first, which is why
    // src/gen needs no -I of its own. An engine program keeps the tree path.
    s += d.custom ? "#include \"" + d.fileStem + "_program.hpp\"\n\n"
                  : "#include \"renderer/3d/pipeline/static/core/programs/" +
                        d.dir + "/" + d.fileStem + "_program.hpp\"\n\n";
    s += "extern u32 " + d.asmName + "_CodeStart __attribute__((section(\".vudata\")));\n";
    s += "extern u32 " + d.asmName + "_CodeEnd __attribute__((section(\".vudata\")));\n\n";
    s += "namespace Tyra {\n\n";
    s += d.className + "::" + d.className + "()\n";
    s += "    : StaPipVU1Program(" + d.programEnum + ", &" + d.asmName +
         "_CodeStart,\n";
    s += "                       &" + d.asmName + "_CodeEnd,\n";
    s += "                       " + gifRegs + ",\n";
    s += "                       " + std::to_string(regs) + ", " +
         std::to_string(regs) + ") {}\n\n";
    s += d.className + "::~" + d.className + "() {}\n\n";
    s += "std::string " + d.className + "::getStringName() const {\n";
    s += "  return std::string(\"" + d.title + "\");\n";
    s += "}\n\n";
    s += "void " + d.className + "::addProgramQBufferDataToPacket(\n";
    s += "    packet2_t* packet, StaPipQBuffer* qbuffer) const {\n";
    s += "  u32 addr = VU1_STAPIP_VERT_DATA_ADDR;\n";
    // Straight off attrBlocks(): each block sits one `qbuffer->size` past the
    // previous one, so the advance belongs BEFORE every block but the first.
    const std::vector<AttrBlock> blocks = attrBlocks(d);
    for (size_t i = 0; i < blocks.size(); ++i) {
        const AttrBlock& blk = blocks[i];
        const std::string ind = blk.singleColorOpt ? "    " : "  ";
        s += "\n  // ";
        s += blk.comment;
        s += "\n";
        if (blk.singleColorOpt)
            s += "  if (qbuffer->bag->color->single == nullptr) {\n";
        if (i > 0) s += ind + "addr += qbuffer->size;\n";
        s += ind + "packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->";
        s += blk.member;
        s += ",\n";
        s += ind + "                                 qbuffer->size, true);\n";
        if (blk.singleColorOpt) s += "  }\n";
    }
    s += "}\n\n";
    s += "}  // namespace Tyra\n";
    return s;
}

}  // namespace

Built build(const Desc& d) {
    Built out;
    out.program.name = d.vclName;
    out.tagQuads = tagQuads(d);
    out.regsPerVertex = regsPerVertex(d);
    out.attrStreams = attrStreams(d);
    StagePlan plan;
    if (d.clip)
        buildClipBody(d, out.program, &plan);
    else
        buildAsIsBody(d, out.program, &plan);
    out.errors = plan.errors;
    if (plan.scriptWroteQ)
        out.errors.push_back(
            "the script writes Q (a divide, an rsqrt or a sqrt). Q carries the "
            "perspective divide and has a latency the assembler schedules "
            "around, so a Q write from a script body gets moved into that "
            "window and vertices land in the wrong place - grey stippled "
            "patches and z-fighting on the console, with nothing wrong on the "
            "host. Compute what you need without it (docs/vu-authoring.md)");
    out.droppedStages = plan.dropped;
    if (!d.stages.empty()) {
        // The stage cost, measured against the same description with the list
        // removed. A sum of the catalogue's estimates would be a second answer
        // to the same question and would drift the day a stage folds a literal.
        Desc bare = d;
        bare.stages.clear();
        Program plain;
        if (bare.clip)
            buildClipBody(bare, plain);
        else
            buildAsIsBody(bare, plain);
        out.stageInstrs = (int)out.program.code.size() - (int)plain.code.size();
    }
    out.vclpp = emitVclpp(d, out.program);
    out.eeHeader = emitEeHeader(d);
    out.eeSource = emitEeSource(d);
    return out;
}

// ---------------------------------------------------------------------------
// VU0 kernels - the second skeleton
// ---------------------------------------------------------------------------

namespace {

void buildKernelBody(const KernelDesc& k, Program& prog, StagePlan& plan) {
    Vu b(prog);

    // A project's own C++ body rides the SAME preamble the stage list does -
    // it reads the parameter quadword and the clock out of the same registers.
    // Asking for them HERE, before a single instruction is emitted, is what
    // makes a body and a stage list interchangeable to the emitter below.
    if (k.script) {
        plan.needsParams = true;
        plan.needsTime = true;
        if (plan.scratch < k.scriptScratch) plan.scratch = k.scriptScratch;
    }

    const IVal count = b.inamed("count");
    {
        Instr in;
        in.op = Op::Ilw;
        in.dst = count.reg;
        in.base = b.izero().reg;
        in.imm = k.controlAddr;
        in.mask = MX;
        in.comment = "elements in THIS call - the EE rewrites it per vcallms";
        prog.code.push_back(in);
    }

    Val params{}, timeV{}, one{}, kc{};
    if (plan.needsParams) {
        params = b.named("params");
        Instr in;
        in.op = Op::Lq;
        in.dst = params.reg;
        in.base = b.izero().reg;
        in.imm = k.paramAddr;
        in.comment =
            "the four user parameters - stored ONCE, they survive every "
            "vcallms because VU0 data memory persists";
        prog.code.push_back(in);
    }
    if (plan.needsTime) {
        // A kernel has no renderer clock behind it, so the time quadword is
        // written by its driver, next to the parameters.
        timeV = b.named("time");
        Instr in;
        in.op = Op::Lq;
        in.dst = timeV.reg;
        in.base = b.izero().reg;
        in.imm = k.paramAddr + 1;
        in.comment = "(time, sin time, cos time, 1.0), written by the driver";
        prog.code.push_back(in);
    }
    one = b.named("one");
    b.onesInto(one);
    if (plan.needsSine) {
        kc = b.named("sinK");
        b.constants(kc, 0.15915494f, 0.5f, 8388608.0f, 0.225f);
    }
    Val luma{};
    if (plan.needsLuma) {
        luma = b.named("luma");
        b.constants(luma, 0.299f, 0.587f, 0.114f, 0.0f);
    }
    plan.lits.emit(b);
    bindOperands(plan, params);

    const IVal inAddr = b.inamed("inAddr");
    b.iaddiuInto(inAddr, b.izero(), k.inputAddr);
    prog.code.back().comment = "the input block - a FIXED address, not an xtop";
    const IVal outAddr = b.inamed("outAddr");
    b.iaddiuInto(outAddr, b.izero(), k.outputAddr);

    const Lbl done = b.label("done");
    b.branchIfLez(count, done);

    StageCtx sc;
    sc.b = &b;
    sc.p = &prog;
    sc.params = params;
    sc.timeV = timeV;
    sc.kc = kc;
    sc.one = one;
    sc.lumaW = luma;
    sc.lits = &plan.lits;
    static const char* sn[6] = {"s0", "s1", "s2", "s3", "s4", "s5"};
    static const char* qn[3] = {"sinA", "sinB", "sinC"};
    for (int i = 0; i < plan.scratch && i < 6; ++i) sc.s[i] = b.named(sn[i]);
    if (plan.needsSine)
        for (int i = 0; i < 3; ++i) sc.sinS[i] = b.named(qn[i]);

    // WHERE THE PREAMBLE ENDS. Everything after this is inside the loop, so
    // this is where a body's constants get hoisted back to - and on VU0 that is
    // not a tuning question the way it is on VU1: the element loop is a real
    // branch rather than three unrolled copies, so a constant left where it was
    // written is rebuilt on every single element, and a `loi` among them is the
    // one construct vcl reorders in a way the host simulator cannot model.
    sc.scriptPreambleAt = b.mark();
    if (k.scriptPrepare) {
        ScriptCtx ps;
        ps.b = &b;
        ps.params = params;
        ps.time = timeV;
        ps.one = one;
        for (int i = 0; i < 6; ++i) ps.scratch[i] = sc.s[i];
        ps.preambleAt = sc.scriptPreambleAt;
        k.scriptPrepare(ps);
        sc.scriptPreambleAt = ps.preambleAt;
    }

    const Lbl loop = b.label("elementLoop");
    b.bind(loop);
    const Val elem = b.named("elem");
    {
        Instr in;
        in.op = Op::Lq;
        in.dst = elem.reg;
        in.base = inAddr.reg;
        in.imm = 0;
        prog.code.push_back(in);
    }
    sc.vertex = elem;
    // A kernel element is ONE quadword, so there is nothing else for these to
    // name. They alias it rather than staying unnamed: an unnamed Val is vf00,
    // and a body that reached for a colour would then be writing the hardwired
    // register - which fails at vcl, minutes later, saying nothing about why.
    sc.color = elem;
    sc.st = elem;
    applyStages(sc, plan, Slot::ObjectSpace);
    applyStages(sc, plan, Slot::ClipSpace);
    // The body runs LAST, after whatever the stage list did, for the reason it
    // does on VU1: the catalogue is a shortcut layered on the framework and the
    // body IS the framework, so the body gets the final say.
    if (k.script) runScriptBody(sc, k.script);
    b.sq(elem, outAddr, 0);
    b.iaddiuInto(inAddr, inAddr, 1);
    b.iaddiuInto(outAddr, outAddr, 1);
    b.iaddiuInto(count, count, -1);
    b.branchIfNotEq(count, b.izero(), loop);
    b.bind(done);
    // A kernel ENDS - there is no buffer to branch back to, and the EE
    // re-enters at instruction 0 on the next vcallms. VCL puts the E bit on the
    // LAST instruction of the #vuprog block, so the exit label needs a real one
    // to carry it - and `nop` is not available (vcl: "instruction 'nop' is
    // unsupported, except in RAW mode"). Writing the remaining count back is
    // the useful choice rather than a filler: the driver can read it as "the
    // kernel ran to completion".
    b.isw(count, b.izero(), k.controlAddr, 1);
    prog.code.back().comment = "elements left = 0: the kernel ran to completion";
    plan.scriptWroteQ = sc.scriptWroteQ;
    b.finish();
}

std::string emitKernelVclpp(const KernelDesc& k, const Program& p) {
    std::string out;
    auto line = [&](const std::string& s) {
        out += s;
        out += "\n";
    };
    line("; Generated by TyraX (src/vugen.cpp). Do not edit - regenerate from");
    line("; the description with: tyrax-editor --vu-emit <dir>");
    line(";");
    line("; " + k.title + " - a VU0 COMPUTE KERNEL.");
    line(";");
    line("; Nothing of the VU1 pipeline is here and that is the point: no xtop");
    line("; (VIF0 is not double-buffered - the EE stores straight into VU0 data");
    line("; memory), no GIF tag block and no xgkick (VU0 has no path to the GS),");
    line("; and no branch back to a buffer loop - the program ENDS and the EE");
    line("; re-enters it at instruction 0 with the next vcallms.");
    line(";");
    line("; Data memory (quadword addresses, 256 TOTAL on VU0):");
    line(";   " + std::to_string(k.controlAddr) +
         "  .x = element count for this call");
    line(";   " + std::to_string(k.paramAddr) + "  user parameters");
    line(";   " + std::to_string(k.paramAddr + 1) +
         "  (time, sin time, cos time, 1.0)");
    line(";   " + std::to_string(k.inputAddr) + ".." +
         std::to_string(k.inputAddr + k.maxElements - 1) + "  input elements");
    line(";   " + std::to_string(k.outputAddr) + ".." +
         std::to_string(k.outputAddr + k.maxElements - 1) + "  output elements");
    line(";");
    line("; VU0's register file is SHARED with COP2 macro mode - the engine's own");
    line("; Vec4/M4x4 math. Nothing may run this kernel concurrently with that;");
    line("; the emitted driver blocks the EE for exactly this reason.");
    line("");
    line(".syntax new");
    line(".name " + k.asmName);
    line(".vu");
    line(".init_vf_all");
    line(".init_vi_all");
    line("");
    line("--enter");
    line("--endenter");
    line("");
    line("#vuprog " + k.vclName);
    line("");
    out += emitBodyText(p);
    line("");
    line("#endvuprog");
    line("");
    line("--exit");
    line("--endexit");
    return out;
}

std::string emitKernelHeader(const KernelDesc& k) {
    std::string s;
    s += "// Generated by TyraX (src/vugen.cpp). Do not edit.\n";
    s += "//\n";
    s += "// " + k.title + " - a VU0 compute kernel.\n";
    s += "//\n";
    s += "// Usage from a script (inc/scripts/script.hpp):\n";
    s += "//   static Tyra::" + k.className + " kernel;\n";
    s += "//   kernel.setParams(0.5F, 2.0F, 0.0F, 0.0F);\n";
    s += "//   kernel.setTime(elapsedSeconds);\n";
    s += "//   kernel.run(in, out, count);   // count <= " +
         std::to_string(k.maxElements) + "\n";
    s += "//\n";
    s += "// run() BLOCKS the EE until the kernel finishes. That is not a\n";
    s += "// simplification: VU0's register file is the same one COP2 macro mode\n";
    s += "// uses, which is the engine's Vec4/M4x4 math, so nothing else may be\n";
    s += "// doing vector arithmetic while this runs.\n";
    s += "#pragma once\n\n";
    s += "#include <tamtypes.h>\n";
    s += "#include \"math/vec4.hpp\"\n\n";
    s += "namespace Tyra {\n\n";
    s += "class " + k.className + " {\n";
    s += " public:\n";
    s += "  static constexpr int MaxElements = " +
         std::to_string(k.maxElements) + ";\n\n";
    s += "  /** Uploads the microprogram to VU0 micro memory. Idempotent. */\n";
    s += "  void init();\n";
    s += "  void setParams(float x, float y, float z, float w);\n";
    s += "  /** Seconds. Wrap it yourself if your game runs for hours - the\n";
    s += "   * kernel's sine folds through a 2^23 add and loses precision long\n";
    s += "   * before a float would. */\n";
    s += "  void setTime(float seconds);\n";
    s += "  /** count elements in, count elements out. Blocks. */\n";
    s += "  void run(const Vec4* in, Vec4* out, int count);\n\n";
    s += " private:\n";
    s += "  bool uploaded = false;\n";
    s += "  float params[4] = {0.0F, 0.0F, 0.0F, 0.0F};\n";
    s += "  float timeQ[4] = {0.0F, 0.0F, 1.0F, 1.0F};\n";
    s += "};\n\n";
    s += "}  // namespace Tyra\n";
    return s;
}

std::string emitKernelSource(const KernelDesc& k) {
    std::string s;
    s += "// Generated by TyraX (src/vugen.cpp). Do not edit.\n\n";
    s += "#include <math.h>\n";
    s += "#include \"" + k.headerName + "\"\n";
    s += "#include \"debug/debug.hpp\"\n\n";
    s += "extern u32 " + k.asmName +
         "_CodeStart __attribute__((section(\".vudata\")));\n";
    s += "extern u32 " + k.asmName +
         "_CodeEnd __attribute__((section(\".vudata\")));\n\n";
    s += "namespace Tyra {\n\n";
    s += "namespace {\n";
    s += "// VU0 micro/data memory as the EE sees it (identity-mapped,\n";
    s += "// uncached). Only legal to touch while VU0 is idle.\n";
    s += "volatile u32* const kMicro = reinterpret_cast<volatile u32*>(0x11000000);\n";
    s += "volatile u32* const kData = reinterpret_cast<volatile u32*>(0x11004000);\n\n";
    s += "inline void waitIdle() {\n";
    s += "  u32 stat = 0;\n";
    s += "  int spins = 0;\n";
    s += "  do {\n";
    s += "    asm volatile(\"cfc2 %0, $29\" : \"=r\"(stat));\n";
    s += "    TYRA_ASSERT(++spins < 8000000, \"" + k.className +
         ": VU0 kernel timeout (VPU STAT stuck)\");\n";
    s += "  } while (stat & 1);\n";
    s += "}\n\n";
    s += "inline void storeQ(int qw, const float* v) {\n";
    s += "  const u32* src = reinterpret_cast<const u32*>(v);\n";
    s += "  volatile u32* d = kData + qw * 4;\n";
    s += "  d[0] = src[0]; d[1] = src[1]; d[2] = src[2]; d[3] = src[3];\n";
    s += "}\n";
    s += "inline void loadQ(int qw, float* v) {\n";
    s += "  u32* dst = reinterpret_cast<u32*>(v);\n";
    s += "  volatile u32* srcp = kData + qw * 4;\n";
    s += "  dst[0] = srcp[0]; dst[1] = srcp[1]; dst[2] = srcp[2]; dst[3] = srcp[3];\n";
    s += "}\n";
    s += "}  // namespace\n\n";
    s += "void " + k.className + "::init() {\n";
    s += "  if (uploaded) return;\n";
    s += "  waitIdle();\n";
    s += "  const u32 words = static_cast<u32>(&" + k.asmName + "_CodeEnd - &" +
         k.asmName + "_CodeStart);\n";
    s += "  TYRA_ASSERT(words > 0 && words * 4 <= 4096,\n";
    s += "              \"" + k.className +
         " does not fit in VU0's 4KB of micro memory\");\n";
    s += "  const u32* src = &" + k.asmName + "_CodeStart;\n";
    s += "  for (u32 i = 0; i < words; i++) kMicro[i] = src[i];\n";
    s += "  asm volatile(\"sync.l\");\n";
    s += "  uploaded = true;\n";
    s += "}\n\n";
    s += "void " + k.className +
         "::setParams(float x, float y, float z, float w) {\n";
    s += "  params[0] = x; params[1] = y; params[2] = z; params[3] = w;\n";
    s += "}\n\n";
    s += "void " + k.className + "::setTime(float seconds) {\n";
    s += "  timeQ[0] = seconds;\n";
    s += "  timeQ[1] = sinf(seconds);\n";
    s += "  timeQ[2] = cosf(seconds);\n";
    s += "  timeQ[3] = 1.0F;\n";
    s += "}\n\n";
    s += "void " + k.className +
         "::run(const Vec4* in, Vec4* out, int count) {\n";
    s += "  if (count <= 0) return;\n";
    s += "  if (count > MaxElements) count = MaxElements;\n";
    s += "  init();\n";
    s += "  waitIdle();\n";
    s += "  storeQ(" + std::to_string(k.paramAddr) + ", params);\n";
    s += "  storeQ(" + std::to_string(k.paramAddr + 1) + ", timeQ);\n";
    s += "  for (int i = 0; i < count; i++)\n";
    s += "    storeQ(" + std::to_string(k.inputAddr) +
         " + i, reinterpret_cast<const float*>(&in[i]));\n";
    s += "  volatile u32* ctrl = kData + " + std::to_string(k.controlAddr) +
         " * 4;\n";
    s += "  ctrl[0] = static_cast<u32>(count);\n";
    s += "  // Drain the EE write buffer, then restart the microprogram at 0.\n";
    s += "  asm volatile(\"sync.l\");\n";
    s += "  asm volatile(\"vcallms 0\" ::: \"memory\");\n";
    s += "  waitIdle();\n";
    s += "  for (int i = 0; i < count; i++)\n";
    s += "    loadQ(" + std::to_string(k.outputAddr) +
         " + i, reinterpret_cast<float*>(&out[i]));\n";
    s += "}\n\n";
    s += "}  // namespace Tyra\n";
    return s;
}

}  // namespace

BuiltKernel buildKernel(const KernelDesc& k) {
    BuiltKernel out;
    out.program.name = k.vclName;

    // The layout has to fit VU0's 256 quadwords, and a kernel that overruns it
    // wraps silently on hardware - so it is refused here, with the arithmetic.
    const int need = k.outputAddr + k.maxElements;
    if (need > vusim::kVu0MemQuads)
        out.errors.push_back(
            "the layout needs " + std::to_string(need) +
            " quadwords and VU0 has " + std::to_string(vusim::kVu0MemQuads) +
            " - shrink the batch or move the output block down");
    if (k.inputAddr + k.maxElements > k.outputAddr)
        out.errors.push_back("the input block runs into the output block");
    if (k.paramAddr + 1 >= k.inputAddr)
        out.errors.push_back(
            "the parameter block (2 quadwords) runs into the input block");

    Desc fake;  // stages are validated against a description; a kernel is not
    fake.texture = false;
    StagePlan plan = planStages(k.stages, fake);
    for (const Resolved& r : plan.stages)
        if (!r.def->kernelSafe)
            out.errors.push_back(
                std::string(r.def->title) +
                " is a rendering stage and has no meaning in a kernel - it "
                "reads a colour or an ST that a kernel element does not have");
    for (const std::string& e : plan.errors) out.errors.push_back(e);
    out.notes = plan.dropped;
    if (!out.errors.empty()) return out;

    StagePlan built = planStages(k.stages, fake);
    buildKernelBody(k, out.program, built);
    if (built.scriptWroteQ)
        // A NOTE, not the error the same thing is on VU1. Q is dangerous there
        // because the framework itself owns it - persCorrect puts 1/w in it and
        // the position and the ST both ride on it, so a divide vcl slides into
        // that window silently moves the vertex. A kernel has no framework
        // around the body: nothing else reads Q, and a divide is ordinary
        // microcode. Worth saying out loud, because the VU1 rule is absolute
        // and the difference is not guessable.
        out.notes.push_back(
            "the body writes Q (a divide, an rsqrt or a sqrt). That is FINE in "
            "a kernel - nothing else here reads Q - and it is forbidden in a "
            "VU1 script, where the perspective divide owns it");
    out.vclpp = emitKernelVclpp(k, out.program);
    out.eeHeader = emitKernelHeader(k);
    out.eeSource = emitKernelSource(k);
    // The loop body, measured against the same kernel with no stages and no
    // C++ body. The preamble's parameter and clock loads are held CONSTANT
    // across the two so the difference is the per-element cost and not the
    // two quadword loads a body implies.
    KernelDesc bare = k;
    bare.stages.clear();
    bare.script = nullptr;
    bare.scriptPrepare = nullptr;
    Program plain;
    StagePlan emptyPlan = planStages({}, fake);
    if (k.script) {
        emptyPlan.needsParams = built.needsParams;
        emptyPlan.needsTime = built.needsTime;
        emptyPlan.scratch = built.scratch;
    }
    buildKernelBody(bare, plain, emptyPlan);
    out.perElement = (int)out.program.code.size() - (int)plain.code.size();
    return out;
}

std::vector<float> simulateKernel(const BuiltKernel& b, const KernelDesc& k,
                                  const std::vector<float>& in,
                                  const float params[4], float time,
                                  std::string* error) {
    std::vector<float> out;
    if (error) error->clear();
    if (b.program.code.empty()) {
        if (error) *error = "the kernel did not build";
        return out;
    }
    const int count = (int)in.size() / 4;
    if (count <= 0 || count > k.maxElements) {
        if (error)
            *error = "element count must be 1.." + std::to_string(k.maxElements);
        return out;
    }
    std::vector<uint32_t> mem((size_t)vusim::memWords(vusim::Target::VU0), 0u);
    auto put = [&](int qw, int f, float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        mem[(size_t)qw * 4 + f] = bits;
    };
    for (int f = 0; f < 4; ++f) put(k.paramAddr, f, params[f]);
    put(k.paramAddr + 1, 0, time);
    put(k.paramAddr + 1, 1, std::sin(time));
    put(k.paramAddr + 1, 2, std::cos(time));
    put(k.paramAddr + 1, 3, 1.0f);
    mem[(size_t)k.controlAddr * 4] = (uint32_t)count;
    for (int i = 0; i < count; ++i)
        for (int f = 0; f < 4; ++f) put(k.inputAddr + i, f, in[(size_t)i * 4 + f]);

    vusim::Config cfg;
    cfg.target = vusim::Target::VU0;
    const vusim::Result r = vusim::run(b.program, mem, cfg);
    if (!r.ok) {
        if (error) *error = r.error;
        return out;
    }
    out.resize((size_t)count * 4);
    for (int i = 0; i < count; ++i)
        for (int f = 0; f < 4; ++f) {
            float v;
            std::memcpy(&v, &r.mem[(size_t)(k.outputAddr + i) * 4 + f], 4);
            out[(size_t)i * 4 + f] = v;
        }
    return out;
}

// ---------------------------------------------------------------------------
// Equivalence
// ---------------------------------------------------------------------------

namespace {

uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

float randomFloat(uint32_t& s, float lo, float hi) {
    const float t = (float)(xorshift(s) >> 8) / (float)(1 << 24);
    return lo + t * (hi - lo);
}

uint32_t asBits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

/** Stages one randomized draw exactly as the EE would: the per-mesh constants,
 * the buffer header and the attribute arrays. */
std::vector<uint32_t> stageInput(const Desc& d, int top, int verts, uint32_t& s,
                                 bool singleColor, const float* customParams,
                                 float customTime) {
    std::vector<uint32_t> mem(vusim::kMemWords, 0u);
    auto putf = [&](int qw, int f, float v) { mem[(size_t)qw * 4 + f] = asBits(v); };
    auto puti = [&](int qw, int f, uint32_t v) { mem[(size_t)qw * 4 + f] = v; };

    puti(kOptionsAddr, 0, singleColor ? 1u : 0u);
    putf(kOptionsAddr, 2, -255.0f / 900.0f);
    putf(kOptionsAddr, 3, 255.0f * 1000.0f / 900.0f);
    for (int f = 0; f < 4; ++f)
        putf(kSingleColorAddr, f, randomFloat(s, 0.0f, 300.0f));

    // Lights: the rotation matrix, three directions, three colours, ambient.
    for (int i = 0; i < 3; ++i)
        for (int f = 0; f < 3; ++f) {
            putf(kLightsMatrixAddr + i, f, randomFloat(s, -1.0f, 1.0f));
            putf(kLightsDirsAddr + i, f, randomFloat(s, -1.0f, 1.0f));
            putf(kLightsColorsAddr + i, f, randomFloat(s, 0.0f, 120.0f));
        }
    for (int f = 0; f < 3; ++f)
        putf(kLightsColorsAddr + 3, f, randomFloat(s, 0.0f, 60.0f));

    // Env bags reuse the light-matrix addresses for a transposed, pre-scaled
    // camera basis. Overwrite the random light data with a valid orthonormal
    // basis so the TCE equivalence trials exercise the actual packet layout,
    // including non-zero x/y/z contributions to both dot products.
    if (d.env) {
        static const float basis[3][4] = {
            {0.4f, -0.09f, 0.0f, 0.0f},
            {0.0f, -0.477f, 0.0f, 0.0f},
            {-0.3f, -0.12f, 1.0f, 0.5f},
        };
        for (int i = 0; i < 3; ++i)
            for (int f = 0; f < 4; ++f)
                putf(kEnvBasisAddr + i, f, basis[i][f]);
    }

    // The tag quadwords the program copies through verbatim.
    puti(kSetGifTagAddr, 0, 1u);
    puti(kSetGifTagAddr, 1, 1u << 28);
    puti(kSetGifTagAddr, 2, 0xEu);
    for (int a : {kLodAddr, kZTestsAddr, kClutAddr, kAlphaAddr})
        for (int f = 0; f < 4; ++f) puti(a, f, xorshift(s));

    putf(top, 0, 2048.0f);
    putf(top, 1, 2048.0f);
    putf(top, 2, 8388607.5f);
    puti(top, 3, (uint32_t)verts);
    puti(top + 1, 0, (uint32_t)verts | (1u << 15));
    puti(top + 1, 1, (1u << 14) | (0x13u << 15) |
                         ((uint32_t)regsPerVertex(d) << 28));
    puti(top + 1, 2, d.texture ? (0x2u | (0x1u << 4) | (0x4u << 8))
                               : (0x1u | (0x4u << 4)));

    // The CLIP family reads two more blocks, and without them the handwritten
    // program cannot even be RUN here - it would read zeroed planes, decide
    // every vertex is outside and emit nothing, which compares equal to any
    // other program that emits nothing. A vacuous PASS is worse than a
    // failure, so these are written for every clip desc.
    if (d.clip) {
        // x = nearZ - GUARD, y = -farZ - GUARD, z = 0, w = GUARD: the near/far
        // judgement folded into a second clipw through biased constants.
        putf(kClipConstsAddr, 0, 1.0f - kClipGuard);
        putf(kClipConstsAddr, 1, -1000.0f - kClipGuard);
        putf(kClipConstsAddr, 2, 0.0f);
        putf(kClipConstsAddr, 3, kClipGuard);
        // Six planes as (A,B,C,D) + (E,0,0,0); inside = dot4(v,ABCD) + E >= 0.
        // NEAR IS FIRST and that ordering is load-bearing: everything which
        // survives it has w > 0, which is what makes the w-relative side
        // planes below safe to apply at all.
        static const float kPlaneV[6][4] = {
            {0.0f, 0.0f, 1.0f, 0.0f},   // near
            {-1.0f, 0.0f, 0.0f, 1.0f},  // right:  w - x >= 0
            {1.0f, 0.0f, 0.0f, 1.0f},   // left:   w + x >= 0
            {0.0f, -1.0f, 0.0f, 1.0f},  // top:    w - y >= 0
            {0.0f, 1.0f, 0.0f, 1.0f},   // bottom: w + y >= 0
            {0.0f, 0.0f, -1.0f, 0.0f},  // far
        };
        // The near plane sits where the staged geometry actually reaches:
        // this MVP puts clip z in about -7..3, so z >= -5 cuts roughly half
        // the vertices and the clipper has real work in most trials. Far is
        // parked out of reach - one plane doing nothing is worth keeping, it
        // proves the loop handles a pass that changes nothing.
        static const float kPlaneE[6] = {5.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1000.0f};
        for (int i = 0; i < 6; ++i) {
            for (int f = 0; f < 4; ++f)
                putf(kClipPlanesAddr + i * 2, f, kPlaneV[i][f]);
            putf(kClipPlanesAddr + i * 2 + 1, 0, kPlaneE[i]);
        }
    }

    // A project's own program reads two more quadwords, and they sit in the
    // lights-colour block the loop above just filled with noise - so they are
    // written LAST and unconditionally. Zeros here are what make the identity
    // check meaningful: a stage bound to a mesh slot must produce output
    // bit-identical to the base program when the mesh asks for nothing.
    if (d.custom) {
        for (int f = 0; f < 4; ++f)
            putf(kCustomParamsAddr, f, customParams ? customParams[f] : 0.0f);
        putf(kCustomTimeAddr, 0, customTime);
        putf(kCustomTimeAddr, 1, std::sin(customTime));
        putf(kCustomTimeAddr, 2, std::cos(customTime));
        putf(kCustomTimeAddr, 3, 1.0f);
    }

    // The cull family transforms on VU1, so it needs an MVP - and without one
    // the matrix is all zeros, every transformed vertex collapses to the
    // origin, and the whole per-vertex chain downstream is being compared at a
    // degenerate point. Two programs still agree there, which is why the gap
    // survived: it makes the check PASS while testing almost nothing. A
    // plausible view-projection instead, chosen so the clip W stays comfortably
    // positive (w = 60 - z over object-space z in +-5) - persCorrect divides by
    // it, and a w through zero would put the comparison in the saturation
    // region rather than in the arithmetic.
    if (d.cull || d.clip) {
        const float mvp[4][4] = {{1.2f, 0.0f, 0.0f, 0.0f},
                                 {0.0f, 1.6f, 0.0f, 0.0f},
                                 {0.0f, 0.0f, -1.002f, -1.0f},
                                 {0.0f, 0.0f, -2.0f, 60.0f}};
        for (int r = 0; r < 4; ++r)
            for (int f = 0; f < 4; ++f) putf(kMvpMatrixAddr + r, f, mvp[r][f]);
    }

    int addr = top + kVertDataAddr;
    for (int i = 0; i < verts; ++i) {
        if (d.cull || d.clip) {
            // Object space, and W is 1 - a mesh vertex, not a clipper output.
            //
            // A CLIP trial spreads x and y far wider, and that is the whole
            // difference between a real test and a vacuous one. With the cull
            // range the projected |x| never approaches w, every triangle is
            // trivially inside, the Sutherland-Hodgman loop is never entered
            // and two programs that both skip it compare equal. At +-40 the
            // sides genuinely cross, so most trials exercise the clipper and
            // some still take the fully-inside fast path - which is the mix
            // worth comparing.
            const float span = d.clip ? 40.0f : 5.0f;
            putf(addr + i, 0, randomFloat(s, -span, span));
            putf(addr + i, 1, randomFloat(s, -span, span));
            putf(addr + i, 2, randomFloat(s, -5.0f, 5.0f));
            putf(addr + i, 3, 1.0f);
            continue;
        }
        // NDC positions with a healthy clip W: as_is never sees w <= 0, the EE
        // clipper removed those before the packet was built.
        putf(addr + i, 0, randomFloat(s, -1.2f, 1.2f));
        putf(addr + i, 1, randomFloat(s, -1.2f, 1.2f));
        putf(addr + i, 2, randomFloat(s, -1.0f, 1.0f));
        putf(addr + i, 3, randomFloat(s, 0.5f, 90.0f));
    }
    addr += verts;
    if (d.texture) {
        for (int i = 0; i < verts; ++i) {
            putf(addr + i, 0, randomFloat(s, -2.0f, 3.0f));
            putf(addr + i, 1, randomFloat(s, -2.0f, 3.0f));
            putf(addr + i, 2, 1.0f);
            putf(addr + i, 3, 0.0f);
        }
        addr += verts;
    }
    for (int i = 0; i < verts; ++i)
        for (int f = 0; f < 4; ++f)
            putf(addr + i, f,
                 d.dirLights ? randomFloat(s, -1.0f, 1.0f)
                             : randomFloat(s, -20.0f, 300.0f));
    return mem;
}

}  // namespace

Equivalence equivalence(const Program& a, const Program& b, const Desc& d,
                        int trials, uint32_t seed, const float* customParams,
                        float customTime) {
    Equivalence eq;
    eq.handwrittenInstrs = (int)a.code.size();
    eq.generatedInstrs = (int)b.code.size();
    if (a.code.empty() || b.code.empty()) {
        eq.error = "one of the programs is empty";
        return eq;
    }
    eq.ran = true;
    eq.identical = true;
    const int top = 22;  // VU1_STAPIP_LAST_ITEM_ADDR + 1

    for (int t = 0; t < trials; ++t) {
        uint32_t s = seed + (uint32_t)t * 2654435761u;
        if (s == 0) s = 1;
        const int verts = 3 * (1 + (int)(xorshift(s) % 8));
        const bool single = !d.dirLights && (xorshift(s) & 1) != 0;
        uint32_t sa = s, sb = s;
        const std::vector<uint32_t> memA =
            stageInput(d, top, verts, sa, single, customParams, customTime);
        const std::vector<uint32_t> memB =
            stageInput(d, top, verts, sb, single, customParams, customTime);

        vusim::Config cfg;
        cfg.top = top;
        const vusim::Result ra = vusim::run(a, memA, cfg);
        const vusim::Result rb = vusim::run(b, memB, cfg);
        eq.trials = t + 1;
        eq.vertices = verts;
        if (!ra.ok || !rb.ok) {
            eq.identical = false;
            eq.firstBadTrial = t;
            eq.error = !ra.ok ? "handwritten: " + ra.error
                              : "generated: " + rb.error;
            return eq;
        }
        if (ra.kicks != rb.kicks) {
            eq.identical = false;
            eq.firstBadTrial = t;
            eq.detail = "the two programs kicked different addresses";
            return eq;
        }
        // Compare the GIF stream the GS would actually read: from the kicked
        // address through the whole tag block and vertex payload.
        const int start = ra.kicks.empty() ? 0 : ra.kicks.front();
        // A CLIP program emits a variable count: a triangle cut by six planes
        // can reach nine vertices, and fan triangulation turns those into
        // seven triangles. Compare a window generous enough to cover the worst
        // case - anything neither program wrote is zero in both, and a program
        // that emitted MORE than its twin differs inside the window, which is
        // exactly the failure worth catching.
        const int fanOut = d.clip ? 8 : 1;
        const int quads = tagQuads(d) + verts * fanOut * regsPerVertex(d);
        for (int qw = start; qw < start + quads; ++qw)
            for (int f = 0; f < 4; ++f) {
                const size_t k = (size_t)qw * 4 + f;
                if (k >= ra.mem.size() || k >= rb.mem.size()) continue;
                if (ra.mem[k] == rb.mem[k]) continue;
                char buf[256];
                std::snprintf(buf, sizeof buf,
                              "quadword %d field %c: handwritten 0x%08X, "
                              "generated 0x%08X (trial %d, %d vertices)",
                              qw, "xyzw"[f], ra.mem[k], rb.mem[k], t, verts);
                eq.identical = false;
                eq.firstBadTrial = t;
                eq.detail = buf;
                if (std::getenv("VUGEN_DUMP")) {
                    std::fprintf(stderr, "kick %d, %d input vertices\n", start,
                                 verts);
                    for (int w = start; w < start + quads; ++w) {
                        std::fprintf(stderr, "%4d ", w);
                        for (int f = 0; f < 4; ++f)
                            std::fprintf(stderr, " %08X", ra.mem[(size_t)w * 4 + f]);
                        std::fprintf(stderr, "   |");
                        for (int f = 0; f < 4; ++f)
                            std::fprintf(stderr, " %08X", rb.mem[(size_t)w * 4 + f]);
                        std::fprintf(stderr, "\n");
                    }
                }
                return eq;
            }
    }
    return eq;
}

// ---------------------------------------------------------------------------
// Budget
// ---------------------------------------------------------------------------

namespace {

/** Which VF registers one instruction reads and writes. The IR shares operand
 * slots between ops, so this is per-op knowledge and there is no shortcut: `s1`
 * is a VF source on `sq` and an INTEGER register on `mfir`, `base` is always
 * integer, and `dst` is the accumulator or Q on a whole family. */
void vfTouched(const Instr& in, std::vector<int16_t>& reads, int16_t& write) {
    write = -1;
    auto readVf = [&](int16_t r) {
        if (r > 0) reads.push_back(r);  // vf00 is hardwired, never allocated
    };
    switch (in.op) {
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Mula:
        case Op::Madd: case Op::Madda: case Op::Msub: case Op::Msuba:
        case Op::Mini: case Op::Max: case Op::Clipw:
            readVf(in.s1);
            if (in.s2kind == Src::Vf) readVf(in.s2);
            // madd/msub also READ their destination's accumulator, not the
            // register; the register itself is only written.
            if (in.dst > 0) write = in.dst;
            break;
        case Op::Move: case Op::Mr32: case Op::Abs: case Op::Ftoi0:
        case Op::Ftoi4: case Op::Itof0:
            if (in.s2kind == Src::Vf) readVf(in.s2);
            if (in.dst > 0) write = in.dst;
            break;
        case Op::Div: case Op::Rsqrt:
            readVf(in.s1);
            readVf(in.s2);
            break;
        case Op::Sqrt:
            readVf(in.s2);
            break;
        case Op::Mtir:
            readVf(in.s1);
            break;
        case Op::Mfir:
            if (in.dst > 0) write = in.dst;
            break;
        case Op::Lq:
            if (in.dst > 0) write = in.dst;
            break;
        case Op::Sq:
            readVf(in.s1);
            break;
        default:
            break;  // integer, control flow, structural
    }
}

}  // namespace

Pressure vfPressure(const Program& p) {
    Pressure out;
    out.names = (int)p.vfNames.size() - 1;  // less vf00
    const int n = (int)p.code.size();
    if (n == 0) return out;
    std::vector<int> first((size_t)p.vfNames.size(), -1);
    std::vector<int> last((size_t)p.vfNames.size(), -1);
    std::vector<int16_t> reads;
    for (int i = 0; i < n; ++i) {
        reads.clear();
        int16_t write = -1;
        vfTouched(p.code[i], reads, write);
        for (int16_t r : reads) {
            if ((size_t)r >= first.size()) continue;
            // A register read before anything wrote it is a per-mesh constant
            // loaded in the preamble; treat the read as its start rather than
            // dropping it, or the constants would look free.
            if (first[r] < 0) first[r] = i;
            last[r] = i;
        }
        if (write > 0 && (size_t)write < first.size()) {
            if (first[write] < 0) first[write] = i;
            if (last[write] < i) last[write] = i;
        }
    }
    // Sweep: how many ranges cover each instruction.
    for (int i = 0; i < n; ++i) {
        int live = 0;
        for (size_t r = 1; r < first.size(); ++r)
            if (first[r] >= 0 && first[r] <= i && i <= last[r]) ++live;
        if (live > out.peak) {
            out.peak = live;
            out.at = i;
        }
    }
    if (out.at >= 0)
        for (size_t r = 1; r < first.size(); ++r)
            if (first[r] >= 0 && first[r] <= out.at && out.at <= last[r])
                out.live.push_back(p.vfNames[r]);
    return out;
}

Budget budget(const std::vector<std::pair<std::string, const Program*>>& set,
              int ceiling) {
    Budget out;
    out.ceiling = ceiling;
    for (const auto& e : set) {
        if (!e.second) continue;
        int n = 0;
        for (const Instr& in : e.second->code)
            if (in.op != Op::Label && in.op != Op::Barrier && in.op != Op::Cont)
                ++n;
        BudgetEntry be;
        be.name = e.first;
        be.emitted = n;
        be.slotsMin = (n + 1) / 2;
        be.slotsMax = n;
        out.entries.push_back(be);
        out.totalMin += be.slotsMin;
        out.totalMax += be.slotsMax;
    }
    return out;
}

}  // namespace vugen
