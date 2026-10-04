#!/usr/bin/env python3
"""Run every reproducer through a built openvcl and assert the property it was
written to protect.

This is the thing that does not currently exist: the nine reproducers live in a
scratch directory, they are checked by hand, and the next person to touch the
scheduler has no command that tells them they broke one.  Each case here names

    the .vcl,  the assertion,  and the bug it belongs to

so a failure reads as "you have reintroduced the ACC field kill" rather than as a
diff nobody can interpret.

The assertions are deliberately of three different kinds, because the four known
bugs needed three different kinds to be seen at all:

  COUNT     an instruction the source contains must still be emitted.  This is
            what four deleted `clipw` and three deleted `adda` looked like.
  ORDER     a flag reader must observe the writers the source gives it, checked
            by replaying the CLIP shift register and the ACC chains
            (p8-flagorder.py).
  VALUE     the expression stored at each `sq`/`isw` must be the one the source
            computes, register naming abstracted away (pa-dag.py).

  pa-regress.py <clone> <reproducer-src-dir> <emitted-vsm-dir> [--verbose]
Exit status is 0 only when every case passes.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# (directory, program, kind, argument, what it protects)
CASES = [
    ("p6repro", "acc_fields",            "COUNT", ("acc", 2),
     "bug 2: --drop-dead-writes killed an ACC write a later one did not cover"),
    ("p6repro", "acc_fields_covered",    "COUNT", ("acc", 1),
     "bug 2 control: a genuinely covering ACC write must still kill"),
    ("p8repro", "acc_second_reader",     "ORDER", None,
     "bug 3: the flag pass flushed its writer list at the first reader"),
    ("p8repro", "acc_second_reader_upstream", "ORDER", None,
     "bug 3 on stock upstream, no flags"),
    ("p8repro", "clip_second_reader",    "ORDER", None,
     "bug 3 on CLIP: fcand emitted above its clipw"),
    ("p9repro", "clip_full_window_not_landed", "ORDER", None,
     "bug 4: padForClipFlagWindow exempted full-window masks"),
    ("p6repro", "clip_reorder",          "ORDER", None,
     "bug 1: clipw is a shift register, a later push does not kill an earlier"),
    ("p6repro", "clip_liveout",          "COUNT", ("clip", None),
     "bug 1: every clipw the source writes must be emitted"),
    ("p6repro", "clip_liveout_order",    "ORDER", None,
     "bug 1: and in the order the source pushes them"),
    ("p6repro", "r_order",               "ORDER_R", None,
     "report 5: RNEXT/RXOR are read-modify-write, so rget cannot be hoisted"),
    ("p6repro", "r_rnext",               "COUNT", ("rnext", None),
     "report 5: an rnext whose destination is dead still advances the LFSR"),
    ("p6repro", "r_rxor",                "COUNT", ("rxor", None),
     "report 5: rinit + two rxor must all survive"),
    ("p6repro", "status_sticky",         "COUNT", ("mul", None),
     "report 6: an FMAC whose flags fsand reads must not be deleted"),
    ("p6repro", "status_fsset_barrier",  "COUNT", ("mul", None),
     "report 6: fsset is a barrier for the accumulating status resource"),
    ("p6repro", "status_lastwriter",     "ORDER", None,
     "report 6: a non-sticky mask pins the last contributor"),
    ("p6repro", "status_lastwriter_hoist", "ORDER", None,
     "report 6: and that contributor may not be hoisted past the reader"),
    ("p6repro", "status_accumulate",     "COUNT", ("mul", None),
     "report 6: every contributor since the last clear is live"),
    ("pa-repro", "fcset_hoist2",         "VALUE", None,
     "found by the differential run: a CLIP reader around a second fcset"),
    ("pa-repro", "mac_latency",          "VALUE", None,
     "found by the differential run: which FMAC an fmand reads"),
]

MNEM = {
    "acc": re.compile(r"\b(ADDA|SUBA|MULA|MADDA|MSUBA|OPMULA)[A-Za-z]*"
                      r"(?:\.[xyzw]+)?\s+ACC\b", re.I),
    "clip": re.compile(r"\bclipw?\b", re.I),
    "rnext": re.compile(r"\brnext\b", re.I),
    "rxor": re.compile(r"\brxor\b", re.I),
    "mul": re.compile(r"\bmul[a-z]*(?:\.[xyzw]+)?\s+VF", re.I),
}
SRC_MNEM = {
    "acc": re.compile(r"^\s*(add|sub|mul|madd|msub|opmula|adda|suba|mula|madda"
                      r"|msuba)[a-z]*(?:\.[xyzw]+)?\s+acc\b", re.I),
    "clip": re.compile(r"^\s*clipw?\b", re.I),
    "rnext": re.compile(r"^\s*rnext\b", re.I),
    "rxor": re.compile(r"^\s*rxor\b", re.I),
    "mul": re.compile(r"^\s*mul[a-z]*(?:\.[xyzw]+)?\s+(?!acc)", re.I),
}


def code_lines(path):
    out = []
    for line in open(path, errors="replace"):
        s = line.split(";")[0].split("#")[0].strip()
        if s and not s.startswith(".") and not s.startswith("--") \
                and not s.endswith(":"):
            out.append(s)
    return out


def run_tool(tool, args):
    p = subprocess.run([sys.executable, os.path.join(HERE, tool)] + args,
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def main():
    clone, srcdir, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
    verbose = "--verbose" in sys.argv
    fails = 0
    for d, name, kind, arg, why in CASES:
        src = os.path.join(srcdir, name + ".vcl")
        if not os.path.exists(src):
            print("  %-30s MISSING SOURCE" % name)
            fails += 1
            continue
        vsm = os.path.join(outdir, name + ".vsm")
        if not os.path.exists(vsm) or not os.path.getsize(vsm):
            print("  %-30s FAIL   no emitted program" % name)
            fails += 1
            continue
        detail = ""
        if kind == "COUNT":
            what, want = arg
            got = sum(len(MNEM[what].findall(l)) for l in code_lines(vsm))
            if want is None:
                want = sum(1 for l in code_lines(src) if SRC_MNEM[what].match(l))
            ok = got == want
            detail = "%s: %d emitted, %d wanted" % (what, got, want)
        elif kind in ("ORDER", "ORDER_R"):
            rc2, txt = run_tool("p8-flagorder.py", [clone, srcdir, outdir])
            ok = ("%-34s FAIL" % name) not in txt and name + " " not in \
                 "\n".join(l for l in txt.split("\n") if "FAIL" in l)
            detail = "p8-flagorder"
        else:
            rc2, txt = run_tool("pa-dag.py", [clone, srcdir, outdir])
            ok = name not in [l.split()[0] for l in txt.split("\n")
                              if "DIVERGES" in l]
            detail = "pa-dag"
        print("  %-30s %s   %-28s %s" % (name, "ok  " if ok else "FAIL", detail,
                                         why if (verbose or not ok) else ""))
        fails += 0 if ok else 1
    print("%d of %d cases failed" % (fails, len(CASES)))
    sys.exit(1 if fails else 0)


main()
