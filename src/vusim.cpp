#include "vusim.hpp"

#include <array>
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

Result run(const Program& p, const std::vector<uint32_t>& initialMem,
           const Config& cfg) {
    Result res;
    Machine m;
    m.vf.assign(p.vfNames.empty() ? 1 : p.vfNames.size(), {0, 0, 0, 0});
    m.vi.assign(p.viNames.empty() ? 1 : p.viNames.size(), 0);
    // vf00 is hardwired to (0,0,0,1) - a very large share of the arithmetic in
    // these programs leans on that w.
    m.vf[0] = {0, 0, 0, floatToBits(1.0f)};
    m.mem.assign(kMemWords, 0u);
    for (size_t i = 0; i < initialMem.size() && i < (size_t)kMemWords; ++i)
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
    auto writeQ = [&](float v, int pc, int line) {
        if (m.qPending) {
            ++res.qClobbers;
            warn(pc, line,
                 "Q overwritten before it was read - the previous div/rsqrt "
                 "result is lost");
        }
        m.q = v;
        m.qPending = true;
    };

    int pc = 0;
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
        auto put = [&](int f, float v) {
            if (toAcc)
                m.acc[f] = floatToBits(v);
            else
                writeF(f, floatToBits(v));
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
            if (a < 0 || a >= kMemQuads)
                warn(pc, in.line, "quadword address " + std::to_string(a) +
                                      " is outside VU1 data memory (wraps)");
            return (size_t)(((a % kMemQuads) + kMemQuads) % kMemQuads) * 4;
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
                for (int f = 0; f < 4; ++f)
                    if (in.mask & (1 << f)) put(f, src2((f + 1) & 3));
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
                    const float v = src2(f) * (in.op == Op::Ftoi4 ? 16.0f : 1.0f);
                    putBits(f, (uint32_t)(int32_t)v);
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
                    writeQ(a == 0.0f ? 0.0f
                                     : (a > 0.0f ? 3.4028235e38f : -3.4028235e38f),
                           pc, in.line);
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
                    writeQ(3.4028235e38f, pc, in.line);
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
                if (in.dst >= 0 && (size_t)in.dst < m.vi.size())
                    m.vi[in.dst] = wrapVi(cfg.top);
                xtopSeen = true;
                break;

            case Op::Xgkick:
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
