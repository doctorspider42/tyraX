#!/usr/bin/env python3
"""The value-DAG oracle, extended across control flow AND ACROSS CONDITIONS.

WHAT THIS FILE ADDS TO pb-dag.py, AND WHY IT HAD TO BE ADDED.  pb-dag.py runs
the SOURCE first, records its branch decisions, and REPLAYS that exact list on
the emitted program.  That is what makes the two walks the same path and it is
why a value divergence can never be an artefact of comparing two different
paths - but it also means the emitted program's own branch operands are never
looked at.  A value that is STORED is compared; a value whose only reader is a
branch condition is not compared at all, because the outcome was taken from the
source instead of evaluated from the emitted program's registers.  An assembler
that compares against the wrong register, or fills a delay slot with a write
that clobbers the operand of the branch below it, or updates a loop counter on
the wrong side of a back edge, is invisible to every instrument in this work.
The delay-slot bug fixed in the previous round was exactly that shape and had
to be found another way.

So: for every conditional branch on the trace, both sides now record the
EXPRESSION DAG OF THE VALUE THE BRANCH TESTS, and the two sequences are
compared elementwise.  The k-th conditional branch of the emitted program must
test the same value as the k-th conditional branch of the source.  The branch
sequence itself (mnemonic, target) is already required to match before any of
this runs, so the k-th on one side is the k-th on the other by construction.

HOW A REAL DISAGREEMENT IS TOLD FROM TWO CORRECT PROGRAMS COMPUTING THE SAME
CONDITION DIFFERENTLY.  Four layers, in order of how much work they do:

  1. The condition is a VALUE, not a register.  The DAG is interned, so a
     counter openvcl keeps in VI08 and SCE keeps in VI11 is the same node.
     Register allocation is invisible to this by construction; nothing has to
     be normalised away for it.  This is the whole reason the comparison is on
     the DAG and not on the operand text.
  2. Everything pb-dag.py already normalises applies unchanged - integer
     constant folding, commutativity of IADD/IAND/IOR, and the ioff() splitting
     that makes an address SCE folded into a store offset equal to the one
     openvcl computed in a register.
  3. An equality test is normalised to its DIFFERENCE.  `ibeq a,b` is true
     exactly when a-b is zero, so the condition recorded is min(a-b, b-a) - one
     node for the unordered pair, which makes `ibeq VI10,VI13` and
     `ibeq VI13,VI10` one condition, and which also survives an emitter that
     advanced BOTH sides of the comparison by the same constant before the
     branch.  The ORDER tests (ibgez/ibgtz/iblez/ibltz) are against a hard zero
     and get no such licence - shifting their operand by a constant IS a
     different test, and is reported.

     THIS ONE WAS NOT FORCED BY ANYTHING, and the honest record is that it was
     written first and justified afterwards.  PD_EQ_NORM=pair keeps the literal
     unordered operand pair instead, and on every corpus here - the 70, 400
     control-flow programs, 960 condition-stress programs, both assemblers -
     the two settings give bit-identical verdicts.  Neither assembler was ever
     observed to shift both sides of an equality.  It is kept because it is
     sound and cheap, not because it earned its place.
  4. A condition that depends on a register neither program ever wrote on the
     trace is UNJUDGEABLE, not divergent: the source calls it `vertexCounter`
     and the emitted program calls it VI08, and there is no honest way to pair
     the two names.  Counted and reported separately rather than silently
     dropped.

WHAT IS REPORTED.  A mismatch is graded by how much can be proved about it:

  COND-EDGE   both conditions fold to constants and the predicate answers
              DIFFERENTLY - the emitted program provably takes the other edge.
  COND-VALUE  the two expressions differ and at least one is symbolic, so the
              edge cannot be decided here.  Still a finding: the emitted
              program is testing a different value from the one the source
              names.
  COND-CONST  the expressions differ, both fold to constants, and on THIS trace
              the predicate happens to answer the same way.

All three fail the program.  COND-CONST was a note in the first version of this
file and that was a mistake the mutation controls caught: an off-by-one in a
loop counter's initialiser comes out as `tests const(-7), source tests
const(-6)`, both negative, both `ibltz` taken - so grading it as a note shipped
a real miscompile as clean.  The grade says how much the finding proves; it
does not say whether it is one.

PD_COND=0 turns the whole extension off, which is how the store-value results
of pb-dag.py are reproduced from this file to confirm nothing else moved.

pa-dag.py compares two assemblers' output value-by-value, but only over the
straight-line region from entry to the first branch.  That put 69 of the 70 real
programs out of scope, because every one of them has loops - and every one of the
four miscompiles found in this work lived in a loop body.  This file closes that
gap.

HOW CONTROL FLOW IS HANDLED, AND WHY NOT PHI NODES.  The obvious construction is
a phi at each join and a bounded unrolling of each loop.  A phi is a *merge*: it
throws away which arm produced the value and keeps "one of these two".  Two
assemblers that pick different arms for a value that is equal along both paths
then look the same, and two that agree look different whenever the merge is
built in a different order - which is exactly the normalisation swamp that sank
three earlier instruments in this work.  What a phi is an approximation OF is the
set of paths, so this walks the paths directly: a trace is a straight-line
sequence of rows through the CFG, and along one trace the oracle is the same
exact instrument pa-dag.py already is.  Path-sensitive is strictly stronger than
phi-merged, and it needs no normalisation at all.

Bounded unrolling is then just a cap on how many times a trace may jump to any
one label (PB_UNROLL, default 2 - so a bottom-tested loop entered by fall-through
runs its body three times).  A loop-carried mistake is "iteration N reads a value
belonging to iteration N-1", and two iterations of the body make it visible.

THE TWO PROGRAMS ARE MADE TO WALK THE SAME PATH.  The source is run first and
its branch decisions are RECORDED; the emitted program is then REPLAYED against
that exact list.  Neither side gets to choose its own path, so a divergence can
never be an artefact of comparing two different paths.  Alongside the decisions
the walk records the sequence of (mnemonic, target-label) it passed through; if
the two sequences differ, the CFGs are not the same shape and the program is
reported as such rather than judged.

WHAT THIS BUYS OVER THE STRAIGHT-LINE ORACLE.  Four resource mechanics only
exist across an edge:

  * a CLIP judgement pushed at the bottom of a loop body and read at the top of
    the next iteration - the reader is four rows from a push that is a whole
    vertex old (report SS1, SS4, SS9);
  * a value written below a loop's top and read above it (report SS3);
  * a value loaded above the loop and clobbered inside it (report SS2), which
    shows up here as the second iteration storing a colour where the source
    stores a GIF tag.

  * a CLIP judgement whose distance from its reader DEPENDS ON THE PATH: four
    rows below its push in the file, one row below it along a taken branch.
    Both assemblers pad against the layout, so both get this wrong.

All four are ordinary value divergences once the trace crosses the edge.

Q AND P ARE MODELLED WITH REAL LATENCIES, WHICH IS WHAT PUTS THE ENGINE IN SCOPE.
pa-dag.py declared any program with two divisions out of scope, and 22 of the
engine's 25 have three or more, so the Q rule excluded more of the corpus than
control flow did.  Both assemblers issue a second `div` while the first is still
in flight and place the first consumer between the two completions; a
"last division issued" model pairs that consumer with the wrong divisor.  The
FDIV and EFU completion delays come from openvcl's own table - and from SCE,
which settled the EFU column the table is ambiguous about.

AND THAT NEEDS THE HARDWARE STALLS, WHICH ONLY ONE ASSEMBLER WRITES DOWN.  SCE
annotates `STALL_LATENCY ?3`; openvcl annotates nothing, because
`--fmac-interlock` is exactly the decision to let the hardware wait instead of
spending an instruction on it.  Reading openvcl's rows as cycles judged 38 of the
70 real programs miscompiled on Q alone.  The interlock model below (fitted
against SCE's annotations, 99.9% row-exact) gives those rows their cycles, and
PB_HW_MARGIN declares a pairing that the model decides by fewer than N cycles
UNJUDGEABLE rather than reporting it.

Usage:
  pb-dag.py <openvcl-clone> <src-dir> <vsm-dir> [<vsm-dir> ...]
Exit status is 0 only when every program's observables match its source on every
trace.
"""
import os
import random
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from p8isa import IsaTable

FIELDS = "xyzw"
CLIP_LATENCY = int(os.environ.get("PB_CLIP_LAT", 4))
UNROLL = int(os.environ.get("PB_UNROLL", 2))
ROW_CAP = int(os.environ.get("PB_ROW_CAP", 20000))
# The E bit is ignored.  Both assemblers emit VCL's `--exit` epilogue (a NOP with
# the E bit and its delay NOP) BEFORE the `b begin` that `--cont` asks for, so a
# walk that stopped at the E bit would never cross the batch loop's back edge in
# either program.  The oracle compares the loop the SOURCE describes, along the
# same path in both; whether the hardware retires at the E bit decides whether a
# finding is live, not whether the two assemblers agree.
MODEL_STATUS = os.environ.get("PB_STATUS", "0") == "1"
MODEL_MAC = os.environ.get("PB_MAC", "0") == "1"
# Loads are pure and keyed by address by default: two loads from one address are
# one value.  With PB_LOAD_EPOCH=1 a load also carries the number of back edges
# taken so far, which makes a load hoisted out of a loop body visible as a
# divergence.  Off by default until the SCE control says it is safe - a legal
# hoist would otherwise be reported as a miscompile.
LOAD_EPOCH = os.environ.get("PB_LOAD_EPOCH", "0") == "1"
CLIP_ROWS = os.environ.get("PB_CLIP_ROWS", "0") == "1"
QSCALE = float(os.environ.get("PB_QLAT_SCALE", 1.0))
# How many cycles of doubt the modelled interlock is allowed.  For a file
# that carries SCE's own annotations the cycle count is exact and this is 0;
# for openvcl's unannotated output the stalls are modelled, the model
# reproduces 99.9% of SCE's annotations but not 100%, and a Q or P consumer
# that sits within this many cycles of its producer's landing is declared
# UNJUDGEABLE rather than reported.  Reporting it instead is how the first
# run of this oracle "found" a miscompile in 38 of the 70 real programs.
HW_MARGIN = int(os.environ.get("PB_HW_MARGIN", 0))
# What a margin does to a program.  "reject" is the original behaviour: one
# ambiguous Q or P read and the WHOLE program is unjudgeable - at margin 2 that
# is 175 of 480, and everything else those programs compute goes uninspected
# with it.  "taint" marks only the ambiguous VALUE and drops the observables and
# conditions that rest on it, so the rest of the program is still compared. The
# tainted count is reported, so the gap is a number instead of a silence.
MARGIN_MODE = os.environ.get("PB_MARGIN_MODE", "reject")
# The condition extension.  PD_COND=0 reproduces pb-dag.py exactly.
DO_COND = os.environ.get("PD_COND", "1") == "1"
# How an equality test is normalised.  "diff" records min(a-b, b-a); "pair"
# records the unordered pair of operands.  See the SCE control notes.
EQ_NORM = os.environ.get("PD_EQ_NORM", "diff")
# What to do with a condition that depends on a register never written on the
# trace.  "skip" declares it unjudgeable (the honest answer, since the source
# calls it `vertexCounter` and the emitted program calls it VI08); "strict"
# compares it anyway, which is only useful for measuring how many there are.
OPAQUE = os.environ.get("PD_OPAQUE", "skip")

# `--` and `*`/`/` are tokens.  Without them the tokeniser DROPPED the `--` of a
# pre-decrement - `lqd VF01,(--VI02)` read as the plain `(VI02)` the mnemonic
# never means - and truncated a multiplicative offset at the operator, so
# `lq v0, 2*3+1(base)` modelled as offset 4.  Both are silent: the model just
# addressed the wrong quadword and said nothing.  `--` is placed AFTER the
# number alternatives so `iaddi VI01,VI01,-1` still tokenises as `-1`.
TOKEN = re.compile(r"[A-Za-z_][A-Za-z_0-9]*(?:\.[a-zA-Z]+)?|0[xX][0-9a-fA-F]+"
                   r"|[-+]?\d*\.\d+(?:[eE][-+]?\d+)?|[-+]?\d+|[,()\[\]]|\+\+"
                   r"|--|[*/]")
STALL = re.compile(r"STALL_[A-Z_]*\s*\?(\d+)")
BITMARK = re.compile(r"\[[EDTI]\]")


class Unsupported(Exception):
    pass


class Budget(Exception):
    pass


# ------------------------------------------------------------------ the DAG
#
# A node is an integer handle (class R) into a global table, not a nested tuple.
# pa-dag.py used nested tuples, which is fine for a 40-row program: hashing one
# is O(size of the tree) and its trees are shallow.  A trace through three
# iterations of a batch loop with a per-vertex loop inside it is 700 rows and the
# address expressions nest once per iteration, so the same construction spends
# all its time rehashing the same subtrees.  Handles make every comparison and
# every hash O(1) and cost nothing in fidelity: the table is interned, so two
# structurally identical expressions are the same handle no matter which program
# built them, which is the normalisation the oracle wants anyway.

class R(object):
    __slots__ = ("i",)

    def __init__(self, i):
        self.i = i

    def __hash__(self):
        return self.i

    def __eq__(self, o):
        return isinstance(o, R) and o.i == self.i

    def __ne__(self, o):
        return not self.__eq__(o)

    def __lt__(self, o):
        return self.i < o.i

    def __repr__(self):
        return "R%d" % self.i


_NODES = []
_INTERN = {}


def nd(r):
    return _NODES[r.i]


def _mk(a):
    r = _INTERN.get(a)
    if r is None:
        r = R(len(_NODES))
        _NODES.append(a)
        _INTERN[a] = r
    return r


def N(*a):
    """A DAG node in canonical form.

    The four VALUE-PRESERVING IDIOMS both emitters use are folded away here, for
    the reason pa-dag.py records: they are how an assembler fills an idle slot,
    not a difference in what the program computes.  MAX(a,a)/MINI(a,a) is
    openvcl's lowering of `move`; ADD/SUB against vf00.xyz is a hard zero; MUL by
    vf00.w is a hard one.
    """
    if len(a) == 3 and a[0] in ("MAX", "MINI") and a[1] == a[2]:
        return a[1]
    if len(a) == 3 and a[0] == "SUB" and a[1] == a[2]:
        return FZERO
    if len(a) == 3 and a[0] in ("ADD", "SUB"):
        if a[2] == FZERO:
            return a[1]
        if a[0] == "ADD" and a[1] == FZERO:
            return a[2]
    if len(a) == 3 and a[0] == "MUL":
        if a[2] == FONE:
            return a[1]
        if a[1] == FONE:
            return a[2]
        if a[1] == FZERO or a[2] == FZERO:
            return FZERO
    # ADD, MUL, MAX and MINI commute, and both assemblers use it: the source's
    # `add.w dst, vf00, src[w]` comes out of SCE as `addw dst, src, VF00w`.
    # Without a canonical operand order that reads as a different value on every
    # program that adds a constant to a vector - eleven of Sony's own 25.  SUB
    # does not commute and is left alone.
    if len(a) == 3 and a[0] in ("ADD", "MUL", "MAX", "MINI") and a[2] < a[1]:
        a = (a[0], a[2], a[1])
    return _mk(a)


FZERO = _mk(("fzero",))
FONE = _mk(("fone",))
VF00 = {"x": FZERO, "y": FZERO, "z": FZERO, "w": FONE}
ZERO = N("const", 0)


def fmt(node, depth=0):
    if not isinstance(node, R):
        return str(node)
    t = _NODES[node.i]
    if depth > 4:
        return "..."
    if len(t) == 1:
        return str(t[0])
    return "%s(%s)" % (t[0], ",".join(
        fmt(x, depth + 1) if isinstance(x, R)
        else ("[%s]" % ",".join(fmt(y, depth + 1) for y in x)
              if isinstance(x, tuple) else str(x))
        for x in t[1:]))


# ------------------------------------------------------------------- parsing

class Insn(object):
    __slots__ = ("mn", "mask", "bc", "ops", "raw")

    def __init__(self, mn, mask, bc, ops, raw):
        self.mn = mn
        self.mask = mask
        self.bc = bc
        self.ops = ops
        self.raw = raw

    def __repr__(self):
        return "<%s.%s %s>" % (self.mn, self.mask, self.ops)


def strip_comment(line):
    return line.split(";")[0].split("#")[0].rstrip()


class Parser(object):
    def __init__(self, isa):
        self.isa = isa

    def split_row(self, toks, is_source):
        # A .vcl row holds ONE instruction; only a .vsm row holds two.  Splitting
        # a source row on "any token that names a mnemonic" makes an alias
        # called `b` or `c` start a second instruction - `mul.xyzw b, b, a`
        # becomes a `mul` and a branch - which is a silent misparse of a legal
        # source.
        if is_source:
            m = toks[0].split(".")[0].lower() if toks else ""
            return [0] if (m == "nop" or self.isa.base(m) is not None) else []
        starts = []
        for i, t in enumerate(toks):
            if t in (",", "(", ")", "[", "]", "++"):
                continue
            if i and toks[i - 1] in (",", "(", "["):
                continue
            m = t.split(".")[0].lower()
            if m == "nop" or self.isa.base(m) is not None:
                starts.append(i)
        return starts

    def parse(self, path):
        is_source = path.endswith(".vcl")
        # Whether the FILE carries SCE's stall annotations at all - not whether
        # any of them is non-zero.  A short SCE program with no hazards has no
        # annotation and zero stalls, and inferring "unannotated" from that put
        # it through the modelled-interlock path with an error bar it does not
        # need.
        out = [("meta", "STALL_" in open(path, errors="replace").read())]
        for line in open(path, errors="replace"):
            # SCE annotates the cycles the HARDWARE will stall on a row it did
            # not pad.  A flag keeps landing during them and a division keeps
            # completing, so a model that counts rows and ignores them reads
            # both too early.
            stall = sum(int(x) for x in STALL.findall(line))
            # The VU instruction-bit markers.  SCE writes them BETWEEN the
            # mnemonic and its destination mask - `add[E].y VF02,VF10,VF04` is
            # the `.y` half of a source `add.xyzw`, carrying the end bit because
            # a `--cont` fell there.  Left in, the tokeniser lost the mask, took
            # the stray `y` for the destination register and wrote a register
            # named Y; the value the real destination should have held then read
            # as uninitialised and eleven of Sony's own control-flow programs
            # came out divergent.  Only the uppercase single letters are
            # stripped, so a `[0]` array index and an `[x]` broadcast are safe.
            body = BITMARK.sub("", strip_comment(line))
            s = body.strip()
            if not s or s.startswith("--") or s.startswith("."):
                continue
            if s.endswith(":"):
                out.append(("label", s[:-1]))
                continue
            if ":" in s.split()[0] and not s.split()[0].startswith("0x"):
                out.append(("label", s.split(":")[0]))
                body = body[body.index(":") + 1:]
                s = body.strip()
                if not s:
                    continue
            toks = TOKEN.findall(body)
            starts = self.split_row(toks, is_source)
            if not starts:
                continue
            row = []
            for k, st in enumerate(starts):
                end = starts[k + 1] if k + 1 < len(starts) else len(toks)
                ins = self.make(toks[st:end])
                if ins is not None:
                    row.append(ins)
            # A row of two NOPs is still a row.  Dropping it compresses the
            # timeline and a CLIP push then looks closer to its reader than it
            # is.
            out.append(("row", row, stall))
        return out

    def make(self, piece):
        head = piece[0]
        mask = None
        if "." in head:
            head, suffix = head.split(".", 1)
            mask = "".join(c for c in suffix.lower() if c in FIELDS) or None
        mn = head.lower()
        if mn == "nop":
            return None
        if self.isa.base(mn) is None:
            return None
        bc = None
        base = self.isa.base(mn)
        if base != mn and mn[-1] in FIELDS:
            bc = mn[-1]
            mn = base
        ops = []
        i = 1
        while i < len(piece):
            t = piece[i]
            if t == ",":
                i += 1
                continue
            if t == "(":
                j = i + 1
                inner = []
                while j < len(piece) and piece[j] != ")":
                    inner.append(piece[j])
                    j += 1
                # The third element is the addressing MODE as written: "++",
                # "--" or "".  It used to be a bool that only knew about "++",
                # which made `(--VI02)` indistinguishable from `(VI02)` and
                # (because `--` was not even a token) left the register name as
                # the first thing inside the bracket either way.
                mode = "++" if "++" in inner else ("--" if "--" in inner else "")
                names = [x for x in inner if x not in ("++", "--")]
                ops.append(("(base)", names[0] if names else None, mode))
                i = j + 1
                continue
            if t == "[":
                # `vertex1[x]` is a broadcast; `mvp[0]` is a REGISTER ARRAY
                # element, a different register.  Treating the index as a field
                # selector collapsed the four rows of an MVP matrix onto one
                # name, so every program that transforms a vertex read the same
                # row four times - in both assemblers at once, which is the
                # signature of the instrument being wrong rather than either of
                # them.
                if ops and i + 1 < len(piece):
                    name, sel, inc = ops[-1]
                    idx = piece[i + 1]
                    if parse_int(idx) is not None:
                        ops[-1] = ("%s#%s" % (name, idx), sel, inc)
                    else:
                        ops[-1] = (name, idx.lower(), inc)
                i += 3 if (i + 2 < len(piece) and piece[i + 2] == "]") else 2
                continue
            if t == "]":
                i += 1
                continue
            sel = None
            name = t
            m = re.match(r"^(VF\d+|VI\d+)([xyzw]+)$", t, re.I)
            if m:
                name = m.group(1)
                sel = m.group(2).lower() if len(m.group(2)) == 1 else None
            elif "." in t and t[0].isalpha():
                # only a REGISTER may carry a `.field` suffix; splitting on the
                # dot unconditionally turned `loi 0.159154937` into `0`.
                name, sfx = t.split(".", 1)
                sel = "".join(c for c in sfx.lower() if c in FIELDS) or None
            ops.append((name, sel, False))
            i += 1
        return Insn(mn, mask, bc, ops, " ".join(piece))


def f32bits(text):
    t = text.strip()
    if t.lower().startswith("0x"):
        return int(t, 16)
    try:
        return struct.unpack("<I", struct.pack("<f", float(t)))[0]
    except (ValueError, OverflowError):
        return t


def parse_int(t):
    try:
        return int(t, 16) if t.lower().startswith("0x") else int(t)
    except (ValueError, AttributeError):
        return None


def eval_offset(toks):
    """Evaluate a run of offset tokens: signed integers joined by `*` and `/`.

    VCL evaluates the offset as an EXPRESSION and truncates the result like C
    integer division (CodeGenerator writes `static_cast<long>(e.result())`), so
    the emitted program carries the folded constant.  Summing the tokens - which
    is all this did - is exact for `4 + 36` and `956+0`, the only shapes any
    real source here uses, and off by one for `2*3+1`.  A model that disagrees
    with BOTH assemblers is an instrument defect, and this one showed up as
    exactly that: af_expr_offset divergent against openvcl and against SCE.
    """
    total = 0
    cur = None
    op = None
    for t in toks:
        if t in ("*", "/"):
            if cur is None or op is not None:
                return None
            op = t
            continue
        v = parse_int(t)
        if v is None:
            return None
        if op is not None:
            if op == "/" and v == 0:
                return None
            cur = cur * v if op == "*" else int(float(cur) / v)
            op = None
        else:
            if cur is not None:
                total += cur
            cur = v
    if op is not None:
        return None
    return total + (cur or 0)


def is_offset_token(t):
    return t in ("*", "/") or parse_int(t) is not None


def imm_at(ops, k):
    """The immediate at operand k, as a SUM of the tokens that make it up.

    An immediate is an expression, not a token.  `iaddiu staticStqData, vi00,
    4 + 36` tokenises as `4` then `+36`, and reading only the first gave the STQ
    array the same base address as the vertex array - so the source model fetched
    every texture coordinate from the vertex it belonged to and reported SCE
    divergent on three stores per triangle.  Same mistake as the load offsets,
    one operand position over.
    """
    if k >= len(ops) or parse_int(ops[k][0]) is None:
        return None
    j = k
    while j < len(ops) and is_offset_token(ops[j][0]):
        j += 1
    return eval_offset([o[0] for o in ops[k:j]])


BINOP = {"add": "ADD", "sub": "SUB", "mul": "MUL", "max": "MAX", "mini": "MINI",
         # MAXi and MINIi are separate mnemonics, not `max`/`mini` with a
         # broadcast letter, so the base-mnemonic lookup leaves them whole.  They
         # were missing from pa-dag.py's table and never showed it, because every
         # program that uses one was already out of scope on the Q rule.
         "maxi": "MAX", "minii": "MINI",
         "addi": "ADD", "subi": "SUB", "muli": "MUL",
         "addq": "ADD", "subq": "SUB", "mulq": "MUL",
         "adda": "ADD", "suba": "SUB", "mula": "MUL",
         "addai": "ADD", "subai": "SUB", "mulai": "MUL",
         "addaq": "ADD", "subaq": "SUB", "mulaq": "MUL"}
ACCOP = {"madd": "ADD", "msub": "SUB", "madda": "ADD", "msuba": "SUB",
         "maddi": "ADD", "msubi": "SUB", "maddq": "ADD", "msubq": "SUB",
         "maddai": "ADD", "msubai": "SUB", "maddaq": "ADD", "msubaq": "SUB"}
UNOP = {"abs": "ABS", "ftoi0": "FTOI0", "ftoi4": "FTOI4", "ftoi12": "FTOI12",
        "ftoi15": "FTOI15", "itof0": "ITOF0", "itof4": "ITOF4",
        "itof12": "ITOF12", "itof15": "ITOF15"}
INT_FOLD = {"iadd": lambda a, b: a + b, "isub": lambda a, b: a - b,
            "iaddiu": lambda a, b: a + b, "isubiu": lambda a, b: a - b,
            "iaddi": lambda a, b: a + b,
            "iand": lambda a, b: a & b, "ior": lambda a, b: a | b}
EFU_FIELDS = {"esum": "xyzw", "eleng": "xyz", "erleng": "xyz", "esadd": "xyz",
              "ersadd": "xyz", "eatanxy": "xy", "eatanxz": "xz",
              "esqrt": "x", "ersqrt": "x", "ercpr": "x", "esin": "x",
              "eatan": "x", "eexp": "x"}
# openvcl's own VuInstructionInfo.cpp, first of the two numbers (latency, not
# throughput).  Quoted rather than invented, and swept against SCE below.
# openvcl's VuInstructionInfo.cpp carries two numbers per operation and the one
# that decides WHEN A CONSUMER SEES THE RESULT is the second, not the first.  For
# FDIV they are equal (7, 7 / 13, 13) so the corpus cannot tell them apart; for
# EFU they differ, and SCE settles it.  In `fz001190` Sony's vcl issues two
# ERLENGs and then reads P twice on ADJACENT rows, 23 and 24 rows after the
# second - which only makes sense if the second lands at issue+24, the second
# number.  With the first number both reads see the same value and the program
# reads as divergent against the reference.
QLAT = {"div": 7, "sqrt": 7, "rsqrt": 13}
PLAT = {"esadd": 11, "ersadd": 18, "eleng": 18, "erleng": 24, "eatanxy": 54,
        "eatanxz": 54, "esum": 12, "ercpr": 12, "ersqrt": 18, "esin": 29,
        "eatan": 54, "eexp": 44, "esqrt": 12}
STORES = ("sq", "sqi", "sqd", "isw", "iswr", "ish", "isb")
LOADS = ("lq", "lqi", "lqd", "ilw", "ilwr")
COND = ("ibeq", "ibne", "ibgez", "ibgtz", "iblez", "ibltz")
UNCOND = ("b", "bal")
INDIRECT = ("jr", "jalr")
BRANCH = COND + UNCOND + INDIRECT


# --------------------------------------------------- branch conditions
#
# The four functions below are the whole extension.  Everything else in this
# file is pb-dag.py.

SIGN_TEST = {"ibgez": lambda v: v >= 0, "ibgtz": lambda v: v > 0,
             "iblez": lambda v: v <= 0, "ibltz": lambda v: v < 0}


def s16(v):
    """A VU integer register is 16 bits and the ORDER tests are signed.

    The model folds constants in unbounded Python ints, so a counter that the
    hardware sees as -1 arrives here as 65535 and `iblez` on it would answer
    the wrong way.  Only the folded value is narrowed; the DAG is untouched.
    """
    return ((v & 0xFFFF) ^ 0x8000) - 0x8000


_OPAQUE_CACHE = {}


def has_input(v):
    """Does this expression rest on a register nobody wrote on the trace?

    Such a leaf is named by the REGISTER, and the source's name for it
    (`vertexCounter`) and the emitted program's (VI08) are different strings
    for the same thing.  Comparing them is not a test, it is a coin flip, so a
    condition that contains one is declared unjudgeable.  Loads, xtop and
    constants are all fine - those are named by what they read, not by where
    the assembler happened to put it.
    """
    # ITERATIVE ON PURPOSE.  The recursive version blew Python's stack on a
    # trace through three iterations of a loop, because the address expressions
    # nest once per iteration and the chain is thousands of nodes deep.  It
    # survived the 70 by luck and the self-test found it at 4000.  A detector
    # that raises instead of answering would have been read as "no opaque
    # conditions here".
    r = _OPAQUE_CACHE.get(v)
    if r is not None:
        return r
    seen = set()
    stack = [v]
    found = False
    while stack:
        u = stack.pop()
        c = _OPAQUE_CACHE.get(u)
        if c is True:
            found = True
            break
        if c is False or u in seen:
            continue
        seen.add(u)
        t = nd(u)
        if t[0] == "in":
            found = True
            break
        for x in t[1:]:
            for y in (x if isinstance(x, tuple) else (x,)):
                if isinstance(y, R):
                    stack.append(y)
    if not found:
        # Only a COMPLETED walk proves the absence, so only then is the whole
        # visited set safe to cache as False.
        for u in seen:
            _OPAQUE_CACHE[u] = False
    _OPAQUE_CACHE[v] = found
    return found


_AMBIG_CACHE = {}


def has_ambig(v):
    """Does this expression rest on a Q or P read the margin cannot decide?

    Iterative for the reason has_input is: a trace through three loop
    iterations nests thousands of nodes deep, and a predicate that raises
    RecursionError reads as "nothing tainted here" - which is the same shape of
    silence this whole mode exists to remove.
    """
    r = _AMBIG_CACHE.get(v)
    if r is not None:
        return r
    seen = set()
    stack = [v]
    found = False
    while stack:
        u = stack.pop()
        c = _AMBIG_CACHE.get(u)
        if c is True:
            found = True
            break
        if c is False or u in seen:
            continue
        seen.add(u)
        t = nd(u)
        if t[0] == "ambig":
            found = True
            break
        for x in t[1:]:
            for y in (x if isinstance(x, tuple) else (x,)):
                if isinstance(y, R):
                    stack.append(y)
    if not found:
        for u in seen:
            _AMBIG_CACHE[u] = False
    _AMBIG_CACHE[v] = found
    return found


def cond_of(m, ins):
    """(key, folded-value-or-None, opaque) for one conditional branch.

    `key` is what the two programs must agree on.  For the ORDER tests it is
    the tested register's value, because those compare against a hard zero and
    an operand shifted by a constant is a genuinely different test.  For the
    EQUALITY tests it is the difference, canonicalised over the operand order:
    `ibeq a,b` is true exactly when a-b is zero, so a-b and b-a are the same
    question and min() of the two handles picks one of them deterministically
    no matter which side built it first.
    """
    ops = [o for o in ins.ops if o[0] != "(base)"]
    if ins.mn in SIGN_TEST:
        a = m.ivalue(ops[0][0])
        return a, m.const(a), has_input(a)
    a = m.ivalue(ops[0][0])
    b = m.ivalue(ops[1][0]) if len(ops) > 1 else ZERO
    if EQ_NORM == "pair":
        lo, hi = (a, b) if a < b else (b, a)
        return N("eqpair", lo, hi), None, has_input(a) or has_input(b)
    d1 = iadd_norm(a, b, -1)
    d2 = iadd_norm(b, a, -1)
    key = d1 if d1 < d2 else d2
    return key, m.const(d1), has_input(d1)


def cond_outcome(mn, folded):
    """True/False if the condition is decidable from a folded constant."""
    if folded is None:
        return None
    if mn == "ibeq":
        return (folded & 0xFFFF) == 0
    if mn == "ibne":
        return (folded & 0xFFFF) != 0
    return SIGN_TEST[mn](s16(folded))


class Machine(object):
    def __init__(self, sem, margin=0):
        self.sem = sem        # source semantics: producers land immediately
        self.margin = margin  # cycles of doubt in a MODELLED stall count
        self.doubt = None
        self.landing = 0 if sem else CLIP_LATENCY
        self.vf = {}
        self.vi = {}
        self.acc = {}
        self.q = N("undef", "Q")
        self.qpend = []
        self.i = N("undef", "I")
        self.r = N("undef", "R")
        self.p = N("undef", "P")
        self.ppend = []
        self.rvf = {}
        self.rvi = {}
        self.racc = {}
        self.rq = self.q
        self.ri = self.i
        self.rr = self.r
        self.rp = self.p
        self.clip = []
        self.mac = N("undef", "MAC")
        self.status = ()
        self.obs = []
        self.conds = []       # one (key, folded, opaque) per conditional branch
        self.lastwrite = []
        self.irow = 0         # INSTRUCTION rows; stalls excluded, see window()
        self.gen = 0          # back edges taken so far
        self.xcount = {}      # xtop/xitop executions so far
        # Set when a policy forced an edge the SOURCE's own folded condition
        # says cannot be taken.  See the reachability note in run_trace.
        self.infeasible = False
        self.tainted = 0      # ambiguous Q/P reads under PB_MARGIN_MODE=taint

    def pending(self, lst, row, cur):
        """The value a fixed-latency unit holds at `row`.

        FDIV and EFU are not pipelined: the result appears a fixed number of
        cycles after issue, so a consumer sees the most recently COMPLETED
        producer, not the most recently issued one.  Both assemblers rely on it -
        SCE issues a second `div` and then places the FIRST division's consumer
        between the two completions.  In source semantics there is no latency at
        all: the source means the division it just wrote.
        """
        v = opt = cur
        for r, val, lat in lst:
            if self.sem or row - r >= lat:
                v = opt = val
            elif row - r >= lat - self.margin:
                # inside the stall model's error bar: it MIGHT have landed
                opt = val
        if opt != v:
            if MARGIN_MODE == "taint":
                # Both readings are recorded in one node, so anything built on
                # it carries the doubt with it and can be dropped at comparison
                # time. Interned like every other node, so the source and the
                # emitted program agree on it whenever they agree on both arms.
                self.tainted += 1
                return N("ambig", v, opt)
            if self.doubt is None:
                self.doubt = ("a producer lands within %d cycles of its "
                              "consumer, which is inside the modelled "
                              "interlock's error bar" % self.margin)
        return v

    def begin_row(self, row, irow):
        # A .vsm ROW issues an upper and a lower instruction together and BOTH
        # read the file as it stood before the row; SCE relies on it
        # (`opmsub.xyz VF02...` paired with `sq VF02,...` stores the OLD VF02).
        self.irow = irow if CLIP_ROWS else row
        self.rvf = dict(self.vf)
        self.rvi = dict(self.vi)
        self.racc = dict(self.acc)
        self.rq = self.pending(self.qpend, row, self.q)
        self.rp = self.pending(self.ppend, row, self.p)
        self.ri, self.rr = self.i, self.r

    def vfield(self, name, sel, field):
        n = name.upper()
        if n in ("VF00", "VF0"):
            return VF00[sel or field]
        key = (n, sel or field)
        if key not in self.rvf:
            self.rvf[key] = self.vf.setdefault(key, N("in", n, sel or field))
        return self.rvf[key]

    def ivalue(self, name):
        n = name.upper()
        if n in ("VI00", "VI0"):
            return ZERO
        if n not in self.rvi:
            self.rvi[n] = self.vi.setdefault(n, N("in", n))
        return self.rvi[n]

    def const(self, v):
        t = nd(v)
        return t[1] if t[0] == "const" else None

    def window(self, imm):
        """The CLIP entries this reader's mask actually consults, newest first.

        Masking by the immediate is the difference between an instrument and a
        false alarm: `fcand VI01,0x2` asks about entry 0 and nothing else, so the
        three older entries may legally hold anything, and both emitters exploit
        that.
        """
        # THE WINDOW SHIFTS IN CYCLES, AND THE REFERENCE SETTLES IT.  Four is
        # the only landing distance Sony's own output is consistent with: at 3
        # it calls 34 of its own programs divergent and at 5 it calls 827, while
        # at 4 it calls none of 1582.  Counting INSTRUCTION ROWS instead of
        # cycles - which is how report SS4 describes the emitter's own padding,
        # and which would excuse the two findings below - costs 50 of Sony's
        # programs, so it is not what the hardware does.
        #
        # What survives is small and real: SCE places a `clipw` three ROWS above
        # a reader that must not see it, one of those rows carries its own
        # `STALL_LATENCY ?1`, and four cycles later the judgement has landed.
        # That is report SS4's warning - "a wait the hardware interlocks is a
        # cycle that costs no instruction" - happening to the reference in the
        # other direction.  PB_CLIP_ROWS=1 switches to the row count so anyone
        # can see both sides of it.
        landed = [c for c, r in self.clip if self.irow - r >= self.landing]
        w = landed[-4:][::-1]
        w = tuple(w) + ("none",) * (4 - len(w))
        depth = 0
        if imm is not None:
            for e in range(4):
                if (imm >> (6 * e)) & 0x3F:
                    depth = e + 1
        return tuple(w[e] if e < (depth or 4) else "-" for e in range(4))


def dest_mask(ins, default="xyzw"):
    return ins.mask or default


def accval(m, f):
    if f not in m.racc:
        m.racc[f] = m.acc.setdefault(f, N("undef", "ACC", f))
    return m.racc[f]


def put(m, to_acc, d, f, v):
    m.lastwrite.append((f, v))
    if to_acc:
        m.acc[f] = v
    else:
        m.vf[(d.upper(), f)] = v


def newmac(m, mask):
    m.mac = N("mac", tuple(sorted(mask)), tuple(m.lastwrite))
    m.lastwrite = []
    if MODEL_STATUS:
        m.status = m.status + (m.mac,)


def split_off(v):
    """(root, k) with v == root + k."""
    t = nd(v)
    if t[0] == "const":
        return ZERO, t[1]
    if t[0] == "ioff":
        return t[1], t[2]
    return v, 0


def ioff(root, k):
    if root == ZERO:
        return N("const", k)
    return root if k == 0 else N("ioff", root, k)


def iadd_norm(a, b, sign):
    """Integer add/sub with the constant part factored out of the expression.

    This is the normalisation that makes an address SCE folded into an offset
    equal to the one openvcl computed in a register.  SCE hoists
    `iaddiu VI08,VI08,6` ABOVE the stores it belongs after and writes them
    `-6(VI08)`..`-1(VI08)`; openvcl leaves the increment where the source put it
    and writes `0(VI08)`..`5(VI08)`.  Same six quadwords.  Without factoring, all
    twelve stores read as twelve different addresses and every program with a
    per-iteration output pointer - which is all of them - reports divergent.
    Addition is also made commutative here, so `iadd a,b` and `iadd b,a` are one
    expression.
    """
    ra, ka = split_off(a)
    rb, kb = split_off(b)
    k = ka + sign * kb
    if rb == ZERO:
        root = ra
    elif ra == ZERO and sign > 0:
        root = rb
    elif sign < 0 and ra == rb:
        # `isub n3, n4, n4` is how a generated program spells zero, and SCE
        # folds it: `ior n2, n2, n3` then leaves n2 alone and the store lands at
        # a constant address.  Without the fold the source model keeps a
        # symbolic address the emitted code no longer has.
        root = ZERO
    elif sign > 0:
        lo, hi = (ra, rb) if ra < rb else (rb, ra)
        root = N("iadd", lo, hi)
    else:
        root = N("isub", ra, rb)
    return ioff(root, k)


def address(m, ops):
    """The address of a load/store.

    The offset is a SUM of tokens, not one token.  This engine's sources write
    `sq vertex1, 956+0(vi00)` and `isw.w adcBit, 1+4(destAddress)`; taking only
    the token nearest the bracket read those as offset 0 and offset 4, so the
    source model stored nine quadwords on top of each other and every clip
    program looked divergent against both assemblers.
    """
    for k, (name, sel, inc) in enumerate(ops):
        if name != "(base)":
            continue
        j = k - 1
        while j >= 0 and is_offset_token(ops[j][0]):
            j -= 1
        off = eval_offset([o[0] for o in ops[j + 1:k]])
        off = 0 if off is None else off
        if sel is None:
            return N("addr", ZERO, off)
        root, kb = split_off(m.ivalue(sel))
        return N("addr", root, kb + off)
    return N("addr", ZERO, 0)


# The auto-modifying memory forms, keyed on the MNEMONIC.  The mnemonic is what
# the hardware executes; the `++`/`--` in the operand text is decoration the
# assembler prints, and one of the two assemblers here got that decoration wrong
# while the mnemonic stayed right.  Driving the semantics off the text would
# have modelled the bug instead of finding it.
AUTO_INC = {"lqi", "sqi"}
AUTO_DEC = {"lqd", "sqd"}


def autobase(ops):
    """(register, mode-as-written) of the (base) operand."""
    for name, sel, mode in ops:
        if name == "(base)":
            return sel, (mode if isinstance(mode, str) else ("++" if mode else ""))
    return None, ""


def advance(m, ops, delta):
    sel, _ = autobase(ops)
    if sel:
        m.vi[sel.upper()] = iadd_norm(m.ivalue(sel), N("const", abs(delta)),
                                      1 if delta > 0 else -1)


def addr_form_problems(prog):
    """Rows whose operand text contradicts the mnemonic's addressing mode.

    `lqd` IS a pre-decrement and `lqi` IS a post-increment; the bracket has to
    say the same thing the opcode does or the row is not the row the assembler
    thinks it emitted.  dvp-as rejects the mismatch outright, so this is a
    static check and not a value comparison - but it belongs in the oracle
    because "openvcl produced a program dvp-as will not take" and "openvcl
    produced a program that computes the wrong thing" are the same failure of
    the same pass, and only one of them has ever been looked for.
    """
    out = []
    for payload, _ in prog.rows:
        for ins in payload:
            if ins.mn not in STORES and ins.mn not in LOADS:
                continue
            sel, mode = autobase(ins.ops)
            if sel is None:
                continue
            want = ("++" if ins.mn in AUTO_INC
                    else "--" if ins.mn in AUTO_DEC else "")
            if mode != want:
                out.append(("ADDR-FORM",
                            "%s is %s but the operand is written (%s%s%s)"
                            % (ins.mn,
                               "a post-increment" if want == "++" else
                               "a pre-decrement" if want == "--" else
                               "a plain indirect",
                               "--" if mode == "--" else "", sel,
                               "++" if mode == "++" else "")))
    return out


def step(m, ins, row):
    mn, ops = ins.mn, ins.ops
    if not ops:
        return
    d = ops[0][0]

    if mn in STORES:
        src, sel, _ = ops[0]
        # A pre-decrement modifies the base BEFORE the access and a
        # post-increment after it.  Order matters: `sqd v,(--p)` writes p-1 and
        # leaves p-1 behind, `sqi v,(p++)` writes p and leaves p+1.
        if mn in AUTO_DEC:
            advance(m, ops, -1)
        addr = address(m, ops)
        if mn.startswith("is"):
            m.obs.append(N("isw", addr, dest_mask(ins, "x"), m.ivalue(src)))
        else:
            mask = dest_mask(ins)
            m.obs.append(N("sq", addr, mask,
                           tuple(m.vfield(src, sel, f) for f in mask)))
        if mn in AUTO_INC:
            advance(m, ops, 1)
        return
    if mn == "xgkick":
        m.obs.append(N("xgkick", m.ivalue(d)))
        return

    if mn in LOADS:
        if mn in AUTO_DEC:
            advance(m, ops, -1)
        addr = address(m, ops)
        ep = m.gen if LOAD_EPOCH else 0
        if mn.startswith("il"):
            m.vi[d.upper()] = N("iload", addr, dest_mask(ins, "x"), ep)
        else:
            for f in dest_mask(ins):
                m.vf[(d.upper(), f)] = N("load", addr, f, ep)
        if mn in AUTO_INC:
            advance(m, ops, 1)
        return

    if mn in INT_FOLD:
        a = m.ivalue(ops[1][0]) if len(ops) > 1 else ZERO
        k = imm_at(ops, 2)
        bv = N("const", k) if k is not None else (
            m.ivalue(ops[2][0]) if len(ops) > 2 else ZERO)
        if mn in ("iadd", "iaddi", "iaddiu"):
            m.vi[d.upper()] = iadd_norm(a, bv, 1)
            return
        if mn in ("isub", "isubiu"):
            m.vi[d.upper()] = iadd_norm(a, bv, -1)
            return
        ka, kb = m.const(a), m.const(bv)
        if ka is not None and kb is not None:
            m.vi[d.upper()] = N("const", INT_FOLD[mn](ka, kb))
        elif a == bv:                                  # x|x == x&x == x
            m.vi[d.upper()] = a
        elif ka == 0 or kb == 0:
            other = bv if ka == 0 else a
            m.vi[d.upper()] = other if mn == "ior" else ZERO
        else:
            lo, hi = (a, bv) if a < bv else (bv, a)     # IAND/IOR commute
            m.vi[d.upper()] = N(mn, lo, hi)
        return
    if mn in ("xtop", "xitop"):
        # XTOP reads the VIF's double-buffer pointer and answers DIFFERENTLY on
        # every execution.  Modelling it as one symbol makes an `xtop` hoisted
        # out of the batch loop - reuse of the previous buffer, report SS2's
        # family - invisible, because both iterations would then read the same
        # node.  The execution count names it.
        k = m.xcount.get(mn, 0)
        m.xcount[mn] = k + 1
        m.vi[d.upper()] = N(mn, k)
        return
    if mn == "mtir":
        name, sel, _ = ops[1]
        m.vi[d.upper()] = N("mtir", m.vfield(name, sel, sel or "x"))
        return
    if mn == "mfir":
        v = m.ivalue(ops[1][0])
        for f in dest_mask(ins):
            m.vf[(d.upper(), f)] = N("mfir", v)
        return

    if mn in ("fcand", "fcor", "fceq"):
        imm = imm_at(ops, 1) or 0
        m.vi[d.upper()] = N(mn, imm, m.window(imm))
        return
    if mn == "fcget":
        m.vi[d.upper()] = N("fcget", m.window(None))
        return
    if mn == "fcset":
        # FCSET writes the clip register from the integer pipe: no four-row
        # landing delay, the entries are visible to the very next reader.
        imm = parse_int(d) if parse_int(d) is not None else 0
        m.clip = [(N("fcset", imm, k), m.irow - 1000) for k in range(4)]
        return
    if mn in ("fsand", "fsor", "fseq"):
        imm = imm_at(ops, 1) or 0
        m.vi[d.upper()] = N(mn, imm, m.status if MODEL_STATUS else "STATUS")
        return
    if mn == "fsset":
        m.status = ()
        return
    if mn in ("fmand", "fmor", "fmeq"):
        other = m.ivalue(ops[1][0]) if len(ops) > 1 else ZERO
        m.vi[d.upper()] = N(mn, other, m.mac if MODEL_MAC else "MAC")
        return

    if mn in ("div", "rsqrt"):
        a = m.vfield(ops[1][0], ops[1][1], ops[1][1] or "x")
        b = m.vfield(ops[2][0], ops[2][1], ops[2][1] or "x")
        m.qpend.append((row, N(mn, a, b), int(QLAT[mn] * QSCALE)))
        del m.qpend[:-4]
        return
    if mn == "sqrt":
        b = m.vfield(ops[1][0], ops[1][1], ops[1][1] or "x")
        m.qpend.append((row, N("sqrt", b), int(QLAT["sqrt"] * QSCALE)))
        del m.qpend[:-4]
        return
    if mn == "waitq":
        m.qpend = [(-(1 << 30), v, 0) for _, v, _ in m.qpend]
        return
    if mn == "waitp":
        m.ppend = [(-(1 << 30), v, 0) for _, v, _ in m.ppend]
        return
    if mn in EFU_FIELDS:
        # Each EFU operation reads a SPECIFIC set of fields: ELENG and ERLENG are
        # sqrt(x^2+y^2+z^2) and never touch w, ESUM does.
        name, sel, _ = ops[1]
        m.ppend.append((row, N(mn, tuple(m.vfield(name, sel, f)
                                         for f in EFU_FIELDS[mn])),
                        int(PLAT.get(mn, 12) * QSCALE)))
        del m.ppend[:-4]
        return
    if mn == "mfp":
        for f in dest_mask(ins):
            m.vf[(d.upper(), f)] = m.rp
        return
    if mn == "loi":
        m.i = N("imm", f32bits(d))
        return
    if mn == "rinit":
        name, sel, _ = ops[1]
        m.r = N("rinit", m.vfield(name, sel, sel or "x"))
        return
    if mn == "rxor":
        name, sel, _ = ops[1]
        m.r = N("rxor", m.rr, m.vfield(name, sel, sel or "x"))
        return
    if mn == "rnext":
        m.r = N("rnext", m.rr)
        for f in dest_mask(ins):
            m.vf[(d.upper(), f)] = m.r
        return
    if mn == "rget":
        for f in dest_mask(ins):
            m.vf[(d.upper(), f)] = m.rr
        return

    if mn in ("clip", "clipw"):
        a = ops[0]
        b = ops[1] if len(ops) > 1 else ops[0]
        m.clip.append((N("clip",
                         m.vfield(a[0], a[1], "x"), m.vfield(a[0], a[1], "y"),
                         m.vfield(a[0], a[1], "z"), m.vfield(b[0], "w", "w")),
                       m.irow))
        del m.clip[:-8]
        return

    to_acc = d.lower() == "acc"
    mask = dest_mask(ins)

    def src(k, f):
        name, sel, _ = ops[k]
        if name.lower() == "i":
            return m.ri
        if name.lower() == "q":
            return m.rq
        return m.vfield(name, sel, f)

    if mn in ("move", "mr32"):
        rot = {"x": "y", "y": "z", "z": "w", "w": "x"}
        for f in mask:
            put(m, to_acc, d, f, src(1, rot[f] if mn == "mr32" else f))
        newmac(m, mask)
        return
    if mn in UNOP:
        for f in mask:
            put(m, to_acc, d, f, N(UNOP[mn], src(1, f)))
        newmac(m, mask)
        return
    if mn in ("opmula", "opmsub"):
        idx = {"x": ("y", "z"), "y": ("z", "x"), "z": ("x", "y")}
        for f in mask:
            if f not in idx:
                continue
            a, b = idx[f]
            term = N("MUL", src(1, a), src(2, b))
            term2 = N("MUL", src(1, b), src(2, a))
            if mn == "opmula":
                put(m, True, "acc", f, N("SUB", term, term2))
            else:
                put(m, to_acc, d, f,
                    N("SUB", accval(m, f), N("SUB", term, term2)))
        newmac(m, mask)
        return
    if mn in ACCOP:
        for f in mask:
            b = src(2, ins.bc or f) if len(ops) > 2 else src(1, ins.bc or f)
            prod = N("MUL", src(1, f), b)
            put(m, to_acc or mn.endswith("a"), d, f,
                N(ACCOP[mn], accval(m, f), prod))
        newmac(m, mask)
        return
    if mn in BINOP:
        for f in mask:
            if len(ops) > 2:
                b = src(2, ins.bc or f)
            else:
                b = m.ri if mn.endswith("i") else m.rq
            put(m, to_acc, d, f, N(BINOP[mn], src(1, f), b))
        newmac(m, mask)
        return
    raise Unsupported("unmodelled instruction: %s (%s)" % (mn, ins.raw))




# ------------------------------------------------- the hardware stall model
#
# A .vsm row is not a cycle.  SCE says so itself with `STALL_LATENCY ?3`, and
# the parser reads those annotations; openvcl emits none, because
# `--fmac-interlock` is precisely the decision to let the hardware wait instead
# of spending an instruction on it.  Reading openvcl's rows as cycles therefore
# judges every FDIV and EFU pairing in ITS output too early and in no other -
# the first run of this oracle called 38 of 70 openvcl programs divergent on Q
# alone, every one of them a consumer that the interlock actually separates far
# enough.
#
# The rule below is not asserted, it is FITTED: SCE's annotations are labels and
# `pb-stall.py --fit` scores the model against them.  It reproduces 99.88% of
# Sony's own annotations on the 70 real programs row for row, and the residue is
# eight rows where Sony charges a stall to the row above the one this charges it
# to, which leaves the cycle COUNT identical.

FMAC = set(BINOP) | set(ACCOP) | set(UNOP) | {
    "move", "mr32", "opmula", "opmsub"}
VLOAD = {"lq", "lqi", "lqd"}
ILOAD = {"ilw", "ilwr"}
INTALU = set(INT_FOLD) | {"mtir", "xtop", "xitop", "fcand", "fcor", "fceq",
                              "fcget", "fsand", "fsor", "fseq", "fmand", "fmor",
                              "fmeq", "iaddi"}
FMAC_LAT = 4
ACC_LAT = 1
VLOAD_LAT = 4
ILOAD_LAT = 3
INT_LAT = 1
# When the unit is free to take the NEXT operation, which is openvcl's first
# column - not the second, which is when the RESULT reaches P.  SCE annotates
# `STALL_THRUPUT ?18` on a second ERLENG 6 rows after the first, and 23 - 6 + 1
# is 18 only with 23.
QTHRU = {"div": 7, "sqrt": 7, "rsqrt": 13}
PTHRU = {"esadd": 10, "ersadd": 17, "eleng": 17, "erleng": 23, "eatanxy": 53,
         "eatanxz": 53, "esum": 11, "ercpr": 11, "ersqrt": 17, "esin": 28,
         "eatan": 53, "eexp": 43, "esqrt": 11}


def mask_of(ins, default="xyzw"):
    return ins.mask or default


def reads(ins):
    """[(kind, name, field)] the instruction reads."""
    out = []
    mn, ops = ins.mn, ins.ops
    if not ops:
        return out
    m = mask_of(ins)
    if mn in STORES:
        n = ops[0][0].upper()
        if mn.startswith("is"):
            out.append(("vi", n, "x"))
        else:
            sel = ops[0][1]
            for f in mask_of(ins):
                out.append(("vf", n, sel or f))
    elif mn == "xgkick":
        out.append(("vi", ops[0][0].upper(), "x"))
    # A branch's condition operand and a `clip`/`clipw`'s vector are in
    # position 0 and are READS.  Skipping position 0 for everything that is not
    # a store hid both, which is 47 of the 84 rows this model first disagreed
    # with SCE about.
    start = 0 if (mn in STORES or mn == "xgkick" or mn in BRANCH
                  or mn in ("clip", "clipw")) else 1
    if mn in ACCOP or (mn in BINOP and ops[0][0].lower() == "acc"):
        for f in m:
            out.append(("acc", "ACC", f))
    for name, sel, inc in ops[start:]:
        if name == "(base)":
            if sel:
                out.append(("vi", sel.upper(), "x"))
            continue
        n = name.upper()
        if n.startswith("VF") and n not in ("VF00", "VF0"):
            for f in (m if mn in FMAC or mn in ("clip", "clipw") else "x"):
                out.append(("vf", n, sel or f))
        elif n.startswith("VI") and n not in ("VI00", "VI0"):
            out.append(("vi", n, "x"))
    return out


def writes(ins):
    """[((kind, name, field), latency)]."""
    out = []
    mn, ops = ins.mn, ins.ops
    # lqi/lqd/sqi/sqd write their ADDRESS REGISTER as well.  A store writes
    # nothing else, so the auto forms were the one kind of store with a register
    # write and the early return dropped it - the interlock model then let a
    # reader of the pointer issue in the same cycle.
    auto = []
    if mn in AUTO_INC or mn in AUTO_DEC:
        sel, _ = autobase(ops)
        if sel and sel.upper() not in ("VI00", "VI0"):
            auto = [(("vi", sel.upper(), "x"), INT_LAT)]
    if not ops or mn in STORES or mn in ("xgkick", "clip", "clipw",
                                             "waitq", "waitp", "loi"):
        return out + auto
    d = ops[0][0].upper()
    if mn in ILOAD:
        return [(("vi", d, "x"), ILOAD_LAT)] + auto
    if mn in INTALU:
        return [(("vi", d, "x"), INT_LAT)]
    if mn in VLOAD:
        return [(("vf", d, f), VLOAD_LAT) for f in mask_of(ins)] + auto
    if mn in ("mfir", "mfp", "rnext", "rget"):
        return [(("vf", d, f), FMAC_LAT) for f in mask_of(ins)]
    if mn in FMAC:
        if d == "ACC" or ops[0][0].lower() == "acc" or mn.endswith("a") \
                or mn.endswith("ai") or mn.endswith("aq"):
            return [(("acc", "ACC", f), ACC_LAT) for f in mask_of(ins)]
        return [(("vf", d, f), FMAC_LAT) for f in mask_of(ins)]
    return out


class Hw(object):
    """The interlock state, advanced ALONG A TRACE rather than down the file.

    A row at a join point is reached from whichever block the trace came
    through, and its stall depends on that predecessor: SCE's own annotation
    there is the worst case over predecessors, which is why the static fit
    disagrees with it on 0.4% of the control-flow corpus and on none of the
    straight-line one.  Walking the trace answers the question exactly instead.
    """

    def __init__(self):
        self.ready = {}
        self.qfree = 0
        self.pfree = 0
        self.qdone = 0
        self.pdone = 0

    def stall(self, payload, cyc):
        ready, qfree, pfree, qdone, pdone = (self.ready, self.qfree, self.pfree,
                                             self.qdone, self.pdone)
        need = cyc
        for ins in payload:
            br = ins.mn in BRANCH
            for k in reads(ins):
                rdy, from_load = ready.get(k, (0, False))
                need = max(need, rdy + (1 if (br and from_load) else 0))
            if ins.mn == "waitq":
                need = max(need, qdone)
            if ins.mn == "waitp":
                need = max(need, pdone)
            if ins.mn in QTHRU:
                need = max(need, qfree)
            if ins.mn in PTHRU:
                need = max(need, pfree)
        cyc = need
        for ins in payload:
            for k, lat in writes(ins):
                ready[k] = (cyc + lat, ins.mn in ILOAD or ins.mn in VLOAD)
            if ins.mn in QTHRU:
                self.qfree = cyc + QTHRU[ins.mn]
                self.qdone = cyc + QLAT[ins.mn]
            if ins.mn in PTHRU:
                self.pfree = cyc + PTHRU[ins.mn]
                self.pdone = cyc + PLAT[ins.mn]
        return need


def predict(prog):
    """[(stall, cycle)] per row - the STATIC form, used only by the fitter."""
    ready = {}
    qfree = 0
    pfree = 0
    qdone = 0
    pdone = 0
    cyc = 0
    out = []
    for payload, _ in prog.rows:
        need = cyc
        for ins in payload:
            # A branch needs its operand one cycle earlier than an ALU does:
            # `ilw.x VI01,8(VI00)` immediately above `iblez VI01,...` is
            # annotated STALL_LATENCY ?3 while the same load three rows above a
            # plain `iadd` is annotated nothing, and only latency 3 plus a
            # one-cycle branch penalty satisfies both.
            br = ins.mn in BRANCH
            for k in reads(ins):
                rdy, from_load = ready.get(k, (0, False))
                # The branch penalty is on the LOAD result only.  `ilw.x VI01`
                # immediately above `iblez VI01` is annotated ?3 while
                # `iadd VI13,..` immediately above `ibne VI13,VI01,..` is
                # annotated nothing, so the integer ALU result is ready for a
                # branch on the next row and the load result is not.
                need = max(need, rdy + (1 if (br and from_load) else 0))
            if ins.mn == "waitq":
                need = max(need, qdone)
            if ins.mn == "waitp":
                need = max(need, pdone)
            if ins.mn in QTHRU:
                need = max(need, qfree)
            if ins.mn in PTHRU:
                need = max(need, pfree)
        stall = need - cyc
        cyc = need
        for ins in payload:
            for k, lat in writes(ins):
                ready[k] = (cyc + lat, ins.mn in ILOAD or ins.mn in VLOAD)
            if ins.mn in QTHRU:
                qfree = qdone = cyc + QTHRU[ins.mn]
            if ins.mn in PTHRU:
                pfree = pdone = cyc + PTHRU[ins.mn]
        out.append((stall, cyc))
        cyc += 1
    return out



# ------------------------------------------------------------------- the CFG

class Prog(object):
    """Rows, and where the labels sit among them.

    Basic blocks are never materialised.  A trace is walked row by row, and the
    only thing the CFG has to answer is "which row does this label name" - which
    both assemblers preserve, because neither renames or deletes a label a branch
    uses.  SCE adds synthetic ones (`__v__out_..._6`); nothing branches to them
    and they cost nothing.
    """

    def __init__(self, items, model_stalls=True):
        self.rows = []
        self.labels = {}
        self.annotated = False
        for it in items:
            if it[0] == "meta":
                self.annotated = it[1]
                continue
            if it[0] == "label":
                self.labels.setdefault(it[1], len(self.rows))
            else:
                self.rows.append((it[1], it[2]))
        # openvcl annotates nothing, so its rows have to be given the cycles
        # the hardware interlock will actually spend.  SCE's own annotations are
        # trusted where they exist; the model only fills in for a file that has
        # none.
        self.model_stalls = model_stalls and not self.annotated
        self.branch = []
        for payload, _ in self.rows:
            b = None
            for ins in payload:
                if ins.mn in BRANCH:
                    b = ins
            self.branch.append(b)

    def has_indirect(self):
        return any(b is not None and b.mn in INDIRECT for b in self.branch)

    def targets(self):
        return set(b.ops[-1][0] for b in self.branch
                   if b is not None and b.ops)


def run_trace(prog, sem, has_delay, decide, unroll=UNROLL):
    """Walk one path and return (observables, branch-key sequence, decisions).

    `decide(k, mn, target, backward, visits)` answers each CONDITIONAL branch.
    Unconditional branches are always taken and consume no decision.  A trace
    ends when it runs off the end of the program or when it would jump to a
    label for the (unroll+1)-th time - that cap is the bounded unrolling, and it
    is keyed on the LABEL so the source and the emitted program apply it at the
    same point.
    """
    m = Machine(sem, 0 if (sem or prog.annotated) else HW_MARGIN)
    pc = 0
    row = 0          # CYCLES: stalls included, what Q and P land against
    irow = 0         # INSTRUCTION ROWS: what the CLIP window shifts against
    keys = []
    decisions = []
    visits = {}
    hw = Hw() if prog.model_stalls else None
    guard = 0
    nrows = len(prog.rows)
    while 0 <= pc < nrows:
        guard += 1
        if guard > ROW_CAP:
            raise Budget("row cap %d" % ROW_CAP)
        payload, stall = prog.rows[pc]
        if hw is not None:
            stall = hw.stall(payload, row) - row
        br = prog.branch[pc]
        # WAITQ stalls the whole ROW, so the upper instruction paired with it -
        # `subq.yz VF07,VF10,q  waitq`, which is how both emitters spell it -
        # does see the result.
        for ins in payload:
            if ins.mn == "waitq":
                m.qpend = [(-(1 << 30), v, 0) for _, v, _ in m.qpend]
            elif ins.mn == "waitp":
                m.ppend = [(-(1 << 30), v, 0) for _, v, _ in m.ppend]
        # SCE's STALL annotation is the wait the HARDWARE takes BEFORE issuing
        # this row, not after it.  Charging it afterwards moves every later row
        # one cycle early relative to the producers above it, and the pairing it
        # breaks is the one both assemblers rely on: in `mcpip_as_is` SCE's
        # second `mulq` sits exactly seven cycles after its `div` and the third
        # exactly seven after its own, and only the before-the-row reading puts
        # both at seven.  Charged afterwards they come out at 7 and 6, which no
        # single latency can satisfy - the shape of an instrument that is wrong
        # rather than a program that is.
        row += stall
        m.begin_row(row, irow)
        # THE CONDITION IS READ BEFORE THE ROW RUNS, and before the delay slot.
        # Both pipes of a .vsm row see the file as it stood above the row, and
        # the delay slot is BELOW the branch - so a delay-slot write cannot
        # change the branch that owns the slot, only the next one down.  That
        # is the whole point: the wrongly-unconditional delay-slot write this
        # is here to catch reaches the NEXT condition.
        if DO_COND and br is not None and br.mn in COND:
            m.conds.append(cond_of(m, br) + (pc,))
        for ins in payload:
            if ins.mn in BRANCH:
                continue
            step(m, ins, row)
        row += 1
        irow += 1
        if br is None:
            pc += 1
            continue
        if br.mn in INDIRECT:
            raise Unsupported("indirect branch %s" % br.mn)
        tgt = br.ops[-1][0] if br.ops else None
        if tgt not in prog.labels:
            raise Unsupported("branch to unknown label %r" % tgt)
        keys.append((br.mn, tgt))
        if br.mn in COND:
            take = bool(decide(len(decisions), br.mn, tgt,
                               prog.labels[tgt] <= pc, visits.get(tgt, 0)))
            # IS THIS EDGE REACHABLE AT ALL?  The policies force both sides of
            # every branch, including ones whose operand this walk has already
            # folded to a constant - `ibgtz` on const(-7) cannot be taken, and a
            # finding on the taken side of it is a finding about code that never
            # runs.  Nothing here refused such a path before, so the residual
            # could not say how much of itself was unreachable.  Only the SOURCE
            # walk decides this: it is the specification, and a constant in the
            # emitted program is a fact about the emitted program.
            if sem:
                folded = cond_of(m, br)[1]
                if folded is not None:
                    real = cond_outcome(br.mn, folded)
                    if real is not None and bool(real) != take:
                        m.infeasible = True
            decisions.append(take)
        else:
            take = True
        nxt = pc + 1
        if has_delay and nxt < nrows:
            # The row AFTER a branch is its delay slot and executes whichever way
            # the branch goes.  Sony's vcl puts real work there - a store, and in
            # one program an `xgkick`.
            dp, dstall = prog.rows[nxt]
            if hw is not None:
                dstall = hw.stall(dp, row) - row
            for ins in dp:
                if ins.mn == "waitq":
                    m.qpend = [(-(1 << 30), v, 0) for _, v, _ in m.qpend]
                elif ins.mn == "waitp":
                    m.ppend = [(-(1 << 30), v, 0) for _, v, _ in m.ppend]
            row += dstall
            m.begin_row(row, irow)
            for ins in dp:
                if ins.mn in BRANCH:
                    continue
                step(m, ins, row)
            row += 1
            irow += 1
        if not take:
            pc = nxt + 1 if has_delay else nxt
            continue
        if visits.get(tgt, 0) + 1 > unroll:
            break
        visits[tgt] = visits.get(tgt, 0) + 1
        if prog.labels[tgt] <= pc:
            m.gen += 1
        pc = prog.labels[tgt]
    if m.doubt:
        raise Unsupported(m.doubt)
    return m.obs, tuple(keys), decisions, m.conds, m.infeasible


# ------------------------------------------------------------------ policies
#
# A policy is only a way of picking WHICH paths get compared; correctness never
# depends on it, because whatever path the source takes is replayed exactly on
# the emitted program.  The six named ones cover the shapes the engine's programs
# actually have - an outer batch loop, an inner per-vertex loop, and a diamond
# inside each - and the seeded random ones fill in the rest.

def pol_fall(k, mn, tgt, backward, visits):
    return False


def pol_take(k, mn, tgt, backward, visits):
    return True


def pol_loop_fall(k, mn, tgt, backward, visits):
    return backward


def pol_loop_take(k, mn, tgt, backward, visits):
    return True if backward else True


def pol_alt(phase):
    def f(k, mn, tgt, backward, visits):
        return True if backward else ((k + phase) % 2 == 0)
    return f


def pol_rand(seed):
    state = {}

    def f(k, mn, tgt, backward, visits):
        if "r" not in state:
            state["r"] = random.Random(seed)
        if backward:
            return state["r"].random() < 0.8
        return state["r"].random() < 0.5
    return f


def pol_one(k, taken=True):
    """Only the k-th conditional branch goes the unusual way.

    Named policies and random ones both leave EDGES uncovered: a program with
    eight conditionals has 256 paths and thirteen policies cannot promise that
    every branch was taken at least once and fallen through at least once.  A
    real bug can sit on one edge - openvcl fills a delay slot with the
    fall-through block's first instruction, which is only wrong on the TAKEN
    side - so every edge gets its own trace.
    """
    def f(i, mn, tgt, backward, visits):
        if backward:
            return True
        return taken if i == k else (not taken)
    return f


def policies(nbranch=0):
    out = [("fall", pol_fall), ("take", pol_take),
           ("loop+fall", pol_loop_fall), ("alt0", pol_alt(0)),
           ("alt1", pol_alt(1))]
    for k in range(min(nbranch, 12)):
        out.append(("only%d" % k, pol_one(k, True)))
        out.append(("but%d" % k, pol_one(k, False)))
    for s in range(1, 9):
        out.append(("rand%d" % s, pol_rand(s)))
    return out


# ---------------------------------------------------------------- comparison

def compare(want, got):
    """What must hold between the source's observables and an emitted program's.

    NOT the flat ordered list: both assemblers reorder stores to different
    addresses and they are right to.  Three things are checked, each of which a
    real miscompile breaks and a legal reordering does not - the multiset, the
    order per address per field, and every store's order against every xgkick.
    """
    from collections import Counter
    problems = []
    if MARGIN_MODE == "taint":
        # Dropped from BOTH sides. An observable that rests on an undecidable
        # read is not evidence either way, and keeping it on one side only
        # would turn the doubt into a finding.
        want = [o for o in want if not has_ambig(o)]
        got = [o for o in got if not has_ambig(o)]
    cw, cg = Counter(want), Counter(got)
    if cw != cg:
        diff = (cw - cg) + (cg - cw)
        problems.append(("MULTISET", "%d observables differ" % sum(diff.values()),
                         sorted(diff)[:1]))
        return problems
    addrs = set(nd(o)[1] for o in want if nd(o)[0] != "xgkick")
    for a in addrs:
        for f in FIELDS:
            sw = [o for o in want
                  if nd(o)[0] != "xgkick" and nd(o)[1] == a and f in nd(o)[2]]
            sg = [o for o in got
                  if nd(o)[0] != "xgkick" and nd(o)[1] == a and f in nd(o)[2]]
            if sw != sg:
                problems.append(("ADDR-ORDER",
                                 "stores to %s field .%s reordered" % (fmt(a), f),
                                 sw[:1]))
                break
        if problems:
            break
    if any(nd(o)[0] == "xgkick" for o in want):
        kw = [i for i, o in enumerate(want) if nd(o)[0] == "xgkick"]
        kg = [i for i, o in enumerate(got) if nd(o)[0] == "xgkick"]
        if [want[i] for i in kw] != [got[i] for i in kg] or kw != kg:
            problems.append(("KICK-ORDER",
                             "xgkick at positions %s emitted, source has %s"
                             % (kg, kw), []))
    return problems


def compare_conds(keys, want, got):
    """The k-th conditional branch must test the same value on both sides.

    `keys` is the (mnemonic, target) sequence the two walks already agreed on,
    so the mnemonic of the k-th condition is known and the same on both sides;
    only the VALUE is in question here.  Returns [(kind, text)] plus a count of
    the conditions that could not be judged.
    """
    out = []
    opaque = 0
    # HOW MANY OF THE COMPARED CONDITIONS ARE ACTUALLY SYMBOLIC.  A condition
    # that folds to a constant on both sides tests the constant folder and
    # nothing else, and a corpus made of those can report a large number of
    # "conditions compared" while putting no assembler under any pressure -
    # which is exactly what pb-gen.py's 400 programs turned out to be.  This
    # number is the one that says whether a run means anything.
    symbolic = 0
    cbr = [(mn, tgt) for mn, tgt in keys if mn in COND]
    if len(want) != len(got):
        out.append(("COND-COUNT", "%d conditional branches in the source, "
                    "%d emitted" % (len(want), len(got))))
    for k in range(min(len(want), len(got))):
        (kw, fw, ow, _), (kg, fg, og, _) = want[k], got[k]
        mn, tgt = cbr[k] if k < len(cbr) else ("?", "?")
        # WHICH SIDE IS OPAQUE DECIDES WHETHER IT IS AN AMBIGUITY OR A BUG.
        # The first version skipped whenever EITHER side rested on a register
        # nobody wrote, and the mutation controls showed what that costs: 11 of
        # 64 wrong-register mutants went unreported, every one of them because
        # the register substituted in was one the emitted program never writes,
        # so the condition became opaque and the oracle declined to judge it.
        # Declining is right only when the SOURCE is the undefined one - then
        # any value the emitted program produces satisfies it, and the two
        # names cannot be paired anyway.  A source condition that is properly
        # defined and an emitted one that is not is the emitted program having
        # LOST the computation, which is a finding and one of the worse ones.
        if MARGIN_MODE == "taint" and (has_ambig(kw) or has_ambig(kg)):
            # A condition resting on an undecidable Q or P read joins the
            # unjudgeable count rather than the findings, on the same terms as
            # an opaque register: the doubt is the instrument's, not the
            # compiler's.
            opaque += 1
            continue
        if OPAQUE == "skip" and ow:
            opaque += 1
            continue
        if OPAQUE == "skip" and og and not ow:
            out.append(("COND-UNDEF",
                        "branch %d (%s %s) tests %s, which rests on a register "
                        "the emitted program never writes; source tests %s"
                        % (k, mn, tgt, fmt(kg), fmt(kw))))
            continue
        if fw is None or fg is None:
            symbolic += 1
        if kw == kg:
            continue
        dw, dg = cond_outcome(mn, fw), cond_outcome(mn, fg)
        if dw is not None and dg is not None:
            kind = "COND-EDGE" if dw != dg else "COND-CONST"
            extra = " -> source %s, emitted %s" % (
                "TAKEN" if dw else "not taken", "TAKEN" if dg else "not taken")
        else:
            kind, extra = "COND-VALUE", ""
        out.append((kind, "branch %d (%s %s) tests %s, source tests %s%s"
                    % (k, mn, tgt, fmt(kg), fmt(kw), extra)))
    return out, opaque, symbolic


def replay(decisions):
    def f(k, mn, tgt, backward, visits):
        return decisions[k] if k < len(decisions) else False
    return f


def judge(sprog, eprog, name):
    """Compare one emitted program against its source over every policy.

    Returns (traces_compared, [problem, ...]).  A CFG whose branch sequence
    differs is reported as such and never judged on values: comparing two
    different paths cannot produce anything but noise.
    """
    seen = set()
    sites = set()
    problems = []
    n = 0
    nc = nop = nsy = 0
    # Every conditional branch the SOURCE has.  A condition on a site no policy
    # ever reaches is not compared, and saying "20004 conditions compared"
    # without saying which SITES they were is the same kind of number as
    # "nothing was reported".
    allsites = set(i for i, b in enumerate(sprog.branch)
                   if b is not None and b.mn in COND)
    # Static, path-independent, and checked on the EMITTED program only: the
    # source is the specification, so a source that spells an addressing mode
    # the assembler will not take is a source bug, not a compiler bug.
    for kind, text in addr_form_problems(eprog):
        problems.append((kind, text, "static"))
    try:
        nbranch = len(run_trace(sprog, True, False, pol_fall)[2])
    except (Unsupported, Budget):
        nbranch = 0
    for pname, pol in policies(nbranch):
        try:
            want, kw, decisions, cw, infeasible = run_trace(sprog, True, False, pol)
        except (Unsupported, Budget) as e:
            return (0, [("SRC-%s" % type(e).__name__.upper(), str(e), pname)],
                    0, 0, 0, 0, len(allsites))
        key = tuple(decisions)
        if key in seen:
            continue
        seen.add(key)
        try:
            got, kg, _, cg, _ = run_trace(eprog, False, True, replay(decisions))
        except (Unsupported, Budget) as e:
            problems.append(("EMIT-%s" % type(e).__name__.upper(), str(e), pname))
            continue
        n += 1
        if kw != kg:
            i = 0
            while i < min(len(kw), len(kg)) and kw[i] == kg[i]:
                i += 1
            # NOT a divergence.  Sony's vcl reorders whole blocks - on the
            # control-flow generator's corpus it moves 55 of 398 programs'
            # first branch to a different target - so the two walks are simply
            # not the same path any more and there is nothing to compare.  The
            # program is declared unpairable and counted, which is the honest
            # answer; calling it a miscompile would be the instrument reporting
            # a violation in the reference.
            problems.append(("EMIT-CFG-REORDERED",
                             "branch %d is %s in the source and %s emitted"
                             % (i, kw[i] if i < len(kw) else "(end)",
                                kg[i] if i < len(kg) else "(end)"), pname))
            continue
        # A problem seen only where the source's own condition folds against the
        # forced edge is a problem about unreachable code.  Tagged, not dropped:
        # the path policy is what made it unreachable, and the reader is owed
        # the distinction rather than a silently smaller number.
        tag = "-UNREACHABLE" if infeasible else ""
        if DO_COND:
            cprobs, nopq, nsym = compare_conds(kw, cw, cg)
            nc += len(cw)
            nop += nopq
            nsy += nsym
            sites.update(c[3] for c in cw)
            for kind, text in cprobs:
                problems.append((kind + tag, text, pname))
        for kind, text, sample in compare(want, got):
            problems.append((kind + tag, text, pname))
            if kind == "MULTISET":
                ws = sorted(set(want) - set(got))
                gs = sorted(set(got) - set(want))
                if ws:
                    problems.append(("  source-only", fmt(ws[0], 0), pname))
                if gs:
                    problems.append(("  emitted-only", fmt(gs[0], 0), pname))
            break
        if any(p[0] not in ("  source-only", "  emitted-only")
               for p in problems):
            break
        if len(problems) >= 3:
            break
    return n, problems, nc, nop, nsy, len(sites), len(allsites)


def find_source(srcdir, base):
    for cand in (base, "gen_" + base, "eng_" + base):
        p = os.path.join(srcdir, cand + ".vcl")
        if os.path.exists(p):
            return p
    return None


def main():
    isa = IsaTable(sys.argv[1])
    par = Parser(isa)
    srcdir = sys.argv[2]
    tot = bad = traces = skipped = 0
    conds = copaque = csym = sitehit = siteall = 0
    kinds = {}
    for d in sys.argv[3:]:
        print("=== %s" % d)
        dtot = dbad = dtr = dc = dcq = dsym = dsite = dall = 0
        for f in sorted(os.listdir(d)):
            if not f.endswith(".vsm"):
                continue
            src = find_source(srcdir, f[:-4])
            if src is None or not os.path.getsize(os.path.join(d, f)):
                continue
            try:
                sprog = Prog(par.parse(src))
                eprog = Prog(par.parse(os.path.join(d, f)))
            except Exception as e:                       # noqa: BLE001
                print("  %-30s PARSE %s" % (f[:-4], e))
                skipped += 1
                continue
            n, problems, nc, nop, nsy, nsite, nall = judge(sprog, eprog,
                                                            f[:-4])
            dtr += n
            traces += n
            dc += nc
            dcq += nop
            conds += nc
            copaque += nop
            dsym += nsy
            csym += nsy
            dsite += nsite
            dall += nall
            sitehit += nsite
            siteall += nall
            if problems and problems[0][0].startswith(("SRC-", "EMIT-")):
                print("  %-30s SKIP  %s" % (f[:-4], problems[0][1]))
                skipped += 1
                continue
            dtot += 1
            tot += 1
            if problems:
                dbad += 1
                bad += 1
                kinds[problems[0][0]] = kinds.get(problems[0][0], 0) + 1
                print("  %-30s DIVERGES over %d traces" % (f[:-4], n))
                for kind, text, pname in problems[:4]:
                    print("      %-14s [%s] %s" % (kind, pname, text))
        print("  %d programs in scope, %d divergent, %d traces, "
              "%d conditions compared (%d unjudgeable, %d symbolic), "
              "%d/%d branch sites"
              % (dtot, dbad, dtr, dc - dcq, dcq, dsym, dsite, dall))
    print("TOTAL %d programs compared over %d traces, %d divergent, %d skipped"
          % (tot, traces, bad, skipped))
    print("CONDITIONS %d compared, %d unjudgeable (%d total), %d SYMBOLIC; "
          "%d of %d conditional-branch sites reached"
          % (conds - copaque, copaque, conds, csym, sitehit, siteall))
    print("kinds: %s" % (", ".join("%s x%d" % kv for kv in sorted(kinds.items()))
                         or "none"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
