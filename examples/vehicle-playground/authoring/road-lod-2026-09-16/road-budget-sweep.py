"""Sweep the road lateral budget over the REAL Motor District, and price it.

Counts are cheap and exact off the console, because the tessellator is host
code: this compiles the SHIPPED src/roadgen.cpp at a given (flatness, shear)
against a renamed copy of the same file at a reference commit, runs both over
the district's seven authored roads and its real heightfield, and reports the
geometry removed together with the error it costs at every dense sample.

It reproduces the console's own producer line exactly at the reference budget
(`ROADSTRIP scene 0 strips 1 packages 470 triangles 31050`), which is what
makes it usable as an oracle rather than an estimate.

Usage, from this directory:

    # the reference half: any commit-ish whose roadgen is the baseline
    python road-budget-sweep.py --reference HEAD~1 0.00001,0.00001 0.00001,0.05

Each positional argument is `flatness,shear`. Requires g++ and python3 only.

WHY THE BINARIES ARE LINKED -static: this harness uses <fstream>, and on a
machine whose PATH puts an MSYS2 /mingw64/bin ahead of the compiler's own
runtime the dynamically linked binary picks up a mismatched libstdc++ and
segfaults inside the ifstream constructor, before main can print anything.
"""
import argparse
import glob
import json
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
EXAMPLE = HERE.parents[1]

ap = argparse.ArgumentParser()
ap.add_argument('budgets', nargs='*', default=['0.00001,0.05'],
                help='flatness,shear pairs to sweep')
ap.add_argument('--reference', default='HEAD~1',
                help='commit-ish whose src/roadgen.* is the reference surface')
args = ap.parse_args()


def dump_fixture(dst):
    """The district's roads and heightfield, as a flat text fixture."""
    proj = [f for f in os.listdir(EXAMPLE) if f.endswith('.tyra')][0]
    d = json.loads((EXAMPLE / proj).read_text(encoding='utf-8'))
    scene = d['scenes'][0]
    tok = (EXAMPLE / 'terrain-main.heights').read_text().split()
    hmW, hmD = int(tok[0]), int(tok[1])
    heights = [float(x) for x in tok[2:2 + hmW * hmD]]
    roads = []
    for f in sorted(glob.glob(str(EXAMPLE / 'objects' / '*.json'))):
        o = json.loads(Path(f).read_text(encoding='utf-8'))
        if o.get('type') == 'road':
            roads.append((o['name'], float(o['roadWidth']), o['roadPoints']))
    with open(dst, 'w') as fh:
        fh.write('%d %d %g %g\n' % (hmW, hmD, scene['terrain']['width'],
                                    scene['terrain']['depth']))
        fh.write(' '.join('%.9g' % h for h in heights) + '\n')
        fh.write('%d\n' % len(roads))
        for name, w, pts in roads:
            fh.write('%s|%.9g|%d\n' % (name, w, len(pts) // 2))
            fh.write(' '.join('%.9g' % p for p in pts) + '\n')
    return len(roads)


def git(*a):
    return subprocess.run(['git', '-C', str(ROOT)] + list(a), check=True,
                          capture_output=True, text=True).stdout


with tempfile.TemporaryDirectory(prefix='tyrax-roadbudget-') as tmp:
    tmp = Path(tmp)
    fixture = tmp / 'district.txt'
    n = dump_fixture(fixture)
    print('%d roads, reference %s (%s)' %
          (n, args.reference, git('rev-parse', '--short', args.reference).strip()))

    # The reference surface, as a renamed namespace so both can be linked.
    ref_hpp = git('show', '%s:src/roadgen.hpp' % args.reference)
    ref_cpp = git('show', '%s:src/roadgen.cpp' % args.reference)
    (tmp / 'ref_roadgen.hpp').write_text(
        ref_hpp.replace('namespace roadgen', 'namespace roadref'))
    (tmp / 'ref_roadgen.cpp').write_text(
        ref_cpp.replace('#include "roadgen.hpp"', '#include "ref_roadgen.hpp"')
               .replace('namespace roadgen', 'namespace roadref'))

    for budget in args.budgets:
        flat, shear = budget.split(',')
        exe = tmp / ('probe_%s.exe' % budget.replace('.', 'p').replace(',', '_'))
        subprocess.run(
            ['g++', '-std=c++20', '-O2', '-static',
             '-DTYRA_ROAD_SPAN_FLATNESS=%sf' % flat,
             '-DTYRA_ROAD_SPAN_SHEAR=%sf' % shear,
             '-I', str(ROOT / 'src'), '-I', str(tmp),
             str(HERE / 'road-budget-probe.cpp'), str(ROOT / 'src/roadgen.cpp'),
             str(tmp / 'ref_roadgen.cpp'), '-o', str(exe)], check=True)
        print('=== flatness %s shear %s ===' % (flat, shear))
        out = subprocess.run([str(exe), str(fixture)], check=True,
                             capture_output=True, text=True).stdout
        print(out.rstrip())
