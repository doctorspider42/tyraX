#!/usr/bin/env python3
"""Does every implicit-flag reader in an EMITTED program observe the writers the
source says it observes?

This is the question `addPreciseImplicitFlagDependencies()` is supposed to make
true and, at a second reader, does not: it FLUSHES its pending-writer list at a
reader, so a following reader of the same resource gets no edge to the writers
the first one consumed and may be hoisted above them.

Not a count.  The source is replayed to give every reader the exact writer
IDENTITIES it must observe, the emitted program is replayed the same way, and a
divergence names which writer the reader should have seen and which one it got.
Reading a count is what let the CLIP miscompile survive: four of seven `clipw`
were being deleted and every count that was checked still agreed with itself.

WHAT IS AND IS NOT A BUG, per resource:

CLIP is a four-entry shift register with a four-row landing delay, and BOTH
emitters exploit the delay: SCE routinely issues the next `clipw` ABOVE a reader,
because a push that has not landed is not in the window yet.  So program order
alone proves nothing, and the check separates the two directions:

  * the reader's landed window must hold the same judgements the source gives it
    (LAND).  This is the correctness criterion, and SCE passes it everywhere;
  * a writer appearing BELOW a reader that the source puts ABOVE it is a HOIST -
    the reader moved, not the writer - and that is the flush bug's signature.
    Told apart from the legal case by which way the writer ids move.

ACC has no landing delay (it is forwarded inside the FMAC pipe) but it IS
permuted: SCE moves whole accumulation chains past each other, so reader k of the
source is not reader k of the output and any index-based identity is fiction.
The trace is therefore decomposed into CHAINS - a seed, its accumulating
contributors, and the readers that drain it - and the invariant is that the
emitted chains are a PERMUTATION of the source's, with per-field coverage
preserved inside each.  A reader that has no seed above it is an orphan, which is
what a hoisted second reader looks like.

MAC is checked the same way as CLIP.  It has no readers at all in this corpus,
and that is now a measurement rather than an assumption - see p8-census.py.

Usage:
  p8-flagorder.py <openvcl-clone> <src-dir> <vsm-dir> [<vsm-dir> ...]
  p8-flagorder.py <openvcl-clone> --self-test
Exit status is 0 only when every program checked is clean.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import p8isa
from p8isa import IsaTable, scan_program

# CLIP and MAC flags reach the integer pipe four rows after the FMAC issues.
LATENCY = {"CLIP": int(os.environ.get("P8_CLIP_LAT", 4)), "MAC": 4}
DEPTH = {"CLIP": 4, "MAC": 1}
FIELDNAME = {1: "x", 2: "y", 4: "z", 8: "w"}


# ---------------------------------------------------------------- CLIP / MAC

def align(src, emit):
    """LCS alignment of two writer lists; returns emit-index -> src-index."""
    key = lambda e: (e.mnemonic, e.fields)
    n, m = len(src), len(emit)
    t = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        for j in range(m - 1, -1, -1):
            t[i][j] = t[i + 1][j + 1] + 1 if key(src[i]) == key(emit[j]) \
                else max(t[i + 1][j], t[i][j + 1])
    out, i, j = {}, 0, 0
    while i < n and j < m:
        if key(src[i]) == key(emit[j]):
            out[j] = i
            i += 1
            j += 1
        elif t[i + 1][j] >= t[i][j + 1]:
            i += 1
        else:
            j += 1
    return out


def window_replay(events, res, latency, idmap=None):
    """[(reader, window-newest-first)] for a shift-register resource."""
    depth = DEPTH[res]
    window, pending, wid, out = [], [], 0, []

    def land(row):
        while pending and row - pending[0][0] >= latency:
            _, pid, full = pending.pop(0)
            for _ in range(depth if full else 1):
                window.insert(0, pid)
            del window[depth:]

    for e in events:
        land(e.row)
        if res in e.reads:
            out.append((e, list(window) + [None] * (depth - len(window))))
        if res in e.writes:
            pending.append((e.row, wid if idmap is None else idmap.get(wid, "NEW"),
                            e.mnemonic == "fcset"))
            wid += 1
    land(1 << 30)
    return out


def mask_depth(imm, default):
    if imm is None:
        return default
    d = 0
    for e in range(4):
        if (imm >> (6 * e)) & 0x3F:
            d = e + 1
    return d or 1


def check_window(isa, src, emit, res):
    sw = [e for e in src if res in e.writes]
    ew = [e for e in emit if res in e.writes]
    sr = [e for e in src if res in e.reads]
    er = [e for e in emit if res in e.reads]
    if not sr:
        return None
    if len(sr) != len(er):
        return ["%d readers emitted, source has %d" % (len(er), len(sr))]
    # Two readers of the same resource have no edge between them, so a hoisted
    # one can also swap PAST ANOTHER READER.  That leaves every window in place
    # and only exchanges which integer register gets which answer, so it is
    # invisible to the window comparison; the mask sequence is what sees it.
    if [e.imm for e in sr] != [e.imm for e in er]:
        return ["SWAP   reader masks %s emitted, source has %s"
                % ([hex(x) if x is not None else None for x in [e.imm for e in er]],
                   [hex(x) if x is not None else None for x in [e.imm for e in sr]])]

    m = align(sw, ew)
    idmap = dict((j, m.get(j, "NEW%d" % j)) for j in range(len(ew)))
    want = window_replay(src, res, 0)
    order = window_replay(emit, res, 0, idmap)
    land = window_replay(emit, res, LATENCY[res], idmap)

    problems = []
    for k in range(len(sr)):
        d = mask_depth(sr[k].imm, DEPTH[res])
        w, o, l = want[k][1][:d], order[k][1][:d], land[k][1][:d]
        tag = "0x%X" % sr[k].imm if sr[k].imm is not None else "-"
        # A HOIST is a writer the source puts ABOVE this reader turning up BELOW
        # it: the ids the reader sees go DOWN.  A writer issued early but not yet
        # landed makes them go UP, and that is what both emitters do on purpose.
        def num(x):
            return x if isinstance(x, int) else -1
        if any(num(o[i]) < num(w[i]) for i in range(d)):
            problems.append("HOIST  reader %d (%s row %d %s): source window %s, "
                            "emitted program order %s"
                            % (k, er[k].mnemonic, er[k].row, tag, w, o))
        if l != w:
            problems.append("LAND   reader %d (%s row %d %s): source window %s, "
                            "landed window %s"
                            % (k, er[k].mnemonic, er[k].row, tag, w, l))
    return problems


# ---------------------------------------------------------------------- ACC

def field_chains(events, f):
    """Accumulation chains of ONE ACC field, in order.

    Per field is the granularity the hardware has, and the granularity SCE
    schedules at: it freely interleaves `add.xy acc ... madd.x` with
    `add.z acc ... madd.z`, because they share a mnemonic and nothing else.  A
    whole-register chain model calls that a reordering and is simply wrong - it
    was the first thing this instrument got wrong, and SCE's output is what said
    so.

    A chain is a seed, its accumulating contributors, and the readers that drain
    it; a new one starts at a write-only event that follows a read.  The first
    chain may open with readers (a value carried in from a previous block), which
    is exactly what a HOISTED reader also looks like - so the signature carries
    it and the comparison against the source decides which it is.
    """
    out, cur, prev_was_read = [], [], True
    for e in events:
        r, w = "ACC" in e.reads, "ACC" in e.writes
        if not (e.fields & f) or not (r or w):
            continue
        role = "RW" if (r and w) else ("W" if w else "R")
        if role == "W" and prev_was_read and cur:
            out.append(cur)
            cur = []
        cur.append((role, e))
        prev_was_read = (role == "R")
    if cur:
        out.append(cur)
    return out


def check_acc(isa, src, emit):
    if not [e for e in src if "ACC" in e.reads]:
        return None
    problems = []
    for f in (1, 2, 4, 8):
        sc = field_chains(src, f)
        ec = field_chains(emit, f)
        ssig, esig = {}, {}
        for c in sc:
            k = tuple(r for r, _ in c)
            ssig[k] = ssig.get(k, 0) + 1
        for c in ec:
            k = tuple(r for r, _ in c)
            esig[k] = esig.get(k, 0) + 1
        for k in sorted(set(list(ssig) + list(esig))):
            if ssig.get(k, 0) != esig.get(k, 0):
                rows = [str(c[0][1].row) for c in ec if tuple(r for r, _ in c) == k]
                problems.append("CHAIN  ACC.%s chain %s: %d emitted, %d in source%s"
                                % (FIELDNAME[f], " ".join(k), esig.get(k, 0),
                                   ssig.get(k, 0),
                                   (" (at rows %s)" % ",".join(rows[:6])) if rows else ""))
    return problems


# -------------------------------------------------------------------- driver

def check(isa, srcpath, vsmpath, res):
    src = scan_program(isa, srcpath)
    emit = scan_program(isa, vsmpath)
    if res == "ACC":
        return check_acc(isa, src, emit)
    return check_window(isa, src, emit, res)


def self_test(isa):
    ok = True
    for mn, dest, want_r, want_w in (("add", "acc", None, "ACC"),
                                     ("add", "vf01", None, None),
                                     ("maddaz", "ACC", "ACC", "ACC"),
                                     ("madd", "vf01", "ACC", None),
                                     ("clipw", "vf01", None, "CLIP"),
                                     ("fcand", "vi01", "CLIP", None)):
        b, rd, wr = isa.resolve(mn, dest)
        good = ((want_r is None or want_r in rd) and (want_w is None or want_w in wr)
                and (want_w is not None or "ACC" not in wr or mn != "add"))
        print("  %-8s %-5s -> %-8s reads=%-8s writes=%s%s"
              % (mn, dest, b, ",".join(sorted(rd)) or "-",
                 ",".join(sorted(wr)) or "-", "" if good else "   *** WRONG ***"))
        ok &= good
    here = os.path.dirname(os.path.abspath(__file__))
    ref = os.path.join(here, "p8self", "ref.vcl")
    for res, cases in (("CLIP", (("clean", "good.vsm", False),
                                 ("early writer", "early.vsm", False),
                                 ("reader swapped", "bad.vsm", True),
                                 ("reader above writer", "hoist.vsm", True))),
                       ("ACC", (("clean", "good.vsm", False),
                                ("early writer", "early.vsm", False),
                                ("reader above seed", "bad.vsm", True),
                                ("clip-only motion", "hoist.vsm", False)))):
        for name, f, expect in cases:
            pr = check(isa, ref, os.path.join(here, "p8self", f), res) or []
            fired = bool(pr)
            print("  %-5s %-15s -> %-6s%s" % (res, name, "FIRES" if fired else "clean",
                                              "" if fired == expect else "   *** WRONG ***"))
            for p in pr:
                print("        %s" % p)
            ok &= (fired == expect)
    print("self-test %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    isa = IsaTable(sys.argv[1])
    if len(sys.argv) > 2 and sys.argv[2] == "--self-test":
        sys.exit(self_test(isa))
    srcdir = sys.argv[2]
    bad = 0
    for d in sys.argv[3:]:
        print("=== %s" % d)
        counts, kinds = {}, {}
        for f in sorted(os.listdir(d)):
            if not f.endswith(".vsm"):
                continue
            src = os.path.join(srcdir, f[:-4] + ".vcl")
            if not os.path.exists(src):
                print("  %-34s NO SOURCE" % f[:-4])
                bad += 1
                continue
            allp = []
            for res in ("CLIP", "MAC", "ACC"):
                pr = check(isa, src, os.path.join(d, f), res)
                if pr is None:
                    continue
                counts[res] = counts.get(res, 0) + 1
                for p in pr:
                    kinds[p.split()[0]] = kinds.get(p.split()[0], 0) + 1
                allp += ["%-5s %s" % (res, p) for p in pr]
            if allp:
                bad += 1
                print("  %-34s FAIL" % f[:-4])
                for p in allp:
                    print("        %s" % p)
        print("  checked: " + ", ".join("%s in %d programs" % (r, n)
                                        for r, n in sorted(counts.items())))
        print("  findings: " + (", ".join("%s x%d" % (k, v) for k, v in sorted(kinds.items()))
                                or "none"))
    if p8isa.ANOMALIES:
        print("TOKENISER ANOMALIES: %d rows (an alias read as a mnemonic would "
              "invalidate the trace)" % len(p8isa.ANOMALIES))
        for a in p8isa.ANOMALIES[:5]:
            print("   %s:%d  %s" % a)
    print("%d programs with a divergence" % bad)
    sys.exit(1 if bad else 0)


main()
