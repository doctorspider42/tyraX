#!/usr/bin/env python3
"""How far does each assembler put a CLIP-flag reader from the push it reads?

The question this answers is worth stating precisely, because two earlier
instruments in this effort answered a DIFFERENT one and their numbers were quoted
as if comparable.  Here, per emitted program:

  * a PUSH is `clipw`/`clip` - it shifts a new 6-bit judgement into the CLIP
    register's 24-bit window and drops the oldest;
  * a READER is `fcand`/`fcor`/`fceq` with a mask;
  * the GAP is the number of emitted ROWS strictly between a reader and the
    NEAREST PRECEDING push, counting every row including `nop`/`nop` padding.

Rows, not cycles.  A row model is not a cycle model and this file says so twice
elsewhere; the CLIP window shifts once per pushed INSTRUCTION, which is exactly
what rows count, so rows are the right unit here and only here.

Masks are bucketed by which of the four 6-bit generations they touch.  That is the
distinction the whole argument turns on: a mask covering only older generations
depends on an older push, and the pushes in between are themselves instructions.

Usage: pt-clipgap.py <label> <dir-of-vsm> [<label> <dir> ...]
"""
import os
import re
import sys
from collections import defaultdict

PUSH = re.compile(r"\bclip(w)?\b", re.I)
# The hex alternative comes FIRST.  With `[0-9]+` first, `0x3FFFF` matched the
# leading `0` and every reader in the corpus was recorded with mask 0 - which reads
# as "no generation touched" and would have made the whole census say the opposite
# of the truth.  Caught by looking at the emitted text, which is the only reason
# this file is not wrong.
READ = re.compile(r"\b(fcand|fcor|fceq)\b[^,]*,\s*(0[xX][0-9a-fA-F]+|[0-9]+)", re.I)
ROW = re.compile(r"^\s+\S")


def generations(mask):
    """Which of the four 6-bit generations does this mask touch? 0 = newest."""
    return tuple(g for g in range(4) if (mask >> (6 * g)) & 0x3F)


def census(directory):
    per_mask = defaultdict(list)
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".vsm"):
            continue
        rows = []
        with open(os.path.join(directory, name), errors="replace") as fh:
            for line in fh:
                if ROW.match(line) and not line.strip().startswith(";"):
                    rows.append(line)
        last_push = None
        for i, line in enumerate(rows):
            if PUSH.search(line):
                last_push = i
                # a row can push AND read; the read then sees the old window
            m = READ.search(line)
            if m and last_push is not None:
                mask = int(m.group(2), 16) if m.group(2).lower().startswith("0x") \
                    else int(m.group(2))
                per_mask[(mask, generations(mask))].append(i - last_push)
    return per_mask


def main():
    args = sys.argv[1:]
    if len(args) < 2 or len(args) % 2:
        sys.exit(__doc__)
    arms = [(args[i], args[i + 1]) for i in range(0, len(args), 2)]
    tables = [(label, census(d)) for label, d in arms]
    keys = sorted({k for _, t in tables for k in t})
    head = "%-12s %-14s" % ("mask", "generations")
    for label, _ in tables:
        head += " | %-26s" % label
    print(head)
    print("-" * len(head))
    for mask, gens in keys:
        row = "%-12s %-14s" % ("0x%X" % mask, ",".join(str(g) for g in gens) or "-")
        for _, tab in tables:
            gaps = tab.get((mask, gens), [])
            if not gaps:
                row += " | %-26s" % "-"
            else:
                counts = defaultdict(int)
                for g in gaps:
                    counts[g] += 1
                row += " | %-26s" % ("n=%d min=%d  %s" % (
                    len(gaps), min(gaps),
                    " ".join("%d:x%d" % (g, counts[g]) for g in sorted(counts))))
        print(row)


if __name__ == "__main__":
    main()
