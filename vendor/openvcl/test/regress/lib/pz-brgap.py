#!/usr/bin/env python3
"""How far, ALONG A PATH, is a conditional branch from the producer of its condition?

WHY THIS IS A KIND OF ITS OWN, AND NOT A VALUE ORACLE.  Every other checker in
this directory compares WHAT a program computes.  The defect this one exists for
does not change what any instruction computes - it changes WHEN a value is
readable.  A VU integer register written in one row and tested by a branch in the
next is a hazard, not an expression, so pa-dag, pb-dag and pd-cond all report a
clean program and the picture on the television is wrong anyway.  Both value
oracles were run over the engine's whole shipping corpus on a build carrying the
defect and reported 0 divergent over 277 traces.  That is the reason for a fourth
question rather than a fourth case.

THE SHAPE, which is the part no file-order check can see.  openvcl's baseline is
one bubble row in front of every conditional branch; --branch-bubble-on-dependency
narrows that to branches whose condition is produced nearby, and asks the question
of the one or two SCHEDULE SLOTS above the branch.  A branch that is the first row
of a LABELLED block has another predecessor - the DELAY SLOT of every jump to that
label - and a delay slot is exactly where --emit-delay-fillers puts an integer op.
The file shows the producer far above the label; the hardware executes it one row
before the branch.

WHAT IS MEASURED.  The emitted rows become a control-flow graph the way
CodeGenerator::insertOneCrossPathBranchBubble builds one - a branch fans out from
its DELAY SLOT, never from the branch row - and for every conditional branch, the
fewest rows any path executes strictly between a writer of one of its condition
registers and the branch.

WHAT IS EXEMPT, AND WHY IT MUST BE.  Loads and CLIP/MAC/STATUS readers feeding a
branch: --branch-interlock is the deliberate decision to let the hardware wait for
those, and Sony's vcl emits exactly that shape and annotates the stall it leaves
behind (`iblez VI01,multiColor ; STALL_LATENCY ?3`).  Counting them would report
the reference as broken, which is the definition of an instrument that has to be
thrown away.

ROWS, NOT CYCLES.  Every row is at least one cycle, so a gap at or above the
threshold is proof and a gap below it is a question.  The threshold here is not a
hardware measurement and is not claimed to be one: it is the emitter's OWN
invariant, the bubble it already emits when it can see the dependency, and this
asserts that the invariant holds along paths as well as down the page.  Sony's
vcl, over the engine's 25 programs, never puts an ordinary integer producer closer
than one row to the branch that tests it - 122 sites, floor 1, zero at 0 - which
is the positive control for the threshold.

Usage:
  pz-brgap.py <openvcl-clone-or-VuInstructionInfo.cpp> <src-dir> <vsm-dir> [min]
Prints one line per offending program (named so run.sh can match it) and exits
non-zero if any program is below `min` (default 1).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from p8isa import IsaTable

ROW = re.compile(r"^\s+\S")
BR_NONE, BR_COND, BR_UNCOND, BR_INDIRECT = 0, 1, 2, 3
UNREACHED = 10 ** 6
CAP = 48


class Isa(object):
    """Which mnemonics write an integer register, which branch on one, and which
    producers --branch-interlock exempts - all read out of the table rather than
    listed here, so a table change cannot leave a stale copy behind."""

    def __init__(self, path):
        t = IsaTable(path)
        self.known = set(t.info)
        self.intwrite = {}          # mnemonic -> operand index of the destination
        self.cond = {}              # mnemonic -> operand indices of conditions
        self.exempt = set()
        for name, e in t.info.items():
            ops = [o.strip() for o in e.pattern.split(",")] if e.pattern else []
            if ops and ops[0].startswith("vi") and ":write" in ops[0]:
                self.intwrite[name] = 0
            if ops and ops[-1].endswith("imm:branch"):
                idx = [i for i, o in enumerate(ops) if o == "vi"]
                if idx:
                    self.cond[name] = idx
            # A memory form carries its address register in parentheses; those are
            # the loads, and they are interlocked for a branch.
            if any("(vi)" in o for o in ops):
                self.exempt.add(name)
            if e.reads & {"CLIP", "MAC", "STATUS"}:
                self.exempt.add(name)
        # `ilw` with an immediate offset spells its address operand differently in
        # the table's alternation, so name the two loads directly as well.
        self.exempt |= {"ilw", "ilwr"}


def parse(path, isa):
    """-> rows of [(mnemonic, [operand tokens])], labels {name: row index}"""
    rows, labels = [], {}
    for line in open(path, errors="replace"):
        text = line.split(";")[0].rstrip()
        stripped = text.strip()
        if not stripped:
            continue
        if not ROW.match(text):
            if stripped.endswith(":"):
                labels.setdefault(stripped[:-1].strip().lower(), len(rows))
            continue
        # Tokenise the whole row and start a new instruction at every token that
        # IS a mnemonic.  Splitting on runs of whitespace does NOT work: Sony's
        # vcl pads between a mnemonic and its operands, and doing it that way
        # reported zero sites for the entire reference arm.
        ins = []
        for tok in re.split(r"[\s,]+", stripped):
            if not tok:
                continue
            base = tok.split(".")[0].split("[")[0].lower()
            if base in isa.known:
                ins.append((base, []))
            elif ins:
                ins[-1][1].append(tok.lower())
        rows.append(ins)
    return rows, labels


def graph(rows, labels, isa):
    n = len(rows)
    kind = [BR_NONE] * n
    target = [None] * n
    for i, ins in enumerate(rows):
        for mn, ops in ins:
            if mn in ("jr", "jalr"):
                kind[i] = BR_INDIRECT
            elif mn in ("b", "bal"):
                kind[i] = BR_UNCOND
            elif mn in isa.cond:
                kind[i] = BR_COND
            if kind[i] in (BR_COND, BR_UNCOND):
                for o in reversed(ops):
                    if o in labels:
                        target[i] = labels[o]
                        break
    succ = [[] for _ in range(n)]
    for i in range(n):
        if kind[i] == BR_NONE:
            if i + 1 < n:
                succ[i].append(i + 1)
            continue
        if kind[i] == BR_INDIRECT or i + 1 >= n:
            continue
        succ[i].append(i + 1)
        slot = i + 1
        if target[i] is not None:
            succ[slot].append(target[i])
        if kind[i] == BR_COND and slot + 1 < n:
            succ[slot].append(slot + 1)
    return kind, succ


def int_writes(row, isa):
    out = set()
    for mn, ops in row:
        idx = isa.intwrite.get(mn)
        if idx is None or mn in isa.exempt:
            continue
        if idx < len(ops) and re.match(r"^vi\d+$", ops[idx]) and ops[idx] != "vi00":
            out.add(ops[idx])
    return out


def cond_regs(row, isa):
    out = set()
    for mn, ops in row:
        for idx in isa.cond.get(mn, ()):
            if idx < len(ops) and re.match(r"^vi\d+$", ops[idx]) and ops[idx] != "vi00":
                out.add(ops[idx])
    return out


def mindist(succ, n, srcs, dest):
    dist = [UNREACHED] * n
    work = []
    for s in srcs:
        for nx in succ[s]:
            if dist[nx] > 0:
                dist[nx] = 0
                work.append(nx)
    best = UNREACHED
    while work:
        i = work.pop(0)
        if i == dest:
            best = min(best, dist[i])
            continue
        if i in srcs or dist[i] >= CAP:
            continue
        for nx in succ[i]:
            if dist[i] + 1 < dist[nx]:
                dist[nx] = dist[i] + 1
                work.append(nx)
    return best


def worst_site(path, isa):
    rows, labels = parse(path, isa)
    kind, succ = graph(rows, labels, isa)
    writes = [int_writes(r, isa) for r in rows]
    worst = (UNREACHED, None)
    for i, k in enumerate(kind):
        if k != BR_COND:
            continue
        for reg in cond_regs(rows[i], isa):
            srcs = {p for p in range(len(rows)) if reg in writes[p]}
            if not srcs:
                continue
            d = mindist(succ, len(rows), srcs, i)
            if d < worst[0]:
                worst = (d, (i, reg))
    return worst


def main():
    isa = Isa(sys.argv[1])
    src, vsm = sys.argv[2], sys.argv[3]
    need = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    bad = 0
    for name in sorted(os.listdir(vsm)):
        if not name.endswith(".vsm"):
            continue
        d, site = worst_site(os.path.join(vsm, name), isa)
        if d == UNREACHED:
            continue
        if d < need:
            bad += 1
            print("%s: conditional branch at row %d reads %s only %d row(s) after "
                  "an integer producer on some path (need %d)"
                  % (name[:-4], site[0], site[1], d, need))
    print("pz-brgap: %d program(s) below %d" % (bad, need))
    return 1 if bad else 0


sys.exit(main())
