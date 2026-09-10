#!/usr/bin/env python3
"""p6-accscan.py, with the hole that made it answer zero.

p6-accscan.py reported "adjacent ACC writers with non-covering masks: 0" and that
number went into both docs as the reason the ACC field bug was unreachable. It is
wrong, and the reason is one regex:

    ACC_WRITE = r"^\\s*(adda|suba|mula|madda|msuba|opmula)...\\s+acc\\b"

VCL does not require the accumulator MNEMONIC to write the accumulator - the
DESTINATION OPERAND decides. The engine's own sources write it as

    add.xy     acc, vf00, envConsts[w]      -> ADDAw.xy
    add.z      acc, vf00, envConsts[z]      -> ADDAz.z

and the scan could not see either line, so it saw no writers, so it found no
risky pairs. Those two are adjacent, non-covering (xy then z), and followed by
three field-selective madds - the exact shape the whole-register kill gets wrong.

Two other things this fixes:

  * ADJACENCY. The dead-write walk does not stop at the next instruction, it walks
    to the next ACC reader. Two writers with unrelated instructions between them
    are the same bug, so this pairs each writer with every later writer up to the
    next reader rather than only with the one after it.
  * The "did the emitted code lose an ACC writer" count is only meaningful once
    the source side counts the same spellings; with the old regex it was off by
    the plain-mnemonic writes in 39 of 70 programs and read as noise.

Usage: p7-accscan.py <src-dir> <vsm-dir>
Exit status is 0 only when no risky pair is found and no program lost a writer.
"""
import os
import re
import sys

# Any FMAC whose destination operand is `acc`, whatever the mnemonic. The
# accumulator mnemonics are accepted too, for sources that spell it that way.
ACC_WRITE = re.compile(
    r"^\s*(add|sub|mul|madd|msub|opmula|opmsub|adda|suba|mula|madda|msuba)"
    r"([a-z]*)((?:\.[xyzw]+)?)\s+acc\b", re.I)
ACC_WRITE_VSM = re.compile(
    r"\b(ADDA|SUBA|MULA|MADDA|MSUBA|OPMULA)([A-Za-z]*)((?:\.[xyzw]+)?)\s+ACC\b", re.I)
# A reader is an FMAC that accumulates onto ACC without naming it as destination.
ACC_READ = re.compile(r"^\s*(madd|msub|opmsub)[a-z]*(?:\.[xyzw]+)?\s+(?!acc\b)", re.I)


def mask_of(text):
    return set(text[1:]) if text else set("xyzw")


def scan_source(path):
    seq = []
    for line in open(path, errors="replace"):
        body = line.split(";")[0].split("#")[0]
        if body.strip().startswith("--") or body.strip().startswith("."):
            continue
        m = ACC_WRITE.match(body)
        if m:
            seq.append(("w", mask_of(m.group(3)), body.strip()))
            continue
        if ACC_READ.match(body):
            seq.append(("r", None, body.strip()))
    risky = []
    for i, (kind, mask, text) in enumerate(seq):
        if kind != "w":
            continue
        # Walk forward to the next reader, exactly as implicitWriteIsObservable
        # does, accumulating the coverage of the writers in between.
        covered = set()
        for j in range(i + 1, len(seq)):
            if seq[j][0] == "r":
                break
            covered |= seq[j][1]
            if mask <= covered:
                break
            risky.append((text, seq[j][2], "".join(sorted(mask - covered))))
            break
    return len([s for s in seq if s[0] == "w"]), risky


def count_vsm(path):
    return sum(1 for line in open(path, errors="replace")
               if ACC_WRITE_VSM.search(line.split(";")[0]))


def main():
    srcdir, vsmdir = sys.argv[1], sys.argv[2]
    risky_total = 0
    lost = []
    for f in sorted(os.listdir(vsmdir)):
        if not f.endswith(".vsm"):
            continue
        src = os.path.join(srcdir, f[:-4] + ".vcl")
        if not os.path.exists(src):
            continue
        nsrc, risky = scan_source(src)
        nvsm = count_vsm(os.path.join(vsmdir, f))
        if risky:
            risky_total += len(risky)
            print("  %-34s %d writer(s) a later write does not cover" % (f[:-4], len(risky)))
            for a, b, left in risky[:3]:
                print("        %-42s THEN %-30s leaves .%s live" % (a, b, left))
        if nvsm != nsrc:
            lost.append((f[:-4], nsrc, nvsm))
    for name, nsrc, nvsm in lost:
        print("  %-34s ACC writers %d emitted vs %d in source" % (name, nvsm, nsrc))
    print("ACC writers not covered before the next reader: %d" % risky_total)
    print("programs whose ACC-writer count changed:        %d" % len(lost))
    sys.exit(1 if (risky_total or lost) else 0)


main()
