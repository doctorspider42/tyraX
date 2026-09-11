#!/usr/bin/env python3
"""The VU instruction table, read out of openvcl's own source, plus a scanner
that turns a .vcl or a .vsm into a list of implicit-resource events.

WHY THE TABLE IS PARSED RATHER THAN RETYPED.  The ACC audit that missed a live
miscompile did so because its scanner carried a hand-written list of accumulator
MNEMONICS (adda|mula|madda) - and VCL lets the DESTINATION OPERAND select the
accumulator form, so `add.xy acc, ...` never matched.  Everything here is derived
from src/VuInstructionInfo.cpp: the mnemonics, their implicit reads and writes,
and the `acc:dest:write` patterns.  The dest-operand rule is applied explicitly
below (`resolve`), and `p8-flagorder.py --self-test` asserts that `add.xy acc`
classifies as an ACC WRITER.
"""
import os
import re

FIELDS = {"x": 1, "y": 2, "z": 4, "w": 8}
ALLF = 15

# rows where the tokeniser found more instructions than a row can hold
ANOMALIES = []

MACRO_DEFAULTS = {
    # macro name           reads               writes
    "VU_UPPER_DEST":        ("", "MAC|STATUS"),
    "VU_UPPER_BC":          ("", "MAC|STATUS"),
    "VU_UPPER_DEST_READS":  ("$", "MAC|STATUS"),
    "VU_UPPER_DEST_ACC_READ":  ("ACC", "MAC|STATUS"),
    "VU_UPPER_DEST_ACC_READS": ("ACC|$", "MAC|STATUS"),
    "VU_UPPER_BC_ACC_READ":    ("ACC", "MAC|STATUS"),
    "VU_ACC_DEST":          ("", "ACC|MAC|STATUS"),
    "VU_ACC_BC":            ("", "ACC|MAC|STATUS"),
    "VU_ACC_READS":         ("$", "ACC|MAC|STATUS"),
    "VU_ACC_ACC_READ":      ("ACC", "ACC|MAC|STATUS"),
    "VU_ACC_ACC_READS":     ("ACC|$", "ACC|MAC|STATUS"),
    "VU_ACC_BC_ACC_READ":   ("ACC", "ACC|MAC|STATUS"),
}


def split_args(text):
    out, depth, cur, quote = [], 0, "", False
    for ch in text:
        if quote:
            cur += ch
            if ch == '"':
                quote = False
            continue
        if ch == '"':
            quote = True
            cur += ch
        elif ch == "(":
            depth += 1
            cur += ch
        elif ch == ")":
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def resources(text):
    return set(t.strip().replace("VU_RESOURCE_", "")
               for t in text.split("|")
               if t.strip() and t.strip() not in ("VU_RESOURCE_NONE", "NONE", ""))


class Entry(object):
    def __init__(self, name, pattern, reads, writes):
        self.name = name
        self.pattern = pattern
        self.reads = reads
        self.writes = writes
        self.acc_dest = "acc:dest:write" in pattern


class IsaTable(object):
    def __init__(self, path):
        if os.path.isdir(path):
            path = os.path.join(path, "src", "VuInstructionInfo.cpp")
        text = open(path, errors="replace").read()
        self.info = {}
        body = text[text.index("kInstructions[]"):]
        for m in re.finditer(r"\b(VU_INFO|VU_[A-Z_]+)\s*\(", body):
            macro = m.group(1)
            if macro not in MACRO_DEFAULTS and macro != "VU_INFO":
                continue
            # take the balanced argument list
            i = m.end()
            depth, start = 1, i
            while depth:
                if body[i] == "(":
                    depth += 1
                elif body[i] == ")":
                    depth -= 1
                i += 1
            args = split_args(body[start:i - 1])
            name = args[1].strip('"').lower()
            # a few table rows carry a literal where the name goes (the pipe/unit
            # forms); a mnemonic always starts with a letter.
            if not name or not name[0].isalpha():
                continue
            pattern = args[4].strip('"') if macro == "VU_INFO" else args[3].strip('"')
            if macro == "VU_INFO":
                rd, wr = resources(args[11]), resources(args[12])
            else:
                rtxt, wtxt = MACRO_DEFAULTS[macro]
                extra = args[4] if "$" in rtxt else ""
                rd = resources(rtxt.replace("$", extra))
                wr = resources(wtxt)
            e = Entry(name, pattern, rd, wr)
            old = self.info.get(name)
            # several table rows share a name (dest and broadcast forms); union
            # their resources, and keep acc_dest if ANY form has it.
            if old:
                old.reads |= rd
                old.writes |= wr
                old.acc_dest = old.acc_dest or e.acc_dest
            else:
                self.info[name] = e

    def mnemonics_reading(self, res):
        return [n for n, e in self.info.items() if res in e.reads]

    def mnemonics_writing(self, res):
        return [n for n, e in self.info.items() if res in e.writes]

    def base(self, mnemonic):
        """Exact match first, then strip one trailing broadcast letter."""
        m = mnemonic.lower()
        if m in self.info:
            return m
        if m and m[-1] in "xyzw" and m[:-1] in self.info:
            return m[:-1]
        return None

    def resolve(self, mnemonic, dest):
        """(reads, writes) for one instruction, applying the dest-operand rule.

        `add.xy acc, ...` is the ADDA entry even though it is spelled `add`.
        """
        b = self.base(mnemonic)
        if b is None:
            return None, set(), set()
        if dest is not None and dest.lower() == "acc" and "ACC" not in self.info[b].writes:
            for cand in ([b + "a"] + ([b[:-1] + "a" + b[-1]] if b[-1] in "iq" else [])):
                if cand in self.info and "ACC" in self.info[cand].writes:
                    b = cand
                    break
        e = self.info[b]
        return b, set(e.reads), set(e.writes)


class Event(object):
    def __init__(self, row, mnemonic, fields, reads, writes, imm, text):
        self.row = row
        self.mnemonic = mnemonic
        self.fields = fields
        self.reads = reads
        self.writes = writes
        self.imm = imm
        self.text = text

    def __repr__(self):
        return "<%s row=%d f=%X r=%s w=%s>" % (self.mnemonic, self.row, self.fields,
                                               ",".join(sorted(self.reads)),
                                               ",".join(sorted(self.writes)))


# SCE marks the cycles the hardware will stall on a row it did not pad.
# Only the pure LATENCY stalls are counted: a flag keeps landing during
# them, and they are the ones vcl computes exactly.  The nine THRUPUT
# annotations in the whole corpus are bandwidth estimates, and counting
# them moves three readers PAST a push the source puts below them.
STALL = re.compile(r"STALL_[A-Z_]*\s*\?(\d+)")
TOKEN = re.compile(r"[A-Za-z_][A-Za-z_0-9]*(?:\.[a-zA-Z]+)?|0[xX][0-9a-fA-F]+|-?\d+|[,()\[\]]")
IMM = re.compile(r"(0[xX][0-9a-fA-F]+|\d+)\s*$")


def parse_int(t):
    return int(t, 16) if t.lower().startswith("0x") else int(t)


def scan_program(isa, path):
    """Ordered implicit-resource events for a .vcl or a .vsm.

    `row` counts issue rows: one per emitted instruction pair in a .vsm, one per
    instruction in a .vcl.  Labels and directives take no row.
    """
    is_source = path.endswith(".vcl")
    out = []
    row = 0
    for line in open(path, errors="replace"):
        # SCE's vcl annotates the cycles the HARDWARE will stall on a row it did
        # not pad.  They are cycles like any other - a flag keeps landing during
        # them - so a row-counting model that ignores them reads flags too early
        # and invents divergences.  Two of SCE's programs turn on exactly this.
        stall = 0
        for m in STALL.finditer(line):
            stall += int(m.group(1))
        body = line.split(";")[0].split("#")[0].rstrip()
        s = body.strip()
        if not s or s.startswith("--") or s.startswith("."):
            continue
        if s.endswith(":"):
            continue
        # a label may share the line with code:  "begin:  lq vf01, 0(vi00)"
        if ":" in s.split()[0] and not s.split()[0].startswith("0x"):
            body = body[body.index(":") + 1:]
            s = body.strip()
            if not s:
                continue
        toks = TOKEN.findall(body)
        # split the row into instructions at every token that is a mnemonic
        starts = []
        for i, t in enumerate(toks):
            if t in (",", "(", ")", "[", "]"):
                continue
            m = t.split(".")[0]
            if i and toks[i - 1] in (",", "(", "["):
                continue
            if isa.base(m) is not None:
                starts.append(i)
        if not starts:
            continue
        row += 1 + stall
        # A .vsm row holds at most two instructions and a .vcl line exactly one.
        # More than that means a register ALIAS was mistaken for a mnemonic, and
        # the whole trace after it would be fiction - so it is counted, not
        # swallowed.
        if len(starts) > (1 if is_source else 2):
            ANOMALIES.append((path, row, body.strip()))
        for k, st in enumerate(starts):
            end = starts[k + 1] if k + 1 < len(starts) else len(toks)
            piece = toks[st:end]
            mn = piece[0]
            fields = ALLF
            if "." in mn:
                mn, suffix = mn.split(".", 1)
                f = 0
                for c in suffix.lower():
                    f |= FIELDS.get(c, 0)
                fields = f or ALLF
            ops = [t for t in piece[1:] if t not in (",", "(", ")", "[", "]")]
            dest = ops[0] if ops else None
            base, rd, wr = isa.resolve(mn, dest)
            if base is None:
                continue
            if not (rd | wr):
                continue
            imm = None
            for t in reversed(ops):
                if re.match(r"^(0[xX][0-9a-fA-F]+|\d+)$", t):
                    imm = parse_int(t)
                    break
            out.append(Event(row, base, fields, rd, wr, imm,
                             " ".join(piece).replace(" ,", ",")))
    return out
