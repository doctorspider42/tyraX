#!/usr/bin/env python3
"""
Regenerate the SCEI vs OpenVCL cost table inside the integration PLAN.md.

The target markdown file is expected to contain the markers
``<!-- BEGIN_COST_TABLE -->`` and ``<!-- END_COST_TABLE -->``; the rendered
table is written between them. The canonical location of that file is the
workspace-level ``ps2_opengl_integration/PLAN.md`` (formerly
``GENERIC_COMPILER_ROADMAP.md`` inside the openvcl repo).

Usage:
  python3 tools/update_cost_table.py \
      --openvcl-bin /full/path/to/openvcl \
      --md /full/path/to/PLAN.md \
      --pairs-file /tmp/ps2gl_pairs.txt

If a pairs file is provided with lines `baseline_path candidate_path [label]`,
the script renders a per-pair comparison table. Otherwise it lists all `.vsm`
files found in the fixtures directory and shows their metrics.
"""

import argparse
import glob
import json
import os
import subprocess
import sys


def run_cost(openvcl_bin, path):
    p = subprocess.run([openvcl_bin, '--cost', '--cost-json', path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"openvcl failed on {path}: {p.stderr.strip()}")
    return json.loads(p.stdout)


def gather_files(fixtures_dir):
    pattern = os.path.join(fixtures_dir, '*.vsm')
    return sorted(glob.glob(pattern))


def render_per_file_table(rows):
    header = (
        '| Shader | Path | weighted_estimated_total_cycles | weighted_static_cycles | affine_estimated_cycles | weighted_paired_cycles | weighted_nop_only_cycles |\n'
        '|---|---|---:|---:|---|---:|---:|\n'
    )
    lines = [header]
    for r in rows:
        lines.append('| {shader} | {path} | {we} | {ws} | {aff} | {wp} | {wn} |\n'.format(
            shader=r['shader'], path=r['path'], we=r.get('weighted_estimated_total_cycles',''), ws=r.get('weighted_static_cycles',''), aff=r.get('affine_estimated_cycles',''), wp=r.get('weighted_paired_cycles',''), wn=r.get('weighted_nop_only_cycles','')
        ))
    return ''.join(lines)


def render_pair_table(pairs):
    header = (
        '| Shader | Baseline (file) | baseline_we | Candidate (file) | candidate_we | Δ (candidate - baseline) | baseline_affine | candidate_affine | baseline_paired | candidate_paired | baseline_nop | candidate_nop |\n'
        '|---|---|---:|---|---:|---:|---|---|---:|---:|---:|---:|\n'
    )
    lines = [header]
    for label, base, cand in pairs:
        bname = os.path.basename(base)
        cname = os.path.basename(cand)
        rows = pairs[(label, base, cand)]
        b = rows['baseline']
        c = rows['candidate']
        bwe = b.get('weighted_estimated_total_cycles','')
        cwe = c.get('weighted_estimated_total_cycles','')
        delta = ''
        try:
            delta = int(cwe) - int(bwe)
        except Exception:
            delta = ''
        lines.append('| {label} | {b} | {bwe} | {c} | {cwe} | {delta} | {baff} | {caff} | {bp} | {cp} | {bn} | {cn} |\n'.format(
            label=label, b=bname, bwe=bwe, c=cname, cwe=cwe, delta=delta,
            baff=b.get('affine_estimated_cycles',''), caff=c.get('affine_estimated_cycles',''),
            bp=b.get('weighted_paired_cycles',''), cp=c.get('weighted_paired_cycles',''),
            bn=b.get('weighted_nop_only_cycles',''), cn=c.get('weighted_nop_only_cycles','')
        ))
    return ''.join(lines)


def update_md(md_path, content):
    with open(md_path, 'r', encoding='utf-8') as f:
        s = f.read()

    begin = '<!-- BEGIN_COST_TABLE -->'
    end = '<!-- END_COST_TABLE -->'
    if begin in s and end in s:
        before, rest = s.split(begin, 1)
        _, after = rest.split(end, 1)
        new_s = before + begin + '\n' + content + '\n' + end + after
    else:
        new_section = '\n## SCEI vs OpenVCL Cost Table (auto-generated)\n\n' + begin + '\n' + content + '\n' + end + '\n'
        new_s = s + new_section

    with open(md_path, 'w', encoding='utf-8') as f:
        f.write(new_s)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--openvcl-bin', default='openvcl', help='path to openvcl binary')
    ap.add_argument('--md', required=True, help='markdown file to update (expects BEGIN_COST_TABLE/END_COST_TABLE markers)')
    ap.add_argument('--fixtures-dir', default=os.path.join('test','fixtures','vsm_cost'), help='fixtures directory')
    ap.add_argument('--pairs-file', help='optional pairs file with: baseline candidate [label] per line')
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()

    openvcl_bin = args.openvcl_bin
    md_path = args.md
    fixtures_dir = args.fixtures_dir

    if args.pairs_file:
        pairs = []
        with open(args.pairs_file, 'r', encoding='utf-8') as f:
            for ln in f:
                ln = ln.strip()
                if not ln or ln.startswith('#'):
                    continue
                parts = ln.split()
                if len(parts) < 2:
                    print('Skipping malformed line in pairs file:', ln, file=sys.stderr)
                    continue
                baseline = parts[0]
                candidate = parts[1]
                label = parts[2] if len(parts) > 2 else os.path.splitext(os.path.basename(baseline))[0]
                pairs.append((label, baseline, candidate))

        results = {}
        for label, base, cand in pairs:
            print('Analyzing', base, 'and', cand)
            b = run_cost(openvcl_bin, base)
            c = run_cost(openvcl_bin, cand)
            results[(label, base, cand)] = {'baseline': b, 'candidate': c}

        md = render_pair_table(results.keys()) if False else render_pair_table({k:v for k,v in results.items()})
    else:
        files = gather_files(fixtures_dir)
        if not files:
            print('No .vsm files found in', fixtures_dir, file=sys.stderr)
            sys.exit(1)
        rows = []
        for f in files:
            print('Analyzing', f)
            j = run_cost(openvcl_bin, f)
            rows.append({
                'shader': os.path.splitext(os.path.basename(f))[0],
                'path': f,
                'weighted_estimated_total_cycles': j.get('weighted_estimated_total_cycles',''),
                'weighted_static_cycles': j.get('weighted_static_cycles',''),
                'affine_estimated_cycles': j.get('affine_estimated_cycles',''),
                'weighted_paired_cycles': j.get('weighted_paired_cycles',''),
                'weighted_nop_only_cycles': j.get('weighted_nop_only_cycles','')
            })
        md = render_per_file_table(rows)

    if args.dry_run:
        print(md)
    else:
        update_md(md_path, md)
        print('Updated', md_path)


if __name__ == '__main__':
    main()
