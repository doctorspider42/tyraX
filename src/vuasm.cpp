#include "vuasm.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace vuasm {

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

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool identChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

/** Strips ";" and "//" comments. */
std::string stripComment(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == ';') return s.substr(0, i);
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/')
            return s.substr(0, i);
    }
    return s;
}

/** Whole-token replacement, one pass (see the header on vclpp's one level). */
std::string replaceTokens(const std::string& in,
                          const std::map<std::string, std::string>& table) {
    if (table.empty()) return in;
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        if (!identChar(in[i]) || std::isdigit((unsigned char)in[i])) {
            out += in[i++];
            continue;
        }
        size_t j = i;
        while (j < in.size() && identChar(in[j])) ++j;
        const std::string tok = in.substr(i, j - i);
        const auto it = table.find(tok);
        out += it == table.end() ? tok : it->second;
        i = j;
    }
    return out;
}

bool hasDefinedToken(const std::string& in,
                     const std::map<std::string, std::string>& table) {
    size_t i = 0;
    while (i < in.size()) {
        if (!identChar(in[i]) || std::isdigit((unsigned char)in[i])) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < in.size() && identChar(in[j])) ++j;
        if (table.count(in.substr(i, j - i))) return true;
        i = j;
    }
    return false;
}

/** Splits on `sep` at paren depth 0. */
std::vector<std::string> splitTop(const std::string& s, char sep) {
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (char c : s) {
        if (c == '(' || c == '[' || c == '{') ++depth;
        if (c == ')' || c == ']' || c == '}') --depth;
        if (c == sep && depth == 0) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!trim(cur).empty() || !out.empty()) out.push_back(trim(cur));
    return out;
}

/** Integer literal or a sum/difference of them. Defines are already
 * substituted by the time this runs, so "STQ_STORE_OFFSET+3" arrives as
 * "0+3". */
bool evalInt(const std::string& text, int32_t& out) {
    const std::string s = trim(text);
    if (s.empty()) return false;
    int32_t total = 0;
    int sign = 1;
    size_t i = 0;
    bool any = false;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            sign = s[i] == '-' ? -1 : 1;
            ++i;
            while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        }
        size_t j = i;
        if (j + 1 < s.size() && s[j] == '0' && (s[j + 1] == 'x' || s[j + 1] == 'X'))
            j += 2;
        while (j < s.size() && (std::isxdigit((unsigned char)s[j]) ||
                                std::isdigit((unsigned char)s[j])))
            ++j;
        if (j == i) return false;
        total += sign * (int32_t)std::strtol(s.substr(i, j - i).c_str(), nullptr, 0);
        any = true;
        sign = 1;
        i = j;
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        if (i < s.size() && s[i] != '+' && s[i] != '-') return false;
    }
    if (!any) return false;
    out = total;
    return true;
}

uint8_t maskFromSuffix(const std::string& suffix) {
    if (suffix.empty()) return MALL;
    uint8_t m = 0;
    for (char c : suffix) {
        if (c == 'x') m |= MX;
        else if (c == 'y') m |= MY;
        else if (c == 'z') m |= MZ;
        else if (c == 'w') m |= MW;
    }
    return m ? m : MALL;
}

// ---------------------------------------------------------------------------
// The vclpp layer
// ---------------------------------------------------------------------------

struct Macro {
    std::vector<std::string> params;
    std::vector<std::string> body;   // comment lines already dropped
    int line = 0;
    std::string file;
};

struct Flat {
    std::string text;
    int line = 0;
    std::string file;
};

struct Pre {
    Options opt;
    std::map<std::string, std::string> defines;
    std::map<std::string, Macro> macros;
    std::vector<Flat> out;
    std::vector<std::string> notes;
    std::string error;
    int includeDepth = 0;
};

void processText(Pre& pre, const std::string& text, const std::string& name);

bool readFileText(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

/** `Name{ a, b }` - returns the macro name, or "" when the line is not one. */
std::string macroCallName(const std::string& line, std::string* argsOut) {
    const size_t open = line.find('{');
    if (open == std::string::npos) return "";
    const size_t close = line.rfind('}');
    if (close == std::string::npos || close < open) return "";
    const std::string head = trim(line.substr(0, open));
    if (head.empty()) return "";
    for (char c : head)
        if (!identChar(c)) return "";
    if (argsOut) *argsOut = line.substr(open + 1, close - open - 1);
    return head;
}

void expandMacro(Pre& pre, const Macro& mac, const std::string& macName,
                 const std::string& argText, int callLine,
                 const std::string& callFile) {
    std::vector<std::string> args = splitTop(argText, ',');
    // A no-parameter macro invoked as "Name{ }" splits to one empty argument.
    if (args.size() == 1 && args[0].empty() && mac.params.empty()) args.clear();
    if (args.size() != mac.params.size()) {
        pre.notes.push_back(macName + ": expected " +
                            std::to_string(mac.params.size()) + " arguments, got " +
                            std::to_string(args.size()) + " (" + callFile + ":" +
                            std::to_string(callLine) + ")");
        return;
    }
    std::map<std::string, std::string> subst;
    for (size_t i = 0; i < args.size(); ++i) subst[mac.params[i]] = args[i];

    for (const std::string& raw : mac.body) {
        const std::string line = trim(stripComment(replaceTokens(raw, subst)));
        if (line.empty()) continue;
        if (!macroCallName(line, nullptr).empty()) {
            pre.notes.push_back(
                macName +
                ": a macro call inside a macro body - vclpp does NOT expand "
                "nested macros, this line reaches the assembler as-is");
            continue;
        }
        pre.out.push_back({replaceTokens(line, pre.defines), callLine, callFile});
    }
}

void processText(Pre& pre, const std::string& text, const std::string& name) {
    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        const std::string line = trim(stripComment(raw));
        if (line.empty()) continue;

        if (line.rfind("#include", 0) == 0) {
            const size_t a = line.find('"');
            const size_t b = a == std::string::npos ? a : line.find('"', a + 1);
            if (a == std::string::npos || b == std::string::npos) continue;
            const std::string rel = line.substr(a + 1, b - a - 1);
            if (pre.includeDepth > 8) {
                pre.error = "#include nested too deeply at " + name;
                return;
            }
            const std::string path =
                (std::filesystem::path(pre.opt.includeRoot) / rel).string();
            std::string body;
            if (!readFileText(path, body)) {
                pre.notes.push_back("cannot open include \"" + rel + "\" (" +
                                    path + ") - continuing without it");
                continue;
            }
            ++pre.includeDepth;
            processText(pre, body, rel);
            --pre.includeDepth;
            continue;
        }

        if (line.rfind("#define", 0) == 0) {
            const std::string rest = trim(line.substr(7));
            size_t sp = 0;
            while (sp < rest.size() && identChar(rest[sp])) ++sp;
            if (sp == 0) continue;
            const std::string key = rest.substr(0, sp);
            const std::string val = trim(rest.substr(sp));
            // vclpp expands one level only; an alias of an alias reaches the
            // assembler unresolved, so flag it instead of quietly resolving.
            if (hasDefinedToken(val, pre.defines))
                pre.notes.push_back(
                    "#define " + key + " refers to another #define - vclpp "
                    "expands only ONE level, so this reaches dvp-as unresolved");
            pre.defines[key] = val;
            continue;
        }

        if (line.rfind("#macro", 0) == 0) {
            const std::string rest = trim(line.substr(6));
            const size_t colon = rest.find(':');
            Macro mac;
            mac.line = lineNo;
            mac.file = name;
            const std::string macName =
                trim(colon == std::string::npos ? rest : rest.substr(0, colon));
            if (colon != std::string::npos)
                for (const std::string& pRaw : splitTop(rest.substr(colon + 1), ',')) {
                    const std::string p = trim(pRaw);
                    if (!p.empty()) mac.params.push_back(p);
                }
            std::string bodyLine;
            while (std::getline(in, bodyLine)) {
                ++lineNo;
                if (!bodyLine.empty() && bodyLine.back() == '\r') bodyLine.pop_back();
                if (trim(bodyLine).rfind("#endmacro", 0) == 0) break;
                const std::string t = trim(bodyLine);
                if (t.empty()) continue;
                if (t[0] == ';') continue;
                mac.body.push_back(bodyLine);
            }
            if (!macName.empty()) pre.macros[macName] = mac;
            continue;
        }

        if (line[0] == '#' || line.rfind("--", 0) == 0) {
            // #vuprog / #endvuprog / --enter / --cont / --barrier: structure
            // and scheduling hints, kept for the instruction pass.
            pre.out.push_back({line, lineNo, name});
            continue;
        }

        std::string argText;
        const std::string call = macroCallName(line, &argText);
        if (!call.empty()) {
            const auto it = pre.macros.find(call);
            if (it != pre.macros.end()) {
                expandMacro(pre, it->second, call, argText, lineNo, name);
                continue;
            }
            pre.notes.push_back("unknown macro \"" + call + "\" at " + name + ":" +
                                std::to_string(lineNo));
            continue;
        }

        pre.out.push_back({replaceTokens(line, pre.defines), lineNo, name});
    }
}

// ---------------------------------------------------------------------------
// The instruction pass
// ---------------------------------------------------------------------------

struct OpEntry {
    const char* text;
    Op op;
};

const OpEntry kOps[] = {
    {"nop", Op::Nop},       {"add", Op::Add},       {"sub", Op::Sub},
    {"mul", Op::Mul},       {"mula", Op::Mula},     {"madd", Op::Madd},
    {"madda", Op::Madda},   {"msub", Op::Msub},     {"msuba", Op::Msuba},
    {"mini", Op::Mini},     {"max", Op::Max},       {"move", Op::Move},
    {"mr32", Op::Mr32},     {"abs", Op::Abs},       {"ftoi0", Op::Ftoi0},
    {"ftoi4", Op::Ftoi4},   {"itof0", Op::Itof0},   {"clipw", Op::Clipw},
    {"clip", Op::Clipw},    {"div", Op::Div},       {"rsqrt", Op::Rsqrt},
    {"sqrt", Op::Sqrt},     {"loi", Op::Loi},       {"mtir", Op::Mtir},
    {"mfir", Op::Mfir},     {"fcand", Op::Fcand},   {"fcor", Op::Fcor},
    {"fceq", Op::Fceq},     {"fcset", Op::Fcset},   {"fcget", Op::Fcget},
    {"fmand", Op::Fmand},   {"fmeq", Op::Fmeq},     {"fmor", Op::Fmor},
    {"fsand", Op::Fsand},   {"fseq", Op::Fseq},     {"fsor", Op::Fsor},
    {"fsset", Op::Fsset},
    {"lq", Op::Lq},         {"sq", Op::Sq},         {"lqi", Op::Lq},
    {"sqi", Op::Sq},        {"ilw", Op::Ilw},       {"isw", Op::Isw},
    {"iadd", Op::Iadd},     {"iaddi", Op::Iaddi},   {"iaddiu", Op::Iaddiu},
    {"isub", Op::Isub},     {"iand", Op::Iand},     {"ior", Op::Ior},
    {"b", Op::B},           {"ibeq", Op::Ibeq},     {"ibne", Op::Ibne},
    {"iblez", Op::Iblez},   {"ibgez", Op::Ibgez},   {"ibgtz", Op::Ibgtz},
    {"ibltz", Op::Ibltz},   {"xtop", Op::Xtop},     {"xitop", Op::Xitop},
    {"xgkick", Op::Xgkick},
};

bool lookupOp(const std::string& mnem, Op& op) {
    for (const OpEntry& e : kOps)
        if (mnem == e.text) {
            op = e.op;
            return true;
        }
    return false;
}

struct Parser {
    Program* p = nullptr;
    std::vector<std::string> pendingLabels;
    std::map<std::string, int> labels;
    std::vector<std::pair<int, std::string>> fixups;  // instr index -> label
    std::string error;
};

/** A VF operand: "vf00", "name", "name[x]" (broadcast), "name[0]" (VCL register
 * array - a distinct register, NOT a field), "acc", "q", "i". */
void parseVf(Parser& ps, const std::string& textIn, int16_t& reg, uint8_t& bc,
             Src& kind) {
    const std::string text = trim(textIn);
    bc = kNoBc;
    kind = Src::Vf;
    if (text == "acc" || text == "ACC") {
        reg = kAcc;
        return;
    }
    if (text == "q" || text == "Q") {
        reg = kQ;
        kind = Src::Q;
        return;
    }
    if (text == "i" || text == "I") {
        reg = kI;
        kind = Src::I;
        return;
    }
    const size_t open = text.find('[');
    if (open == std::string::npos) {
        reg = ps.p->vf(text);
        return;
    }
    const size_t close = text.find(']', open);
    const std::string inner =
        close == std::string::npos ? "" : trim(text.substr(open + 1, close - open - 1));
    if (inner.size() == 1 && (inner[0] == 'x' || inner[0] == 'y' ||
                              inner[0] == 'z' || inner[0] == 'w')) {
        bc = inner[0] == 'x' ? 0 : inner[0] == 'y' ? 1 : inner[0] == 'z' ? 2 : 3;
        reg = ps.p->vf(trim(text.substr(0, open)));
        return;
    }
    // Digits: a VCL register array element. Each element is its own register.
    reg = ps.p->vf(text);
}

int16_t parseVi(Parser& ps, const std::string& textIn) {
    return ps.p->vi(trim(textIn));
}

/** "12(base)", "(base)" or a bare expression addressed off vi00. */
bool parseMem(Parser& ps, const std::string& textIn, int32_t& off, int16_t& base) {
    const std::string text = trim(textIn);
    const size_t open = text.find('(');
    if (open == std::string::npos) {
        base = vuir::kVi00;
        return evalInt(text, off);
    }
    const size_t close = text.rfind(')');
    if (close == std::string::npos || close < open) return false;
    const std::string offText = trim(text.substr(0, open));
    off = 0;
    if (!offText.empty() && !evalInt(offText, off)) return false;
    base = parseVi(ps, text.substr(open + 1, close - open - 1));
    return true;
}

/** Flushes any pending labels as no-op marker instructions, then appends `in`.
 * Returns the index of the appended instruction - branch fixups must be keyed
 * off THAT, not off the size before the call, because the label flush shifts
 * everything. */
int emit(Parser& ps, Instr in) {
    for (const std::string& l : ps.pendingLabels) {
        Instr lab;
        lab.op = Op::Label;
        lab.line = in.line;
        ps.p->code.push_back(lab);
        ps.labels[l] = (int)ps.p->code.size() - 1;
    }
    ps.pendingLabels.clear();
    ps.p->code.push_back(in);
    return (int)ps.p->code.size() - 1;
}

bool parseInstruction(Parser& ps, const std::string& line, int lineNo) {
    // mnemonic[.mask] operands...
    size_t sp = 0;
    while (sp < line.size() && !std::isspace((unsigned char)line[sp])) ++sp;
    std::string head = line.substr(0, sp);
    const std::string rest = trim(line.substr(sp));

    std::string mnem = head, suffix;
    const size_t dot = head.find('.');
    if (dot != std::string::npos) {
        mnem = head.substr(0, dot);
        suffix = head.substr(dot + 1);
    }
    for (char& c : mnem) c = (char)std::tolower((unsigned char)c);

    Instr in;
    in.line = lineNo;
    in.mask = maskFromSuffix(suffix);
    std::string branchLabel;  // resolved after emit(), which may shift indices

    Op op;
    Src forcedKind = Src::Vf;
    bool forceAcc = false;
    if (!lookupOp(mnem, op)) {
        // VCL spells the operand-flavoured variants into the mnemonic: mulq /
        // addi / adda. Peel one suffix and remember what it meant.
        bool ok = false;
        if (mnem.size() > 1) {
            const char last = mnem.back();
            const std::string base = mnem.substr(0, mnem.size() - 1);
            if (last == 'q' && lookupOp(base, op)) {
                forcedKind = Src::Q;
                ok = true;
            } else if (last == 'i' && lookupOp(base, op)) {
                forcedKind = Src::I;
                ok = true;
            } else if (last == 'a' && lookupOp(base, op)) {
                forceAcc = true;
                ok = true;
            }
        }
        if (!ok) {
            ps.error = "unknown instruction \"" + mnem + "\" on line " +
                       std::to_string(lineNo);
            return false;
        }
    }
    in.op = op;

    std::vector<std::string> ops = splitTop(rest, ',');
    auto need = [&](size_t n) {
        if (ops.size() < n) {
            ps.error = mnem + ": expected " + std::to_string(n) +
                       " operands on line " + std::to_string(lineNo);
            return false;
        }
        return true;
    };
    uint8_t bc = kNoBc;
    Src kind = Src::Vf;

    switch (op) {
        case Op::Nop:
            break;

        case Op::Loi: {
            if (!need(1)) return false;
            in.fimm = (float)std::atof(ops[0].c_str());
            break;
        }

        case Op::Lq: {
            if (!need(2)) return false;
            parseVf(ps, ops[0], in.dst, bc, kind);
            if (!parseMem(ps, ops[1], in.imm, in.base)) {
                ps.error = "bad memory operand on line " + std::to_string(lineNo);
                return false;
            }
            break;
        }

        case Op::Sq: {
            if (!need(2)) return false;
            parseVf(ps, ops[0], in.s1, bc, kind);
            if (!parseMem(ps, ops[1], in.imm, in.base)) {
                ps.error = "bad memory operand on line " + std::to_string(lineNo);
                return false;
            }
            break;
        }

        case Op::Ilw: {
            if (!need(2)) return false;
            in.dst = parseVi(ps, ops[0]);
            if (!parseMem(ps, ops[1], in.imm, in.base)) return false;
            break;
        }

        case Op::Isw: {
            if (!need(2)) return false;
            in.s1 = parseVi(ps, ops[0]);
            if (!parseMem(ps, ops[1], in.imm, in.base)) return false;
            break;
        }

        case Op::Iadd:
        case Op::Isub:
        case Op::Iand:
        case Op::Ior: {
            if (!need(3)) return false;
            in.dst = parseVi(ps, ops[0]);
            in.s1 = parseVi(ps, ops[1]);
            in.s2 = parseVi(ps, ops[2]);
            break;
        }

        case Op::Iaddi:
        case Op::Iaddiu: {
            if (!need(3)) return false;
            in.dst = parseVi(ps, ops[0]);
            in.s1 = parseVi(ps, ops[1]);
            if (!evalInt(ops[2], in.imm)) {
                ps.error = "bad immediate on line " + std::to_string(lineNo);
                return false;
            }
            break;
        }

        case Op::Fcset: {
            if (!need(1)) return false;
            if (!evalInt(ops[0], in.imm)) return false;
            break;
        }

        case Op::Fcget: {
            if (!need(1)) return false;
            in.dst = parseVi(ps, ops[0]);
            break;
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
            if (!need(2)) return false;
            in.dst = parseVi(ps, ops[0]);
            if (!evalInt(ops[1], in.imm)) return false;
            break;
        }

        case Op::Mtir: {
            if (!need(2)) return false;
            in.dst = parseVi(ps, ops[0]);
            parseVf(ps, ops[1], in.s1, in.bc1, kind);
            break;
        }

        case Op::Mfir: {
            if (!need(2)) return false;
            parseVf(ps, ops[0], in.dst, bc, kind);
            in.s1 = parseVi(ps, ops[1]);
            break;
        }

        case Op::Div:
        case Op::Rsqrt: {
            if (!need(3)) return false;  // "q, a[f], b[f]"
            in.dst = kQ;
            parseVf(ps, ops[1], in.s1, in.bc1, kind);
            parseVf(ps, ops[2], in.s2, in.bc2, kind);
            break;
        }

        case Op::Sqrt: {
            if (!need(2)) return false;
            in.dst = kQ;
            parseVf(ps, ops[1], in.s2, in.bc2, kind);
            break;
        }

        case Op::Clipw: {
            if (!need(2)) return false;
            parseVf(ps, ops[0], in.s1, in.bc1, kind);
            parseVf(ps, ops[1], in.s2, in.bc2, kind);
            in.mask = MXYZ;
            break;
        }

        case Op::Xtop:
        case Op::Xitop: {
            if (!need(1)) return false;
            in.dst = parseVi(ps, ops[0]);
            break;
        }

        case Op::Xgkick: {
            if (!need(1)) return false;
            in.s1 = parseVi(ps, ops[0]);
            break;
        }

        case Op::B: {
            if (!need(1)) return false;
            branchLabel = trim(ops[0]);
            break;
        }

        case Op::Ibeq:
        case Op::Ibne: {
            if (!need(3)) return false;
            in.s1 = parseVi(ps, ops[0]);
            in.s2 = parseVi(ps, ops[1]);
            branchLabel = trim(ops[2]);
            break;
        }

        case Op::Iblez:
        case Op::Ibgez:
        case Op::Ibgtz:
        case Op::Ibltz: {
            if (!need(2)) return false;
            in.s1 = parseVi(ps, ops[0]);
            branchLabel = trim(ops[1]);
            break;
        }

        case Op::Move:
        case Op::Mr32:
        case Op::Abs:
        case Op::Ftoi0:
        case Op::Ftoi4:
        case Op::Itof0: {
            if (!need(2)) return false;
            parseVf(ps, ops[0], in.dst, bc, kind);
            parseVf(ps, ops[1], in.s2, in.bc2, in.s2kind);
            break;
        }

        default: {  // three-operand float ops
            if (!need(3)) return false;
            parseVf(ps, ops[0], in.dst, bc, kind);
            parseVf(ps, ops[1], in.s1, in.bc1, kind);
            parseVf(ps, ops[2], in.s2, in.bc2, in.s2kind);
            break;
        }
    }

    if (forcedKind != Src::Vf) in.s2kind = forcedKind;
    if (forceAcc) in.dst = kAcc;
    const int at = emit(ps, in);
    if (!branchLabel.empty()) ps.fixups.push_back({at, branchLabel});
    return true;
}

}  // namespace

bool parseText(const std::string& text, const std::string& name,
               const Options& opt, Program& out, std::string& error) {
    Pre pre;
    pre.opt = opt;
    for (const auto& d : opt.defines) pre.defines[d.first] = d.second;
    processText(pre, text, name);
    if (!pre.error.empty()) {
        error = pre.error;
        return false;
    }
    out.notes = pre.notes;

    Parser ps;
    ps.p = &out;
    bool inProg = false;
    for (const Flat& f : pre.out) {
        const std::string line = trim(f.text);
        if (line.empty()) continue;

        if (line.rfind("#vuprog", 0) == 0) {
            inProg = true;
            const std::string rest = trim(line.substr(7));
            if (!rest.empty() && out.name.empty()) out.name = rest;
            continue;
        }
        if (line.rfind("#endvuprog", 0) == 0) {
            inProg = false;
            continue;
        }
        if (line.rfind(".name", 0) == 0) {
            out.name = trim(line.substr(5));
            continue;
        }
        if (line[0] == '.' || line[0] == '#' || line.rfind("--", 0) == 0) continue;
        if (!inProg) continue;

        // A label is a bare identifier followed by ':'.
        if (line.back() == ':' && line.find(' ') == std::string::npos) {
            ps.pendingLabels.push_back(line.substr(0, line.size() - 1));
            continue;
        }
        // "label: instruction" on one line.
        const size_t colon = line.find(':');
        if (colon != std::string::npos && colon > 0 &&
            line.find('(') > colon && line.find(',') > colon) {
            bool ident = true;
            for (size_t i = 0; i < colon; ++i)
                ident = ident && identChar(line[i]);
            if (ident) {
                ps.pendingLabels.push_back(line.substr(0, colon));
                const std::string tail = trim(line.substr(colon + 1));
                if (tail.empty()) continue;
                if (!parseInstruction(ps, tail, f.line)) {
                    error = ps.error;
                    return false;
                }
                continue;
            }
        }
        if (!parseInstruction(ps, line, f.line)) {
            error = ps.error;
            return false;
        }
    }

    // Any labels left pending sat at the very end of the program; give them a
    // real instruction index so a branch there terminates cleanly.
    for (const std::string& l : ps.pendingLabels) {
        Instr lab;
        lab.op = Op::Label;
        out.code.push_back(lab);
        ps.labels[l] = (int)out.code.size() - 1;
    }
    for (const auto& fix : ps.fixups) {
        const auto it = ps.labels.find(fix.second);
        if (it == ps.labels.end()) {
            error = "branch to unknown label \"" + fix.second + "\"";
            return false;
        }
        if (fix.first >= 0 && (size_t)fix.first < out.code.size())
            out.code[fix.first].target = it->second;
    }
    return true;
}

bool parseFile(const std::string& path, const Options& opt, Program& out,
               std::string& error) {
    std::string text;
    if (!readFileText(path, text)) {
        error = "cannot open " + path;
        return false;
    }
    Options o = opt;
    if (o.includeRoot.empty())
        o.includeRoot = std::filesystem::path(path).parent_path().string();
    return parseText(text, std::filesystem::path(path).filename().string(), o,
                     out, error);
}

}  // namespace vuasm
