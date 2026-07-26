#include "vuir.hpp"

#include <cstdio>

namespace vuir {

namespace {

struct OpText {
    Op op;
    const char* text;
};

// Spelling table. Order is irrelevant; the lookup is linear and this runs at
// emission/diagnostic time only.
const OpText kOpText[] = {
    {Op::Nop, "nop"},       {Op::Add, "add"},     {Op::Sub, "sub"},
    {Op::Mul, "mul"},       {Op::Mula, "mula"},   {Op::Madd, "madd"},
    {Op::Madda, "madda"},   {Op::Msub, "msub"},   {Op::Msuba, "msuba"},
    {Op::Mini, "mini"},     {Op::Max, "max"},     {Op::Move, "move"},
    {Op::Mr32, "mr32"},     {Op::Abs, "abs"},     {Op::Ftoi0, "ftoi0"},
    {Op::Ftoi4, "ftoi4"},   {Op::Itof0, "itof0"}, {Op::Clipw, "clipw"},
    {Op::Div, "div"},       {Op::Rsqrt, "rsqrt"}, {Op::Sqrt, "sqrt"},
    {Op::Loi, "loi"},       {Op::Mtir, "mtir"},   {Op::Mfir, "mfir"},
    {Op::Fcand, "fcand"},   {Op::Fcor, "fcor"},   {Op::Fceq, "fceq"},
    {Op::Fcset, "fcset"},   {Op::Fcget, "fcget"}, {Op::Fmand, "fmand"},
    {Op::Fmeq, "fmeq"},     {Op::Fmor, "fmor"},   {Op::Fsand, "fsand"},
    {Op::Fseq, "fseq"},     {Op::Fsor, "fsor"},   {Op::Fsset, "fsset"},
    {Op::Lq, "lq"},
    {Op::Sq, "sq"},         {Op::Ilw, "ilw"},     {Op::Isw, "isw"},
    {Op::Iadd, "iadd"},     {Op::Iaddi, "iaddi"}, {Op::Iaddiu, "iaddiu"},
    {Op::Isub, "isub"},     {Op::Iand, "iand"},   {Op::Ior, "ior"},
    {Op::B, "b"},           {Op::Ibeq, "ibeq"},   {Op::Ibne, "ibne"},
    {Op::Iblez, "iblez"},   {Op::Ibgez, "ibgez"}, {Op::Ibgtz, "ibgtz"},
    {Op::Ibltz, "ibltz"},   {Op::Xtop, "xtop"},   {Op::Xitop, "xitop"},
    {Op::Xgkick, "xgkick"}, {Op::Label, ":"},     {Op::End, "--exit"},
    {Op::Barrier, "--barrier"}, {Op::Cont, "--cont"},
};

const char* opText(Op op) {
    for (const OpText& e : kOpText)
        if (e.op == op) return e.text;
    return "???";
}

/** True for the float ops that write a masked destination field. */
bool isFloatOp(Op op) {
    switch (op) {
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Mula:
        case Op::Madd: case Op::Madda: case Op::Msub: case Op::Msuba:
        case Op::Mini: case Op::Max: case Op::Move: case Op::Mr32:
        case Op::Abs: case Op::Ftoi0: case Op::Ftoi4: case Op::Itof0:
        case Op::Clipw:
            return true;
        default:
            return false;
    }
}

}  // namespace

const char* fieldName(uint8_t bc) {
    switch (bc) {
        case 0: return "x";
        case 1: return "y";
        case 2: return "z";
        case 3: return "w";
        default: return "";
    }
}

std::string maskSuffix(uint8_t mask) {
    if (mask == MALL) return "";
    std::string s = ".";
    if (mask & MX) s += "x";
    if (mask & MY) s += "y";
    if (mask & MZ) s += "z";
    if (mask & MW) s += "w";
    return s;
}

int16_t Program::vf(const std::string& name) {
    const int16_t found = findVf(name);
    if (found >= 0) return found;
    vfNames.push_back(name);
    return (int16_t)(vfNames.size() - 1);
}

int16_t Program::vi(const std::string& name) {
    const int16_t found = findVi(name);
    if (found >= 0) return found;
    viNames.push_back(name);
    return (int16_t)(viNames.size() - 1);
}

int16_t Program::findVf(const std::string& name) const {
    for (size_t i = 0; i < vfNames.size(); ++i)
        if (vfNames[i] == name) return (int16_t)i;
    return -1;
}

int16_t Program::findVi(const std::string& name) const {
    for (size_t i = 0; i < viNames.size(); ++i)
        if (viNames[i] == name) return (int16_t)i;
    return -1;
}

namespace {

std::string vfText(const Program& p, int16_t r) {
    if (r == kAcc) return "acc";
    if (r == kQ) return "q";
    if (r == kI) return "i";
    if (r == kR) return "r";
    if (r < 0 || (size_t)r >= p.vfNames.size()) return "vf??";
    return p.vfNames[r];
}

std::string viText(const Program& p, int16_t r) {
    if (r < 0 || (size_t)r >= p.viNames.size()) return "vi??";
    return p.viNames[r];
}

std::string srcText(const Program& p, const Instr& in) {
    if (in.s2kind == Src::I) return "i";
    if (in.s2kind == Src::Q) return "q";
    std::string s = vfText(p, in.s2);
    if (in.bc2 != kNoBc) s += std::string("[") + fieldName(in.bc2) + "]";
    return s;
}

}  // namespace

std::string disassemble(const Program& p, const Instr& in) {
    char buf[256];
    const std::string mnem = std::string(opText(in.op)) +
                             (isFloatOp(in.op) ? maskSuffix(in.mask) : "");

    switch (in.op) {
        case Op::Label:
            return std::string();  // the listing printer names these itself
        case Op::Barrier:
            return "--barrier";
        case Op::Cont:
            return "--cont";
        case Op::Nop:
            return "nop";
        case Op::End:
            return "--exit";
        case Op::Lq: {
            std::snprintf(buf, sizeof buf, "%-10s %s, %d(%s)", mnem.c_str(),
                          vfText(p, in.dst).c_str(), in.imm,
                          viText(p, in.base).c_str());
            return buf;
        }
        case Op::Sq: {
            std::snprintf(buf, sizeof buf, "%-10s %s, %d(%s)", mnem.c_str(),
                          vfText(p, in.s1).c_str(), in.imm,
                          viText(p, in.base).c_str());
            return buf;
        }
        case Op::Ilw: {
            std::snprintf(buf, sizeof buf, "ilw%s      %s, %d(%s)",
                          maskSuffix(in.mask).c_str(), viText(p, in.dst).c_str(),
                          in.imm, viText(p, in.base).c_str());
            return buf;
        }
        case Op::Isw: {
            std::snprintf(buf, sizeof buf, "isw%s      %s, %d(%s)",
                          maskSuffix(in.mask).c_str(), viText(p, in.s1).c_str(),
                          in.imm, viText(p, in.base).c_str());
            return buf;
        }
        case Op::Iadd:
        case Op::Isub:
        case Op::Iand:
        case Op::Ior: {
            std::snprintf(buf, sizeof buf, "%-10s %s, %s, %s", mnem.c_str(),
                          viText(p, in.dst).c_str(), viText(p, in.s1).c_str(),
                          viText(p, in.s2).c_str());
            return buf;
        }
        case Op::Iaddi:
        case Op::Iaddiu: {
            std::snprintf(buf, sizeof buf, "%-10s %s, %s, %d", mnem.c_str(),
                          viText(p, in.dst).c_str(), viText(p, in.s1).c_str(),
                          in.imm);
            return buf;
        }
        case Op::Loi: {
            std::snprintf(buf, sizeof buf, "loi        %g", (double)in.fimm);
            return buf;
        }
        case Op::Mtir: {
            std::snprintf(buf, sizeof buf, "mtir       %s, %s[%s]",
                          viText(p, in.dst).c_str(), vfText(p, in.s1).c_str(),
                          fieldName(in.bc1));
            return buf;
        }
        case Op::Mfir: {
            std::snprintf(buf, sizeof buf, "mfir%s     %s, %s",
                          maskSuffix(in.mask).c_str(), vfText(p, in.dst).c_str(),
                          viText(p, in.s1).c_str());
            return buf;
        }
        case Op::Fcset: {
            std::snprintf(buf, sizeof buf, "fcset      0x%06X", (unsigned)in.imm);
            return buf;
        }
        case Op::Fcget: {
            std::snprintf(buf, sizeof buf, "fcget      %s",
                          viText(p, in.dst).c_str());
            return buf;
        }
        case Op::Fcand:
        case Op::Fcor:
        case Op::Fceq:
        case Op::Fmand:
        case Op::Fmeq:
        case Op::Fmor:
        case Op::Fsand:
        case Op::Fseq:
        case Op::Fsor:
        case Op::Fsset: {
            std::snprintf(buf, sizeof buf, "%-10s %s, 0x%X", mnem.c_str(),
                          viText(p, in.dst).c_str(), (unsigned)in.imm);
            return buf;
        }
        case Op::Div:
        case Op::Rsqrt: {
            std::snprintf(buf, sizeof buf, "%-10s q, %s[%s], %s[%s]",
                          mnem.c_str(), vfText(p, in.s1).c_str(),
                          fieldName(in.bc1), vfText(p, in.s2).c_str(),
                          fieldName(in.bc2));
            return buf;
        }
        case Op::Sqrt: {
            std::snprintf(buf, sizeof buf, "sqrt       q, %s[%s]",
                          vfText(p, in.s2).c_str(), fieldName(in.bc2));
            return buf;
        }
        case Op::Xtop:
        case Op::Xitop: {
            std::snprintf(buf, sizeof buf, "%-10s %s", mnem.c_str(),
                          viText(p, in.dst).c_str());
            return buf;
        }
        case Op::Xgkick: {
            std::snprintf(buf, sizeof buf, "xgkick     %s",
                          viText(p, in.s1).c_str());
            return buf;
        }
        case Op::B: {
            std::snprintf(buf, sizeof buf, "b          L%d", in.target);
            return buf;
        }
        case Op::Ibeq:
        case Op::Ibne: {
            std::snprintf(buf, sizeof buf, "%-10s %s, %s, L%d", mnem.c_str(),
                          viText(p, in.s1).c_str(), viText(p, in.s2).c_str(),
                          in.target);
            return buf;
        }
        case Op::Iblez:
        case Op::Ibgez:
        case Op::Ibgtz:
        case Op::Ibltz: {
            std::snprintf(buf, sizeof buf, "%-10s %s, L%d", mnem.c_str(),
                          viText(p, in.s1).c_str(), in.target);
            return buf;
        }
        case Op::Move:
        case Op::Mr32:
        case Op::Abs:
        case Op::Ftoi0:
        case Op::Ftoi4:
        case Op::Itof0:
        case Op::Clipw: {
            std::snprintf(buf, sizeof buf, "%-10s %s, %s", mnem.c_str(),
                          vfText(p, in.dst).c_str(), srcText(p, in).c_str());
            return buf;
        }
        default: {  // the three-operand float ops
            std::snprintf(buf, sizeof buf, "%-10s %s, %s, %s", mnem.c_str(),
                          vfText(p, in.dst).c_str(), vfText(p, in.s1).c_str(),
                          srcText(p, in).c_str());
            return buf;
        }
    }
}

}  // namespace vuir
