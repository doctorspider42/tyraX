#include "vugen.hpp"

#include <cstdio>
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
// The env (matcap) camera basis reuses the lights-matrix area: an env bag
// never carries lighting, so the two can share (stapip_vu1_shared_defines.h).
constexpr int kEnvBasisAddr = kLightsMatrixAddr;
constexpr int kVertDataAddr = 2;  // relative to the double-buffer base

}  // namespace

// ---------------------------------------------------------------------------
// Vu - the builder
// ---------------------------------------------------------------------------

Vu::Vu(Program& program) : p_(&program) {}

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

IVal Vu::fogCoefficient(Val vertex, Val fogParams, Val scratch, const char* hint) {
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
    return mtir(scratch, 0, hint);
}

void Vu::envStq(Val stq, Val envRight, Val envUp, Val envConsts,
                const Val scratch[4]) {
    const Val len = scratch[0], nrm = scratch[1], dotR = scratch[2],
              dotU = scratch[3];
    mulInto(len, stq, stq, MXYZ);
    p_->code.back().comment = "CalculateTyraEnvStq: renormalize (uses Q)";
    addInto(len, len, len.broadcast(1), MX);
    addInto(len, len, len.broadcast(2), MX);
    rsqrtQ(zero(), 3, len, 0);
    mulQInto(nrm, stq, MXYZ);
    mulInto(dotR, nrm, envRight, MXYZ);
    addInto(dotR, dotR, dotR.broadcast(1), MX);
    addInto(dotR, dotR, dotR.broadcast(2), MX);
    mulInto(dotU, nrm, envUp, MXYZ);
    addInto(dotU, dotU, dotU.broadcast(1), MX);
    addInto(dotU, dotU, dotU.broadcast(2), MX);
    emit(floatOp(Op::Add, kAcc, zero(), envConsts.broadcast(3),
                 (uint8_t)(MX | MY)));
    emit(floatOp(Op::Add, kAcc, zero(), envConsts.broadcast(2), MZ));
    maddInto(stq, envConsts, dotR.broadcast(0), MX);
    maddInto(stq, envConsts, dotU.broadcast(0), MY);
    maddInto(stq, zero(), zero().broadcast(0), MZ);
}

void Vu::transform(Val dst, const Val m[4], Val v) {
    mulAcc(m[0], v.broadcast(0), MALL);
    p_->code.back().comment = "MatrixMultiplyVertex";
    maddAcc(m[1], v.broadcast(1), MALL);
    maddAcc(m[2], v.broadcast(2), MALL);
    maddInto(dst, m[3], v.broadcast(3), MALL);
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

std::vector<Desc> allAsIsDescs() {
    return {descAsIsColor(), descAsIsTextureColor(), descAsIsDirLights(),
            descAsIsTextureDirLights(), descAsIsTextureEnv()};
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
int attrStreams(const Desc& d) {
    int n = 1;
    if (d.texture) n += 1;
    n += 1;  // colors, or normals for the lit variants
    return n;
}

void buildAsIsBody(const Desc& d, Program& prog) {
    Vu b(prog);
    const int stride = regsPerVertex(d);
    const int stq = 0, rgba = d.texture ? 1 : 0, xyz = d.texture ? 2 : 1;

    // --- preamble: the per-mesh constants ---------------------------------
    const Val gifSetTag = b.named("gifSetTag");
    {
        Instr in;
        in.op = Op::Lq;
        in.dst = gifSetTag.reg;
        in.base = b.izero().reg;
        in.imm = kSetGifTagAddr;
        in.comment = "VU1_SET_GIFTAG_ADDR";
        prog.code.push_back(in);
    }

    Val singleColor{}, lightMatrix[3], lightDirs[3], lightColors[3], ambient{};
    IVal singleColorEnabled{};
    const bool colorStream = !d.dirLights;
    if (colorStream) {
        singleColor = b.named("singleColor");
        prog.code.push_back([&] {
            Instr in;
            in.op = Op::Lq;
            in.dst = singleColor.reg;
            in.base = b.izero().reg;
            in.imm = kSingleColorAddr;
            in.comment = "VU1_SINGLE_COLOR_ADDR";
            return in;
        }());
        singleColorEnabled = b.inamed("singleColorEnabled");
        prog.code.push_back([&] {
            Instr in;
            in.op = Op::Ilw;
            in.dst = singleColorEnabled.reg;
            in.base = b.izero().reg;
            in.imm = kOptionsAddr;
            in.mask = MX;
            in.comment = "VU1_OPTIONS_ADDR.x = single colour flag";
            return in;
        }());
    } else {
        // LoadTyraDirectionalLights: the rotation part of the world matrix,
        // three light directions, three light colours and the ambient term.
        static const char* mn[3] = {"lightMatrix[0]", "lightMatrix[1]",
                                    "lightMatrix[2]"};
        static const char* dn[3] = {"lightDirections[0]", "lightDirections[1]",
                                    "lightDirections[2]"};
        static const char* cn[3] = {"lightColors[0]", "lightColors[1]",
                                    "lightColors[2]"};
        for (int i = 0; i < 3; ++i) {
            lightMatrix[i] = b.named(mn[i]);
            prog.code.push_back([&] {
                Instr in;
                in.op = Op::Lq;
                in.dst = lightMatrix[i].reg;
                in.base = b.izero().reg;
                in.imm = kLightsMatrixAddr + i;
                in.mask = MXYZ;
                if (i == 0) in.comment = "LoadTyraDirectionalLights";
                return in;
            }());
        }
        for (int i = 0; i < 3; ++i) {
            lightDirs[i] = b.named(dn[i]);
            prog.code.push_back([&] {
                Instr in;
                in.op = Op::Lq;
                in.dst = lightDirs[i].reg;
                in.base = b.izero().reg;
                in.imm = kLightsDirsAddr + i;
                in.mask = MXYZ;
                return in;
            }());
        }
        for (int i = 0; i < 3; ++i) {
            lightColors[i] = b.named(cn[i]);
            prog.code.push_back([&] {
                Instr in;
                in.op = Op::Lq;
                in.dst = lightColors[i].reg;
                in.base = b.izero().reg;
                in.imm = kLightsColorsAddr + i;
                in.mask = MXYZ;
                return in;
            }());
        }
        ambient = b.named("ambientColor");
        prog.code.push_back([&] {
            Instr in;
            in.op = Op::Lq;
            in.dst = ambient.reg;
            in.base = b.izero().reg;
            in.imm = kLightsColorsAddr + 3;
            in.mask = MXYZ;
            return in;
        }());
    }

    const Val lodGifTag = b.named("lodGifTag");
    prog.code.push_back([&] {
        Instr in;
        in.op = Op::Lq;
        in.dst = lodGifTag.reg;
        in.base = b.izero().reg;
        in.imm = kLodAddr;
        in.comment = "VU1_LOD_ADDR";
        return in;
    }());
    const Val testsTag = b.named("testsTag");
    prog.code.push_back([&] {
        Instr in;
        in.op = Op::Lq;
        in.dst = testsTag.reg;
        in.base = b.izero().reg;
        in.imm = kZTestsAddr;
        in.comment = "VU1_Z_TESTS_ADDR";
        return in;
    }());
    Val clutTag{};
    if (d.texture) {
        clutTag = b.named("texBufferClutGifTag");
        prog.code.push_back([&] {
            Instr in;
            in.op = Op::Lq;
            in.dst = clutTag.reg;
            in.base = b.izero().reg;
            in.imm = kClutAddr;
            in.comment = "VU1_CLUT_ADDR";
            return in;
        }());
    }
    const Val alphaTag = b.named("alphaGifTag");
    prog.code.push_back([&] {
        Instr in;
        in.op = Op::Lq;
        in.dst = alphaTag.reg;
        in.base = b.izero().reg;
        in.imm = kAlphaAddr;
        in.comment = "VU1_ALPHA_ADDR - per-mesh GS blend equation, in-band";
        return in;
    }());
    const Val fogParams = b.named("fogParams");
    prog.code.push_back([&] {
        Instr in;
        in.op = Op::Lq;
        in.dst = fogParams.reg;
        in.base = b.izero().reg;
        in.imm = kOptionsAddr;
        in.comment = "VU1_OPTIONS_ADDR.zw = GS hardware fog";
        return in;
    }());

    Val envRight{}, envUp{}, envConsts{};
    if (d.env) {
        static const char* en[3] = {"envRight", "envUp", "envConsts"};
        Val* slot[3] = {&envRight, &envUp, &envConsts};
        for (int i = 0; i < 3; ++i) {
            *slot[i] = b.named(en[i]);
            prog.code.push_back([&] {
                Instr in;
                in.op = Op::Lq;
                in.dst = slot[i]->reg;
                in.base = b.izero().reg;
                in.imm = kEnvBasisAddr + i;
                if (i == 0)
                    in.comment =
                        "LoadTyraEnvBasis - VU1_ENV_BASIS_ADDR (the lights "
                        "matrix area; env bags carry no lighting)";
                return in;
            }());
        }
    }

    // --- per-buffer ---------------------------------------------------------
    const Lbl begin = b.label("begin");
    b.bind(begin);
    const IVal buffer = b.inamed("buffer");
    b.xtop(buffer);

    const Val scale = b.named("scale");
    prog.code.push_back([&] {
        Instr in;
        in.op = Op::Lq;
        in.dst = scale.reg;
        in.base = buffer.reg;
        in.imm = 0;
        in.mask = MXYZ;
        in.comment = "LoadTyraBufferTags";
        return in;
    }());
    const Val primTag = b.named("primTag");
    prog.code.push_back([&] {
        Instr in;
        in.op = Op::Lq;
        in.dst = primTag.reg;
        in.base = buffer.reg;
        in.imm = 1;
        return in;
    }());

    const IVal vertexData = b.inamed("vertexData");
    b.iaddiuInto(vertexData, buffer, kVertDataAddr);
    prog.code.back().comment = "VU1_STAPIP_VERT_DATA_ADDR";
    const IVal vertexCount = b.inamed("vertexCount");
    prog.code.push_back([&] {
        Instr in;
        in.op = Op::Ilw;
        in.dst = vertexCount.reg;
        in.base = buffer.reg;
        in.imm = 0;
        in.mask = MW;
        return in;
    }());

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
        b.branchIfLez(singleColorEnabled, multi);
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
    int q = 0;
    b.sq(gifSetTag, destAddress, q++);
    prog.code.back().comment = "GIF tag block";
    b.sq(testsTag, destAddress, q++);
    b.sq(gifSetTag, destAddress, q++);
    b.sq(lodGifTag, destAddress, q++);
    if (d.texture) {
        b.sq(gifSetTag, destAddress, q++);
        b.sq(clutTag, destAddress, q++);
    }
    b.sq(gifSetTag, destAddress, q++);
    b.sq(alphaTag, destAddress, q++);
    b.sq(primTag, destAddress, q++);
    b.iaddiuInto(destAddress, destAddress, q);

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
        b.branchIfLez(singleColorEnabled, multiColor);
        for (int i = 0; i < 3; ++i) b.addInto(color[i], b.zero(), singleColor);
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

    Val envScratch[4];
    if (d.env) {
        static const char* sn[4] = {"tyraEnvLen", "tyraEnvNrm", "tyraEnvDotR",
                                    "tyraEnvDotU"};
        for (int i = 0; i < 4; ++i) envScratch[i] = b.named(sn[i]);
    }

    for (int i = 0; i < 3; ++i) {
        // The env ST goes FIRST: its rsqrt writes Q, so the position divide
        // below has to be the last Q write before the perspective mulq.
        if (d.env) b.envStq(st[i], envRight, envUp, envConsts, envScratch);
        b.scaleToGsFormat(vertex[i], scale);
        if (d.texture) {
            b.divQ(b.zero(), 3, vertex[i], 3);
            b.mulQInto(outStq[i], st[i], MALL);
            prog.code.back().comment = "PerformTexturePerspectiveCorrection";
        }
        if (!colorStream) {
            // CalculateTyraDirectionalLights, instruction for instruction.
            b.mulAcc(lightMatrix[0], normal[i].broadcast(0), MXYZ);
            prog.code.back().comment = "CalculateTyraDirectionalLights";
            b.maddAcc(lightMatrix[1], normal[i].broadcast(1), MXYZ);
            b.maddInto(normal[i], lightMatrix[2], normal[i].broadcast(2), MXYZ);
            b.mulAcc(lightDirs[0], normal[i].broadcast(0), MXYZ);
            b.maddAcc(lightDirs[1], normal[i].broadcast(1), MXYZ);
            b.maddInto(color[i], lightDirs[2], normal[i].broadcast(2), MXYZ);
            b.minimumInto(color[i], color[i], b.zero().broadcast(3), MXYZ);
            b.maximumInto(color[i], color[i], b.zero().broadcast(0), MXYZ);
            b.mulAcc(lightColors[0], color[i].broadcast(0), MXYZ);
            b.maddAcc(lightColors[1], color[i].broadcast(1), MXYZ);
            b.maddAcc(lightColors[2], color[i].broadcast(2), MXYZ);
            b.maddInto(color[i], ambient, b.zero().broadcast(3), MXYZ);
            b.loadI(128.0f);
            {
                Instr in;
                in.op = Op::Add;
                in.dst = color[i].reg;
                in.s1 = b.zero().reg;
                in.s2 = kI;
                in.s2kind = Src::I;
                in.mask = MW;
                prog.code.push_back(in);
            }
        }
        b.fixColor(color[i]);
    }

    const Val fogScratch = b.named("fogAccum");
    for (int i = 0; i < 3; ++i) {
        const IVal fogInt =
            b.fogCoefficient(vertex[i], fogParams, fogScratch, "fogInt");
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
}

// ---------------------------------------------------------------------------
// The .vclpp emitter
// ---------------------------------------------------------------------------

std::string emitVclpp(const Desc& d, const Program& p) {
    std::string out;
    auto line = [&](const std::string& s) {
        out += s;
        out += "\n";
    };

    line("; Generated by TyraX (src/vugen.cpp). Do not edit - regenerate from");
    line("; the C++ description with: tyrax-editor --vu-emit <dir>");
    line(";");
    line("; " + d.title);
    line("; AsIs = NO TRANSFORM: the EE clipper already produced NDC positions,");
    line("; with the clip-space W kept for fog and the texture perspective divide.");
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
    s += "#include \"renderer/3d/pipeline/static/core/programs/as_is/" +
         d.fileStem + "_program.hpp\"\n\n";
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
    s += "  u32 addr = VU1_STAPIP_VERT_DATA_ADDR;\n\n";
    s += "  // Vertices\n";
    s += "  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->vertices,\n";
    s += "                                   qbuffer->size, true);\n";
    s += "  addr += qbuffer->size;\n";
    if (d.texture) {
        s += d.env ? "\n  // Normals - they ride in the ST slot (StaPipTextureBag)\n"
                   : "\n  // ST\n";
        s += "  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->sts, "
             "qbuffer->size,\n";
        s += "                                   true);\n";
    }
    if (d.dirLights) {
        s += "\n  // Normals\n";
        s += "  packet2_utils_vu_add_unpack_data(packet, addr, qbuffer->normals,\n";
        s += "                                   qbuffer->size, true);\n";
    } else {
        s += "\n  // Colors - absent when the mesh draws in a single colour\n";
        s += "  if (qbuffer->bag->color->single == nullptr) {\n";
        s += "    addr += qbuffer->size;\n";
        s += "    packet2_utils_vu_add_unpack_data(packet, addr, "
             "qbuffer->colors,\n";
        s += "                                     qbuffer->size, true);\n";
        s += "  }\n";
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
    buildAsIsBody(d, out.program);
    out.vclpp = emitVclpp(d, out.program);
    out.eeHeader = emitEeHeader(d);
    out.eeSource = emitEeSource(d);
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
                                 bool singleColor) {
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

    int addr = top + kVertDataAddr;
    for (int i = 0; i < verts; ++i) {
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
                        int trials, uint32_t seed) {
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
        const std::vector<uint32_t> memA = stageInput(d, top, verts, sa, single);
        const std::vector<uint32_t> memB = stageInput(d, top, verts, sb, single);

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
        const int quads = tagQuads(d) + verts * regsPerVertex(d);
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
                return eq;
            }
    }
    return eq;
}

// ---------------------------------------------------------------------------
// Budget
// ---------------------------------------------------------------------------

Budget budget(const std::vector<std::pair<std::string, const Program*>>& set) {
    Budget out;
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
