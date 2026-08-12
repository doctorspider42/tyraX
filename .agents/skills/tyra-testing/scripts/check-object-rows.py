#!/usr/bin/env python3
"""Check a generated SceneObjectData struct against every emitted row.

Nothing on the host compiles those rows. A mismatch therefore survives every
editor build, every harness and `--refresh-gen`; the PS2 toolchain inside Docker
is the first thing that sees it, and it reports something like
`invalid conversion from 'const char*' to 'int'` in a generated header, far from
the edit that caused it. It does not take a merge either - two edits anchored on
the same struct line can put a `const char*` between two ints while the emitter
keeps the old order.

This is the cheap check for that, with no Docker: read the struct and the real
rows out of the GENERATED headers and compare them column by column.

    python check-object-rows.py <projectDir>
    python check-object-rows.py <inc/scene_data.hpp> [more headers with rows...]

Exit 0 = struct and rows agree. Exit 1 = they do not, with the field named.
Run it after `--refresh-gen` (or any build), and always after merging main.

Two things about the file shape it has to respect, both of which broke a naive
version of this script: the tables are named per scene (`SCENE_0_OBJECTS`, ...)
plus `PREFAB_MEMBERS` in prefab_data.gen.hpp, not one `SCENE_OBJECTS`; and a row
contains nested `{...}` aggregates for the vectors, so columns must be split on
BALANCED braces rather than on the first `},`.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

# Scalars a row column can be. Anything else in the struct is an aggregate
# (a float[3] or a nested vector) and its column must be a braced group.
SCALARS = r'const char\s*\*|float|double|int|unsigned char|signed char|char|' \
          r'short|unsigned short|unsigned int|unsigned|long|bool'


def inputs(argv: list[str]) -> list[Path]:
    """Resolve the command line to a list of generated headers to read."""
    if len(argv) == 1 and Path(argv[0]).is_dir():
        root = Path(argv[0])
        found = [p for p in (root / 'inc' / 'scene_data.hpp',
                             root / 'inc' / 'prefab_data.gen.hpp') if p.is_file()]
        if not found:
            sys.exit('%s holds no inc/scene_data.hpp - is it a generated '
                     'project? Run a build or --refresh-gen first.' % root)
        return found
    paths = [Path(a) for a in argv]
    for p in paths:
        if not p.is_file():
            sys.exit('not a file: %s' % p)
    return paths


def struct_fields(src: str, where: str) -> list[tuple[str, str]]:
    """[(kind, name)] for SceneObjectData, kind in {'str', 'num', 'agg'}.

    ONE DECLARATION LINE CAN DECLARE SEVERAL FIELDS, and skipping what it
    cannot parse is how a checker like this lies. A first version of this
    script matched a single `type name;` per line, silently dropped the four
    multi-declarator lines the pre-v18 Foot IK block used
    (`const char *ikLeftHip, *ikLeftKnee, *ikLeftAnkle;` and three like it),
    counted 63 fields against 77 real columns and reported examples/cube as
    fourteen mismatches - a project that was perfectly consistent. The same
    silence can hide a REAL mismatch just as easily, so every non-blank,
    non-comment line in the struct must parse or this exits.
    """
    m = re.search(r'struct SceneObjectData \{(.*?)\n\};', src, re.S)
    if not m:
        sys.exit('no `struct SceneObjectData` in %s' % where)
    out, unparsed = [], []
    for line in m.group(1).split('\n'):
        line = re.sub(r'//.*', '', line).strip()
        if not line:
            continue
        head = re.match(r'(' + SCALARS + r')\s*(.*);$', line)
        if not head:
            unparsed.append(line)
            continue
        ty, rest = head.group(1), head.group(2)
        for decl in rest.split(','):
            d = re.match(r'\s*(\*?)\s*(\w+)\s*(\[\s*\d+\s*\])?\s*$', decl)
            if not d:
                unparsed.append(line)
                break
            star, name, arr = d.group(1), d.group(2), d.group(3)
            pointer = '*' in ty or star == '*'
            out.append(('agg' if arr else 'str' if pointer else 'num', name))
    if unparsed:
        for line in unparsed:
            print('  !! cannot parse struct line: %s' % line, file=sys.stderr)
        sys.exit('SceneObjectData in %s holds %d line(s) this script cannot '
                 'read. Fix the parser rather than trusting a partial count - '
                 'an under-count reports false mismatches AND hides real ones.'
                 % (where, len(unparsed)))
    if not out:
        sys.exit('SceneObjectData in %s parsed to zero fields - the struct '
                 'shape changed and this script needs updating.' % where)
    return out


def after_group(s: str, start: int) -> int:
    """Index just past the balanced '{...}' group that opens at s[start]."""
    depth, i = 0, start
    while i < len(s):
        c = s[i]
        if c == '"':                      # skip a string literal whole
            i += 1
            while i < len(s) and s[i] != '"':
                i += 2 if s[i] == '\\' else 1
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    sys.exit('unbalanced braces at offset %d - the header is truncated' % start)


def columns(row: str) -> list[str]:
    """Split one row body into its top-level columns."""
    out, depth, cur, i = [], 0, '', 0
    while i < len(row):
        c = row[i]
        if c == '"':
            j = i + 1
            while j < len(row) and row[j] != '"':
                j += 2 if row[j] == '\\' else 1
            cur += row[i:j + 1]
            i = j + 1
            continue
        if c in '{(':
            depth += 1
        elif c in '})':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += c
        i += 1
    if cur.strip():
        out.append(cur.strip())
    return out


def value_kind(col: str) -> str:
    """'str', 'num', 'agg', or '?' when it is a name this script cannot type."""
    if col.startswith('"'):
        return 'str'
    if col.startswith('{'):
        return 'agg'
    if re.match(r'^[-+]?[\d.]', col) or col in ('true', 'false'):
        return 'num'
    return '?'


def main() -> int:
    if len(sys.argv) < 2 or sys.argv[1] in ('-h', '--help'):
        sys.exit(__doc__)
    paths = inputs(sys.argv[1:])
    texts = {p: p.read_text(encoding='utf-8', errors='replace') for p in paths}

    fields = None
    for p, t in texts.items():
        if 'struct SceneObjectData {' in t:
            fields = struct_fields(t, str(p))
            break
    if fields is None:
        sys.exit('none of %s declares SceneObjectData'
                 % ', '.join(str(p) for p in paths))
    print('struct SceneObjectData: %d fields (%d aggregate)'
          % (len(fields), sum(1 for k, _ in fields if k == 'agg')))

    bad = rows = tables = 0
    for p, src in texts.items():
        for tbl in re.finditer(
                r'constexpr SceneObjectData (\w+)\[[^\]]*\]\s*=\s*\{\n', src):
            name, seen, pos = tbl.group(1), 0, tbl.end()
            tables += 1
            while True:
                nxt = src.find('{', pos)
                if nxt < 0:
                    break
                stop = after_group(src, nxt)
                cols = columns(src[nxt + 1:stop - 1])
                seen += 1
                rows += 1
                if len(cols) != len(fields):
                    print('  !! %s[%d]: %d columns against %d struct fields'
                          % (name, seen - 1, len(cols), len(fields)))
                    bad += 1
                for k, (want, fname) in enumerate(fields):
                    if k >= len(cols):
                        break
                    got = value_kind(cols[k])
                    if got in ('?', want):
                        continue
                    print('  !! %s[%d] column %d: field `%s` is %s, the value '
                          'is %s: %s'
                          % (name, seen - 1, k, fname, want, got, cols[k][:48]))
                    bad += 1
                pos = stop
                if not re.match(r'\s*,\s*(//[^\n]*\n)?\s*\{', src[stop:stop + 256]):
                    break
            print('  %-24s %d row(s) in %s' % (name, seen, p.name))

    if not tables:
        sys.exit('no `constexpr SceneObjectData <name>[...]` table found - '
                 'pass inc/scene_data.hpp, or a project directory.')
    print('%d table(s), %d row(s): %s'
          % (tables, rows, 'OK' if not bad else '%d MISMATCH(ES)' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
