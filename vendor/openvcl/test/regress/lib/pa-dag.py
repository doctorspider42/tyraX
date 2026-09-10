#!/usr/bin/env python3
"""The value-DAG oracle: for every store and every xgkick, the expression that
produced the value, normalised over register naming.

WHY THIS AND NOT THE OPERATION MULTISET.  p4-safety.py's multiset is sound
between two builds of ONE assembler and is not sound across two assemblers:
Sony's vcl narrows loads to the fields anybody reads (`lq.w VF01,6(VI00)` where
the source loads a whole quadword), folds address arithmetic into the offset
(`iaddiu VI01,VI00,0` plus `lq 44(VI01)` for a source that says base 40 offset 4)
and deletes everything dead.  None of that changes a stored VALUE, and all of it
changes the multiset.  So the multiset stays an intra-openvcl check and this is
the cross-assembler one.

WHAT IS COMPARED.  The program is executed symbolically.  Every register field
holds an expression, every expression bottoms out in a load, an immediate, VF00
or an entry symbol, and register NAMES appear nowhere in it - so an allocator
that picks different registers produces a literally identical tree.  The
comparison is the ordered list of observables:

    ('sq',  address, mask, (value per field))
    ('isw', address, mask, integer value)
    ('xgkick', address)

Integer arithmetic is constant-folded, because that is the only way an address
SCE folded into an offset can equal the one openvcl computed in a register.

THE FLAG RESOURCES ARE READ THE WAY THE HARDWARE READS THEM.  A `fcand` in an
emitted program sees the CLIP entries that have LANDED - pushes at least four
rows above it - while in the source it sees every push above it.  So a reader
that moved one row too close to its `clipw` produces a different window tuple
here, and the ADC bit computed from it is a different expression.  That is bug 1
and bug 4 showing up as a wrong VALUE rather than as a wrong distance.

SCOPE, STATED PLAINLY.  This walks the straight-line region from program entry to
the first label that something branches to (or the first branch).  On the
straight-line half of the fuzz corpus that is the whole program and the oracle is
exact.  On a program with control flow it covers the entry region only, and the
rest is left to p8-flagorder and to the multiset check, which are CFG-agnostic.
A phi-node version that covered loops was not attempted, because a merge point
whose two arms are compared by a bounded unrolling is a weaker claim than "this
prefix is exactly right", and the resource bugs all live inside straight-line
regions.

Usage:
  pa-dag.py <openvcl-clone> <src-dir> <vsm-dir> [<vsm-dir> ...]
Exit status is 0 only when every program's observables match its source.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from p8isa import IsaTable

FIELDS = "xyzw"
CLIP_LATENCY = int(os.environ.get("PA_CLIP_LAT", 4))

# ------------------------------------------------------------------- parsing

TOKEN = re.compile(r"[A-Za-z_][A-Za-z_0-9]*(?:\.[a-zA-Z]+)?|0[xX][0-9a-fA-F]+"
                   r"|[-+]?\d*\.\d+(?:[eE][-+]?\d+)?|[-+]?\d+|[,()\[\]]|\+\+")
STALL = re.compile(r"STALL_[A-Z_]*\s*\?(\d+)")

# STATUS is deliberately NOT modelled as a value.  Its sticky bits accumulate
# from EVERY FMAC until an `fsset`, and Sony's vcl deletes dead FMACs - so a
# program's status word is not preserved by SCE at all, and comparing it would
# report a divergence on every program that reads status in either direction.
# That is a real fact about the reference, not a miscompile, so `fsand` reads an
# opaque token and the finding is written up rather than counted.  Set
# PA_STATUS=1 to model it and see the difference for yourself.
MODEL_STATUS = os.environ.get("PA_STATUS", "0") == "1"
MODEL_MAC = os.environ.get("PA_MAC", "0") == "1"
# Sony's vcl exploits the FDIV latency exactly as it exploits the EFU one:
# `rsqrt` twice with STALL_THRUPUT annotations, and a consumer placed
# between the two COMPLETIONS reads the first even though the second was
# issued above it.  Deciding that needs a cycle model, so by default a
# program with two divisions in the region is out of scope.  PA_STRICT_Q=0
# judges it on "the last division issued" and is how the one remaining SCE
# case was found and understood.
STRICT_Q = os.environ.get("PA_STRICT_Q", "1") == "1"


class Insn(object):
    def __init__(self, mn, mask, bc, ops, raw):
        self.mn = mn          # base mnemonic, lowercase, broadcast letter removed
        self.mask = mask      # destination field mask, a string over xyzw
        self.bc = bc          # broadcast source field, or None
        self.ops = ops        # [(name, fieldsel-or-None, postinc)]
        self.raw = raw

    def __repr__(self):
        return "<%s.%s %s>" % (self.mn, self.mask, self.ops)


def strip_comment(line):
    return line.split(";")[0].split("#")[0].rstrip()


def parse_operand(toks, i):
    """(name, fieldsel, postinc, offset_expr, base) starting at token i."""
    return None


class Parser(object):
    def __init__(self, isa):
        self.isa = isa

    def split_row(self, toks, is_source):
        starts = []
        for i, t in enumerate(toks):
            if t in (",", "(", ")", "[", "]", "++"):
                continue
            if i and toks[i - 1] in (",", "(", "["):
                continue
            m = t.split(".")[0].lower()
            if m == "nop":
                starts.append(i)
            elif self.isa.base(m) is not None:
                starts.append(i)
        return starts

    def parse(self, path):
        """[(kind, payload)] where kind is 'label' or 'row'."""
        is_source = path.endswith(".vcl")
        out = []
        for line in open(path, errors="replace"):
            # SCE annotates the cycles the HARDWARE will stall on a row it did
            # not pad.  A flag keeps landing during them, so a model that counts
            # rows and ignores them reads the CLIP window too early and invents
            # divergences - the same correction p8isa.py already carries.  Only
            # the pure LATENCY stalls count; the THRUPUT ones are bandwidth
            # estimates.
            stall = sum(int(x) for x in STALL.findall(line))
            body = strip_comment(line)
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
                ins = self.make(toks[st:end], is_source)
                if ins is not None:
                    row.append(ins)
            # A row of two NOPs is still a row.  Dropping it compresses the
            # timeline, and a CLIP push then looks closer to its reader than it
            # is - which reported ten of Sony's own forty programs divergent.
            out.append(("row", row, stall))
        return out

    def make(self, piece, is_source):
        head = piece[0]
        mask = None
        if "." in head:
            head, suffix = head.split(".", 1)
            mask = "".join(c for c in suffix.lower() if c in FIELDS) or None
        mn = head.lower()
        if mn == "nop":
            return None
        bc = None
        if self.isa.base(mn) is None:
            return None
        base = self.isa.base(mn)
        if base != mn and mn[-1] in FIELDS:
            # a broadcast form: addw, mulx, madday, ...
            bc = mn[-1]
            mn = base
        ops = []
        i = 1
        cur = None
        while i < len(piece):
            t = piece[i]
            if t == ",":
                i += 1
                continue
            if t == "(":
                # `off(base)` - the token before was the offset
                j = i + 1
                inner = []
                while j < len(piece) and piece[j] != ")":
                    inner.append(piece[j])
                    j += 1
                names = [x for x in inner if x != "++"]
                ops.append(("(base)", names[0] if names else None,
                            "++" in inner))
                i = j + 1
                continue
            if t == "[":
                if ops and i + 1 < len(piece):
                    name, sel, inc = ops[-1]
                    ops[-1] = (name, piece[i + 1].lower(), inc)
                i += 3 if (i + 2 < len(piece) and piece[i + 2] == "]") else 2
                continue
            if t == "]":
                i += 1
                continue
            sel = None
            name = t
            # SCE spells a broadcast as `VF02w` and a field SET as `VF02xyz`;
            # only a single letter is a broadcast, a set is just a mask and
            # leaves each field reading itself.
            m = re.match(r"^(VF\d+|VI\d+)([xyzw]+)$", t, re.I)
            if m:
                name = m.group(1)
                sel = m.group(2).lower() if len(m.group(2)) == 1 else None
            elif "." in t and t[0].isalpha():
                # only a REGISTER may carry a `.field` suffix.  Splitting on the
                # dot unconditionally turned `loi 0.159154937` into the operand
                # `0`, so every source read that immediate as zero while the
                # emitted code read the correct bit pattern - a divergence in
                # both assemblers at once, which is the signature of the
                # instrument being wrong rather than either of them.
                name, sfx = t.split(".", 1)
                sel = "".join(c for c in sfx.lower() if c in FIELDS) or None
            ops.append((name, sel, False))
            i += 1
        return Insn(mn, mask, bc, ops, " ".join(piece))


# --------------------------------------------------------------- expressions

# vf00.x/y/z ARE the float zero and vf00.w IS one, so they get the canonical
# names rather than a per-field symbol: Sony's vcl folds `sub.y a,b,b` to a read
# of vf00.y, and unless the two spellings of zero are the same node the fold
# reads as a divergence.
FZERO = ("fzero",)
FONE = ("fone",)
VF00 = {"x": FZERO, "y": FZERO, "z": FZERO, "w": FONE}
VF00_ZERO = (FZERO,)


def N(*a):
    """A DAG node, built in a canonical form.

    Tuples are hashable and compare structurally, which is exactly the
    normalisation wanted: no register name is ever in one.  On top of that the
    four VALUE-PRESERVING IDIOMS both emitters use are folded away, because they
    are how an assembler fills an idle slot and not a difference in what the
    program computes:

      MAX(a,a) / MINI(a,a)   openvcl lowers `move.xz f7,f2` to
                             `max.xz VF02,VF01,VF01`, which uses the upper pipe
                             for a move and is exactly `a`;
      ADD/SUB with vf00.xyz  those fields are hard zero;
      MUL by vf00.w          that field is hard one.

    Without this the oracle called 32 of 202 of openvcl's programs and 120 of
    202 of Sony's divergent, all of them on a `move`.
    """
    if len(a) == 3 and a[0] in ("MAX", "MINI") and a[1] == a[2]:
        return a[1]
    if len(a) == 3 and a[0] == "SUB" and a[1] == a[2]:
        return FZERO
    if len(a) == 3 and a[0] in ("ADD", "SUB"):
        if a[2] in VF00_ZERO:
            return a[1]
        if a[0] == "ADD" and a[1] in VF00_ZERO:
            return a[2]
    if len(a) == 3 and a[0] == "MUL":
        if a[2] == FONE:
            return a[1]
        if a[1] == FONE:
            return a[2]
        if a[1] == FZERO or a[2] == FZERO:
            return FZERO
    return a


ZERO = N("const", 0)


def f32bits(text):
    """A `loi` operand as IEEE-754 bits, so `loi 1.5` in a source and
    `loi 0x3fc00000` in the emitted code are the same immediate."""
    t = text.strip()
    if t.lower().startswith("0x"):
        return int(t, 16)
    try:
        return struct.unpack("<I", struct.pack("<f", float(t)))[0]
    except (ValueError, OverflowError):
        return t


BINOP = {"add": "ADD", "sub": "SUB", "mul": "MUL", "max": "MAX", "mini": "MINI",
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
EFU = {"esum": "xyzw", "eleng": "xyz", "erleng": "xyz", "esadd": "xyz",
       "ersadd": "xyz", "eatanxy": "xy", "eatanxz": "xz",
       "esqrt": "x", "ersqrt": "x", "ercpr": "x", "esin": "x", "eatan": "x",
       "eexp": "x"}
QLAT = {"div": 0, "sqrt": 0, "rsqrt": 0}   # see MULTI_PRODUCER below
STORES = ("sq", "sqi", "sqd", "isw", "iswr", "ish", "isb")
LOADS = ("lq", "lqi", "lqd", "ilw", "ilwr")
BRANCH = ("b", "bal", "ibeq", "ibne", "ibgez", "ibgtz", "iblez", "ibltz",
          "jr", "jalr")


class Machine(object):
    def __init__(self):
        self.vf = {}          # (name, field) -> node
        self.vi = {}          # name -> node or ('const', k)
        self.acc = {}
        self.q = N("undef", "Q")
        self.qpend = []          # [(row, value, latency)] divisions in flight
        self.i = N("undef", "I")
        self.r = N("undef", "R")
        self.p = N("undef", "P")
        self.rvf = {}
        self.rvi = {}
        self.racc = {}
        self.rq = self.q
        self.ri = self.i
        self.rr = self.r
        self.rp = self.p
        self.clip = []        # pushed judgements, oldest first
        self.mac = N("undef", "MAC")
        self.status = ()      # accumulated contributors since the last fsset
        self.obs = []
        self.lastwrite = []
        self.entry = {}

    # ---- operand access
    #
    # A .vsm ROW issues an upper and a lower instruction together, and BOTH read
    # the register file as it stood before the row.  Sony's vcl relies on it:
    #
    #     opmsub.xyz VF02xyz,VF03xyz,VF05xyz      sq VF02,301(VI00)
    #
    # stores the value VF02 held BEFORE the opmsub overwrote it.  A model that
    # applies the upper's write before running the lower reads the new value and
    # calls the program wrong - which is what put 111 of Sony's 202 programs in
    # the divergence list.  So reads go through the snapshot taken at row start
    # and writes land in the live state.
    def qvalue(self, row, landing):
        """What the Q register holds at `row`.

        FDIV is not pipelined and its result appears after a fixed latency, so a
        consumer sees the most recently COMPLETED division, not the most recently
        issued one.  Both assemblers exploit that: they issue a second `div`
        while a first is still in flight and place the first consumer where only
        the first has landed.  Taking "the last div issued" - which is what a
        source-order model does - pairs the consumer with the wrong divisor.
        `waitq` completes everything outstanding, and the emitted code contains
        it explicitly.
        """
        v = self.q
        for r, val, lat in self.qpend:
            if landing == 0 or row - r >= lat:
                v = val
        return v

    def begin_row(self, row=0, landing=0):
        self.rvf = dict(self.vf)
        self.rvi = dict(self.vi)
        self.racc = dict(self.acc)
        self.rq = self.qvalue(row, landing)
        self.ri, self.rr, self.rp = self.i, self.r, self.p

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
        return v[1] if isinstance(v, tuple) and v and v[0] == "const" else None

    # ---- the CLIP window, read the way the hardware reads it
    def window(self, row, landing, imm):
        """The entries this reader's mask actually consults, newest first.

        Masking by the immediate is not a refinement, it is the difference
        between an instrument and a false alarm: `fcand VI01,0x2` asks about
        entry 0 and nothing else, so the three older entries may legally hold
        anything - and both emitters exploit that.  Comparing the whole
        four-entry tuple called 14 of 40 of SONY's own programs divergent, which
        is the instrument being wrong.
        """
        landed = [c for c, r in self.clip if row - r >= landing]
        w = landed[-4:][::-1]
        w = tuple(w) + ("none",) * (4 - len(w))
        depth = 0
        if imm is not None:
            for e in range(4):
                if (imm >> (6 * e)) & 0x3F:
                    depth = e + 1
        return tuple(w[e] if e < (depth or 4) else "-" for e in range(4))


def parse_int(t):
    try:
        return int(t, 16) if t.lower().startswith("0x") else int(t)
    except ValueError:
        return None


def run(prog, landing, has_delay=False):
    """Execute the straight-line region and return its observables."""
    m = Machine()
    targets = set()
    for item in prog:
        if item[0] == "row":
            for ins in item[1]:
                if ins.mn in BRANCH and ins.ops:
                    tgt = ins.ops[-1][0]
                    if tgt:
                        targets.add(tgt)
    row = 0
    delay_slot = 0
    for item in prog:
        if item[0] == "label":
            if item[1] in targets:
                break
            continue
        payload, stall = item[1], item[2]
        stop = any(ins.mn in BRANCH for ins in payload)
        # the whole row issues, then the row counter advances
        # WAITQ stalls the whole ROW until the division lands, so the upper
        # instruction paired with it in the same row - which is exactly how both
        # emitters spell it, `subq.yz VF07,VF10,q  waitq` - does see the result.
        if any(ins.mn in ("waitq", "waitp") for ins in payload):
            m.qpend = [(-(1 << 30), v, 0) for _, v, _ in m.qpend]
        m.begin_row(row, landing)
        for ins in payload:
            if ins.mn in BRANCH:
                continue
            step(m, ins, row, landing)
        row += 1 + stall
        if delay_slot:
            delay_slot -= 1
            if not delay_slot:
                break
        if stop:
            # The row AFTER a branch is its delay slot and executes whichever
            # way the branch goes, so it belongs to the straight-line region.
            # Sony's vcl puts real work there - a store, and in one program an
            # `xgkick` - and a walk that stopped at the branch reported those as
            # observables the emitted code had lost.
            if not has_delay:
                break
            delay_slot = 2
    return m.obs


def dest_mask(ins, default="xyzw"):
    return ins.mask or default


def step(m, ins, row, landing):
    mn, ops = ins.mn, ins.ops
    if not ops:
        if mn == "waitq" or mn == "waitp":
            return
        return
    d = ops[0][0]

    # ---------------------------------------------------------------- stores
    if mn in STORES:
        src, sel, _ = ops[0]
        base = ops[1] if len(ops) > 1 else (None, None, False)
        off = 0
        if len(ops) > 2:
            off = parse_int(ops[1][0]) or 0
            base = ops[2]
        elif len(ops) == 2 and base[0] == "(base)":
            off = 0
        # `off(base)` came through as [.., offset, ('(base)', reg, inc)]
        addr = address(m, ops)
        if mn.startswith("is"):
            m.obs.append(N("isw", addr, dest_mask(ins, "x"), m.ivalue(src)))
        else:
            mask = dest_mask(ins)
            m.obs.append(N("sq", addr, mask,
                           tuple(m.vfield(src, sel, f) for f in mask)))
        postinc(m, ops)
        return
    if mn == "xgkick":
        m.obs.append(N("xgkick", m.ivalue(d)))
        return

    # ----------------------------------------------------------------- loads
    if mn in LOADS:
        addr = address(m, ops)
        if mn.startswith("il"):
            m.vi[d.upper()] = N("iload", addr, dest_mask(ins, "x"))
        else:
            for f in dest_mask(ins):
                m.vf[(d.upper(), f)] = N("load", addr, f)
        postinc(m, ops)
        return

    # ------------------------------------------------------------- integer
    if mn in INT_FOLD:
        a = m.ivalue(ops[1][0]) if len(ops) > 1 else ZERO
        b = ops[2][0] if len(ops) > 2 else "0"
        k = parse_int(b)
        bv = N("const", k) if k is not None else m.ivalue(b)
        ka, kb = m.const(a), m.const(bv)
        if ka is not None and kb is not None:
            m.vi[d.upper()] = N("const", INT_FOLD[mn](ka, kb))
        else:
            m.vi[d.upper()] = N(mn, a, bv)
        return
    if mn == "xtop" or mn == "xitop":
        m.vi[d.upper()] = N(mn)
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

    # ------------------------------------------------------ flag readers
    if mn in ("fcand", "fcor", "fceq"):
        imm = parse_int(ops[1][0]) if len(ops) > 1 else 0
        m.vi[d.upper()] = N(mn, imm, m.window(row, landing, imm))
        return
    if mn == "fcget":
        m.vi[d.upper()] = N("fcget", m.window(row, landing, None))
        return
    if mn == "fcset":
        # FCSET writes the clip register from the integer pipe, so unlike a
        # CLIP push from an FMAC there is no four-row landing delay: row - row
        # is 0 and the entries are visible to the very next reader.  Modelling
        # it with the FMAC latency made a `fcset`/`fcand` pair look like a
        # reader of an empty window in both assemblers' output, which is the
        # instrument being wrong rather than either of them.
        imm = parse_int(d) if parse_int(d) is not None else 0
        m.clip = [(N("fcset", imm, k), row - 1000) for k in range(4)]
        return
    if mn in ("fsand", "fsor", "fseq"):
        imm = parse_int(ops[1][0]) if len(ops) > 1 else 0
        m.vi[d.upper()] = N(mn, imm, m.status if MODEL_STATUS else "STATUS")
        return
    if mn == "fsset":
        m.status = ()
        return
    if mn in ("fmand", "fmor", "fmeq"):
        other = m.ivalue(ops[1][0]) if len(ops) > 1 else ZERO
        m.vi[d.upper()] = N(mn, other, m.mac if MODEL_MAC else "MAC")
        return

    # ------------------------------------------------------------ Q, P, I, R
    if mn in ("div", "rsqrt"):
        a = m.vfield(ops[1][0], ops[1][1], ops[1][1] or "x")
        b = m.vfield(ops[2][0], ops[2][1], ops[2][1] or "x")
        m.qpend.append((row, N(mn, a, b), QLAT[mn]))
        return
    if mn == "sqrt":
        b = m.vfield(ops[1][0], ops[1][1], ops[1][1] or "x")
        m.qpend.append((row, N("sqrt", b), QLAT["sqrt"]))
        return
    if mn == "waitq":
        m.qpend = [(-(1 << 30), v, 0) for _, v, _ in m.qpend]
        return
    if mn in EFU:
        # Each EFU operation reads a SPECIFIC set of fields - ELENG and ERLENG
        # are sqrt(x^2+y^2+z^2) and never touch w, while ESUM does.  Reading all
        # four for every one of them made the model demand a value Sony's vcl had
        # correctly left dead, and reported it as a divergence.
        name, sel, _ = ops[1]
        m.p = N(mn, tuple(m.vfield(name, sel, f) for f in EFU[mn]))
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

    # ---------------------------------------------------------------- CLIP
    if mn in ("clip", "clipw"):
        a = ops[0]
        b = ops[1] if len(ops) > 1 else ops[0]
        m.clip.append((N("clip",
                         m.vfield(a[0], a[1], "x"), m.vfield(a[0], a[1], "y"),
                         m.vfield(a[0], a[1], "z"), m.vfield(b[0], "w", "w")),
                       row))
        return

    # ---------------------------------------------------------------- FMAC
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
            v = src(1, rot[f] if mn == "mr32" else f)
            put(m, to_acc, d, f, v)
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
                put(m, to_acc, d, f, N("SUB", accval(m, f), N("SUB", term, term2)))
        newmac(m, mask)
        return
    if mn in ACCOP:
        for f in mask:
            b = src(2, ins.bc or f) if len(ops) > 2 else src(1, ins.bc or f)
            prod = N("MUL", src(1, f), b)
            v = N(ACCOP[mn], accval(m, f), prod)
            put(m, to_acc or mn.endswith("a"), d, f, v)
        newmac(m, mask)
        return
    if mn in BINOP:
        for f in mask:
            if len(ops) > 2:
                b = src(2, ins.bc or f)
            else:
                b = m.i if mn.endswith("i") else m.q
            put(m, to_acc, d, f, N(BINOP[mn], src(1, f), b))
        newmac(m, mask)
        return
    # anything unmodelled must be loud, not silently ignored
    raise KeyError("unmodelled instruction: %s (%s)" % (mn, ins.raw))


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
    """MAC holds the flags of the LAST FMAC, identified by what it computed.

    Identifying it by its field mask alone is too weak - two different FMACs
    with the same mask look identical - and by its position is meaningless once
    the scheduler has moved things.  The VALUES it wrote name it exactly and
    survive register allocation, which is the whole point of this file.
    """
    m.mac = N("mac", tuple(sorted(mask)), tuple(m.lastwrite))
    m.lastwrite = []
    m.status = m.status + (m.mac,)


def address(m, ops):
    """The address of a load/store, with integer arithmetic folded."""
    off, base = 0, None
    for k, (name, sel, inc) in enumerate(ops):
        if name == "(base)":
            base = sel
            prev = ops[k - 1][0] if k else None
            v = parse_int(prev) if prev else None
            if v is not None:
                off = v
            break
    if base is None:
        return N("addr", ZERO, 0)
    b = m.ivalue(base)
    kb = m.const(b)
    if kb is not None:
        return N("addr", ZERO, kb + off)
    return N("addr", b, off)


def postinc(m, ops):
    for name, sel, inc in ops:
        if name == "(base)" and inc and sel:
            v = m.ivalue(sel)
            k = m.const(v)
            m.vi[sel.upper()] = N("const", k + 1) if k is not None \
                else N("iaddiu", v, N("const", 1))


# ------------------------------------------------------------------- driver

def compare(want, got):
    """What must hold between the source's observables and an emitted program's.

    NOT the flat ordered list.  Both assemblers reorder stores to different
    addresses, and they are right to: two stores that cannot alias commute.  So
    three things are checked, each of which a real miscompile breaks and a legal
    reordering does not:

      1. the MULTISET - every store the source makes is made, with the same
         address, the same field mask and the same value, and no store is
         invented, lost or duplicated;
      2. per ADDRESS, the ORDER - two stores to one address must stay in order,
         or the memory ends up holding the wrong one;
      3. every store's order relative to every XGKICK - a kick reads the memory
         a store just wrote, so moving one across the other changes what the GS
         receives.
    """
    problems = []
    from collections import Counter
    cw, cg = Counter(want), Counter(got)
    if cw != cg:
        diff = (cw - cg) + (cg - cw)
        problems.append(("MULTISET", "%d observables differ" % sum(diff.values()),
                         sorted(diff)[:1]))
        return problems
    addrs = set(o[1] for o in want if o[0] != "xgkick")
    for a in addrs:
        # Two stores to one address only conflict if their field masks overlap;
        # `sq.xyz` and `sq.w` to the same quadword touch disjoint lanes and may
        # be issued in either order.
        for f in FIELDS:
            sw = [o for o in want if o[0] != "xgkick" and o[1] == a and f in o[2]]
            sg = [o for o in got if o[0] != "xgkick" and o[1] == a and f in o[2]]
            if sw != sg:
                problems.append(("ADDR-ORDER",
                                 "stores to %s field .%s reordered" % (fmt(a), f),
                                 sw[:1]))
                break
    if any(o[0] == "xgkick" for o in want):
        kw = [i for i, o in enumerate(want) if o[0] == "xgkick"]
        kg = [i for i, o in enumerate(got) if o[0] == "xgkick"]
        if [want[i] for i in kw] != [got[i] for i in kg] or kw != kg:
            problems.append(("KICK-ORDER",
                             "xgkick at positions %s emitted, source has %s"
                             % (kg, kw), []))
    return problems


def fmt(node, depth=0):
    if not isinstance(node, tuple):
        return str(node)
    if depth > 3:
        return "..."
    return "%s(%s)" % (node[0], ",".join(fmt(x, depth + 1) for x in node[1:]))


def main():
    isa = IsaTable(sys.argv[1])
    par = Parser(isa)
    srcdir = sys.argv[2]
    bad = total = skipped = nocf = multi = 0
    kinds = {}
    for d in sys.argv[3:]:
        print("=== %s" % d)
        dbad = dtot = 0
        for f in sorted(os.listdir(d)):
            if not f.endswith(".vsm"):
                continue
            src = os.path.join(srcdir, f[:-4] + ".vcl")
            if not os.path.exists(src) or not os.path.getsize(os.path.join(d, f)):
                continue
            sprog = par.parse(src)
            # MULTI_PRODUCER.  Q and P each hold ONE result, delivered after a
            # fixed latency, and both emitters schedule around that: Sony puts
            # twenty NOP rows between two `eleng`s so the first `mfp` lands
            # between the two completions (pa-repro/efu_pair.vcl).  Deciding
            # which producer a consumer sees therefore needs a cycle model with
            # per-operation EFU latencies, and a wrong constant there turns a
            # correct schedule into a reported miscompile - the exact mistake
            # this work has already made twice.  So a program with more than one
            # division or more than one EFU operation in the region is declared
            # OUT OF SCOPE rather than judged on a guess.  With one producer the
            # pairing is unambiguous and the oracle is exact, and one producer is
            # what every program in the engine's corpus has.
            prods = sum(1 for it in sprog if it[0] == "row" for i in it[1]
                        if i.mn in ("div", "sqrt", "rsqrt"))
            efus = sum(1 for it in sprog if it[0] == "row" for i in it[1]
                       if i.mn in EFU)
            if efus > 1 or (prods > 1 and STRICT_Q):
                multi += 1
                continue
            if any(i.mn in BRANCH for it in sprog if it[0] == "row"
                   for i in it[1]):
                # A source with control flow has no well-defined straight-line
                # region to compare: the emitted code may pull work from AFTER
                # the branch into its delay slot and push work from before it
                # past the branch, and both are legal.  Judging such a program on
                # a prefix produces findings that are artefacts of where the
                # prefix was cut, so it is declared out of scope instead.  Its
                # flag ordering is still checked, by p8-flagorder.
                nocf += 1
                continue
            try:
                want = run(sprog, 0)
                got = run(par.parse(os.path.join(d, f)), CLIP_LATENCY, True)
            except KeyError as e:
                print("  %-30s SKIP %s" % (f[:-4], e))
                skipped += 1
                continue
            dtot += 1
            total += 1
            problems = compare(want, got)
            if problems:
                dbad += 1
                bad += 1
                kinds[problems[0][0]] = kinds.get(problems[0][0], 0) + 1
                print("  %-30s DIVERGES  %d observables in source, %d emitted"
                      % (f[:-4], len(want), len(got)))
                for kind, text, sample in problems[:2]:
                    print("      %-11s %s" % (kind, text))
                    for s in sample:
                        print("        %s" % fmt(s))
                if problems[0][0] == "MULTISET":
                    ws = set(want) - set(got)
                    gs = set(got) - set(want)
                    for s in sorted(ws)[:1]:
                        print("        only in source:  %s" % fmt(s))
                    for s in sorted(gs)[:1]:
                        print("        only in emitted: %s" % fmt(s))
        print("  %d programs, %d divergent" % (dtot, dbad))
    print("TOTAL %d compared, %d divergent, %d unmodelled, %d out of scope "
          "(control flow), %d out of scope (two Q or two EFU producers)"
          % (total, bad, skipped, nocf, multi))
    print("kinds: %s" % (", ".join("%s x%d" % kv for kv in sorted(kinds.items()))
                         or "none"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
