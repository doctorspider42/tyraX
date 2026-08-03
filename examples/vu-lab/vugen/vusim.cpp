#include "vusim.hpp"

#include <array>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vusim {

using vuir::Instr;
using vuir::kAcc;
using vuir::kI;
using vuir::kNoBc;
using vuir::kQ;
using vuir::MALL;
using vuir::Op;
using vuir::Program;
using vuir::Src;

namespace {

float bitsToFloat(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

uint32_t floatToBits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

/** The largest float the VU can hold: 0x7F7FFFFF. Not a coincidence that it is
 * also what a division by zero yields - there is no other "infinity" here. */
constexpr float kVuMax = 3.4028235e38f;
/** Smallest normal float. Anything below this is a denormal, which the VU FPU
 * does not have: it reads as zero. */
constexpr float kVuMinNormal = 1.17549435e-38f;

/** The VU FPU is NOT IEEE-754, and the difference is not academic: it has no
 * infinities and no NaN. An overflowing result SATURATES to +/-kVuMax, and a
 * denormal is zero. Host floats do neither, so without this an overflow becomes
 * an `inf` here, the next subtraction turns that into a `nan`, and the whole run
 * downstream is a fiction the console could never have produced - the exact
 * failure mode that makes an unfaithful simulator worse than no simulator.
 *
 * Every float the machine WRITES goes through here (see `put`/`writeQ`). Raw bit
 * moves (`move`, `mr32`, `mfir`, `ftoi*`, `lq`/`sq`) deliberately do NOT: those
 * carry integer payloads - a packed GIF tag or an ftoi4 result - and clamping
 * one as if it were a float is how you corrupt it. */
float vuFloat(float v) {
    if (v != v) return 0.0f;  // no NaN on the VU
    if (v > kVuMax) return kVuMax;
    if (v < -kVuMax) return -kVuMax;
    if (v != 0.0f && std::fabs(v) < kVuMinNormal) return v < 0.0f ? -0.0f : 0.0f;
    return v;
}

/** `ftoi0`/`ftoi4` saturate on hardware. In C++ the same conversion is UNDEFINED
 * once the value leaves the int32 range, so clamp in double before narrowing. */
int32_t vuToInt(double d) {
    if (d != d) return 0;
    if (d >= 2147483647.0) return 2147483647;
    if (d <= -2147483648.0) return -2147483647 - 1;
    return (int32_t)d;
}

/** VU integer registers are 16 bit. Keeping them sign-extended in an int32 is
 * what makes both halves of the engine's ADC trick work: `iaddiu adcBit, VI01,
 * 0x7FFF` wraps to 0x8000 (bit 15 = ADC) while `iblez` still compares as a
 * signed 16-bit value. */
int32_t wrapVi(int32_t v) { return (int32_t)(int16_t)(v & 0xFFFF); }

/** Field index a single-field mask selects (ilw.w / isw.w / mtir). */
int singleField(uint8_t mask) {
    for (int f = 0; f < 4; ++f)
        if (mask & (1 << f)) return f;
    return 3;
}

struct Machine {
    std::vector<std::array<uint32_t, 4>> vf;
    std::vector<int32_t> vi;
    std::array<uint32_t, 4> acc{};
    float q = 0.0f;
    float ireg = 0.0f;
    uint32_t clipFlags = 0;  // 24-bit shift register, 6 bits per clipw
    std::vector<uint32_t> mem;
    bool qPending = false;  // Q written, nothing has read it yet
};

}  // namespace

namespace {

/** The VU FPU does NOT round the way the host does: it truncates toward zero,
 * where an x86 rounds to nearest-even. Ignoring that was measurable, not
 * theoretical - replaying a real console capture (`--vu-replay`) reproduced
 * screen X and Y bit for bit and missed the 24-bit Z by one or two units in the
 * last place, every time. Z is the coordinate scaled by 8388607.5 rather than
 * 2048, so it is where a single-ULP mantissa difference first shows.
 *
 * Switching the host rounding mode for the duration of a run is the whole fix:
 * every add/mul/madd below then rounds the way VU1 does. It composes with
 * `vuFloat` above rather than replacing it - that one handles the RANGE the VU
 * has (saturation, no NaN, no denormals), this one handles how it rounds inside
 * that range. Both are needed; the capture only matched once both were in. */
struct RoundTowardZero {
    int saved;
    RoundTowardZero() : saved(std::fegetround()) { std::fesetround(FE_TOWARDZERO); }
    ~RoundTowardZero() { std::fesetround(saved); }
};

}  // namespace

Result run(const Program& p, const std::vector<uint32_t>& initialMem,
           const Config& cfg) {
    const RoundTowardZero vuRounding;
    // The whole point of the target: a VU0 program addressed against 1024
    // quadwords wraps somewhere the hardware would not.
    const int kMemQuadsT = memQuads(cfg.target);
    const int kMemWordsT = memWords(cfg.target);
    Result res;
    Machine m;
    m.vf.assign(p.vfNames.empty() ? 1 : p.vfNames.size(), {0, 0, 0, 0});
    m.vi.assign(p.viNames.empty() ? 1 : p.viNames.size(), 0);
    // vf00 is hardwired to (0,0,0,1) - a very large share of the arithmetic in
    // these programs leans on that w.
    m.vf[0] = {0, 0, 0, floatToBits(1.0f)};
    m.mem.assign(kMemWordsT, 0u);
    for (size_t i = 0; i < initialMem.size() && i < (size_t)kMemWordsT; ++i)
        m.mem[i] = initialMem[i];

    auto warn = [&](int pc, int line, const std::string& text) {
        if (res.warnings.size() < 64) res.warnings.push_back({pc, line, text});
    };

    // Where the per-buffer loop starts. The programs are
    // "<preamble> begin: xtop ... xgkick ... b begin", so a backward branch to
    // at-or-before the first xtop means this buffer is done.
    int firstXtop = -1;
    for (size_t i = 0; i < p.code.size(); ++i)
        if (p.code[i].op == Op::Xtop || p.code[i].op == Op::Xitop) {
            firstXtop = (int)i;
            break;
        }
    bool xtopSeen = false;

    auto readQ = [&]() {
        m.qPending = false;
        return m.q;
    };
    auto writeQ = [&](float vRaw, int pc, int line) {
        const float v = vuFloat(vRaw);
        if (m.qPending) {
            ++res.qClobbers;
            warn(pc, line,
                 "Q overwritten before it was read - the previous div/rsqrt "
                 "result is lost");
        }
        m.q = v;
        m.qPending = true;
    };

    // Micro memory is a quarter the size on VU0, and a kernel that cannot be
    // uploaded is not a runtime failure anyone will see - it is a silent
    // truncation at load. Only claim it when it is certain: VCL pairs an upper
    // and a lower op into one slot at best, so ceil(n/2) is the floor.
    {
        int real = 0;
        for (const Instr& in : p.code)
            if (in.op != Op::Label && in.op != Op::Barrier &&
                in.op != Op::Cont && in.op != Op::Nop)
                ++real;
        if ((real + 1) / 2 > microSlots(cfg.target))
            warn(0, 0,
                 std::string("the program cannot fit ") +
                     targetName(cfg.target) + " micro memory: " +
                     std::to_string(real) + " instructions need at least " +
                     std::to_string((real + 1) / 2) + " of " +
                     std::to_string(microSlots(cfg.target)) + " slots");
    }

    int pc = cfg.entry;
    const int n = (int)p.code.size();
    while (pc >= 0 && pc < n) {
        if (++res.steps > cfg.maxSteps) {
            res.hitStepLimit = true;
            res.error = "step budget exhausted - the program did not terminate";
            res.mem = m.mem;
            return res;
        }
        const Instr& in = p.code[pc];
        if (cfg.trace && (cfg.traceLimit <= 0 ||
                          (int)res.trace.size() < cfg.traceLimit)) {
            const std::string text = vuir::disassemble(p, in);
            if (!text.empty()) res.trace.push_back(text);
        }

        // --- source helpers, bound to this instruction -----------------------
        auto vfField = [&](int16_t r, int f) -> float {
            if (r == kAcc) return bitsToFloat(m.acc[f]);
            if (r < 0 || (size_t)r >= m.vf.size()) return 0.0f;
            return bitsToFloat(m.vf[r][f]);
        };
        auto src1 = [&](int f) {
            return vfField(in.s1, in.bc1 == kNoBc ? f : in.bc1);
        };
        auto src2 = [&](int f) -> float {
            if (in.s2kind == Src::I) return m.ireg;
            if (in.s2kind == Src::Q) return readQ();
            return vfField(in.s2, in.bc2 == kNoBc ? f : in.bc2);
        };
        auto writeF = [&](int f, uint32_t bits) {
            if (in.dst == kAcc) {
                m.acc[f] = bits;
                return;
            }
            if (in.dst < 0 || (size_t)in.dst >= m.vf.size()) return;
            m.vf[in.dst][f] = bits;
        };
        auto accF = [&](int f) { return bitsToFloat(m.acc[f]); };
        // "a"-variant ops write the accumulator whatever the written dst says.
        const bool toAcc = in.dst == kAcc || in.op == Op::Mula ||
                           in.op == Op::Madda || in.op == Op::Msuba;
        // Every arithmetic result lands here, so this is the one place the VU's
        // saturate-and-flush float behaviour has to be applied.
        auto put = [&](int f, float vRaw) {
            const uint32_t bits = floatToBits(vuFloat(vRaw));
            if (toAcc)
                m.acc[f] = bits;
            else
                writeF(f, bits);
        };
        auto putBits = [&](int f, uint32_t b) {
            if (toAcc)
                m.acc[f] = b;
            else
                writeF(f, b);
        };
        auto memAddr = [&]() {
            const int32_t a = wrapVi(m.vi[in.base >= 0 && (size_t)in.base <
                                                                  m.vi.size()
                                              ? in.base
                                              : 0]) +
                              in.imm;
            if (a < 0 || a >= kMemQuadsT)
                warn(pc, in.line,
                     "quadword address " + std::to_string(a) + " is outside " +
                         targetName(cfg.target) + " data memory (" +
                         std::to_string(kMemQuadsT) + " quadwords - it wraps)");
            return (size_t)(((a % kMemQuadsT) + kMemQuadsT) % kMemQuadsT) * 4;
        };

        int next = pc + 1;

        switch (in.op) {
            case Op::Nop:
            case Op::Label:
            case Op::Barrier:
            case Op::Cont:
                break;

            case Op::Add:
            case Op::Sub:
            case Op::Mul:
            case Op::Mula:
            case Op::Mini:
            case Op::Max:
                for (int f = 0; f < 4; ++f) {
                    if (!(in.mask & (1 << f))) continue;
                    const float a = src1(f), b = src2(f);
                    switch (in.op) {
                        case Op::Add: put(f, a + b); break;
                        case Op::Sub: put(f, a - b); break;
                        case Op::Mini: put(f, a < b ? a : b); break;
                        case Op::Max: put(f, a > b ? a : b); break;
                        default: put(f, a * b); break;  // Mul, Mula
                    }
                }
                break;

            case Op::Madd:
            case Op::Madda:
            case Op::Msub:
            case Op::Msuba:
                for (int f = 0; f < 4; ++f) {
                    if (!(in.mask & (1 << f))) continue;
                    const float prod = src1(f) * src2(f);
                    const float base = accF(f);
                    put(f, (in.op == Op::Madd || in.op == Op::Madda)
                               ? base + prod
                               : base - prod);
                }
                break;

            case Op::Move:
                // A raw field copy, not an arithmetic one: `move` must carry
                // integer bit patterns (an ftoi4 result, a packed GIF tag)
                // through unchanged.
                for (int f = 0; f < 4; ++f) {
                    if (!(in.mask & (1 << f))) continue;
                    const int sf = in.bc2 == kNoBc ? f : in.bc2;
                    putBits(f, in.s2 == kAcc ? m.acc[sf]
                               : (in.s2 >= 0 && (size_t)in.s2 < m.vf.size())
                                   ? m.vf[in.s2][sf]
                                   : 0u);
                }
                break;

            case Op::Mr32:
                // Move family, like `move`: a raw field ROTATION (dst[f] takes
                // src[f+1]), not arithmetic. It must carry integer bit patterns
                // through untouched, and must not be clamped as a float. There
                // is no broadcast form - rotating IS the operation.
                for (int f = 0; f < 4; ++f) {
                    if (!(in.mask & (1 << f))) continue;
                    const int sf = (f + 1) & 3;
                    putBits(f, in.s2 == kAcc ? m.acc[sf]
                               : (in.s2 >= 0 && (size_t)in.s2 < m.vf.size())
                                   ? m.vf[in.s2][sf]
                                   : 0u);
                }
                break;

            case Op::Abs:
                for (int f = 0; f < 4; ++f)
                    if (in.mask & (1 << f)) put(f, std::fabs(src2(f)));
                break;

            case Op::Ftoi0:
            case Op::Ftoi4:
                // The conversion the GS reads: ftoi4 yields the value in 12.4
                // fixed point, truncated toward zero, and the INTEGER BITS stay
                // in the register for the following sq to store.
                for (int f = 0; f < 4; ++f) {
                    if (!(in.mask & (1 << f))) continue;
                    const double v =
                        (double)src2(f) * (in.op == Op::Ftoi4 ? 16.0 : 1.0);
                    putBits(f, (uint32_t)vuToInt(v));
                }
                break;

            case Op::Itof0:
                for (int f = 0; f < 4; ++f) {
                    if (!(in.mask & (1 << f))) continue;
                    const int16_t rr = in.s2;
                    const uint32_t bits =
                        (rr >= 0 && (size_t)rr < m.vf.size())
                            ? m.vf[rr][in.bc2 == kNoBc ? f : in.bc2]
                            : 0u;
                    put(f, (float)(int32_t)bits);
                }
                break;

            case Op::Clipw: {
                // Against |w| of the second operand; six bits per vertex,
                // shifted into a 24-bit window (four vertices deep).
                const float w = std::fabs(vfField(in.s2, 3));
                uint32_t bits = 0;
                for (int f = 0; f < 3; ++f) {
                    const float v = vfField(in.s1, f);
                    if (v > w) bits |= 1u << (f * 2);
                    if (v < -w) bits |= 1u << (f * 2 + 1);
                }
                m.clipFlags = ((m.clipFlags << 6) | bits) & 0xFFFFFF;
                break;
            }

            case Op::Fcset:
                m.clipFlags = (uint32_t)in.imm & 0xFFFFFF;
                break;

            case Op::Fcget:
                if (in.dst >= 0 && (size_t)in.dst < m.vi.size())
                    m.vi[in.dst] = (int32_t)(m.clipFlags & 0xFFF);
                break;

            case Op::Fcand:
            case Op::Fcor:
            case Op::Fceq:
                if (in.dst >= 0 && (size_t)in.dst < m.vi.size()) {
                    const uint32_t mask = (uint32_t)in.imm;
                    m.vi[in.dst] =
                        in.op == Op::Fcand  ? ((m.clipFlags & mask) != 0 ? 1 : 0)
                        : in.op == Op::Fcor ? (((m.clipFlags | mask) & 0xFFFFFF) ==
                                                       0xFFFFFF
                                                   ? 1
                                                   : 0)
                                            : (m.clipFlags == mask ? 1 : 0);
                }
                break;

            case Op::Fmand:
            case Op::Fmeq:
            case Op::Fmor:
            case Op::Fsand:
            case Op::Fseq:
            case Op::Fsor:
            case Op::Fsset:
                // The MAC and STATUS flags are not modelled (see vusim.hpp).
                // Yield 0 and SAY SO - a plausible-looking wrong flag would
                // send someone hunting a hardware bug that is not there.
                if (in.dst >= 0 && (size_t)in.dst < m.vi.size())
                    m.vi[in.dst] = 0;
                warn(pc, in.line,
                     "MAC/STATUS flag instruction is not modelled - this run's "
                     "result is not authoritative for programs that branch on it");
                break;

            case Op::Div: {
                const float a = vfField(in.s1, in.bc1 == kNoBc ? 0 : in.bc1);
                const float b = vfField(in.s2, in.bc2 == kNoBc ? 0 : in.bc2);
                if (b == 0.0f) {
                    ++res.divByZero;
                    warn(pc, in.line, "division by zero");
                    // Hardware yields the max float SIGNED BY BOTH OPERANDS
                    // (the D flag for x/0, the I flag for 0/0 - but the result
                    // is the saturated value either way, never zero).
                    const bool neg =
                        std::signbit(a) != std::signbit(b);
                    writeQ(neg ? -kVuMax : kVuMax, pc, in.line);
                } else {
                    writeQ(a / b, pc, in.line);
                }
                break;
            }

            case Op::Rsqrt: {
                const float a = vfField(in.s1, in.bc1 == kNoBc ? 0 : in.bc1);
                const float b = std::fabs(vfField(in.s2, in.bc2 == kNoBc ? 0 : in.bc2));
                if (b == 0.0f) {
                    ++res.divByZero;
                    warn(pc, in.line, "rsqrt of zero");
                    writeQ(std::signbit(a) ? -kVuMax : kVuMax, pc, in.line);
                } else {
                    writeQ(a / std::sqrt(b), pc, in.line);
                }
                break;
            }

            case Op::Sqrt: {
                const float b = std::fabs(vfField(in.s2, in.bc2 == kNoBc ? 0 : in.bc2));
                writeQ(std::sqrt(b), pc, in.line);
                break;
            }

            case Op::Loi:
                m.ireg = in.fimm;
                break;

            case Op::Mtir:
                if (in.dst >= 0 && (size_t)in.dst < m.vi.size()) {
                    const int16_t rr = in.s1;
                    const uint32_t bits =
                        (rr >= 0 && (size_t)rr < m.vf.size())
                            ? m.vf[rr][in.bc1 == kNoBc ? 0 : in.bc1]
                            : 0u;
                    m.vi[in.dst] = wrapVi((int32_t)(bits & 0xFFFF));
                }
                break;

            case Op::Mfir:
                for (int f = 0; f < 4; ++f)
                    if (in.mask & (1 << f))
                        putBits(f, (uint32_t)(in.s1 >= 0 &&
                                                      (size_t)in.s1 < m.vi.size()
                                                  ? m.vi[in.s1]
                                                  : 0));
                break;

            case Op::Lq: {
                const size_t a = memAddr();
                for (int f = 0; f < 4; ++f)
                    if (in.mask & (1 << f)) writeF(f, m.mem[a + f]);
                break;
            }

            case Op::Sq: {
                const size_t a = memAddr();
                for (int f = 0; f < 4; ++f) {
                    if (!(in.mask & (1 << f))) continue;
                    const int16_t rr = in.s1;
                    m.mem[a + f] = rr == kAcc ? m.acc[f]
                                   : (rr >= 0 && (size_t)rr < m.vf.size())
                                       ? m.vf[rr][f]
                                       : 0u;
                }
                break;
            }

            case Op::Ilw: {
                const size_t a = memAddr();
                if (in.dst >= 0 && (size_t)in.dst < m.vi.size())
                    m.vi[in.dst] =
                        wrapVi((int32_t)(m.mem[a + singleField(in.mask)] & 0xFFFF));
                break;
            }

            case Op::Isw: {
                const size_t a = memAddr();
                const int32_t v = (in.s1 >= 0 && (size_t)in.s1 < m.vi.size())
                                      ? m.vi[in.s1]
                                      : 0;
                m.mem[a + singleField(in.mask)] = (uint32_t)(v & 0xFFFF);
                break;
            }

            case Op::Iadd:
            case Op::Isub:
            case Op::Iand:
            case Op::Ior: {
                const int32_t a = m.vi[in.s1 >= 0 && (size_t)in.s1 < m.vi.size()
                                           ? in.s1
                                           : 0];
                const int32_t b = m.vi[in.s2 >= 0 && (size_t)in.s2 < m.vi.size()
                                           ? in.s2
                                           : 0];
                int32_t r = 0;
                switch (in.op) {
                    case Op::Iadd: r = a + b; break;
                    case Op::Isub: r = a - b; break;
                    case Op::Iand: r = (a & 0xFFFF) & (b & 0xFFFF); break;
                    default: r = (a & 0xFFFF) | (b & 0xFFFF); break;
                }
                if (in.dst >= 0 && (size_t)in.dst < m.vi.size())
                    m.vi[in.dst] = wrapVi(r);
                break;
            }

            case Op::Iaddi:
            case Op::Iaddiu: {
                const int32_t a = m.vi[in.s1 >= 0 && (size_t)in.s1 < m.vi.size()
                                           ? in.s1
                                           : 0];
                if (in.dst >= 0 && (size_t)in.dst < m.vi.size())
                    m.vi[in.dst] = wrapVi(a + in.imm);
                break;
            }

            case Op::Xtop:
            case Op::Xitop:
                // VU0 is fed by VIF0, which the engine never double-buffers -
                // a VU0 kernel addresses fixed data-memory locations because it
                // has to. An xtop here yields whatever the harness set and is
                // almost certainly a program written against the wrong unit.
                if (cfg.target == Target::VU0)
                    warn(pc, in.line,
                         "xtop/xitop on VU0: there is no VIF1 double buffer "
                         "here, so this yields the harness value rather than a "
                         "buffer base");
                if (in.dst >= 0 && (size_t)in.dst < m.vi.size())
                    m.vi[in.dst] = wrapVi(cfg.top);
                xtopSeen = true;
                break;

            case Op::Xgkick:
                // VU0 has no path to the GIF at all (PATH1 belongs to VU1).
                // Nothing is staged, so the run continues - but a kernel that
                // tries this would draw nothing on hardware and say nothing
                // about it.
                if (cfg.target == Target::VU0) {
                    warn(pc, in.line,
                         "xgkick on VU0: VU0 has no GIF path (PATH1 is VU1's) "
                         "- on hardware this draws nothing");
                    break;
                }
                res.kicks.push_back(
                    wrapVi(m.vi[in.s1 >= 0 && (size_t)in.s1 < m.vi.size()
                                    ? in.s1
                                    : 0]));
                break;

            case Op::B:
            case Op::Ibeq:
            case Op::Ibne:
            case Op::Iblez:
            case Op::Ibgez:
            case Op::Ibgtz:
            case Op::Ibltz: {
                const int32_t a = m.vi[in.s1 >= 0 && (size_t)in.s1 < m.vi.size()
                                           ? in.s1
                                           : 0];
                const int32_t b = m.vi[in.s2 >= 0 && (size_t)in.s2 < m.vi.size()
                                           ? in.s2
                                           : 0];
                bool take = false;
                switch (in.op) {
                    case Op::B: take = true; break;
                    case Op::Ibeq: take = a == b; break;
                    case Op::Ibne: take = a != b; break;
                    case Op::Iblez: take = a <= 0; break;
                    case Op::Ibgez: take = a >= 0; break;
                    case Op::Ibgtz: take = a > 0; break;
                    default: take = a < 0; break;  // Ibltz
                }
                if (take) {
                    if (in.target < 0) {
                        res.error = "branch with an unresolved target";
                        res.mem = m.mem;
                        return res;
                    }
                    // A backward branch to the buffer loop = this buffer is
                    // done. The real program waits for VIF to hand it the next
                    // one; the host only ever simulates one.
                    if (xtopSeen && firstXtop >= 0 && in.target <= firstXtop) {
                        res.ok = true;
                        res.mem = m.mem;
                        return res;
                    }
                    next = in.target;
                }
                break;
            }

            case Op::End:
                res.ok = true;
                res.mem = m.mem;
                return res;
        }
        pc = next;
    }

    res.ok = true;
    res.mem = m.mem;
    return res;
}

std::vector<Result> runKernel(
    const Program& p, const std::vector<uint32_t>& initialMem,
    const std::vector<std::vector<KernelWrite>>& perCall, const Config& cfg) {
    std::vector<Result> out;
    // Data memory PERSISTS across vcallms - that is the whole difference
    // between a kernel and a pipeline program, and staging it fresh per call
    // would hide a kernel that reads something the previous call left behind.
    std::vector<uint32_t> mem(memWords(cfg.target), 0u);
    for (size_t i = 0; i < initialMem.size() && i < mem.size(); ++i)
        mem[i] = initialMem[i];
    for (const auto& writes : perCall) {
        for (const KernelWrite& w : writes)
            if (w.word >= 0 && (size_t)w.word < mem.size()) mem[w.word] = w.value;
        out.push_back(run(p, mem, cfg));
        mem = out.back().mem;
        if (!out.back().ok) break;
    }
    return out;
}

std::string listing(const Program& p) {
    std::string out;
    char buf[64];
    // Which instructions are branch targets, so the listing can name them.
    std::vector<int> isTarget(p.code.size(), 0);
    for (const Instr& in : p.code)
        if (in.target >= 0 && (size_t)in.target < isTarget.size())
            isTarget[in.target] = 1;

    for (size_t i = 0; i < p.code.size(); ++i) {
        if (isTarget[i]) {
            std::snprintf(buf, sizeof buf, "L%d:\n", (int)i);
            out += buf;
        }
        const std::string text = vuir::disassemble(p, p.code[i]);
        if (text.empty()) continue;
        std::snprintf(buf, sizeof buf, "%4d  ", (int)i);
        out += buf;
        out += text;
        out += "\n";
    }
    return out;
}

}  // namespace vusim
